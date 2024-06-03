#include <gio/gio.h>
#include <glib-unix.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sqlite3.h>
#include <unistd.h>
#include <locale.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>

#include "lightmediascanner.h"
#include "lightmediascanner_conf.h"
#include "lightmediascanner_logger.h"
#include "lightmediascanner_private.h"

static char *bus_name = NULL;
static char *object_path = NULL;
static char *db_path = NULL;
static char **charsets = NULL;
static lms_country_t country = lms_country_unknown;
static int commit_interval = 100;
static int slave_timeout = 60;
static int delete_older_than = 30;

static gboolean vacuum = FALSE;
static gboolean startup_scan = FALSE;

#if defined(ENABLE_FRONT_REAR_SEPARATE_STARTUP_SCAN_OPTION)
    static gboolean startup_scan_rear = FALSE;
#endif

static gboolean omit_scan_progress = FALSE;

static GHashTable *categories = NULL;

#ifdef PATCH_LGE
static double commit_duration = 0.5f;
static int keep_recent_device = 5;
static int charset_detect_level = 0;
#endif

#if LOCALE_CHARSETS
static char **locale_charsets = NULL;
#endif

static LMS_TARGET lmsTarget = LMS_TARGET_FRONT;

#if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
    static int maxFileScanCount = 1000;
    static int currentFileCount = 0;

    static gboolean isPrintDirectoryStructure = FALSE;
    static const char front_path[] = "/rw_data/media/usb/front/";
    static const char rear_path[] = "/rw_data/media/usb/rear/";
#endif

//BUS_PATH : "/org/lightmediascanner/Scanner1";
static GDBusNodeInfo *introspection_data = NULL;
static const char BUS_IFACE[] = "org.lightmediascanner.Scanner1";

static const char introspection_xml[] =
    "<node>"
    "  <interface name=\"org.lightmediascanner.Scanner1\">"
    "    <property name=\"DataBasePath\" type=\"s\" access=\"read\" />"
    "    <property name=\"IsScanning\" type=\"b\" access=\"read\" />"
    "    <property name=\"WriteLocked\" type=\"b\" access=\"read\" />"
    "    <property name=\"UpdateID\" type=\"t\" access=\"read\" />"
    "    <property name=\"Categories\" type=\"a{sv}\" access=\"read\" />"
    "    <method name=\"Scan\">"
    "      <arg direction=\"in\" type=\"a{sv}\" name=\"specification\" />"
    "    </method>"
    "    <method name=\"Stop\" />"
    "    <method name=\"RequestWriteLock\" />"
    "    <method name=\"ReleaseWriteLock\" />"
    "    <method name=\"SetPlayNG\">"
    "      <arg direction=\"in\" type=\"s\" name=\"specification\" />"
    "    </method>"
    "    <signal name=\"ScanProgress\">"
    "      <arg type=\"s\" name=\"Category\" />"
    "      <arg type=\"s\" name=\"Path\" />"
    "      <arg type=\"t\" name=\"UpToDate\" />"
    "      <arg type=\"t\" name=\"Processed\" />"
    "      <arg type=\"t\" name=\"Deleted\" />"
    "      <arg type=\"t\" name=\"Skipped\" />"
    "      <arg type=\"t\" name=\"Errors\" />"
    "    </signal>"
    "    <signal name=\"ScanDevice\">"
    "      <arg type=\"s\" name=\"Category\" />"
    "      <arg type=\"s\" name=\"Path\" />"
    "      <arg type=\"i\" name=\"Status\" />"
    "    </signal>"
    "  </interface>"
    "</node>";

typedef struct scanner_category
{
    char *category;
    GArray *parsers;
    GArray *dirs;
    GArray *skip_dirs;
} scanner_category_t;

typedef struct scanner_pending
{
    char *category;
    GList *paths;
} scanner_pending_t;

typedef struct scan_progress {
    GDBusConnection *conn;
    gchar *category;
    gchar *path;
    guint64 uptodate;
    guint64 processed;
    guint64 deleted;
    guint64 skipped;
    guint64 errors;
    time_t last_report_time;
    gint updated;
} scan_progress_t;

#ifdef PATCH_LGE
typedef struct scan_device {
    GDBusConnection *conn;
    gchar *category;
    gchar *path;
    lms_scanner_device_scan_t status;
} scanDeviceType;
#endif

/* Scan progress signals will be issued if the time since last
 * emission is greated than SCAN_PROGRESS_UPDATE_TIMEOUT _and_ number
 * of items is greater than the SCAN_PROGRESS_UPDATE_COUNT.
 *
 * Be warned that D-Bus signal will wake-up the dbus-daemon (unless
 * k-dbus) and all listener clients, which may hurt scan performance,
 * thus we keep these good enough for GUI to look responsive while
 * conservative to not hurt performance.
 *
 * Note that at after a path is scanned (check/progress) the signal is
 * emitted even if count or timeout didn't match.
 */
#define SCAN_PROGRESS_UPDATE_TIMEOUT 1 /* in seconds */
#define SCAN_PROGRESS_UPDATE_COUNT  50 /* in number of items */
#define SCAN_MOUNTPOINTS_TIMEOUT 1 /* in seconds */
#define MAX_COLS 255

typedef struct scanner {
    GDBusConnection *conn;
    char *write_lock;
    unsigned write_lock_name_watcher;
    GDBusMethodInvocation *pending_stop;
    GList *pending_scan; /* of scanner_pending_t, see scanner_thread_work */
    GList *pending_device_scan;
    GThread *thread; /* see scanner_thread_work */
    unsigned cleanup_thread_idler; /* see scanner_thread_work */
    scan_progress_t *scan_progress;
#ifdef PATCH_LGE
    scanDeviceType *scan_device;
#endif
    GList *unavail_files;
    struct {
        GIOChannel *channel;
        unsigned watch;
        unsigned timer;
        GList *paths;
        GList *pending;
    } mounts;
    guint64 update_id;
    struct {
        unsigned idler; /* not a flag, but g_source tag */
        unsigned is_scanning : 1;
        unsigned write_locked : 1;
        unsigned update_id : 1;
        unsigned categories: 1;
        unsigned scanner_status: 1; //hkchoi
    } changed_props;
} scanner_t;

#ifdef PATCH_LGE
static void update_db_play_ng_file(gpointer data, gpointer user_data);

static pthread_mutex_t  *mtx;
static pthread_mutexattr_t mtxattr;

static int shared_lock_init(void) {
    const char name[30] = "/lms_lock";
    int fd, shm_size;
    int ret = 0, created = 0;
    errno = 0;

    fd = shm_open(name, O_RDWR, S_IRUSR|S_IWUSR|S_IROTH);
    if (fd == -1) {
        if(errno == ENOENT) {
            fd = shm_open(name, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IROTH);
            created = 1;

            if (fd == -1) {
                log_warning("shm_open() error\n");
                return fd;
            }
        }
        else {
            log_warning("shm_open() error\n");
            return fd;
        }
    }

    shm_size = sizeof(pthread_mutex_t);
    ret = ftruncate(fd, shm_size);

    if (ret == -1) {
        log_warning("ftruncate() failed. errno=%d\n", errno);
        return ret;
    }

    mtx = (pthread_mutex_t *)mmap(NULL, shm_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    if (mtx == MAP_FAILED) {
        log_warning("mmap() failed\n");
        return -1;
    }
    close(fd);

    if (created) {
        pthread_mutexattr_init(&mtxattr);
        ret = pthread_mutexattr_setpshared(&mtxattr, PTHREAD_PROCESS_SHARED);
        if (ret) {
            log_warning("pthread_mutexattr_setpshared() failed.(errno=%d)\n", errno);
            return -1;
        }

        pthread_mutex_init(mtx, &mtxattr);
    }
    return ret;
}

static int delete_deleted_files(sqlite3 *db, const char *device_path) {
    sqlite3_stmt *stmt;
    int ret;
    const char sql[] = "DELETE FROM files WHERE (dtime>0 AND path LIKE ? AND EXISTS (SELECT * FROM files WHERE path  LIKE ? AND dtime=0))";
    char path[PATH_MAX] = {'\0',};
    size_t len = 0;

    len = strlen(device_path);
    if ((UINT_MAX - sizeof("/%")) < len) {
        log_error("ERROR: len may wrap");
        return -1;
    } else {
        if ((len + sizeof("/%")) >= PATH_MAX) {
            log_error("ERROR: path is too long: \"%s\" + /%%", device_path);
            return -1;
            }
    }

    memcpy(path, device_path, len);
    if ((len > 0) && (path[len - 1] != '/')) {
        path[len] = '/';
        len++;
    }
    path[len] = '%';
    len++;

    path[len] = '\0';

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't prepare select : %s", sqlite3_errmsg(db));
        ret =-1;
        goto end;
    }

    if (sqlite3_bind_text(stmt, 1, path, len, SQLITE_STATIC) != SQLITE_OK) {
        log_warning("Couldn't bind device path :%s path: %s error: %s", path, db_path, sqlite3_errmsg(db));
        ret =-1;
        goto cleanup;
    }

    if (sqlite3_bind_text(stmt, 2, path, len, SQLITE_STATIC) != SQLITE_OK) {
        log_warning("Couldn't bind device path :%s path: %s error: %s", path, db_path, sqlite3_errmsg(db));
        ret =-1;
        goto cleanup;
    }

    ret = sqlite3_step(stmt);

    if (ret ==  SQLITE_DONE){
        log_info("Delete from DB deleted files in mounted devices \n");
    }
    else {
        log_warning("Couldn't run SQL to delete deleted files, ret=%d: %s",
                 ret, sqlite3_errmsg(db));
    }

  cleanup:
    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

  end:
    return ret;
}

static int delete_over_scanned_files(sqlite3 *db, const char *device, int limit) {
    sqlite3_stmt *stmt = NULL;
    int ret = -1;
    const char sql[] = "DELETE FROM files WHERE id IN (SELECT id FROM (SELECT id FROM files WHERE dtime = 0 AND path LIKE ? ORDER BY itime DESC LIMIT 10000 offset ?))";
    char path[PATH_MAX];
    size_t len_1 = 0;
    size_t len_2 = 0;
    DIR* pdir = NULL;
    struct dirent *pent = NULL;
    char usb_path[PATH_MAX + 1] = {'\0',};
    struct stat st = {0,};

    log_info("delete_over_scanned_files in, device: %s, limt: %d", device, limit);
    if (strcmp(device, "front") == 0) {
        size_t front_path_length = strlen(front_path)+1;
        strncpy(path , front_path , front_path_length);
        path[front_path_length] = '\0';
    }
    else
    {
        size_t rear_path_length = strlen(rear_path)+1;
        strncpy(path , rear_path , rear_path_length);
        path[rear_path_length] = '\0';
    }

    pdir = opendir (path);

    if(pdir == NULL)
    {
        log_warning("couldn't open path :%s", path);
        goto end;
    }

    while ((pent = readdir(pdir)) != NULL)
    {
        if (pent->d_name[0] == '.')
            continue;

        memset(usb_path, '\0', sizeof(usb_path));
        len_1 = strlen(path);
        if (len_1 > (PATH_MAX -1) ) {
            log_error("ERROR: length_1 may wraup");
            ret = -1;
            goto cleanup;
        }

        memcpy(usb_path , path , len_1);
        usb_path[len_1+1] = '\0';

        len_2 = strlen(pent->d_name);
        if (len_2 > (PATH_MAX - len_1 - 2) ) {
            log_error("ERROR: length_2 may wraup");
            ret = -1;
            goto cleanup;
        }
        memcpy(usb_path+len_1+1 , pent->d_name , len_2);
        usb_path[len_1+len_2+2] = '\0';

        if (stat(usb_path, &st) == -1)
        {
            log_error("ERROR: stat(usb_path, &st), usb_path=%s", usb_path);
        }

        if (S_ISDIR(st.st_mode))
        {
            usb_path[sizeof(usb_path) - 1] = '\0';
            len_1 = strlen(usb_path);
            if ((len_1 > 0) && (UINT_MAX > (len_1 - 1)) && (len_1 < (PATH_MAX - 1)) && (path[len_1 - 1] != '/')) {
                usb_path[len_1] = '/';
                len_1++;
            }
            if ((UINT_MAX > (len_1 - 2)) && (len_1 < (PATH_MAX - 2))) {
                usb_path[len_1] = '%';
                len_1++;
                usb_path[len_1] = '\0';
            }
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
                log_warning("Couldn't prepare select : %s", sqlite3_errmsg(db));
                ret = -1;
                goto cleanup;
            }

            if (sqlite3_bind_text(stmt, 1, usb_path, len_1, SQLITE_STATIC) != SQLITE_OK) {
                log_warning("Couldn't bind device path: %s error: %s", usb_path, sqlite3_errmsg(db));
                ret = -1;
                goto cleanup;
            }

            if (sqlite3_bind_int(stmt, 2, limit) != SQLITE_OK) {
                log_warning("Couldn't bind int %s", sqlite3_errmsg(db));
                goto cleanup;
            }


            ret = sqlite3_step(stmt);

            if (ret ==  SQLITE_DONE) {
                log_info("Delete from DB over deleted files in mounted devices path=%s \n", usb_path);
            }
            else {
                log_warning("Couldn't run SQL to delete over scanned files, path=%s, ret=%d: %s",
                        usb_path, ret, sqlite3_errmsg(db));
            }

cleanup:
            sqlite3_reset(stmt);
            sqlite3_finalize(stmt);
        }
    }

end:
    closedir(pdir);
    return ret;
}

static int update_recent_device_files(sqlite3 *db, const char *device_path) {
    sqlite3_stmt *stmt;
    int ret = -1;
    const char sql[] = "UPDATE files SET dtime = ? WHERE (dtime>0 AND dtime < ? AND path LIKE ? )";
    char path[PATH_MAX] = {'\0',};
    size_t len = 0;
    gint64 dtime;

    if(NULL == device_path) {
        log_error("dereferencing NULL pointer");
    } else {
        len = strlen(device_path);
        if ((UINT_MAX  - sizeof("/%")) < len) {
            log_error("ERROR: len may wrap");
            ret =-1;
            goto end;
        } else {
            if ((len + sizeof("/%")) >= PATH_MAX) {
                log_error("ERROR: path is too long: \"%s\" + /%%", device_path);
                ret =-1;
                goto end;
            }
        }
    }

    memcpy(path, device_path, len);
    if ((len > 0) && (path[len - 1] != '/')) {
        path[len] = '/';
        len++;
    }
    path[len] = '%';
    len++;
    path[len] = '\0';

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't prepare select : %s", sqlite3_errmsg(db));
        ret =-1;
        goto end;
    }

    if (delete_older_than <= INT_MIN) {
        log_error("ERROR: delete_older_than may overflow");
        goto cleanup;
    } else {
        dtime = (gint64)time(NULL) - ((delete_older_than - 1) * (24 * 60 * 60));// 1 day before than delete_older_than
        if ((dtime > INT_MAX) || (dtime < INT_MIN)) {
            log_error("ERROR: dtime  may overflow");
            ret =-1;
            goto cleanup;
        } else {
            if (sqlite3_bind_int(stmt, 1, (int)dtime) != SQLITE_OK) {
                log_warning("Couldn't bind dtime :%lld path: %s error: %s", dtime, db_path, sqlite3_errmsg(db));
                ret =-1;
                goto cleanup;
            }
        }
    }

    if (sqlite3_bind_int(stmt, 2, dtime) != SQLITE_OK) {
        log_warning("Couldn't bind dtime :%lld path: %s error: %s", dtime, db_path, sqlite3_errmsg(db));
        ret =-1;
        goto cleanup;
    }

    if (sqlite3_bind_text(stmt, 3, path, len, SQLITE_STATIC) != SQLITE_OK) {
        log_warning("Couldn't bind device path :%s path: %s error: %s", path, db_path, sqlite3_errmsg(db));
        ret =-1;
        goto cleanup;
    }

    ret = sqlite3_step(stmt);

    if (ret ==  SQLITE_DONE){
        log_warning("Update from DB recent devices \n");
    }
    else {
        log_warning("Couldn't run SQL to update recent devices, ret=%d: %s",
                 ret, sqlite3_errmsg(db));
    }

  cleanup:
    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

  end:
    return ret;
}

static int
delete_audio_album(sqlite3 *db, int64_t album_id)
{
    sqlite3_stmt *stmt;
    int ret;
    const char sql[] = "SELECT * FROM audio_albums WHERE id =?";
    const char sqlDel[] = "DELETE FROM audio_albums WHERE id =?";
    const char *path = NULL;

    //1.  Find the album url by album id
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't prepare select : %s", sqlite3_errmsg(db));
        ret =-1;
        goto end;
    }

    if (sqlite3_bind_int64(stmt, 1, album_id) != SQLITE_OK) {
        log_warning("Couldn't bind audio album id :%lld path: %s error: %s", album_id, db_path, sqlite3_errmsg(db));
        ret =-1;
        goto cleanup;
    }

    ret = sqlite3_step(stmt);

    if (ret ==  SQLITE_DONE){
        log_warning("SQLITE_DONE is called \n");
    }
    else if (ret == SQLITE_ROW){
        path = (const char *)sqlite3_column_text(stmt, 3);
        log_debug("Found the album_url path [%s]\n", path);
    }
    else
        log_warning("Couldn't run SQL to get album url, ret=%d: %s",
                 ret, sqlite3_errmsg(db));


    //2.  Delete the file pointed to album url
    if(path) {

        // [ Static Analysis ] 987560 : Unchecked return value from library
        (void)remove(path);
    }

    sqlite3_reset(stmt);

    //3. Delete the DB item by album id
    if (sqlite3_prepare_v2(db, sqlDel, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't prepare select %s: %s",
            db_path, sqlite3_errmsg(db));
        goto end;
    }
    if (sqlite3_bind_int(stmt, 1, album_id) != SQLITE_OK) {
        log_warning("Couldn't bind audio album id :%lld path: %s error: %s", album_id, db_path, sqlite3_errmsg(db));
        goto cleanup;
    }

    ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE){
        log_warning("Couldn't delete audio_album_url, error[%s] \n",sqlite3_errmsg(db));
    }

cleanup:
    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    return ret;
}

static lms_scanner_status_t
check_scanner_status(const scanner_t *scanner)
{
    lms_scanner_status_t status;

    if (scanner->thread != NULL){
        if (scanner->pending_stop == NULL)
            status = LMS_SCANNER_STATUS_RUNNIG;
        else
            status = LMS_SCANNER_STATUS_STOPPED;
    }else
        status = LMS_SCANNER_STATUS_IDLE;

    log_info("[ pid : %d ] , bus_name = %s , status = [[[[[    %s    ]]]]]", getpid() , bus_name , (status==LMS_SCANNER_STATUS_RUNNIG) ? "LMS_SCANNER_STATUS_RUNNIG" : ((status==LMS_SCANNER_STATUS_STOPPED) ? "LMS_SCANNER_STATUS_STOPPED" : "LMS_SCANNER_STATUS_IDLE"));

    return status;
}

#endif //PATCH_LGE

static scanner_category_t *
scanner_category_new(const char *category)
{
    scanner_category_t *sc = g_new0(scanner_category_t, 1);

    sc->category = g_strdup(category);
    sc->parsers = g_array_new(TRUE, TRUE, sizeof(char *));
    sc->dirs = g_array_new(TRUE, TRUE, sizeof(char *));
    sc->skip_dirs = g_array_new(TRUE, TRUE, sizeof(char *));

    return sc;
}

static void
scanner_category_destroy(scanner_category_t *sc)
{
    char **itr;

    for (itr = (char **)sc->parsers->data; *itr != NULL; itr++)
        g_free(*itr);
    for (itr = (char **)sc->dirs->data; *itr != NULL; itr++)
        g_free(*itr);
    for (itr = (char **)sc->skip_dirs->data; *itr != NULL; itr++)
        g_free(*itr);

    g_array_free(sc->parsers, TRUE);
    g_array_free(sc->dirs, TRUE);
    g_array_free(sc->skip_dirs, TRUE);
    g_free(sc->category);

    g_free(sc);
}

static void scanner_release_write_lock(scanner_t *scanner);

static void
scanner_write_lock_vanished(GDBusConnection *conn, const char *name, gpointer data)
{
    scanner_t *scanner = data;
    log_warning("Write lock holder %s vanished, release lock\n", name);
    scanner_release_write_lock(scanner);
}

static guint64
get_update_id(void)
{
    const char sql[] = "SELECT version FROM lms_internal WHERE tab='update_id'";
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int ret;
    guint64 update_id = 0;

    pthread_mutex_lock(mtx);
    log_info("+ lock [pid:%d]", getpid());
    ret = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL);
    if (ret != SQLITE_OK) {
        log_warning("Couldn't open '%s': %s", db_path, sqlite3_errmsg(db));
        goto end;
    }

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't get update_id from %s: %s",
                  db_path, sqlite3_errmsg(db));
        goto end;
    }

    ret = sqlite3_step(stmt);
    if (ret ==  SQLITE_DONE) {
        update_id = 0;
    } else if (ret == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        if (id < 0) {
            log_error("ERROR: id is -ve");
            goto end;
        }
        update_id = (guint64)id;
    } else {
        log_warning("Couldn't run SQL to get update_id, ret=%d: %s",
                  ret, sqlite3_errmsg(db));
    }

    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    sqlite3_close(db);
    log_info("- unlock [pid:%d] update id: %llu", getpid(), (unsigned long long)update_id);
    pthread_mutex_unlock(mtx);
    return update_id;
}

static void
do_delete_old(void)
{
    const char sql[] = "DELETE FROM files WHERE dtime > 0 and dtime <= ?";
    sqlite3 *db;
    sqlite3_stmt *stmt;
    gint64 dtime;
    int ret;

    ret = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL);
    if (ret != SQLITE_OK) {
        log_warning("Couldn't open '%s': %s", db_path, sqlite3_errmsg(db));
        goto end;
    }

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't prepare delete old from %s: %s",
                  db_path, sqlite3_errmsg(db));
        goto end;
    }

    dtime = (gint64)time(NULL) - delete_older_than * (24 * 60 * 60);
    if (sqlite3_bind_int64(stmt, 1, dtime) != SQLITE_OK) {
        log_warning("Couldn't bind delete old dtime '%"G_GINT64_FORMAT
                  "'from %s: %s", dtime, db_path, sqlite3_errmsg(db));
        goto cleanup;
    }

    ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE)
        log_warning("Couldn't run SQL delete old dtime '%"G_GINT64_FORMAT
                  "', ret=%d: %s", dtime, ret, sqlite3_errmsg(db));

cleanup:
    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    sqlite3_close(db);
}

static void
do_vacuum(void)
{
    const char sql[] = "VACUUM";
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int ret;

    ret = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL);
    if (ret != SQLITE_OK) {
        log_warning("Couldn't open '%s': %s", db_path, sqlite3_errmsg(db));
        goto end;
    }

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't vacuum from %s: %s",
                  db_path, sqlite3_errmsg(db));
        goto end;
    }

    ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE)
        log_warning("Couldn't run SQL VACUUM, ret=%d: %s",
                  ret, sqlite3_errmsg(db));

    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    sqlite3_close(db);
}

#ifdef PATCH_LGE
static void
db_execute_stmt(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *stmt;
    int ret;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK){
        log_error("ERROR: could not prepare \"%s\": %s", sql,
        sqlite3_errmsg(db));
        goto end;
    }

    ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE)
        log_warning("Couldn't run SQL execute statement, ret=%d: %s, sql=%s",
            ret, sqlite3_errmsg(db), sql);

    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    return;
}

static guint64
get_device_path_id(sqlite3 *db, char *device_path)
{
    const char sql[] = "SELECT id FROM devices WHERE path=?";
    sqlite3_stmt *stmt = NULL;
    int ret = 0;
    int temp_id = -1;
    guint64 device_id = 0;
    char path[PATH_MAX] = {0,};
    size_t len = 0;

    len = strlen(device_path);
    if (len >= PATH_MAX) {
        log_error("ERROR: path is too long: \"%s\" ", device_path);
        return 0;
    }

    memcpy(path, device_path, len);
    if ((len > 0) && (path[len - 1] != '/')) {
       path[len] = '/';
       len++;

       if (len >= PATH_MAX) {
          log_error("ERROR: path is too long: \"%s\" ", path);
          return 0;
       }
    }
    path[len] = '\0';

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't get device_id from %s: %s",
                  db_path, sqlite3_errmsg(db));
        goto end;
    }

    if (sqlite3_bind_text(stmt, 1, path, len, SQLITE_STATIC) != SQLITE_OK) {
        log_warning("Couldn't bind get device path :%s path: %s error: %s", path, db_path, sqlite3_errmsg(db));
        goto cleanup;
    }

    ret = sqlite3_step(stmt);
    if (ret ==  SQLITE_DONE) {
        device_id = 0;
    } else if (ret ==  SQLITE_ROW) {
        int column_int = sqlite3_column_int(stmt, 0);
        if (column_int < 0) {
            log_error("ERROR: coun_int may underflow");
            goto cleanup;
        }
        temp_id = sqlite3_column_int(stmt, 0);
    } else {
        log_warning("Couldn't run SQL to get device_id, ret=%d: %s",
                  ret, sqlite3_errmsg(db));
    }

cleanup:
    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    if(temp_id < 0) {
        log_error("casting may lost or misinterpreted data");
    } else {
        device_id = (guint64) temp_id;
        log_debug("device id: %llu", (unsigned long long)device_id);
    }
    return device_id;
}

static int
delete_old_device_path(sqlite3 *db)
{
    const char sql[] = "DELETE FROM devices WHERE mtime NOT IN (SELECT mtime FROM devices ORDER BY mtime DESC LIMIT ?)";
    sqlite3_stmt *stmt;
    int ret;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't delete old device from %s: %s",
                  db_path, sqlite3_errmsg(db));
        ret = -1;
        goto end;
    }

    if (sqlite3_bind_int(stmt, 1, keep_recent_device) != SQLITE_OK) {
        log_warning("Couldn't bind keep_recent_device :%d path: %s error: %s", keep_recent_device, db_path, sqlite3_errmsg(db));
        ret = -2;
        goto cleanup;
    }

    ret = sqlite3_step(stmt);
    if (ret !=  SQLITE_DONE)
        log_warning("Couldn't run SQL to delete old device, ret=%d: %s",
                  ret, sqlite3_errmsg(db));

cleanup:
    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    log_debug("Delete device path older than recent %d devices \n", keep_recent_device);
    return ret;
}


static int
update_device_path(sqlite3 *db, unsigned int device_id, time_t mtime)
{
    const char sql[] = "UPDATE devices SET mtime = ? WHERE id = ?";
    sqlite3_stmt *stmt;
    int ret;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't prepare device_id from %s: %s",
                  db_path, sqlite3_errmsg(db));
        ret = -1;
        goto end;
    }

    if ((mtime > INT_MAX) || (mtime < INT_MIN)) {
      log_error("ERROR: Integer operation may overfolow");
    }
    else{
      if (sqlite3_bind_int(stmt, 1, (int)mtime) != SQLITE_OK) {
        log_warning("Couldn't bind update device mtime, path: %s error: %s", db_path, sqlite3_errmsg(db));
        ret = -2;
        goto cleanup;
      }
    }

    if (device_id > INT_MAX) {
      log_error("ERROR: Integer operation may overflow");
    }
    else{
      if (sqlite3_bind_int(stmt, 2, (int)device_id) != SQLITE_OK) {
        log_warning("Couldn't bind update device id :%d path: %s error: %s", device_id, db_path, sqlite3_errmsg(db));
        ret = -3;
        goto cleanup;
      }
    }

    ret = sqlite3_step(stmt);
    if (ret !=  SQLITE_DONE)
        log_warning("Couldn't run SQL to update device path, ret=%d: %s",
                  ret, sqlite3_errmsg(db));

cleanup:
    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    log_debug("Update Device: %d", device_id);
    return ret;
}

static int
insert_device_path(sqlite3 *db, char *device_path, time_t mtime)
{
    const char sql[] = "INSERT INTO devices (path, mtime) VALUES(?,?)";
    sqlite3_stmt *stmt;
    int ret = 0;
    char path[PATH_MAX] = {'\0'};
    size_t len = 0;

    len = strlen(device_path);
    if (len >= (PATH_MAX -1)) {
        log_error("ERROR: path is too long: \"%s\" ", device_path);
        ret = -1;
        goto end;
    }

    memcpy(path, device_path, len);

    if ((len > 0) && (path[len - 1] != '/')) {
       path[len] = '/';
       len++;
    }
    path[len] = '\0';

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't insert device path from %s: %s",
                  db_path, sqlite3_errmsg(db));
        ret = -2;
        goto end;
    }

    if (sqlite3_bind_text(stmt, 1, path, len, SQLITE_STATIC) != SQLITE_OK) {
        log_warning("Couldn't bind insert device path :%s path: %s error: %s", path, db_path, sqlite3_errmsg(db));
        ret = -3;
        goto cleanup;
    }

    if ((mtime > INT_MAX) || (mtime < INT_MIN)) {
      log_error("ERROR:Integer operation overflow");
    }
    else {
      if (sqlite3_bind_int(stmt, 2, (int)mtime) != SQLITE_OK) {
        log_warning("Couldn't bind insert device mtime, path: %s error: %s", db_path, sqlite3_errmsg(db));
        ret = -4;
        goto cleanup;
      }
    }

    ret = sqlite3_step(stmt);
    if (ret !=  SQLITE_DONE)
        log_warning("Couldn't run SQL to get device path, ret=%d: %s",
                  ret, sqlite3_errmsg(db));

cleanup:
    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    log_debug("Insert Device : %s", device_path);
    return ret;
}


static int
force_enable_recent_device_files(sqlite3 *db, char *device_path)
{
    sqlite3_stmt *stmt;
    int ret;
    const char sql[] = "UPDATE files SET dtime = 0 WHERE path LIKE ?";
    char path[PATH_MAX] = {'\0',};
    size_t len = 0;

    len = strlen(device_path);
    if ((UINT_MAX - sizeof("/%")) < len) {
        log_error("ERROR: len may wrap");
        ret =-1;
        goto end;
    } else {
        if ((len + sizeof("/%")) >= PATH_MAX) {
            log_error("ERROR: path is too long: \"%s\" + /%%", device_path);
            ret =-1;
            goto end;
        }
    }

    memcpy(path, device_path, len);
    if (len > 0 && path[len - 1] != '/') {
        path[len] = '/';
        len++;
    }
    path[len] = '%';
    len++;
    path[len] = '\0';

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't prepare select : %s", sqlite3_errmsg(db));
        ret =-1;
        goto end;
    }

    if (sqlite3_bind_text(stmt, 1, path, len, SQLITE_STATIC) != SQLITE_OK) {
        log_warning("Couldn't bind device path :%s path: %s error: %s", path, db_path, sqlite3_errmsg(db));
        ret =-1;
        goto cleanup;
    }

    ret = sqlite3_step(stmt);

    if (ret ==  SQLITE_DONE){
        log_warning("Force enable from DB recent devices \n");
    }
    else {
        log_warning("Couldn't run SQL to force enable recent devices, ret=%d: %s",
                 ret, sqlite3_errmsg(db));
    }

  cleanup:
    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

  end:
    return ret;
}


static int
set_device_path(gpointer data, gpointer user_data)
{
    char* device_path = (char*)data;
    sqlite3 *db;
    int ret = SQLITE_OK;
    guint64 device_id = 0;
    time_t mtime;

    log_info("set_device_path in");

    // OYK_2019_03_25 : Add NULL checking.
    // Media Indexer sometimes crashes at GEN5 model
    if (device_path == NULL)
        return ret;

    mtime = time(NULL);
    if (mtime == (time_t)-1)
      log_error("ERROR: mtime is failed ");

    pthread_mutex_lock(mtx);
    log_info("+ lock [ pid:%d ] , bus_name = %s", getpid() , bus_name);

    ret = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL);
    if (ret != SQLITE_OK) {
        log_warning("Couldn't open '%s': %s", db_path, sqlite3_errmsg(db));
        goto end;
    }

    db_execute_stmt(db, "BEGIN TRANSACTION");

    device_id = get_device_path_id(db, device_path);
    if (device_id > 0) {
        ret = update_device_path(db, device_id, mtime);
        if (ret == SQLITE_DONE)
            ret = force_enable_recent_device_files(db, device_path);
    }
    else {
        ret = insert_device_path(db, device_path, mtime);
    }
    delete_old_device_path(db);

    db_execute_stmt(db, "COMMIT");

end:
    sqlite3_close(db);
    log_info("- unlock [ pid:%d ] , bus_name = %s", getpid() , bus_name);
    pthread_mutex_unlock(mtx);
    return ret;
}

static void
do_update_recent_device_files(void)
{
    const char sql[] = "SELECT path FROM devices WHERE mtime IN (SELECT mtime FROM devices ORDER BY mtime DESC LIMIT ?)";
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int ret;
    const char *path = NULL;

    ret = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL);
    if (ret != SQLITE_OK) {
        log_warning("Couldn't open '%s': %s", db_path, sqlite3_errmsg(db));
        goto end;
    }

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't prepare update recent devices from %s: %s",
                  db_path, sqlite3_errmsg(db));
        goto end;
    }

    if (sqlite3_bind_int(stmt, 1, keep_recent_device) != SQLITE_OK) {
        log_warning("Couldn't bind keep_recent_device :%d path: %s error: %s", keep_recent_device, db_path, sqlite3_errmsg(db));
        ret =-1;
        goto cleanup;
    }

    while( sqlite3_step(stmt) == SQLITE_ROW ) {
        path = (const char *)sqlite3_column_text(stmt, 0);
        if (!path) {
            log_error("ERROR: path is null");
            goto cleanup;
        } else {
            log_debug("run SQL to update recent devices, path=%s", path);
            update_recent_device_files(db, path);
        }
    }

cleanup:
    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    sqlite3_close(db);
}


static void
do_delete_deleted_files(void)
{
    const char sql[] = "SELECT path FROM devices";
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int ret = 0;
    const unsigned char *device_path = NULL;

    // delete not exists albums & album cover art images
    ret = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL);
    if (ret != SQLITE_OK) {
        log_warning("Couldn't open '%s': %s", db_path, sqlite3_errmsg(db));
        goto end;
    }

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't prepare delete files from %s: %s",
                  db_path, sqlite3_errmsg(db));
        goto end;
    }

    while( sqlite3_step(stmt) == SQLITE_ROW ) {
        device_path = sqlite3_column_text(stmt, 0);
        if (device_path == NULL) {
            log_error("ERROR: device_path is null");
            goto end;
        } else {
            log_debug("run SQL to delete files, id=%s",device_path);
            if(device_path == NULL)
                log_error("device path is null");
            else
                delete_deleted_files(db, (const char*)device_path);
        }
    }
    #if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
    if (lmsTarget == LMS_TARGET_REAR)
        delete_over_scanned_files(db, "rear", maxFileScanCount);
    else
        delete_over_scanned_files(db, "front", maxFileScanCount);
    #endif

    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    sqlite3_close(db);
}


static void
do_delete_not_exists_indexes(void)
{
    const char sql[] = "SELECT id FROM audio_albums WHERE NOT EXISTS ( SELECT album_id FROM audios WHERE audio_albums.id=audios.album_id)";
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int ret;
    int album_id = 0;

    ret = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL);
    if (ret != SQLITE_OK) {
        log_warning("Couldn't open '%s': %s", db_path, sqlite3_errmsg(db));
        goto end;
    }

    // delete not exists genres & artists
    db_execute_stmt(db, "DELETE FROM audio_genres WHERE NOT EXISTS ( SELECT genre_id  FROM audios WHERE audio_genres.id=audios.genre_id)");
    db_execute_stmt(db, "DELETE FROM audio_artists WHERE NOT EXISTS ( SELECT artist_id  FROM audios WHERE audio_artists.id=audios.artist_id)");

    // delete not exists albums & album cover art images
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't prepare delete kinds from %s: %s",
                  db_path, sqlite3_errmsg(db));
        goto end;
    }

    while( sqlite3_step(stmt) == SQLITE_ROW ) {
        album_id = sqlite3_column_int(stmt, 0);
        log_debug("run SQL to delete kinds, id=%d",album_id);
        delete_audio_album(db, album_id);
    }

    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    sqlite3_close(db);
}

static void refresh_database(void) {

    pthread_mutex_lock(mtx);

    log_info("+ lock [ pid : %d ] , bus_name = %s", getpid() , bus_name);

    if (delete_older_than >= 0) {
        log_debug("Update dtime from DB files to keep recent %d devices.",
                keep_recent_device);
        do_update_recent_device_files();
        log_debug("Delete from DB files with dtime older than %d days.",
                delete_older_than);
        do_delete_old();
    }

    do_delete_deleted_files();
    do_delete_not_exists_indexes();

    if (vacuum) {
        GTimer *timer = g_timer_new();

        log_debug("Starting SQL VACUUM...");
        g_timer_start(timer);
        do_vacuum();
        g_timer_stop(timer);
        log_debug("Finished VACUUM in %0.3f seconds.",
                g_timer_elapsed(timer, NULL));
        g_timer_destroy(timer);
    }

    log_info("- unlock [pid:%d]", getpid());
    pthread_mutex_unlock(mtx);
}
#endif

static gboolean
check_write_locked(const scanner_t *scanner)
{
    return scanner->write_lock != NULL || scanner->thread != NULL;
}

static void
category_variant_foreach(gpointer key, gpointer value, gpointer user_data)
{
    scanner_category_t *sc = value;
    GVariantBuilder *builder = user_data;
    GVariantBuilder *sub;
    GVariantBuilder *darr;
    GVariantBuilder *parr;
    GVariantBuilder *sarr;

    char **itr;

    darr = g_variant_builder_new(G_VARIANT_TYPE("as"));
    for (itr = (char **)sc->dirs->data; *itr != NULL; itr++) {

        log_debug("[ pid : %d ] , dirs , *itr = %s" , getpid() , *itr);

        // OYK_2019_07_25 : The defense code for the dbus communication.
        //                  The dbus communication was blocked intermittently because of Glib assertion failure.
        //                  Refer to [GENSIX-50487].
        if (!g_utf8_validate (*itr, -1, NULL)) {

            log_error("[ pid : %d ] , [[[ ERROR ]]] , dirs , g_utf8_validate(*itr = %s , ...) FAILED." , getpid() , *itr);

            continue;
        }

        g_variant_builder_add(darr, "s", *itr);
    }

    sarr = g_variant_builder_new(G_VARIANT_TYPE("as"));
    for (itr = (char **)sc->skip_dirs->data; *itr != NULL; itr++) {

        log_debug("[ pid : %d ] , skip_dirs , *itr = %s" , getpid() , *itr);

        // OYK_2019_07_25 : The defense code for the dbus communication.
        //                  The dbus communication was blocked intermittently because of Glib assertion failure.
        //                  Refer to [GENSIX-50487].
        if (!g_utf8_validate (*itr, -1, NULL)) {

            log_error("[ pid : %d ] , [[[ ERROR ]]] , skip_dirs , g_utf8_validate(*itr = %s , ...) FAILED." , getpid() , *itr);

            continue;
        }

        g_variant_builder_add(sarr, "s", *itr);
    }

    parr = g_variant_builder_new(G_VARIANT_TYPE("as"));
    for (itr = (char **)sc->parsers->data; *itr != NULL; itr++) {

        log_debug("[ pid : %d ] , parsers , *itr = %s" , getpid() , *itr);

        // OYK_2019_07_25 : The defense code for the dbus communication.
        //                  The dbus communication was blocked intermittently because of Glib assertion failure.
        //                  Refer to [GENSIX-50487].
        if (!g_utf8_validate (*itr, -1, NULL)) {

            log_error("[ pid : %d ] , [[[ ERROR ]]] , parsers , g_utf8_validate(*itr = %s , ...) FAILED." , getpid() , *itr);

            continue;
        }

        g_variant_builder_add(parr, "s", *itr);
    }

    sub = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(sub, "{sv}", "dirs", g_variant_builder_end(darr));
    g_variant_builder_add(sub, "{sv}", "skip_dirs", g_variant_builder_end(sarr));
    g_variant_builder_add(sub, "{sv}", "parsers", g_variant_builder_end(parr));

    g_variant_builder_add(builder, "{sv}", sc->category,
                          g_variant_builder_end(sub));

    g_variant_builder_unref(sub);
    g_variant_builder_unref(parr);
    g_variant_builder_unref(sarr);
    g_variant_builder_unref(darr);
}

static GVariant *
categories_get_variant(void)
{
    GVariantBuilder *builder;
    GVariant *variant;

    builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

    g_hash_table_foreach(categories, category_variant_foreach, builder);

    variant = g_variant_builder_end(builder);
    g_variant_builder_unref(builder);

    return variant;
}

static gboolean
scanner_dbus_props_changed(gpointer data)
{
    GVariantBuilder *builder;
    GError *error = NULL;
    scanner_t *scanner = data;
    guint64 update_id = 0;

    if (!check_write_locked(scanner))
        update_id = get_update_id();
    if (update_id > 0 && update_id != scanner->update_id) {
        scanner->changed_props.update_id = TRUE;
        scanner->update_id = update_id;
    }

    builder = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);

    if (scanner->changed_props.is_scanning) {
        scanner->changed_props.is_scanning = FALSE;
        g_variant_builder_add(builder, "{sv}", "IsScanning",
                              g_variant_new_boolean(scanner->thread != NULL));
    }
    if (scanner->changed_props.write_locked) {
        scanner->changed_props.write_locked = FALSE;
        g_variant_builder_add(
            builder, "{sv}", "WriteLocked",
            g_variant_new_boolean(check_write_locked(scanner)));
    }
    if (scanner->changed_props.update_id) {
        scanner->changed_props.update_id = FALSE;
        g_variant_builder_add(builder, "{sv}", "UpdateID",
                              g_variant_new_uint64(scanner->update_id));
    }
    if (scanner->changed_props.categories) {
        scanner->changed_props.categories = FALSE;
        g_variant_builder_add(builder, "{sv}", "Categories",
                              categories_get_variant());
    }
#ifdef PATCH_LGE
    if (scanner->changed_props.scanner_status) {
        scanner->changed_props.scanner_status = FALSE;
        g_variant_builder_add(builder, "{sv}", "ScannerStatus",
                              g_variant_new_int32(check_scanner_status(scanner)));
    }
#endif

    log_info("    [[[[[ Emit signal ]]]]] properties changed [ pid : %d ] , bus_name = %s", getpid() , bus_name);

    g_dbus_connection_emit_signal(scanner->conn,
                                  NULL,
                                  object_path,
                                  "org.freedesktop.DBus.Properties",
                                  "PropertiesChanged",
                                  g_variant_new("(sa{sv}as)",
                                                BUS_IFACE, builder, NULL),
                                  &error);
    g_variant_builder_unref(builder);
    g_assert_no_error(error);

    scanner->changed_props.idler = 0;
    return FALSE;
}

static void
scanner_write_lock_changed(scanner_t *scanner)
{
    log_info("scanner_dbus_props_changed(...) Called.");

    if (scanner->changed_props.idler == 0)
        scanner->changed_props.idler = g_idle_add(scanner_dbus_props_changed,
                                                  scanner);

    scanner->changed_props.write_locked = TRUE;
}

static void
scanner_acquire_write_lock(scanner_t *scanner, const char *sender)
{
    log_debug("acquired write lock for %s", sender);
    scanner->write_lock = g_strdup(sender);
    scanner->write_lock_name_watcher = g_bus_watch_name_on_connection(
        scanner->conn, sender, G_BUS_NAME_WATCHER_FLAGS_NONE,
        NULL, scanner_write_lock_vanished, scanner, NULL);

    scanner_write_lock_changed(scanner);
}

static void
scanner_release_write_lock(scanner_t *scanner)
{
    log_debug("release write lock previously owned by %s", scanner->write_lock);

    g_free(scanner->write_lock);
    scanner->write_lock = NULL;

    g_bus_unwatch_name(scanner->write_lock_name_watcher);
    scanner->write_lock_name_watcher = 0;

    scanner_write_lock_changed(scanner);
}

static void
scanner_pending_free(scanner_pending_t *pending)
{
    g_list_free_full(pending->paths, g_free);
    g_free(pending->category);
    g_free(pending);
}

static scanner_pending_t *
scanner_pending_get_or_add(scanner_t *scanner, const char *category)
{
    scanner_pending_t *pending;
    GList *n, *nlast = NULL;

    g_assert(scanner->thread == NULL);

    for (n = scanner->pending_scan; n != NULL; n = n->next) {
        nlast = n;
        pending = n->data;
        if (strcmp(pending->category, category) == 0)
            return pending;
    }

    pending = g_new0(scanner_pending_t, 1);
    pending->category = g_strdup(category);
    pending->paths = NULL;

    /* I can't believe there is no g_list_insert_after() :-( */
    n = g_list_alloc();
    n->data = pending;
    n->next = NULL;
    n->prev = nlast;

    if (nlast)
        nlast->next = n;
    else
        scanner->pending_scan = n;

    return pending;
}

static scanner_pending_t *
scanner_pending_device_get_or_add(scanner_t *scanner, const char *category)
{
    scanner_pending_t *pending;
    GList *n, *nlast = NULL;

    //g_assert(scanner->thread == NULL);

    for (n = scanner->pending_device_scan; n != NULL; n = n->next) {
        nlast = n;
        pending = n->data;
        if (strcmp(pending->category, category) == 0)
            return pending;
    }

    pending = g_new0(scanner_pending_t, 1);
    pending->category = g_strdup(category);
    pending->paths = NULL;

    /* I can't believe there is no g_list_insert_after() :-( */
    n = g_list_alloc();
    n->data = pending;
    n->next = NULL;
    n->prev = nlast;

    if (nlast)
        nlast->next = n;
    else
        scanner->pending_device_scan = n;

    return pending;
}


/* NOTE:  assumes array was already validated for duplicates/restrictions */
static void
scanner_pending_add_all(scanner_pending_t *pending, const GArray *arr)
{
    char **itr;


    for (itr = (char **)arr->data; *itr != NULL; itr++)
        pending->paths = g_list_prepend(pending->paths, g_strdup(*itr));

    pending->paths = g_list_reverse(pending->paths);
}

static gboolean
scanner_category_allows_path(const GArray *restrictions, const char *path)
{
    const char * const *itr;
    for (itr = (const char *const*)restrictions->data; *itr != NULL; itr++) {
        if (g_str_has_prefix(path, *itr))
            return TRUE;
    }
    return FALSE;
}

static void
scanner_pending_add(scanner_pending_t *pending, const GArray *restrictions, const char *path)
{
    GList *n, *nlast;

    for (n = pending->paths; n != NULL; n = n->next) {
        const char *other = n->data;
        if (g_str_has_prefix(path, other)) {
            log_debug("Path already in pending scan in category %s: %s (%s)",
                    pending->category, path, other);
            return;
        }
    }


    if (restrictions && (!scanner_category_allows_path(restrictions, path))) {
        log_warning("Path is outside of category %s directories: %s",
                  pending->category, path);
        return;
    }

    nlast = NULL;
    for (n = pending->paths; n != NULL; ) {
        char *other = n->data;

        nlast = n;

        if (!g_str_has_prefix(other, path))
            n = n->next;
        else {
            GList *tmp;

            g_debug("Path covers previous pending scan in category %s, "
                    "replace %s (%s)",
                    pending->category, other, path);

            tmp = n->next;
            nlast = n->next ? n->next : n->prev;

            pending->paths = g_list_delete_link(pending->paths, n);
            g_free(other);

            n = tmp;
        }
    }

    log_debug("New scan path for category %s: %s", pending->category, path);

    /* I can't believe there is no g_list_insert_after() :-( */
    n = g_list_alloc();
    n->data = g_strdup(path);
    n->next = NULL;
    n->prev = nlast;

    if (nlast)
        nlast->next = n;
    else
        pending->paths = n;
}

static void
scan_params_all(gpointer key, gpointer value, gpointer user_data)
{
    scanner_pending_t *pending;
    scanner_category_t *sc = value;
    scanner_t *scanner = user_data;

    pending = scanner_pending_get_or_add(scanner, sc->category);
    scanner_pending_add_all(pending, sc->dirs);
}

static void
dbus_scanner_scan_params_set(scanner_t *scanner, GVariant *params)
{
    GVariantIter *itr;
    GVariant *el;
    char *cat;
    gboolean empty = TRUE;

    g_variant_get(params, "(a{sv})", &itr);
    while (g_variant_iter_loop(itr, "{sv}", &cat, &el)) {
        scanner_category_t *sc;
        scanner_pending_t *pending;
        GVariantIter *subitr;
        char *path;

        sc = g_hash_table_lookup(categories, cat);

        if (!sc) {
            log_warning("Unexpected scan category: %s, skipped.", cat);
            continue;
        }

        log_info("category = %s" , cat);

        pending = scanner_pending_get_or_add(scanner, cat);
        empty = FALSE;

        g_variant_get(el, "as", &subitr);
        while (g_variant_iter_loop(subitr, "s", &path)) {
            scanner_pending_add(pending, sc->dirs, path);
        }

        scanner_pending_add_all(pending, sc->dirs);
    }
    g_variant_iter_free(itr);

    log_info("empty = %d" , empty);

    if (empty){
        g_hash_table_foreach(categories, scan_params_all, scanner);
    }
}

static void
scanner_is_scanning_changed(scanner_t *scanner)
{
    log_info("scanner_dbus_props_changed(...) Called.");

    if (scanner->changed_props.idler == 0)
        scanner->changed_props.idler = g_idle_add(scanner_dbus_props_changed,
                                                  scanner);

    scanner->changed_props.is_scanning = TRUE;
}

#ifdef PATCH_LGE
static void
scanner_status_changed(scanner_t *scanner)
{
    log_info("scanner_dbus_props_changed(...) Called.");

    if (scanner->changed_props.idler == 0)
        scanner->changed_props.idler = g_idle_add(scanner_dbus_props_changed,
                                                  scanner);

    scanner->changed_props.scanner_status = TRUE;
}

static gboolean
report_scan_device(gpointer data)
{
    scanDeviceType *sd = data;
    GError *error = NULL;

    log_debug("emit signal cat[%s] path[%s], status[%d]",
        sd->category,sd->path, sd->status);
    g_dbus_connection_emit_signal(sd->conn,
                                  NULL,
                                  object_path,
                                  BUS_IFACE,
                                  "ScanDevice",
                                  g_variant_new("(ssi)",
                                                sd->category,
                                                sd->path,
                                                sd->status),
                                  &error);
    g_assert_no_error(error);

    return FALSE;
}


#endif

static gboolean
report_scan_progress(gpointer data)
{
    scan_progress_t *sp = data;
    GError *error = NULL;

    g_dbus_connection_emit_signal(sp->conn,
                                  NULL,
                                  object_path,
                                  BUS_IFACE,
                                  "ScanProgress",
                                  g_variant_new("(ssttttt)",
                                                sp->category,
                                                sp->path,
                                                sp->uptodate,
                                                sp->processed,
                                                sp->deleted,
                                                sp->skipped,
                                                sp->errors),
                                  &error);
    g_assert_no_error(error);

    return FALSE;
}

static gboolean
report_scan_progress_and_free(gpointer data)
{
    scan_progress_t *sp = data;

    if (sp->updated)
        report_scan_progress(sp);

    g_object_unref(sp->conn);
    g_free(sp->category);
    g_free(sp->path);
    g_free(sp);
    return FALSE;
}

#ifdef PATCH_LGE
static gboolean
report_scan_device_and_free(gpointer data)
{
    scanDeviceType *sd = data;

    g_object_unref(sd->conn);
    g_free(sd->category);
    g_free(sd->path);
    g_free(sd);
    return FALSE;
}
#endif

static void
scan_progress_cb(lms_t *lms, const char *path, int pathlen, lms_progress_status_t status, void *data)
{
    const scanner_t *scanner = data;
    scan_progress_t *scan_progress = scanner->scan_progress;

    if (scanner->pending_stop)
        lms_stop_processing(lms);

    if (!scan_progress)
        return;

    switch (status) {
        case LMS_PROGRESS_STATUS_UP_TO_DATE:
            if (ULONG_MAX - scan_progress->uptodate < 1) {
                log_error("ERROR:unsigned integer opeartion");
            }
            else {
                scan_progress->uptodate++;
            }
            break;
        case LMS_PROGRESS_STATUS_PROCESSED:
            if (ULONG_MAX - scan_progress->processed < 1) {
                log_error("ERROR:unsigned integer opeartion may wrap");
            }
            else {
                scan_progress->processed++;
            }
            break;
        case LMS_PROGRESS_STATUS_DELETED:
            if (ULONG_MAX - scan_progress->deleted < 1) {
                log_error("ERROR:unsigned integer opeartion");
            }
            else {
                scan_progress->deleted++;
            }
            break;
        case LMS_PROGRESS_STATUS_UNKNOWN:
        case LMS_PROGRESS_STATUS_KILLED:
        case LMS_PROGRESS_STATUS_ERROR_PARSE:
        case LMS_PROGRESS_STATUS_ERROR_COMM:
            if (ULONG_MAX - scan_progress->errors < 1) {
                log_error("ERROR:unsigned integer opeartion may wrap");
            }
            else {
                scan_progress->errors++;
            }
            break;
        case LMS_PROGRESS_STATUS_SKIPPED:
            if (ULONG_MAX - scan_progress->skipped < 1) {
                log_error("ERROR: unsigned integer operation may wrap");
            }
            else {
                scan_progress->skipped++;
            }
            break;
        default :
            log_error("ERROR: invalid status");
    }

    if (scan_progress->updated < 0)
    {
      log_error("ERROR:scan_progress->updated may result in lost or misinterpreted data");
    }
    else {
      if ((UINT_MAX - (unsigned int)scan_progress->updated) < 1)
      {
        log_error("ERROR:unsigned integer opeartion may wrap");
      }
      else
      {
        scan_progress->updated++;
      }
    }
    if (scan_progress->updated > SCAN_PROGRESS_UPDATE_COUNT) {
        time_t now = time(NULL);
        time_t temp;
        if (__builtin_saddl_overflow(scan_progress->last_report_time,SCAN_PROGRESS_UPDATE_TIMEOUT, &temp))
        {
           log_error("ERROR:signed integer operation may overflow");
        }
        else {
            if (temp < now){
              scan_progress->last_report_time = now;
              scan_progress->updated = 0;
              g_idle_add(report_scan_progress, scan_progress);
            }
        }
    }
}

#ifdef PATCH_LGE
static void
scan_device_cb(lms_t *lms, const char *path, int pathlen, lms_progress_status_t status, void *data)
{
    const scanner_t *scanner = data;
    scanDeviceType *sd = scanner->scan_device;
    sd->status = status;

    report_scan_device(sd);
    //g_idle_add(report_scan_device, sd);
}
#endif

static lms_t *
setup_lms(const char *category, const scanner_t *scanner)
{
    scanner_category_t *sc;
    char **itr;
    lms_t *lms;
    scanner_pending_t *pending;
    GList *n;
    GList *p;
    char *device_path = NULL;

    log_info("category = %s", category);

    sc = g_hash_table_lookup(categories, category);
    if (!sc) {
        log_error("Unknown category %s", category);
        return NULL;
    }

    if (sc->parsers->len == 0) {
        log_warning("No parsers for category %s", category);
        return NULL;
    }

    lms = lms_new(db_path);
    if (!lms) {
        log_warning("Failed to create lms");
        return NULL;
    }

    if (commit_interval < 0)
    {
      log_error("ERROR: Invalid commit interval is less than zero");
    }
    else
    {
      lms_set_commit_interval(lms, (unsigned int)commit_interval);
    }
    lms_set_commit_duration(lms, commit_duration);
    lms_set_slave_timeout(lms, slave_timeout * 1000);
    lms_set_country(lms, country);
    lms_set_chardet_level(lms, charset_detect_level);
    lms_set_lms_target(lms, lmsTarget);

    #if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
        lms_set_maxFileScanCount(lms, maxFileScanCount);
        lms_set_currentFileScanCount(lms, currentFileCount);
        lms_set_isPrintDirectoryStructure(lms, isPrintDirectoryStructure);
    #endif

    if (charsets) {
        for (itr = charsets; *itr != NULL; itr++)
            if (lms_charset_add(lms, *itr) != 0)
                log_warning("Couldn't add charset: %s", *itr);
    }

#if LOCALE_CHARSETS
    if (country == lms_country_kor)
        ParseLocaleCharsets("ko_KR", &locale_charsets);
    else if (country == lms_country_chn)
        ParseLocaleCharsets("zh_CN", &locale_charsets);

    if (locale_charsets) {
        for (itr = locale_charsets; *itr != NULL; itr++){
            if (lms_charset_add(lms, *itr) != 0)
               log_warning("Couldn't add locale_charsets: %s", *itr);
        }
    }
#endif

    for (itr = (char **)sc->parsers->data; *itr != NULL; itr++) {
        const char *parser = *itr;
        lms_plugin_t *plugin;

        log_info("parser = %s", parser);

        if (parser[0] == '/')
            plugin = lms_parser_add(lms, parser);
        else
            plugin = lms_parser_find_and_add(lms, parser);

        if (!plugin)
            log_warning("Couldn't add parser: %s", parser);
    }

    lms_clear_device_scan_path(lms);

    for (n = scanner->pending_device_scan; n != NULL; n = n->next) {
        pending = n->data;
        if (strcmp(pending->category, category) == 0){
            for(p=pending->paths; p != NULL; p = p->next){
                device_path = p->data;
                lms_set_device_scan_path(lms, device_path);
            }
        }
    }

    lms_clear_completed_scan_path(lms);
    for(itr = (char**)sc->skip_dirs->data; *itr != NULL; itr++) {
        lms_set_completed_scan_path(lms, g_strdup(*itr));
    }

    lms_set_progress_callback(lms, scan_progress_cb, scanner, NULL);
#ifdef PATCH_LGE
    lms_set_progress_device_callback(lms, scan_device_cb, scanner, NULL);
#endif

    return lms;
}

static void scan_mountpoints(scanner_t *scanner);

static gboolean
scanner_thread_cleanup(gpointer data)
{
    scanner_t *scanner = data;
    gpointer ret;

    log_info("cleanup scanner work thread , bus_name = %s" , bus_name);

    ret = g_thread_join(scanner->thread);
    g_assert(ret == scanner);

    scanner->thread = NULL;
    scanner->cleanup_thread_idler = 0;

    if (scanner->pending_stop) {
        g_dbus_method_invocation_return_value(scanner->pending_stop, NULL);
        g_object_unref(scanner->pending_stop);
        scanner->pending_stop =  NULL;
    }

    g_list_free_full(scanner->pending_scan,
                     (GDestroyNotify)scanner_pending_free);
    scanner->pending_scan = NULL;

    if (scanner->mounts.pending && !scanner->mounts.timer)
        scan_mountpoints(scanner);
    else {
        scanner_is_scanning_changed(scanner);
        scanner_write_lock_changed(scanner);
#ifdef PATCH_LGE
        scanner_status_changed(scanner);
#endif
    }

    return FALSE;
}

/*
 * Note on thread usage and locks (or lack of locks):
 *
 * The main thread is responsible for launching the worker thread,
 * setting 'scanner->thread' pointer, which is later checked *ONLY* by
 * main thread. When the thread is done, it will notify the main
 * thread with scanner_thread_cleanup() so it can unset the pointer
 * and do whatever it needs, so 'scanner->thread' is exclusively
 * managed by main thread.
 *
 * The other shared data 'scanner->pending_scan' is managed by the
 * main thread only when 'scanner->thread' is unset. If there is a
 * worker thread the main thread should never touch that list, thus
 * there is *NO NEED FOR LOCKS*.
 *
 * The thread will stop its work by checking 'scanner->pending_stop',
 * this is also done without a lock as there is no need for such thing
 * given above. The stop is also voluntary and it can happen on a
 * second iteration of work.
 */
static gpointer
scanner_thread_work(gpointer data)
{
    GList *lst;
    scanner_t *scanner = data;
    scanner_pending_t *device_pending;
    GTimer *timer_scanner = NULL;

    log_info("started scanner thread , [ pid : %d ] , bus_name = %s" , getpid() , bus_name);

    timer_scanner = g_timer_new();

    g_list_foreach(scanner->mounts.paths, (GFunc)set_device_path, (gpointer)NULL);

    lst = scanner->pending_scan;
    scanner->pending_scan = NULL;

    while (lst) {
        scanner_pending_t *pending;
        lms_t *lms = NULL;

        if (scanner->pending_stop)
            break;

        pending = lst->data;
        lst = g_list_delete_link(lst, lst);

        log_info("scan category: %s , bus_name = %s", pending->category , bus_name);

        lms = setup_lms(pending->category, scanner);

        if (lms) {

            lms_set_mutex(lms, mtx);

            while (pending->paths) {

                char *path;
                scan_progress_t *scan_progress = NULL;
#ifdef PATCH_LGE
                scanDeviceType *scan_device = NULL;
#endif

                if (scanner->pending_stop)
                    break;

                path = pending->paths->data;
                pending->paths = g_list_delete_link(pending->paths,
                                                    pending->paths);

                g_info("scan category = %s , path = %s , bus_name = %s", pending->category, path , bus_name);

                if(strcmp(path,"/media/")!=0 &&
                   strcmp(path,"/media/usb/")!=0 &&
                   strcmp(path,"/media/mtp/")!=0 ) {
                    device_pending = scanner_pending_device_get_or_add(scanner, pending->category);
                    scanner_pending_add(device_pending, NULL, path);
                    lms_set_device_scan_path(lms, path);
                    log_info("device scan path : %s, %s , bus_name = %s", pending->category, path , bus_name);
                }

                if (!omit_scan_progress) {
                    scan_progress = g_new0(scan_progress_t, 1);
                    scan_progress->conn = g_object_ref(scanner->conn);
                    scan_progress->category = g_strdup(pending->category);
                    scan_progress->path = g_strdup(path);
                    scanner->scan_progress = scan_progress;

#ifdef PATCH_LGE
                    scan_device = g_new0(scanDeviceType, 1);
                    scan_device->conn = g_object_ref(scanner->conn);
                    scan_device->category = g_strdup(pending->category);
                    scan_device->path = g_strdup(path);
                    scanner->scan_device = scan_device;
#endif
                }

                if (!scanner->pending_stop) {

                    log_info("lms_check [ pid : %d ] , bus_name = %s", getpid() , bus_name);

                    lms_check(lms, path);
                }

                if (!scanner->pending_stop && g_file_test(path, G_FILE_TEST_EXISTS)) {

                    log_info("lms_process [ pid : %d ] , path = %s , bus_name = %s", getpid() , path , bus_name);

                    lms_process(lms, path);
                }

                if (scan_progress)
                    g_idle_add(report_scan_progress_and_free, scan_progress);

#ifdef PATCH_LGE
                if (scan_device)
                    g_idle_add(report_scan_device_and_free, scan_device);
#endif

                g_free(path);
            }
            lms_free(lms);
        }

        scanner_pending_free(pending);
    }

    log_info("finished scanner thread , bus_name = %s" , bus_name);

    refresh_database();

    if (scanner->unavail_files){
        g_list_foreach(scanner->unavail_files, update_db_play_ng_file, NULL);
        g_list_free(scanner->unavail_files);
        scanner->unavail_files = NULL;
    }

    scanner->cleanup_thread_idler = g_idle_add(scanner_thread_cleanup, scanner);

    log_info("Finished scanner thread , Elapsed time: %0.3f seconds [ pid : %d ] [ bus_name : %s ]\n" , g_timer_elapsed(timer_scanner, NULL), getpid(), bus_name);

    g_timer_destroy (timer_scanner);

    return scanner;
}

static void
do_scan(scanner_t *scanner)
{
    log_info("[ pid : %d ] , bus_name = %s" , getpid() , bus_name);

    scanner->thread = g_thread_new("scanner", scanner_thread_work, scanner);

    scanner_is_scanning_changed(scanner);
    scanner_write_lock_changed(scanner);
#ifdef PATCH_LGE
    scanner_status_changed(scanner);
#endif
}

static void
dbus_scanner_scan(GDBusMethodInvocation *inv, scanner_t *scanner, GVariant *params)
{
    log_info("bus_name = %s" , bus_name);

    if (scanner->thread) {
        g_dbus_method_invocation_return_dbus_error(
            inv, "org.lightmediascanner.AlreadyScanning",
            "Scanner was already scanning.");
        return;
    }

    if (scanner->write_lock) {
        g_dbus_method_invocation_return_dbus_error(
            inv, "org.lightmediascanner.WriteLocked",
            "Data Base has a write lock for another process.");
        return;
    }

    dbus_scanner_scan_params_set(scanner, params);

    if (scanner->mounts.timer) { // prevent scan twice
        g_source_remove(scanner->mounts.timer);
        scanner->mounts.timer = 0;
        if (scanner->mounts.pending) {
            g_list_free_full(scanner->mounts.pending, g_free);
            scanner->mounts.pending = NULL;
        }
    }
    log_info("dbus_scanner_scan : do_scan");
    do_scan(scanner);

    g_dbus_method_invocation_return_value(inv, NULL);
}

static void
dbus_scanner_stop(GDBusMethodInvocation *inv, scanner_t *scanner)
{
    log_info("bus_name = %s" , bus_name);

    if (!scanner->thread) {
        g_dbus_method_invocation_return_dbus_error(
            inv, "org.lightmediascanner.NotScanning",
            "Scanner was already stopped.");
        return;
    }
    if (scanner->pending_stop) {
        g_dbus_method_invocation_return_dbus_error(
            inv, "org.lightmediascanner.AlreadyStopping",
            "Scanner was already being stopped.");
        return;
    }

    scanner->pending_stop = g_object_ref(inv);
#ifdef PATCH_LGE
    scanner_status_changed(scanner);
#endif
}

static void
dbus_scanner_request_write_lock(GDBusMethodInvocation *inv, scanner_t *scanner, const char *sender)
{
    if (check_write_locked(scanner)) {
        if (scanner->write_lock && strcmp(scanner->write_lock, sender) == 0)
            g_dbus_method_invocation_return_value(inv, NULL);
        else if (scanner->write_lock)
            g_dbus_method_invocation_return_dbus_error(
                inv, "org.lightmediascanner.AlreadyLocked",
                "Scanner is already locked");
        else
            g_dbus_method_invocation_return_dbus_error(
                inv, "org.lightmediascanner.IsScanning",
                "Scanner is scanning and can't grant a write lock");
        return;
    }

    scanner_acquire_write_lock(scanner, sender);
    g_dbus_method_invocation_return_value(inv, NULL);
}

static void
dbus_scanner_release_write_lock(GDBusMethodInvocation *inv, scanner_t *scanner, const char *sender)
{
    if (!scanner->write_lock || strcmp(scanner->write_lock, sender) != 0) {
        g_dbus_method_invocation_return_dbus_error(
            inv, "org.lightmediascanner.NotLocked",
            "Scanner was not locked by you.");
        return;
    }

    scanner_release_write_lock(scanner);
    g_dbus_method_invocation_return_value(inv, NULL);
}

#ifdef PATCH_LGE
static void update_db_play_ng_file(gpointer data, gpointer user_data)
{
    const char sql_path[] = "UPDATE files SET playng = ? WHERE path LIKE ?";

    sqlite3 *db;
    sqlite3_stmt *stmt;
    int ret;
    char *path = (char *)data;
    size_t path_len = 0;

    ret = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL);
    if (ret != SQLITE_OK) {
        log_warning("Couldn't open '%s': %s", db_path, sqlite3_errmsg(db));
        goto end;
    }

    if (sqlite3_prepare_v2(db, sql_path, -1, &stmt, NULL) != SQLITE_OK) {
        log_warning("Couldn't prepare select : %s", sqlite3_errmsg(db));
        goto cleanup;
    }

    path_len = strlen(path);
    if (path_len > INT_MAX) {
        log_error("ERROR: path_len may overflow");
        goto cleanup;
    }
    else {
      if (sqlite3_bind_text(stmt, 2, path, (int)path_len ,SQLITE_STATIC) != SQLITE_OK) {
        log_warning("Couldn't bind find path :%s error: %s", path, sqlite3_errmsg(db));
        goto cleanup;
      }
    }

    if (sqlite3_bind_int(stmt, 1, 1) != SQLITE_OK) {
        log_warning("Couldn't bind int %s", sqlite3_errmsg(db));
        goto cleanup;
    }

    ret = sqlite3_step(stmt);
    if (ret !=  SQLITE_DONE){
        log_warning("Couldn't run SQL to update files playng, ret=%d: %s",
                  ret, sqlite3_errmsg(db));
    }

cleanup:
    sqlite3_reset(stmt);
    sqlite3_finalize(stmt);

end:
    sqlite3_close(db);
}

static void dbus_scanner_set_playNG(GDBusMethodInvocation *inv, scanner_t *scanner, GVariant *params)
{
    char *path = NULL;

    g_variant_get(params, "(s)", &path);
    scanner->unavail_files = g_list_append(scanner->unavail_files, path);

    if (scanner->thread == NULL){
        g_list_foreach(scanner->unavail_files, update_db_play_ng_file, NULL);
        g_list_free(scanner->unavail_files);
        scanner->unavail_files = NULL;
    }

    g_dbus_method_invocation_return_value(inv, NULL);
}
#endif

static void
scanner_method_call(GDBusConnection *conn, const char *sender, const char *opath, const char *iface, const char *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data)
{
    scanner_t *scanner = data;

    log_info("bus_name = %s , method = [[[[[[[[[[ %s ]]]]]]]]]]]" , bus_name , method);

    if (strcmp(method, "Scan") == 0)
        dbus_scanner_scan(inv, scanner, params);
    else if (strcmp(method, "Stop") == 0)
        dbus_scanner_stop(inv, scanner);
    else if (strcmp(method, "RequestWriteLock") == 0)
        dbus_scanner_request_write_lock(inv, scanner, sender);
    else if (strcmp(method, "ReleaseWriteLock") == 0)
        dbus_scanner_release_write_lock(inv, scanner, sender);
#ifdef PATCH_LGE
    else if (strcmp(method, "SetPlayNG") == 0)
        dbus_scanner_set_playNG(inv, scanner, params);
#endif
}

static void
scanner_update_id_changed(scanner_t *scanner)
{
    log_info("scanner_dbus_props_changed(...) Called.");

    if (scanner->changed_props.idler == 0)
        scanner->changed_props.idler = g_idle_add(scanner_dbus_props_changed,
                                                  scanner);

    scanner->changed_props.update_id = TRUE;
}

static void
scanner_categories_changed(scanner_t *scanner)
{
    log_info("scanner_dbus_props_changed(...) Called.");

    if (scanner->changed_props.idler == 0)
        scanner->changed_props.idler = g_idle_add(scanner_dbus_props_changed,
                                                  scanner);

    scanner->changed_props.categories = TRUE;
}

static GVariant *
scanner_get_prop(GDBusConnection *conn, const char *sender, const char *opath, const char *iface, const char *prop,  GError **error, gpointer data)
{
    scanner_t *scanner = data;
    GVariant *ret;

    log_info("prop = %s , bus_name = %s" , prop , bus_name);

    if (strcmp(prop, "DataBasePath") == 0)
        ret = g_variant_new_string(db_path);
    else if (strcmp(prop, "IsScanning") == 0)
        ret = g_variant_new_boolean(scanner->thread != NULL);
    else if (strcmp(prop, "WriteLocked") == 0)
        ret = g_variant_new_boolean(check_write_locked(scanner));
    else if (strcmp(prop, "UpdateID") == 0) {
        guint64 update_id = 0;

        if (!check_write_locked(scanner))
            update_id = get_update_id();
        if (update_id > 0 && update_id != scanner->update_id) {
            scanner->update_id = update_id;
            scanner_update_id_changed(scanner);
        }
        ret = g_variant_new_uint64(scanner->update_id);
    } else if (strcmp(prop, "Categories") == 0)
        ret = categories_get_variant();
#ifdef PATCH_LGE
    else if(strcmp(prop, "ScannerStatus"))
    {
        lms_scanner_status_t status = LMS_SCANNER_STATUS_IDLE;
        status = check_scanner_status(scanner);

        log_info("ScannerStatus : %d" , status);

        ret = g_variant_new_uint32(status);
    }
#endif
    else
        ret = NULL;

    return ret;
}

struct scanner_should_monitor_data {
    const char *mountpoint;
    gboolean should_monitor;
};

static void
category_should_monitor(gpointer key, gpointer value, gpointer user_data)
{
    const scanner_category_t *sc = value;
    struct scanner_should_monitor_data *data = user_data;

    if (data->should_monitor)
        return;

    if (scanner_category_allows_path(sc->dirs, data->mountpoint)) {
        data->should_monitor = TRUE;
        return;
    }
}

static gboolean
should_monitor(const char *mountpoint)
{
    struct scanner_should_monitor_data data = {mountpoint, FALSE};
    g_hash_table_foreach(categories, category_should_monitor, &data);
    return data.should_monitor;
}

static GList *
scanner_mounts_parse(scanner_t *scanner)
{
    GList *paths = NULL;
    GIOStatus status;
    GError *error = NULL;

    status = g_io_channel_seek_position(scanner->mounts.channel, 0, G_SEEK_SET,
                                        &error);
    if (error) {
        log_warning("Couldn't rewind mountinfo channel: %s", error->message);
        g_error_free(error);
        return NULL;
    }

    do {
        gchar *str = NULL;
        gsize len = 0;
        int mount_id, parent_id, major, minor;
        char root[1024], mountpoint[1024];

        status = g_io_channel_read_line(scanner->mounts.channel, &str, NULL,
                                        &len, &error);
        switch (status) {
            case G_IO_STATUS_AGAIN:
                if (str) {
                    g_free(str);
                }
                continue;
            case G_IO_STATUS_NORMAL:
                str[len] = '\0';
                break;
            case G_IO_STATUS_ERROR:
                if (error) {
                    log_warning("Couldn't read line of mountinfo: %s", error->message);
                    g_error_free(error);
                }
                break;
            case G_IO_STATUS_EOF:
                g_free(str);
                return g_list_sort(paths, (GCompareFunc)strcmp);
            default : 
                log_warning("Unknown status type");
        }

        if(str) {
            if (sscanf(str, "%d %d %d:%d %1023s %1023s", &mount_id, &parent_id,
                       &major, &minor, root, mountpoint) != 6)
                log_warning("Error parsing mountinfo line: %s", str);
            else {
                char *mp = g_strcompress(mountpoint);

                if (!should_monitor(mp)) {
                    log_debug("Ignored mountpoint: %s. Not in any category.", mp);
                    g_free(mp);
                } else {
                    log_debug("Got mountpoint: %s", mp);
//                  set_device_path(mp);
                    paths = g_list_prepend(paths, mp);
                }
            }
            g_free(str);
        }
    } while (TRUE);
}

static void
scanner_mount_pending_add_or_delete(scanner_t *scanner, gchar *mountpoint)
{
    GList *itr;

    for (itr = scanner->mounts.pending; itr != NULL; itr = itr->next) {
        if (strcmp(itr->data, mountpoint) == 0) {
            g_free(mountpoint);
            return;
        }
    }

    scanner->mounts.pending = g_list_prepend(scanner->mounts.pending,
                                             mountpoint);
}

static void
category_scan_pending_mountpoints(gpointer key, gpointer value, gpointer user_data)
{
    scanner_pending_t *pending = NULL;
    scanner_category_t *sc = value;
    scanner_t *scanner = user_data;
    GList *n;

    for (n = scanner->mounts.pending; n != NULL; n = n->next) {
        const char *mountpoint = n->data;

        if (scanner_category_allows_path(sc->dirs, mountpoint)) {
            if (!pending)
                pending = scanner_pending_get_or_add(scanner, sc->category);

            scanner_pending_add(pending, NULL, mountpoint);
        }
    }
}

static void
scan_mountpoints(scanner_t *scanner)
{
    g_assert(scanner->thread == NULL);

    g_hash_table_foreach(categories, category_scan_pending_mountpoints, scanner);
    g_list_free_full(scanner->mounts.pending, g_free);
    scanner->mounts.pending = NULL;

    do_scan(scanner);
}

static gboolean
on_scan_mountpoints_timeout(gpointer data)
{
    scanner_t *scanner = data;

    if (!scanner->thread)
        scan_mountpoints(scanner);

    scanner->mounts.timer = 0;
    return FALSE;
}

static gboolean
on_mounts_changed(GIOChannel *source, GIOCondition cond, gpointer data)
{
    scanner_t *scanner = data;
    GList *oldpaths = scanner->mounts.paths;
    GList *newpaths = scanner_mounts_parse(scanner);
    GList *o, *n;
    GList *current = NULL;

    for (o = oldpaths, n = newpaths; o != NULL && n != NULL;) {
        int r = strcmp(o->data, n->data);
        if (r == 0) {
            current = g_list_prepend(current, o->data);
            g_free(n->data);
            o = o->next;
            n = n->next;
        } else if (r < 0) { /* removed */
            scanner_mount_pending_add_or_delete(scanner, o->data);
            o = o->next;
        } else { /* added (goes to both pending and current) */
            current = g_list_prepend(current, g_strdup(n->data));
            scanner_mount_pending_add_or_delete(scanner, n->data);
            n = n->next;
        }
    }

    for (; o != NULL; o = o->next)
        scanner_mount_pending_add_or_delete(scanner, o->data);
    for (; n != NULL; n = n->next) {
        current = g_list_prepend(current, g_strdup(n->data));
        scanner_mount_pending_add_or_delete(scanner, n->data);
    }

    scanner->mounts.paths = g_list_reverse(current);

    g_list_free(oldpaths);
    g_list_free(newpaths);

    if (scanner->mounts.timer) {
        g_source_remove(scanner->mounts.timer);
        scanner->mounts.timer = 0;
    }
    if (scanner->mounts.pending) {
        scanner->mounts.timer = g_timeout_add(SCAN_MOUNTPOINTS_TIMEOUT * 1000,
                                              on_scan_mountpoints_timeout,
                                              scanner);
    }

    return TRUE;
}

static void
scanner_destroyed(gpointer data)
{
    scanner_t *scanner = data;

    log_info("scanner destoryed !!!!!");

    g_free(scanner->write_lock);

    if (scanner->mounts.timer)
        g_source_remove(scanner->mounts.timer);
    if (scanner->mounts.watch)
        g_source_remove(scanner->mounts.watch);
    if (scanner->mounts.channel)
        g_io_channel_unref(scanner->mounts.channel);
    g_list_free_full(scanner->mounts.paths, g_free);
    g_list_free_full(scanner->mounts.pending, g_free);

    if (scanner->write_lock_name_watcher)
        g_bus_unwatch_name(scanner->write_lock_name_watcher);

    if (scanner->thread) {
        log_warning("Shutdown while scanning, wait...");
        g_thread_join(scanner->thread);
    }

    if (scanner->cleanup_thread_idler) {
        g_source_remove(scanner->cleanup_thread_idler);
        scanner_thread_cleanup(scanner);
    }

    if (scanner->changed_props.idler) {
        g_source_remove(scanner->changed_props.idler);
        scanner_dbus_props_changed(scanner);
    }

    g_assert(scanner->thread == NULL);
    g_assert(scanner->pending_scan == NULL);
    g_assert(scanner->pending_device_scan == NULL);
    g_assert(scanner->cleanup_thread_idler == 0);
    g_assert(scanner->pending_stop == NULL);
    g_assert(scanner->changed_props.idler == 0);

    g_free(scanner);
}

static const GDBusInterfaceVTable scanner_vtable = {
    scanner_method_call,
    scanner_get_prop,
    NULL
};

static void
on_name_acquired(GDBusConnection *conn, const gchar *name, gpointer data)
{
    GDBusInterfaceInfo *iface;
    GError *error = NULL;
    unsigned id;
    scanner_t *scanner;

    scanner = g_new0(scanner_t, 1);
    g_assert(scanner != NULL);
    scanner->conn = conn;
    scanner->pending_scan = NULL;
    scanner->pending_device_scan = NULL;
    scanner->update_id = get_update_id();
    scanner->unavail_files = NULL;
    scanner->thread = NULL;

    iface = g_dbus_node_info_lookup_interface(introspection_data, BUS_IFACE);

    id = g_dbus_connection_register_object(conn,
                                           object_path,
                                           iface,
                                           &scanner_vtable,
                                           scanner,
                                           scanner_destroyed,
                                           NULL);
    g_assert(id > 0);

    scanner->mounts.channel = g_io_channel_new_file("/proc/self/mountinfo",
                                                    "r", &error);
    if (error) {
      log_warning("No /proc/self/mountinfo file: %s. Disabled mount monitoring.",
                error->message);
      g_error_free(error);
    } else {
        // scanner->mounts.watch = g_io_add_watch(scanner->mounts.channel,
        //                                        G_IO_ERR,
        //                                        on_mounts_changed, scanner);
        scanner->mounts.paths = scanner_mounts_parse(scanner);
    }

    if (startup_scan) {

        log_info("Do startup scan , [ pid : %d ] , bus_name = %s" , getpid() , bus_name);

        g_hash_table_foreach(categories, scan_params_all, scanner);
        do_scan(scanner);
    }

    scanner_update_id_changed(scanner);
    scanner_write_lock_changed(scanner);
    scanner_is_scanning_changed(scanner);
    scanner_categories_changed(scanner);
#ifdef PATCH_LGE
    scanner_status_changed(scanner);
#endif

    log_info("Acquired name org.lightmediascanner and registered object , bus_name = %s" , bus_name);

}

static gboolean
str_array_find(const GArray *arr, const char *str)
{
    char **itr;
    for (itr = (char **)arr->data; *itr; itr++)
        if (strcmp(*itr, str) == 0)
            return TRUE;

    return FALSE;
}

static scanner_category_t *
scanner_category_get_or_add(const char *category)
{
    scanner_category_t *sc = g_hash_table_lookup(categories, category);

    log_debug("[ pid : %d ] , category = %s" , getpid() , category);

    if (sc)
        return sc;

    sc = scanner_category_new(category);
    g_hash_table_insert(categories, sc->category, sc);
    return sc;
}

static void
scanner_category_add_parser(scanner_category_t *sc, const char *parser)
{
    char *p;

    if (str_array_find(sc->parsers, parser))
        return;

    p = g_strdup(parser);
    g_array_append_val(sc->parsers, p);
}

static void
scanner_category_add_dir(scanner_category_t *sc, const char *dir)
{
    char *p;

    if (str_array_find(sc->dirs, dir))
        return;

    p = g_strdup(dir);
    g_array_append_val(sc->dirs, p);
}

static void
scanner_category_add_skip_dir(scanner_category_t *sc, const char *skip_dir)
{
    char *p;

    if (str_array_find(sc->skip_dirs, skip_dir))
        return;

    p = g_strdup(skip_dir);
    g_array_append_val(sc->skip_dirs, p);
}

static int
populate_categories(void *data, const char *path)
{
    struct lms_parser_info *info;
    const char * const *itr;
    long do_parsers = (long)data;

    log_debug("[ pid : %d ] , do_parsers = %ld , path = %s" , getpid() , do_parsers , path);

    info = lms_parser_info(path);
    if (!info)
        return 1;

    log_debug("[ pid : %d ] , info->name = %s , path = %s" , getpid() , info->name , path);

    if (strcmp(info->name, "dummy") == 0)
        goto end;

    if (!info->categories)
        goto end;


    for (itr = info->categories; *itr != NULL; itr++) {
        scanner_category_t *sc;

        log_info("[ pid : %d ] , path = %s , info->name = %s , *itr = %s" , getpid() , path , info->name , *itr);

        if (strcmp(*itr, "all") == 0)
            continue;

        sc = scanner_category_get_or_add(*itr);

        if (do_parsers)
            scanner_category_add_parser(sc, path);
    }

end:
    lms_parser_info_free(info);

    return 1;
}

static int
populate_category_all_parsers(void *data, const char *path)
{
    struct lms_parser_info *info;
    scanner_category_t *sc = data;

    info = lms_parser_info(path);
    if (!info)
        return 1;

    if (strcmp(info->name, "dummy") != 0)
        scanner_category_add_parser(sc, path);

    lms_parser_info_free(info);
    return 1;
}

static int
populate_category_parsers(void *data, const char *path, const struct lms_parser_info *info)
{
    scanner_category_t *sc = data;

    if (strcmp(info->name, "dummy") != 0)
        scanner_category_add_parser(sc, path);

    return 1;
}

static void
_populate_parser_internal(const char *category, const char *parser)
{
    scanner_category_t *sc = scanner_category_get_or_add(category);

    if (strcmp(parser, "all") == 0)
        lms_parsers_list(populate_category_all_parsers, sc);
    else if (strcmp(parser, "all-category") == 0)
        lms_parsers_list_by_category(sc->category, populate_category_parsers,
                                     sc);
    else
        scanner_category_add_parser(sc, parser);
}

static void
populate_parser_foreach(gpointer key, gpointer value, gpointer user_data)
{
    const char *category = key;
    const char *parser = user_data;
    _populate_parser_internal(category, parser);
}

static void
populate_parser(const char *category, const char *parser)
{
    if (!category)
        g_hash_table_foreach(categories, populate_parser_foreach,
                             (gpointer)parser);
    else
        _populate_parser_internal(category, parser);
}

static void
_populate_dir_internal(const char *category, const char *dir)
{
    scanner_category_t *sc = scanner_category_get_or_add(category);

    if (strcmp(dir, "defaults") != 0)
        scanner_category_add_dir(sc, dir);
    else {
        struct {
            const char *cat;
            const char *path;
        } *itr, defaults[] = {
            {"audio", g_get_user_special_dir(G_USER_DIRECTORY_MUSIC)},
            {"video", g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS)},
            {"picture", g_get_user_special_dir(G_USER_DIRECTORY_PICTURES)},
            {"multimedia", g_get_user_special_dir(G_USER_DIRECTORY_MUSIC)},
            {"multimedia", g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS)},
            {"multimedia", g_get_user_special_dir(G_USER_DIRECTORY_PICTURES)},
            {NULL, NULL}
        };
        for (itr = defaults; itr->cat != NULL; itr++) {
            if (strcmp(itr->cat, category) == 0) {
                if (itr->path && itr->path[0] != '\0')
                    scanner_category_add_dir(sc, itr->path);
                else
                    log_warning("Requested default directories but "
                              "xdg-user-dirs is not setup. Category %s",
                              category);
            }
        }
    }
}

static void
populate_dir_foreach(gpointer key, gpointer value, gpointer user_data)
{
    const char *category = key;
    const char *dir = user_data;
    _populate_dir_internal(category, dir);
}

static void
populate_dir(const char *category, const char *dir)
{
    if (!category)
        g_hash_table_foreach(categories, populate_dir_foreach,
                             (gpointer)dir);
    else
        _populate_dir_internal(category, dir);
}

static void
_populate_skip_dir_internal(const char *category, const char *skip_dir)
{
    scanner_category_t *sc = scanner_category_get_or_add(category);

    if (strcmp(skip_dir, "defaults") != 0)
        scanner_category_add_skip_dir(sc, skip_dir);
    else {
        struct {
            const char *cat;
            const char *path;
        } *itr, defaults[] = {
            {"audio", g_get_user_special_dir(G_USER_DIRECTORY_MUSIC)},
            {"video", g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS)},
            {"picture", g_get_user_special_dir(G_USER_DIRECTORY_PICTURES)},
            {"multimedia", g_get_user_special_dir(G_USER_DIRECTORY_MUSIC)},
            {"multimedia", g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS)},
            {"multimedia", g_get_user_special_dir(G_USER_DIRECTORY_PICTURES)},
            {NULL, NULL}
        };
        for (itr = defaults; itr->cat != NULL; itr++) {
            if (strcmp(itr->cat, category) == 0) {
                if (itr->path && itr->path[0] != '\0')
                    scanner_category_add_skip_dir(sc, itr->path);
                else
                    log_warning("Requested default directories but "
                              "xdg-user-dirs is not setup. Category %s",
                              category);
            }
        }
    }
}

static void
populate_skip_dir_foreach(gpointer key, gpointer value, gpointer user_data)
{
    const char *category = key;
    const char *dir = user_data;
    _populate_skip_dir_internal(category, dir);
}

static void
populate_skip_dir(const char *category, const char *skip_dir)
{
    if (!category)
        g_hash_table_foreach(categories, populate_skip_dir_foreach,
                             (gpointer)skip_dir);
    else
        _populate_skip_dir_internal(category, skip_dir);
}

static void
debug_categories(gpointer key, gpointer value, gpointer user_data)
{
    const scanner_category_t *sc = value;
    const char * const *itr;
    log_info("category: %s", sc->category);

    if (sc->parsers->len) {
        for (itr = (const char * const *)sc->parsers->data; *itr != NULL; itr++)
            log_info("  parser: %s", *itr);
    } else
            log_info("  parser: <none>");

    if (sc->dirs->len) {
        for (itr = (const char * const *)sc->dirs->data; *itr != NULL; itr++)
            log_info("  dir...: %s", *itr);
    } else
            log_info("  dir...: <none>");

    if (sc->skip_dirs->len) {
        for (itr = (const char * const *)sc->skip_dirs->data; *itr != NULL; itr++)
            log_info("  skip_dir...: %s", *itr);
    } else
            log_info("  skip_dir...: <none>");
}

// OYK_2019_05_31 : For debugging.....
#if 0
void runExternalProcess(char* command)
{
    FILE* fp = NULL;
    char buffer[512];

    log_info("command = %s ...... [[[ START ]]]" , command);

    fp = popen(command , "r");

    // [ Static Analysis ] 7342997 : Dereference null return value
    //                     7342191 : do not call
    if (fp != NULL) {

        while (fgets(buffer , 512 , fp)) {

            log_info("\t%s" , buffer);

        }

        pclose(fp);
    }

    log_info("command = %s ...... [[[ END ]]]" , command);

}
#endif

static gboolean
on_sig_term(gpointer data)
{
    GMainLoop *loop = data;

    log_error("got SIGTERM, exit.");

    g_main_loop_quit(loop);
    return FALSE;
}

static gboolean
on_sig_int(gpointer data)
{
    GMainLoop *loop = data;

    log_error("got SIGINT, exit.");

    g_main_loop_quit(loop);
    return FALSE;
}

int
main(int argc, char *argv[])
{
    int ret = EXIT_SUCCESS;
    unsigned id = 0;
    GMainLoop *loop;
    GError *error = NULL;
    GOptionContext *opt_ctx;

    char **parsers = NULL;
    char **dirs = NULL;
    char **skip_dirs = NULL;

    lms_conf_info_t *parsedInfo = NULL;
 
    struct sched_param param = {.sched_priority = 0, };

    if(setlocale (LC_ALL,"") == NULL) {
       log_error("ERROR: setlocale failed");
       return 1;
    }

    // OYK_2019_03_25 : Moved to the below.
    //init_log();

    // [ Static Analysis ] 989333 : Use of untrusted string value
    #if 0
    FILE *session;
    const char str[MAX_COLS] = {'\0', };
    #endif
    FILE *version_text = NULL;
    const char version_str[128] = {0,};

    GOptionEntry opt_entries[] = {
        {"bus-name", 'b', 0, G_OPTION_ARG_STRING, &bus_name,
         "bus name to LightMediaScanner. ",
         "NAME"},
        {"object-path", 'H', 0, G_OPTION_ARG_STRING, &object_path,
         "object path to LightMediaScanner. ",
         "NAME"},
        {"db-path", 'p', 0, G_OPTION_ARG_FILENAME, &db_path,
         "Path to LightMediaScanner SQLit3 data base, "
         "defaults to \"~/.config/lightmediascannerd/db.sqlite3\".",
         "PATH"},
        {"commit-interval", 'c', 0, G_OPTION_ARG_INT, &commit_interval,
         "Execute SQL COMMIT after NUMBER files are processed, "
         "defaults to 100.",
         "NUMBER"},
        {"slave-timeout", 't', 0, G_OPTION_ARG_INT, &slave_timeout,
         "Number of seconds to wait for slave to reply, otherwise kills it. "
         "Defaults to 60.",
         "SECONDS"},
        {"delete-older-than", 'd', 0, G_OPTION_ARG_INT, &delete_older_than,
         "Delete from database files that have 'dtime' older than the given "
         "number of DAYS. If not specified LightMediaScanner will keep the "
         "files in the database even if they are gone from the file system "
         "and if they appear again and have the same 'mtime' and 'size' "
         "it will be restored ('dtime' set to 0) without the need to parse "
         "the file again (much faster). This is useful for removable media. "
         "Use a negative number to disable this behavior. "
         "Defaults to 30.",
         "DAYS"},
        {"vacuum", 'V', 0, G_OPTION_ARG_NONE, &vacuum,
         "Execute SQL VACUUM after every scan.", NULL},
        {"startup-scan", 'S', 0, G_OPTION_ARG_NONE, &startup_scan,
         "Execute full scan on startup.", NULL},
        {"omit-scan-progress", 0, 0, G_OPTION_ARG_NONE, &omit_scan_progress,
         "Omit the ScanProgress signal during scans. This will avoid the "
         "overhead of D-Bus signal emission and may slightly improve the "
         "performance, but will make the listener user-interfaces less "
         "responsive as they won't be able to tell the user what is happening.",
         NULL},
        {"charset", 'C', 0, G_OPTION_ARG_STRING_ARRAY, &charsets,
         "Extra charset to use. (Multiple use)", "CHARSET"},
        {"parser", 'P', 0, G_OPTION_ARG_STRING_ARRAY, &parsers,
         "Parsers to use, defaults to all. Format is 'category:parsername' or "
         "'parsername' to apply parser to all categories. The special "
         "parsername 'all' declares all known parsers, while 'all-category' "
         "declares all parsers of that category. If one parser is provided, "
         "then no defaults are used, you can pre-populate all categories "
         "with their parsers by using --parser=all-category.",
         "CATEGORY:PARSER"},
        {"directory", 'D', 0, G_OPTION_ARG_STRING_ARRAY, &dirs,
         "Directories to use, defaults to FreeDesktop.Org standard. "
         "Format is 'category:directory' or 'path' to "
         "apply directory to all categories. The special directory "
         "'defaults' declares all directories used by default for that "
         "category. If one directory is provided, then no defaults are used, "
         "you can pre-populate all categories with their directories by "
         "using --directory=defaults.",
         "CATEGORY:DIRECTORY"},
        {"skip-dir", 's', 0, G_OPTION_ARG_STRING_ARRAY, &skip_dirs,
         "Directories to skip. "
         "Format is 'category:directory' or 'path' to "
         "skip directory to all categories.",
         "CATEGORY:DIRECTORY"},
        {NULL, 0, 0, 0, NULL, NULL, NULL}
    };

    opt_ctx = g_option_context_new(
        "\nLightMediaScanner D-Bus daemon.\n\n"
        "Usually there is no need to declare options, defaults should "
        "be good. However one may want the defaults and in addition to scan "
        "everything in an USB with:\n\n"
        "\tlightmediascannerd --directory=defaults --parser=all-category "
        "--directory=usb:/media/usb --parser=usb:all");
    g_option_context_add_main_entries(opt_ctx, opt_entries,
                                      "lightmediascannerd");
    if (!g_option_context_parse(opt_ctx, &argc, &argv, &error)) {
        g_option_context_free(opt_ctx);
        g_error("Option parsing failed: %s\n", error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }

    if (strcmp(bus_name, REAR_LMS_BUS_NAME) == 0) {
        lmsTarget = LMS_TARGET_REAR;
    }

    // OYK_2019_03_25: For separating the front from the rear log.
    if (lmsTarget == LMS_TARGET_REAR) {
        init_log_rear();
    }
    else {
        init_log();
    }

    // [ Static Analysis ] 989333 : Use of untrusted string value
    #if 0
    if((session = fopen("/tmp/session_appmgr", "rt")) == NULL) {
        log_warning("Couldn't open /tmp/session_appmgr");
    }
    else {

        if (fgets(str, MAX_COLS, session) != NULL) {

            if (strlen(str) > 0) {
                setenv("DBUS_SESSION_BUS_ADDRESS", str, 1);
            }
            else {
                log_error("Can't set DBUS_SESSION_BUS_ADDRESS");
            }
        }

        fclose(session);
    }
    #endif

    if ((version_text = fopen("/app/share/version.txt", "r")) == NULL) {
        log_warning("Can't open version text");
    } else {
        if (fgets(version_str, 128, version_text) != NULL) {
            if (strlen(version_str) > 0) {
                log_info("version string=[%s]", version_str);
                if (strstr(version_str, "CHN") != NULL)
                    country = lms_country_chn;
                else if (strstr(version_str, "chn") != NULL)
                    country = lms_country_chn;
            } else {
                log_error("Can't read version string, use default");
            }
        }
        if (fclose(version_text) != 0) {
            log_error("ERROR: fclose is giving error");
            return EXIT_FAILURE;
        } else {
            version_text = NULL;
        }
    }

    if (shared_lock_init() == -1) {
        g_error("shared_lock_init\n");
        return EXIT_FAILURE;
    }

    parsedInfo = (lms_conf_info_t *)calloc(1, sizeof(lms_conf_info_t));

    if (parsedInfo == NULL)
        return EXIT_FAILURE;

    log_info(".................... STARTING .................... , bus_name = %s" , bus_name);


    if(sched_setscheduler(0, SCHED_OTHER, &param) != 0) {
       log_error("ERROR: Failed to set scheduler");
    } else {
       log_info("Info: set scheduler to SCHED_OTHER");
    }

    InitConf();
    ParseFile(parsedInfo);

    if (parsedInfo->commit_interval != 0)
        commit_interval = parsedInfo->commit_interval;
    if (fabs(parsedInfo->commit_duration - 0.0f) > DBL_EPSILON)
        commit_duration = parsedInfo->commit_duration;
    if (parsedInfo->slave_timeout != 0)
        slave_timeout = parsedInfo->slave_timeout;
    if (parsedInfo->delete_older_than != 0)
        delete_older_than = parsedInfo->delete_older_than;

    vacuum              = parsedInfo->vacuum;
    startup_scan        = parsedInfo->startup_scan;

    #if defined(ENABLE_FRONT_REAR_SEPARATE_STARTUP_SCAN_OPTION)
        startup_scan_rear = parsedInfo->startup_scan_rear;
    #endif

    omit_scan_progress  = parsedInfo->omit_scan_progress;
    charset_detect_level= parsedInfo->chardet_level;
    charsets            = parsedInfo->charsets;
    parsers             = parsedInfo->parsers;
    skip_dirs           = parsedInfo->skip_dir;

    #if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
        if (parsedInfo->maxFileScanCount != 0) {
            maxFileScanCount = parsedInfo->maxFileScanCount;
        }
        isPrintDirectoryStructure  = parsedInfo->isPrintDirectoryStructure;
    #endif

    g_option_context_free(opt_ctx);

    categories = g_hash_table_new_full(
        g_str_hash, g_str_equal, NULL,
        (GDestroyNotify)scanner_category_destroy);

    if (!db_path)
        db_path = g_strdup_printf("%s/lightmediascannerd/db.sqlite3", g_get_user_config_dir());

    if (!g_file_test(db_path, G_FILE_TEST_EXISTS)) {

        char *dname = g_path_get_dirname(db_path);

        if (dname == NULL) {
             log_error("[ pid : %d ] , bus_name = %s , couldn't get directory", getpid() , bus_name );
             ret = EXIT_FAILURE;
             goto end_options;
        }

        if (!g_file_test(dname, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {

            if (g_mkdir_with_parents(dname, 0755) != 0) {
                 log_error("[ pid : %d ] , bus_name = %s , Couldn't create directory [ %s ]", getpid() , bus_name , dname);
                 g_free(dname);
                 ret = EXIT_FAILURE;
                 goto end_options;
            }
        }
        g_free(dname);
    }

#if 0
    if (lmsTarget == LMS_TARGET_REAR) {
        runExternalProcess("df -h");
        runExternalProcess("ls -al /rw_data/service/lightmediascanner/");
    }
#endif

    log_info("+ create database [ pid : %d ] , bus_name = %s", getpid() , bus_name);

    pthread_mutex_lock(mtx);
    if (lms_create_database(db_path) != 0) {

        log_error("[[[ ERROR ]]] lms_create_database(...) FAILED!!!!! [ pid : %d ] , bus_name = %s" , getpid() , bus_name);
        pthread_mutex_unlock(mtx);

        return EXIT_FAILURE;
    }
    pthread_mutex_unlock(mtx);

    log_info("- create database [ pid : %d ] , bus_name = %s" , getpid() , bus_name);

    if (!parsers)
        lms_parsers_list(populate_categories, (void *)1L);
    else {
        char **itr;

        lms_parsers_list(populate_categories, (void *)0L);

        for (itr = parsers; *itr != NULL; itr++) {
            char *sep = strchr(*itr, ':');
            const char *path;
            if (!sep)
                path = *itr;
            else {
                path = sep + 1;
                *sep = '\0';
            }

            if (path[0] != '\0')
                populate_parser(sep ? *itr : NULL, path);

            if (sep)
                *sep = ':';
        }
    }

    if (!dirs)
        populate_dir(NULL, "defaults");
    else {
        char **itr;

        for (itr = dirs; *itr != NULL; itr++) {
            char *sep = strchr(*itr, ':');
            const char *path;
            if (!sep)
                path = *itr;
            else {
                path = sep + 1;
                *sep = '\0';
            }

            if (path[0] != '\0')
                populate_dir(sep ? *itr : NULL, path);

            if (sep)
                *sep = ':';
        }
    }

    if (skip_dirs) {
        char **itr;

        for (itr = skip_dirs; *itr != NULL; itr++) {
            char *sep = strchr(*itr, ':');
            const char *path;
            if (!sep)
                path = *itr;
            else {
                path = sep + 1;
                *sep = '\0';
            }

            if (path[0] != '\0')
                populate_skip_dir(sep ? *itr : NULL, path);

            if (sep)
                *sep = ':';
        }
    }

    log_info("[ pid : %d ] , bus_name = %s , object_path = %s , db_path = %s" , getpid() , bus_name , object_path , db_path);

    log_info("commit-interval: %d files", commit_interval);
    #ifdef PATCH_LGE
        log_info("commit_duration: %f", commit_duration);
    #endif

    log_info("slave-timeout = %d seconds , delete_older_than = %d days , charset_detect_level = %d", slave_timeout , delete_older_than , charset_detect_level);

    log_info("startup_scan: %d", startup_scan);
    #if defined(ENABLE_FRONT_REAR_SEPARATE_STARTUP_SCAN_OPTION)
        log_info("startup_scan_rear: %d", startup_scan_rear);
    #endif
    log_info("lmsTarget: %d" , lmsTarget);

    if (charsets) {
        char *tmp = g_strjoinv(", ", charsets);
        log_info("charsets: %s", tmp);
        g_free(tmp);
    } else
        log_info("charsets: <none>");

    #if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
        log_info("maxFileScanCount = %d , isPrintDirectoryStructure = %d", maxFileScanCount , isPrintDirectoryStructure);
    #endif

    g_hash_table_foreach(categories, debug_categories, NULL);

    log_info("start dbus registration... , bus_name = %s" , bus_name);

    introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    g_assert(introspection_xml != NULL);

    if (!bus_name)
        bus_name = g_strdup_printf("org.lightmediascannerd");


    #if defined(ENABLE_FRONT_REAR_SEPARATE_STARTUP_SCAN_OPTION)
        if (lmsTarget == LMS_TARGET_REAR) {

            startup_scan = startup_scan_rear;

            log_info("startup_scan ( %d ) is set by startup_scan_rear ( %d )", startup_scan , startup_scan_rear);
        }
    #endif

    id = g_bus_own_name(G_BUS_TYPE_SESSION, bus_name,
                        G_BUS_NAME_OWNER_FLAGS_NONE,
                        NULL, on_name_acquired, NULL, NULL, NULL);

    log_info("starting main loop , bus_name = %s" , bus_name);
    log_info("fixed version : build warning");

    loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, on_sig_term, loop);
    g_unix_signal_add(SIGINT, on_sig_int, loop);
    g_main_loop_run(loop);

    log_info("main loop is finished , bus_name = %s" , bus_name);

    g_bus_unown_name(id);
    g_main_loop_unref(loop);
    g_dbus_node_info_unref(introspection_data);

end_options:
    g_free(db_path);
    g_free(bus_name);
    g_free(object_path);
    g_strfreev(charsets);
    g_strfreev(parsers);
    g_strfreev(dirs);
    g_strfreev(skip_dirs);
    free(parsedInfo);
    FreeConf();
#if LOCALE_CHARSETS
    g_strfreev(locale_charsets);
#endif

    if (categories)
        g_hash_table_destroy(categories);

    uninit_log();

    return ret;
}
