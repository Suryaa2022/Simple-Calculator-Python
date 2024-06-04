/**
 * Copyright (C) 2008-2011 by ProFUSION embedded systems
 * Copyright (C) 2007 by INdT
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * @author Gustavo Sverzut Barbieri <barbieri@profusion.mobi>
 */

#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <gio/gio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>

#include "lightmediascanner.h"
#include "lightmediascanner_private.h"
#include "lightmediascanner_db_private.h"
#include "lightmediascanner_platform_conf.h"

#define SEPARATE_FILES_FROM_DIRECTORIES_PROCESSING
#define TAB_BUFFER_SIZE		128

struct db {
    sqlite3 *handle;
    sqlite3_stmt *transaction_begin;
    sqlite3_stmt *transaction_commit;
    sqlite3_stmt *get_file_info;
    sqlite3_stmt *insert_file_info;
    sqlite3_stmt *update_file_info;
    sqlite3_stmt *delete_file_info;
    sqlite3_stmt *set_file_dtime;
};
#if 0
#if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)

static const struct lms_string_size g_mediaFileExtensions[] = {

    /* MPEG-1 System*/
    LMS_STATIC_STRING_SIZE(".mp3"),
    //LMS_STATIC_STRING_SIZE(".mpg"),
    //LMS_STATIC_STRING_SIZE(".mpeg"),

    // remove dat file format by HKMC request.
    //LMS_STATIC_STRING_SIZE(".dat"),

    /* WAV */
    LMS_STATIC_STRING_SIZE(".wav"),

    /* ASF */
    LMS_STATIC_STRING_SIZE(".asf"),
    LMS_STATIC_STRING_SIZE(".wma"),
    //LMS_STATIC_STRING_SIZE(".wmv"),

#if defined(ENABLE_APE_AIFF)
    /* Add ape and aiff formats by OEM request*/
    LMS_STATIC_STRING_SIZE(".ape"),
    LMS_STATIC_STRING_SIZE(".aiff"),
    LMS_STATIC_STRING_SIZE(".aifc"),
#endif
    /* MPEG-4 Part14*/
    LMS_STATIC_STRING_SIZE(".m4a"),
    //LMS_STATIC_STRING_SIZE(".mp4"),
    //LMS_STATIC_STRING_SIZE(".m4v"),

    /*DivX - remove divx file format by OEM request*/
    //LMS_STATIC_STRING_SIZE(".divx"),

    /* Ogg */
    LMS_STATIC_STRING_SIZE(".oga"),
    LMS_STATIC_STRING_SIZE(".ogg"),

    /* Avi */
    //LMS_STATIC_STRING_SIZE(".avi"),

    /* MKV */
    LMS_STATIC_STRING_SIZE(".mka"),
    //LMS_STATIC_STRING_SIZE(".mkv"),
    //LMS_STATIC_STRING_SIZE(".mk3d"),
    //LMS_STATIC_STRING_SIZE(".mks"),
    //LMS_STATIC_STRING_SIZE(".webm"),

    /* Flac */
    LMS_STATIC_STRING_SIZE(".flac"),

    /* DTS */
    LMS_STATIC_STRING_SIZE(".dts"),

    /* Dolby */
    LMS_STATIC_STRING_SIZE(".ac3"),
    LMS_STATIC_STRING_SIZE(".ec3"),

    /* Flash */
    //LMS_STATIC_STRING_SIZE(".flv"),
    //LMS_STATIC_STRING_SIZE(".f4v"),
    LMS_STATIC_STRING_SIZE(".f4p"),
    LMS_STATIC_STRING_SIZE(".f4a"),
    //LMS_STATIC_STRING_SIZE(".f4b"),

    /* Real Media */
    //LMS_STATIC_STRING_SIZE(".rm"),
    //LMS_STATIC_STRING_SIZE(".ra"),
    //LMS_STATIC_STRING_SIZE(".ram"),

    /* DSD */
    //LMS_STATIC_STRING_SIZE(".dsf"),
    //LMS_STATIC_STRING_SIZE(".dff"),

    /* AMR */
    //LMS_STATIC_STRING_SIZE(".amr"),

};
#endif
#endif

static lms_plugin_t* audio_dummy_plugin = NULL;
/***********************************************************************
 * Master-Slave communication.
 ***********************************************************************/

#if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
int scandirFilter(const struct dirent* info);
int scandirFilterFilesOnly(const struct dirent* info);
int scandirFilterDirectoriesOnly(const struct dirent* info);
int alphaSortCaseInsensitiveFilesFirst (const struct dirent **a, const struct dirent **b);
#endif

static int
_master_send_path(const struct fds *master, int plen, int dlen, const char *p)
{
    int lengths[2];

    lengths[0] = plen;
    lengths[1] = dlen;

    if (write(master->w, lengths, sizeof(lengths)) < 0) {
        perror("write");
        return -1;
    }

    if (plen < 0) {
       log_error("ERROR: plen may overflow");
    }
    else {
       if (write(master->w, p, (unsigned long)plen) < 0) {
          perror("write");
          return -1;
      }
    }

    return 0;
}

static int
_master_send_finish(const struct fds *master)
{
    const int lengths[2] = {-1, -1};

    if (write(master->w, lengths, sizeof(lengths)) < 0) {
        perror("write");
        return -1;
    }
    return 0;
}

static int
_master_recv_reply(const struct fds *master, struct pollfd *pfd, int *reply, int timeout)
{
    int r;

    r = poll(pfd, 1, timeout);
    if (r < 0) {
        perror("poll");
        return -1;
    }

    if (r == 0)
        return 1;

    long int _read = read(master->r, reply, sizeof(*reply));
    if (_read < 0) {
        log_error("ERROR: _read may underflow");
    }
    else {
        if ((size_t)_read != sizeof(*reply)) {
           perror("read");
           return -2;
        }
    }

    return 0;
}

static int
_slave_send_reply(const struct fds *slave, int reply)
{
    if (write(slave->w, &reply, sizeof(reply)) == 0) {
        perror("write");
        return -1;
    }
    return 0;
}

static int
_slave_recv_path(const struct fds *slave, int *plen, int *dlen, char *path)
{
    int lengths[2] = { 0 , 0 };
    ssize_t r;

    r = read(slave->r, lengths, sizeof(lengths));
    if (r < 0) {
        log_error("read ret value may overflow");
        return -1;
    } else {
        if ((size_t)r != sizeof(lengths)) {
            perror("read");
            return -1;
        }
    }
    *plen = lengths[0];
    *dlen = lengths[1];

    if (*plen == -1)
        return 0;

    if (*plen > PATH_SIZE) {
        log_error("ERROR: path too long (%d/%d)", *plen, PATH_SIZE);
        return -2;
    }

    // [ Static Analysis ] 989447 : Untrusted pointer write
    if ((*plen > 0) && (*plen < PATH_SIZE)) {
       r = read(slave->r, path, *plen);
       if ((r < INT_MIN) || (r > INT_MAX)) {
           log_error("read ret value may overflow");
           return -1;
       }
       else {
           if ((int)r != *plen) {
              log_error("ERROR: could not read whole path %ld/%d", r, *plen);
              return -3;
          }
       }
       path[*plen] = 0;
    }

    return 0;
}


/***********************************************************************
 * Slave-side.
 ***********************************************************************/

static int
_db_compile_all_stmts(struct db *db)
{
    sqlite3 *handle;

    handle = db->handle;
    db->transaction_begin = lms_db_compile_stmt_begin_transaction(handle);
    if (!db->transaction_begin)
        return -1;

    db->transaction_commit = lms_db_compile_stmt_end_transaction(handle);
    if (!db->transaction_commit)
        return -2;

    db->get_file_info = lms_db_compile_stmt_get_file_info(handle);
    if (!db->get_file_info)
        return -4;

    db->insert_file_info = lms_db_compile_stmt_insert_file_info(handle);
    if (!db->insert_file_info)
        return -5;

    db->update_file_info = lms_db_compile_stmt_update_file_info(handle);
    if (!db->update_file_info)
        return -6;

    db->delete_file_info = lms_db_compile_stmt_delete_file_info(handle);
    if (!db->delete_file_info)
        return -6;

    db->set_file_dtime = lms_db_compile_stmt_set_file_dtime(handle);
    if (!db->set_file_dtime)
        return -7;

    return 0;
}

static struct db *
_db_open(const char *db_path)
{
    struct db *db;

    log_info("[ pid : %d ]", getpid());

    db = calloc(1, sizeof(*db));
    if (!db) {
        perror("calloc");
        return NULL;
    }

    if (sqlite3_open(db_path, &db->handle) != SQLITE_OK) {
        log_error("ERROR: could not open DB \"%s\": %s",
                db_path, sqlite3_errmsg(db->handle));
        goto error;
    }

    if (lms_db_create_core_tables_if_required(db->handle) != 0) {
        log_error("ERROR: could not setup tables and indexes.");
        goto error;
    }

    return db;

  error:
    sqlite3_close(db->handle);
    free(db);
    return NULL;
}

static int
_db_close(struct db *db)
{
    log_info("[ pid : %d ]", getpid());

    if (db->transaction_begin)
        lms_db_finalize_stmt(db->transaction_begin, "transaction_begin");

    if (db->transaction_commit)
        lms_db_finalize_stmt(db->transaction_commit, "transaction_commit");

    if (db->get_file_info)
        lms_db_finalize_stmt(db->get_file_info, "get_file_info");

    if (db->insert_file_info)
        lms_db_finalize_stmt(db->insert_file_info, "insert_file_info");

    if (db->update_file_info)
        lms_db_finalize_stmt(db->update_file_info, "update_file_info");

    if (db->delete_file_info)
        lms_db_finalize_stmt(db->delete_file_info, "delete_file_info");

    if (db->set_file_dtime)
        lms_db_finalize_stmt(db->set_file_dtime, "set_file_dtime");

    if (sqlite3_close(db->handle) != SQLITE_OK) {
        log_error("ERROR: clould not close DB: %s",
                sqlite3_errmsg(db->handle));
        return -1;
    }
    free(db);

    return 0;
}

/*
 * Return:
 *  0: file found and nothing changed
 *  1: file not found or mtime/size is different
 *  < 0: error
 */
static int
_retrieve_file_status(struct db *db, struct lms_file_info *finfo)
{
    struct stat64 st;
    int r;

    if (stat64(finfo->path, &st) != 0) {
        perror("stat");
        return -1;
    }

    r = lms_db_get_file_info(db->get_file_info, finfo);
    if (r == 0) {
        if (st.st_size < 0){
          log_error("ERROR: Unsigned integer overflow");
          return -1;
        } else {
            if (st.st_mtime <= finfo->mtime && st.st_ctime <= finfo->ctime && finfo->size == st.st_size) {
              return 0;
            } else {
                finfo->mtime = st.st_mtime;
                finfo->ctime = st.st_ctime;
                finfo->size = st.st_size;
                return 1;
            }
       }
    } else if (r == 1) {
        finfo->mtime = st.st_mtime;
        finfo->ctime = st.st_ctime;
        finfo->size = st.st_size;
        return 1;
    } else
        return -2;
}

static void
_ctxt_init(struct lms_context *ctxt, const lms_t *lms, sqlite3 *db)
{
    ctxt->cs_conv = lms->cs_conv;
    ctxt->db = db;
    ctxt->country = lms->country;
    ctxt->det_level = lms->chardet_level;
}

int
lms_parsers_setup(lms_t *lms, sqlite3 *db)
{
    struct lms_context ctxt;
    int i;

    _ctxt_init(&ctxt, lms, db);

    for (i = 0; i < lms->n_parsers; i++) {
        lms_plugin_t *plugin;
        int r;

        plugin = lms->parsers[i].plugin;
        r = plugin->setup(plugin, &ctxt);
        if (r != 0) {
            log_error("ERROR: parser \"%s\" failed to setup: %d.",
                    plugin->name, r);
            plugin->finish(plugin, &ctxt);
            lms_parser_del_int(lms, i);
            i--; /* cancel i++ */
        }
    }

    return 0;
}

int
lms_parsers_start(lms_t *lms, sqlite3 *db)
{
    struct lms_context ctxt;
    int i;

    _ctxt_init(&ctxt, lms, db);

    for (i = 0; i < lms->n_parsers; i++) {
        lms_plugin_t *plugin;
        int r;

        plugin = lms->parsers[i].plugin;
        r = plugin->start(plugin, &ctxt);
        if (r != 0) {
            log_error("ERROR: parser \"%s\" failed to start: %d.",
                    plugin->name, r);
            plugin->finish(plugin, &ctxt);
            lms_parser_del_int(lms, i);
            i--; /* cancel i++ */
        } else if(strcmp(plugin->name, "audio-dummy") == 0) {
            log_info("find audio dummy plugin");
            audio_dummy_plugin = plugin;
        }
    }

    return 0;
}

int
lms_parsers_finish(lms_t *lms, sqlite3 *db)
{
    struct lms_context ctxt;
    int i;

    _ctxt_init(&ctxt, lms, db);

    for (i = 0; i < lms->n_parsers; i++) {
        lms_plugin_t *plugin;
        int r;

        plugin = lms->parsers[i].plugin;
        r = plugin->finish(plugin, &ctxt);
        if (r != 0)
            log_error("ERROR: parser \"%s\" failed to finish: %d.",
                    plugin->name, r);
    }

    return 0;
}

int
lms_parsers_check_using(lms_t *lms, void **parser_match, struct lms_file_info *finfo)
{
    int used, i;

    used = 0;
    for (i = 0; i < lms->n_parsers; i++) {
        lms_plugin_t *plugin;
        void *r;

        plugin = lms->parsers[i].plugin;
        r = plugin->match(plugin, finfo->path, finfo->path_len, finfo->base);
        parser_match[i] = r;
        if (r)
            used = 1;
    }

    return used;
}

int
lms_parsers_run(lms_t *lms, sqlite3 *db, void **parser_match, struct lms_file_info *finfo)
{
    struct lms_context ctxt;
    int i, failed, available;

    _ctxt_init(&ctxt, lms, db);

    finfo->parsed = 0;
    failed = 0;
    available = 0;
    for (i = 0; i < lms->n_parsers; i++) {
        lms_plugin_t *plugin;

        plugin = lms->parsers[i].plugin;
        if (parser_match[i]) {
            int r;

            if (available == INT_MAX)
                return -1;
            else
                available++;
            r = plugin->parse(plugin, &ctxt, finfo, parser_match[i]);

            if (r != 0) {
               if (__builtin_sadd_overflow(failed, 1, &failed))
                  log_error("ERROR: failed may overflow");
            }
            else
                finfo->parsed = 1;
        }
    }
    if(finfo->parsed == 0) {
        if(finfo->path_len >= 0 && lms_which_extension(finfo->path, (unsigned int)finfo->path_len, g_mediaFileExtensions, LMS_ARRAY_SIZE(g_mediaFileExtensions)) >= 0 && audio_dummy_plugin) {
            int r = audio_dummy_plugin->parse(audio_dummy_plugin, &ctxt, finfo, NULL);
            if(r != 0) {
                log_error("failed to add default db");
            } else {
                finfo->parsed = 1;
                failed = 0;
            }
        }
    }

    if (!failed)
        return 0;
    else if (failed == available)
        return -1;
    else
        return 1; /* non critical */
}

static int
_db_and_parsers_setup(lms_t *lms, struct db **db_ret, void ***parser_match_ret)
{
    void **parser_match;
    struct db *db;
    int r = 0;

    db = _db_open(lms->db_path);
    if (!db) {
        r = -1;
        return r;
    }

    if (lms_parsers_setup(lms, db->handle) != 0) {
        log_error("ERROR: could not setup parsers.");
        r = -2;
        goto err;
    }

    if (_db_compile_all_stmts(db) != 0) {
        log_error("ERROR: could not compile statements.");
        r = -3;
        goto err;
    }

    if (lms_parsers_start(lms, db->handle) != 0) {
        log_error("ERROR: could not start parsers.");
        r = -4;
        goto err;
    }
    if (lms->n_parsers < 1) {
        log_error("ERROR: no parser could be started, exit.");
        r = -5;
        goto err;
    }

    parser_match = malloc(lms->n_parsers * sizeof(*parser_match));
    if (!parser_match) {
        perror("malloc");
        r = -6;
        goto err;
    }

    *parser_match_ret = parser_match;
    *db_ret = db;
    return r;

  err:
    lms_parsers_finish(lms, db->handle);
    _db_close(db);
    return r;
}

/*
 * Return:
 *  LMS_PROGRESS_STATUS_UP_TO_DATE
 *  LMS_PROGRESS_STATUS_PROCESSED
 *  LMS_PROGRESS_STATUS_SKIPPED
 *  < 0 on error
 */
static int
_db_and_parsers_process_file(lms_t *lms, struct db *db, void **parser_match,
                             char *path, int path_len, int path_base,
                             unsigned int update_id)
{
    struct lms_file_info finfo;
    int used, r;

    finfo.path = path;
    finfo.path_len = path_len;
    finfo.base = path_base;

/*
 * [CHS] : Disabled this log for system performance
 */
//    log_debug("[ pid : %d ] path = %s , path_len = %d , path_base = %d" , getpid() , path , path_len , path_base);

    r = _retrieve_file_status(db, &finfo);
    if (r == 0) {
        if (!finfo.dtime)
            return LMS_PROGRESS_STATUS_UP_TO_DATE;

        finfo.dtime = 0;
        finfo.itime = time(NULL);
        lms_db_set_file_dtime(db->set_file_dtime, &finfo);
        return LMS_PROGRESS_STATUS_PROCESSED;
    } else if (r < 0) {
        log_error("ERROR: could not detect file status.(err=%d)", r);
        return r;
    }

    used = lms_parsers_check_using(lms, parser_match, &finfo);

    log_debug("[ pid : %d ] path = %s , used = %d" , getpid() , path , used);

    if (!used)
        return LMS_PROGRESS_STATUS_SKIPPED;

    finfo.dtime = 0;
    finfo.itime = time(NULL);
    if (!finfo.itime) {
       log_error("ERROR: finfo.itime not available");
       return LMS_PROGRESS_STATUS_UP_TO_DATE;
    }

    if (finfo.id > 0)
        r = lms_db_update_file_info(db->update_file_info, &finfo, update_id);
    else
        r = lms_db_insert_file_info(db->insert_file_info, &finfo, update_id);

    if (r < 0) {
        log_error("ERROR: could not register path in DB");
        return r;
    }

    r = lms_parsers_run(lms, db->handle, parser_match, &finfo);
    if (r < 0) {
        log_warning("ERROR: pid=%d failed to parse \"%s\".",
                getpid(), finfo.path);
        lms_db_delete_file_info(db->delete_file_info, &finfo);
        return r;
    }

    return LMS_PROGRESS_STATUS_PROCESSED;
}

static int
_slave_work(struct pinfo *pinfo)
{
    lms_t *lms = pinfo->common.lms;
    struct fds *fds = &pinfo->slave;
    int r, len, base;
#ifdef PATCH_LGE
    char path[PATH_SIZE] ={0,};
#else
    char path[PATH_SIZE];
#endif
    void **parser_match;
    struct db *db;
    unsigned int total_committed, counter;

    //GTimer *timer = NULL;
    //double duration;

    int parentID = getppid();

    pthread_mutex_lock(lms->mtx);
    log_info("+ db and parsers_setup , [ Parent ID : %d ] , [ pid : %d ]" , parentID , getpid());

    r = _db_and_parsers_setup(lms, &db, &parser_match);

    if (r < 0) {
        log_info("- db and parsers_setup , [ Parent ID : %d ] , [ pid : %d ]" , parentID , getpid());
        pthread_mutex_unlock(lms->mtx);
        return r;
    }

    log_info("- db and parsers_setup , [ Parent ID : %d ] , [ pid : %d ]" , parentID , getpid());
    pthread_mutex_unlock(lms->mtx);

    pthread_mutex_lock(lms->mtx);
    log_info("+ get update id , [ Parent ID : %d ] , [ pid : %d ]" , parentID , getpid());

    r = lms_db_update_id_get(db->handle);

    log_info("- get update id , [ Parent ID : %d ] , [ pid : %d ]" , parentID , getpid());
    pthread_mutex_unlock(lms->mtx);

    if (r < 0) {
        log_error("ERROR: could not get global update id.");
        goto done;
    }

    pinfo->common.update_id = r + 1;

    counter = 0;
    total_committed = 0;

    pthread_mutex_lock(lms->mtx);

    //timer = g_timer_new();

    log_info("+ begin_transaction , [ Parent ID : %d ] , [ pid : %d ]" , parentID , getpid());

    lms_db_begin_transaction(db->transaction_begin);

    while (((r = _slave_recv_path(fds, &len, &base, path)) == 0) && len > 0) {

/*
 * [CHS] : Disabled this log for system performance
 */
        //log_debug("Path received. [ Parent ID : %d ] , [ pid : %d ] , path = %s" , parentID , getpid() , path);

        r = _db_and_parsers_process_file(lms, db, parser_match, path, len, base, pinfo->common.update_id);

        _slave_send_reply(fds, r);

        if (r < 0 ||
            (r == LMS_PROGRESS_STATUS_UP_TO_DATE ||
             r == LMS_PROGRESS_STATUS_SKIPPED))
            continue;

        counter++;

        //duration = g_timer_elapsed(timer, NULL);

        // Change the criteria for commit judgment.
        //if (duration > lms->commit_duration) {
        if (counter > lms->commit_interval) {

            if (!total_committed) {
                total_committed += counter;
                lms_db_update_id_set(db->handle, pinfo->common.update_id);
            }

            lms_db_end_transaction(db->transaction_commit);

            log_info("- end transaction , [ Parent ID : %d ] , [ pid : %d ]" , parentID , getpid());

            pthread_mutex_unlock(lms->mtx);

            log_info("commit , [ Parent ID : %d ] , [ pid : %d ]" , parentID , getpid());

            pthread_mutex_lock(lms->mtx);

            log_info("+ begin_transaction , [ Parent ID : %d ] , [ pid : %d]" , parentID , getpid());
            lms_db_begin_transaction(db->transaction_begin);

            counter = 0;

            // [ Static Analysis ] 2578276 : Value not atomically updated
            //g_timer_start (timer);
        }

    }

    if (counter) {
        total_committed += counter;
        lms_db_update_id_set(db->handle, pinfo->common.update_id);
    }

    lms_db_end_transaction(db->transaction_commit);

    log_info("- end transaction , [ Parent ID : %d ] , [ pid : %d ]" , parentID , getpid());

    pthread_mutex_unlock(lms->mtx);

done:
    pthread_mutex_lock(lms->mtx);

    log_info("+ slave done , [ Parent ID : %d ] , [ pid : %d ]" , parentID , getpid());

    free(parser_match);

    lms_parsers_finish(lms, db->handle);

    _db_close(db);

    log_info("- slave done , [ Parent ID : %d ] , [ pid : %d ]" , parentID , getpid());

    pthread_mutex_unlock(lms->mtx);

    //g_timer_destroy (timer);

    return r;
}


/***********************************************************************
 * Master-side.
 ***********************************************************************/

static int
_consume_garbage(struct pollfd *pfd)
{
    int r = -1;

    while ((r = poll(pfd, 1, 0)) > 0) {
        if (pfd->revents & (POLLERR | POLLHUP | POLLNVAL))
            return 0;
        else if (pfd->revents & POLLIN) {
            char c;
            ssize_t read_ret = 0;

            // [ Static Analysis ] 987636 : Unchecked return value from library
            //(void)read(pfd->fd, &c, sizeof(c));
            read_ret = read(pfd->fd, &c, sizeof(c));
            if ((unsigned long)read_ret != sizeof(c)) {
                return 0;
            }
        }
    }

    return r;
}

static int
_close_fds(struct fds *fds)
{
    int r;

    r = 0;
    if (close(fds->r) != 0) {
        r--;
        perror("close");
    }

    if (close(fds->w) != 0) {
        r--;
        perror("close");
    }

    return r;
}

int
lms_close_pipes(struct pinfo *pinfo)
{
    int r;

    r = _close_fds(&pinfo->master);
    r += _close_fds(&pinfo->slave);

    return r;
}

int
lms_create_pipes(struct pinfo *pinfo)
{
    int fds[2];

    log_info("lms_create_pipes(...) , [ pid : %d ]" , getpid());

    if (pipe(fds) != 0) {
        perror("pipe");
        return -1;
    }
    pinfo->master.r = fds[0];
    pinfo->slave.w = fds[1];

    if (pipe(fds) != 0) {
        perror("pipe");
        close(pinfo->master.r);
        close(pinfo->slave.w);
        return -1;
    }
    pinfo->slave.r = fds[0];
    pinfo->master.w = fds[1];

    pinfo->poll.fd = pinfo->master.r;
    pinfo->poll.events = POLLIN;

    log_info("lms_create_pipes(...) , [ pid : %d ] , pinfo->master.r = %d , pinfo->slave.w = %d , pinfo->slave.r = %d , pinfo->master.w = %d" , getpid() , pinfo->master.r , pinfo->slave.w , pinfo->slave.r , pinfo->master.w);

    return 0;
}

int
lms_create_slave(struct pinfo *pinfo, int (*work)(struct pinfo *pinfo))
{
    int r = 0;
    int niceValue = -100;
    lms_t *lms = NULL;

    log_info("lms_create_slave(...) , [ pid : %d ]" , getpid());

    pinfo->child = fork();
    if (pinfo->child == -1) {
        perror("fork");
        return -1;
    }

    log_info("lms_create_slave(...) , [ pid : %d ] , pinfo->child = %d" , getpid() , pinfo->child);

    if (pinfo->child > 0)
        return 0;

    //////////////////////////////////////////////////
    ////////// slave process                //////////
    //////////////////////////////////////////////////
    _close_fds(&pinfo->master);

    niceValue = nice(19);

    // OYK_2019_03_25 : Replace the log context with the slave context.
    //init_log();
    lms = pinfo->common.lms;
    if (lms != NULL) {

        switch (lms->lmsTarget)
        {
            case LMS_TARGET_FRONT:
                init_log_slave();
                break;
            case LMS_TARGET_REAR:
                init_log_rear_slave();
                break;

            default:
                init_log_slave();
                break;
        }
    }
    else {
        init_log_slave();
    }

    log_info("lms_create_slave(...) , [ Parent ID : %d ] , [ pid : %d ] , niceValue = %d" , getppid() , getpid() , niceValue);

    r = work(pinfo);

    //  lms_free(pinfo->common.lms);
    uninit_log();

    _exit(r);

    return r; /* shouldn't reach anyway... */
}

static int
_waitpid(pid_t pid)
{
    int status;
    pid_t r;

    r = waitpid(pid, &status, 0);
    if (r > -1)
        return 0;
    else
        perror("waitpid");

    return r;
}

int
lms_finish_slave(struct pinfo *pinfo, int (*finish)(const struct fds *fds))
{
    int r;

    if (pinfo->child <= 0)
        return 0;

    r = finish(&pinfo->master);
    if (r == 0)
        r = _waitpid(pinfo->child);
    else {
        r = kill(pinfo->child, SIGKILL);
        if (r < 0)
            perror("kill");
        else
            r =_waitpid(pinfo->child);
    }
    pinfo->child = 0;

    return r;
}

int
lms_restart_slave(struct pinfo *pinfo, int (*work)(struct pinfo *pinfo))
{
    int status;

    log_info("    [ pid : %d ]" , getpid());

    if (waitpid(pinfo->child, &status, WNOHANG) > 0) {
        int code = 0;
        if (WIFEXITED(status)) {
            code = WEXITSTATUS(status);
            if (code != 0) {
                log_error("ERROR: slave returned %d, exit.", code);
                pinfo->child = 0;
                return -1;
            }
        } else {
            if (WIFSIGNALED(status)) {
                code = WTERMSIG(status);
                log_error("ERROR: slave was terminated by signal %d.",
                        code);
            }
            pinfo->child = 0;
        }
    } else {

        if (kill(pinfo->child, SIGKILL))
            log_error("kill slave");

        if (waitpid(pinfo->child, &status, 0) < 0)
            log_error("waitpid");

    }
    // [ Static Analysis ] 987655 : Unchecked return value from library
    (void)_consume_garbage(&pinfo->poll);

    return lms_create_slave(pinfo, work);
}

static int
_strcat(int base, char *path, const char *name)
{
    int new_len = 0;
    size_t name_len;

    name_len = strlen(name);
    if (name_len > (INT_MAX - PATH_SIZE - 2)) {
        log_error("ERROR: name_len may overflow");
        return -1;
    } else {
        new_len = base + (int)name_len;
        if (new_len >= PATH_SIZE) {
            path[base] = '\0';
            log_error("ERROR: path concatenation too long %d of %d "
                "available: \"%s\" + \"%s\"", new_len, PATH_SIZE,
                path, name);
            return -1;
        }
    }

    memcpy(path + base, name, name_len + 1);

    return new_len;
}

static inline void
_report_progress(struct cinfo *info, const char *path, int path_len, lms_progress_status_t status)
{
    lms_progress_callback_t cb;
    lms_t *lms = info->lms;

    cb = lms->progress.cb;
    if (!cb)
        return;

    cb(lms, path, path_len, status, lms->progress.data);
}

#ifdef PATCH_LGE
//cid: 12393051
static inline void
report_device(struct cinfo *info, const char *path, int path_len, lms_scanner_device_scan_t status)
{
    lms_progress_callback_t cb;
    lms_t *lms = info->lms;

    cb = lms->progress_device.cb;
    if (!cb)
        return;

    cb(lms, path, path_len, status, lms->progress_device.data);
}
#endif

static int
_process_file(struct cinfo *info, int base, char *path, const char *name , int depth)
{
    lms_t *lms = info->lms;
    struct pinfo *pinfo = (struct pinfo *)info;
    int new_len, reply, r;

#if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
    char tapBuffer[TAB_BUFFER_SIZE] = {'\0', };
    int i;
#endif

    //log_debug("    [ pid : %d ] , base = %d , path = %s , name = %s , depth = %d" , getpid() , base , path , name , depth);
    if (lms->currentFileCount == INT_MAX)
        return -1;
    else
        (lms->currentFileCount)++;
    new_len = _strcat(base, path, name);
    if (new_len < 0)
        return -1;

    if (_master_send_path(&pinfo->master, new_len, base, path) != 0)
        return -2;

    r = _master_recv_reply(&pinfo->master, &pinfo->poll, &reply, pinfo->common.lms->slave_timeout);

    if (r < 0) {

        _report_progress(info, path, new_len, LMS_PROGRESS_STATUS_ERROR_COMM);

        return -3;
    }
    else if (r == 1) {

        log_error("ERROR: slave took too long(path:%s), restart %d", path, pinfo->child);

        _report_progress(info, path, new_len, LMS_PROGRESS_STATUS_KILLED);

        pthread_mutex_unlock(lms->mtx);

        if (lms_restart_slave(pinfo, _slave_work) != 0)
            return -4;

        return 1;
    }
    else {

        if (reply < 0) {

            log_warning("ERROR: pid=%d failed to parse \"%s\".", getpid(), path);

            _report_progress(info, path, new_len, LMS_PROGRESS_STATUS_ERROR_PARSE);

            #ifdef PATCH_LGE
                return LMS_PROGRESS_STATUS_ERROR_PARSE;
            #else
                return reply;
            #endif
        }

        // OYK_2019_07_02 : Limit the total file count to 8000(default value).
        #if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)

            // Don't check the reply of slave process for being consistent with media browser.
            #if 1

                if (lms->isPrintDirectoryStructure) {

                    memset(tapBuffer , 0x00 , TAB_BUFFER_SIZE);
                    for (i = 0; i <= depth ; i++) {
                        // [ Static Analysis ] 7342871 : Calling risky function.
                        // Replace strcat(...) with strncat(...).
                        strncat(tapBuffer , "\t" , strlen("\t"));
                    }

                    log_info("%s| [ %d ] %-32s , Processed , reply = %d" , tapBuffer , lms->currentFileCount , name , reply);
                }
            #else
                if ((reply == LMS_PROGRESS_STATUS_UP_TO_DATE)
                    || (reply == LMS_PROGRESS_STATUS_PROCESSED)
                ) {
                    (lms->currentFileCount)++;

                    log_info("%s| [ %d ] %-32s , Processed , reply = %d" , tapBuffer , lms->currentFileCount , name , reply);
                }
                else {
                    log_info("%s| [ %d ] %-32s , Not a media file. Not counted !!!!! , reply = %d" , tapBuffer , lms->currentFileCount , name , reply);
                }
            #endif

        #endif

        _report_progress(info, path, new_len, reply);

        return reply;
    }

}

static int
_process_file_single_process(struct cinfo *info, int base, char *path, const char *name , int depth)
{
    struct sinfo *sinfo = (struct sinfo *)info;
    int new_len, r;

    void **parser_match = sinfo->parser_match;
    struct db *db = sinfo->db;
    lms_t *lms = sinfo->common.lms;

    new_len = _strcat(base, path, name);
    if (new_len < 0)
        return -1;

    r = _db_and_parsers_process_file(lms, db, parser_match, path, new_len,
                                     base, sinfo->common.update_id);
    if (r < 0) {
        log_warning("ERROR: pid=%d failed to parse \"%s\".",
                getpid(), path);
        _report_progress(info, path, new_len, LMS_PROGRESS_STATUS_ERROR_PARSE);
        return r;
    }

    if (r != LMS_PROGRESS_STATUS_UP_TO_DATE) {
        if (UINT_MAX - sinfo->commit_counter < 1)
            return -1;
        else
            sinfo->commit_counter++;
    }

    if (sinfo->commit_counter > lms->commit_interval) {
        if (!sinfo->total_committed) {
            sinfo->total_committed += sinfo->commit_counter;
            lms_db_update_id_set(db->handle, sinfo->common.update_id);
        }

        lms_db_end_transaction(db->transaction_commit);
        lms_db_begin_transaction(db->transaction_begin);
        sinfo->commit_counter = 0;
    }

    _report_progress(info, path, new_len, r);

    return r;
}

static int _process_dir(struct cinfo *info, int base, char *path, const char *name, process_file_callback_t process_file , int depth);

static int
_process_unknown(struct cinfo *info, int base, char *path, const char *name, process_file_callback_t process_file , int depth)
{
    struct stat st;
    int new_len;

    log_info("    [ pid : %d ] , base = %d , path = %s , name = %s ..... [[ START ]]", getpid() , base , path , name);

    new_len = _strcat(base, path, name);
    if (new_len < 0)
        return -1;

    if (stat(path, &st) != 0) {
        perror("stat");
        return -2;
    }

    if (S_ISREG(st.st_mode)) {

        int r = process_file(info, base, path, name , depth);

        log_info("    [ pid : %d ] , path = %s , name = %s ..... [[ END ]]", getpid() , path , name);

        if (r >= 0) /* if success and ignore non-fatal errors */
            return 0;

        return r;
    }
    else if (S_ISDIR(st.st_mode)) {

        int r = _process_dir(info, base, path, name, process_file , depth);

        log_info("    [ pid : %d ] , path = %s , name = %s ..... [[ END ]]", getpid() , path , name);

        if (r >= 0) /* ignore non-fatal errors */
            return 0;

        return r;

    } else {

        log_info("INFO: %s is neither a directory nor a regular file.", path);
        log_info("    [ pid : %d ] , path = %s , name = %s ..... [[ END ]]", getpid() , path , name);

        return -3;
    }

    log_info("    [ pid : %d ] , base = %d , path = %s , name = %s ..... [[ END ]]", getpid() , base , path , name);

}

static gboolean
_check_completed_scan_path(lms_t *lms, char *path)
{
    GList *n;
    char *completed_path;

    for (n = lms->completed_scan_paths; n != NULL; n = n->next) {
        completed_path = n->data;
        if (g_str_has_prefix(path, completed_path) == 1) {
            return TRUE;
        }
    }

    return FALSE;

}

static gboolean
_check_different_device_scan_path(lms_t *lms, char *path)
{
    GList *n;
    char *device_path;

    for (n = lms->device_scan_paths; n != NULL; n = n->next) {
        device_path = n->data;
        if (strcmp(path, device_path) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
_check_skip_scan_path(lms_t *lms, char *path)
{
    gboolean ret = FALSE;
    GList *n;
    char *completed_path;

    if (g_str_has_prefix(path, "/rw_data/") == 1 ) {
        for (n = lms->completed_scan_paths; n != NULL; n = n->next) {
            completed_path = n->data;
            if (strstr(path, completed_path) != NULL) {
                return TRUE;
            }
        }
    }
    return ret;
}

#if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)

    int scandirFilter(const struct dirent* info)
    {
    	// return 1 => valid
    	if ((info->d_name[0] == '.') || (info->d_name[0] == '$')) {
    		return 0;
    	}

    	return 1;
    }

    int scandirFilterFilesOnly(const struct dirent* info)
    {
        int resultExtensions = -1;
        size_t d_name_len = strlen(info->d_name);

        // return 1 => valid
        if ((info->d_type == DT_REG)
            && (info->d_name[0] != '.')
            && ((resultExtensions = lms_which_extension(info->d_name, (unsigned int)d_name_len, g_mediaFileExtensions, LMS_ARRAY_SIZE(g_mediaFileExtensions))) >= 0)) {

            //log_debug("info->d_name = %s , resultExtensions = %d" , info->d_name , resultExtensions);

            return 1;
        } else {
            return 0;
        }
    }

    int scandirFilterDirectoriesOnly(const struct dirent* info)
    {
    	// return 1 => valid
    	if ((info->d_type == DT_DIR)
    	   && (info->d_name[0] != '.')
    	   && (info->d_name[0] != '$')
    	) {
    		return 1;
    	}
    	// return 0 => invalid
    	else {
    		return 0;
    	}
    }

    // Sort the files first.
    int alphaSortCaseInsensitiveFilesFirst (const struct dirent **a, const struct dirent **b)
    {
    	char nameA[1024];
    	char nameB[1024];
    	unsigned int i;

        //log_debug("(*a)->d_name = %s , d_type = %s ( %d )" , (*a)->d_name , ((*a)->d_type==DT_REG) ? "DT_REG" : (((*a)->d_type==DT_DIR) ? "DT_DIR" : "DT_UNKNOWN") , (*a)->d_type);
        //log_debug("(*b)->d_name = %s , d_type = %s ( %d )" , (*b)->d_name , ((*b)->d_type==DT_REG) ? "DT_REG" : (((*b)->d_type==DT_DIR) ? "DT_DIR" : "DT_UNKNOWN") , (*b)->d_type);

    	// Files first.
    	if (((*a)->d_type != DT_REG) && ((*b)->d_type == DT_REG)) {
    		return 1;
    	}
    	// Check the names in case of files.
    	else {
    		for(i = 0 ; i < strlen((*a)->d_name) ; i++) {
            int name_upper = toupper((*a)->d_name[i]);
            if ((name_upper < CHAR_MIN) || (name_upper > CHAR_MAX)) {
               log_error("ERROR: name char conversion overflow");
               return -1;
            } else {
               nameA[i] = (char)name_upper;
            }
    		}

    		for(i = 0 ; i < strlen((*b)->d_name) ; i++) {
    			nameB[i] = (char)toupper((*b)->d_name[i]);
    		}
    	}

    	return strcoll (nameA, nameB);
    }

#endif              /* End of #if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN) */

static int _process_dir(struct cinfo *info, int base, char *path, const char *name, process_file_callback_t process_file , int depth)
{
    lms_t *lms = info->lms;

    #if !defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
        struct dirent *de;
    #endif

    int new_len = 0;
    int r = 0;
    DIR *dir = NULL;
    gboolean device = FALSE;
    char *device_path = NULL;

#if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
    struct dirent** namelist = NULL;
    const char* d_name = NULL;
    int d_type = 0;
    int idx = 0;
    int scanCount = 0;

    char currentDirectory[PATH_SIZE] = {'\0', };
    char tapBuffer[TAB_BUFFER_SIZE] = {'\0', };

    size_t pathLength = 0;
#endif

    //log_debug("base = %d , path = %s , name = %s , depth = %d .......... [[[ START ]]]" , base , path , name , depth);

    new_len = _strcat(base, path, name);
    if (new_len < 0) {

        //log_debug("path = %s , name = %s .......... [[[ END ]]]" , path , name);

        return -1;
    }
    else if ((new_len + 1) >= PATH_SIZE) {

        log_error("ERROR: path too long");
        //log_debug("path = %s , name = %s .......... [[[ END ]]]" , path , name);

        return 2;
    }

#if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
    if (lms->isPrintDirectoryStructure) {

        memset(tapBuffer , 0x00 , TAB_BUFFER_SIZE);
        for (int i = 0; i < depth ; i++) {
            // Replace strcat(...) with strncat(...).
            //strcat(tapBuffer , "\t");
            strncat(tapBuffer , "\t" , strlen("\t"));
        }

        log_info("%s |-- %-32s\n" , tapBuffer , name);
    }
#endif

    dir = opendir(path);
    if (dir == NULL) {

        perror("opendir");

        //log_debug("path = %s , name = %s .......... [[[ END ]]]" , path , name);

        return 3;
    }

    // OYK_2019_07_02 : For using scandir(...) instead of opendir(...), closedir(...) and readdir(...).
    #if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
        closedir(dir);
    #endif

    //log_debug("base = %d , path = %s , new_len = %d" , base , path , new_len);

    path[new_len] = '/';
    new_len++;
    path[new_len] = '\0';

    log_debug("base = %d , path = %s , new_len = %d" , base , path , new_len);

    if (_check_completed_scan_path(lms, path) == TRUE) {

        log_debug("skip completed scan path : %s \n", path);

        #if !defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
            closedir(dir);
        #endif

        //log_debug("path = %s , name = %s , depth = %d .......... [[[ END ]]]" , path , name , depth);

        return 4;
    }

    if (_check_skip_scan_path(lms, path) == TRUE) {

        log_warning("skip scan path : %s \n", path);

        #if !defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
            closedir(dir);
        #endif

        //log_debug("path = %s , name = %s , depth = %d .......... [[[ END ]]]" , path , name , depth);

        return 5;
    }

    if(_check_different_device_scan_path(lms, path) == TRUE) {
        device_path = (char *)calloc(new_len, sizeof(char));
        if (device_path == NULL) {
            log_error("can not aloocate memory");
            return 4;
        } else {
            memcpy(device_path, path, new_len);
        }
        device = TRUE;
        log_info("device scan start path : %s", path);
        report_device(info, path, new_len, LMS_SCANNER_DEVICE_STARTED);
    }

// OYK_2019_07_02 : For using scandir(...) instead of opendir(...), closedir(...) and readdir(...).
//                  Limit the total number of scanned file to 8000(default value).
#if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)

    r = 0;
    if (!lms->stop_processing) {

        ///// Scan the files and directories following the below sequence.
        ///// Check the files first. After that, check directories by HYUNDAI media specification.

		// scandir(...) operation is different from PC version.
		// PC version operates normally.
		// But, AVN scandir(...) result is not good at the root directory.
		// So, the files and directories processing should be separated.

        #if defined(SEPARATE_FILES_FROM_DIRECTORIES_PROCESSING)

            // [ Static Analysis ] 7342861 : Copy into fixed size buffer
            //                     7341850 : Calling risky function
            // Replace strcpy(...) with strncpy(...).
            //strcpy(currentDirectory , path);
            pathLength = strlen(path);
            if (pathLength < (PATH_SIZE - 1)) {
                memcpy(currentDirectory , path , pathLength);
                currentDirectory[pathLength + 1] = '\0';
            }
            else {
                strncpy(currentDirectory , path , PATH_SIZE-1);
            }

            ///// Process the files first.
            if ((scanCount = scandir(path , &namelist , scandirFilterFilesOnly , alphaSortCaseInsensitiveFilesFirst)) == -1) {

                log_debug("base = %d , %s scandir FAILED !!!!! : %s" , base , path , strerror(errno));
            }

            log_debug("base = %d , path = %s , [[[ FILES ]]] scanCount = %d" , base , path , scanCount);

            for (idx = 0 ; idx < scanCount ; idx++) {

                d_name = namelist[idx]->d_name;
                d_type = namelist[idx]->d_type;

                //log_debug("d_name = %-32s \t d_type = %s ( %d ) , currentFileCount = %d" , d_name , (d_type==DT_REG) ? "DT_REG" : ((d_type==DT_DIR) ? "DT_DIR" : "DT_UNKNOWN") , d_type , lms->currentFileCount);

                // If the current file count is greater than max file count, do not scan anymore.
                if (lms->currentFileCount >= lms->maxFileScanCount) {

                    log_error("Do not scan anymore!, cur = [%s%s] , idx/scanCount = %d/%d, curFileCount = %d , maxCount = %d" , currentDirectory, d_name , idx +1 , scanCount , lms->currentFileCount , lms->maxFileScanCount);

                    goto end;
                }

                if (d_type == DT_REG) {

                    if (process_file(info, new_len, path, d_name , depth) < 0) {

                        log_error("ERROR: unrecoverable error parsing file, exit \"%s\".", path);

                        path[new_len - 1] = '\0';
                        r = -4;

                        #ifndef PATCH_LGE
                            goto end;
                        #else
                            continue;
                        #endif
                    }
                }

            }               /* for (idx = 0 ; idx < scanCount ; idx++) */

            // Free memory.
            if (namelist != NULL) {

                for (idx = 0 ; idx < scanCount ; idx++) {
                    free(namelist[idx]);
                }

                free(namelist);
                namelist = NULL;
            }

            ///// Prcess the directories.
            if ((scanCount = scandir(currentDirectory , &namelist , scandirFilterDirectoriesOnly , alphaSortCaseInsensitiveFilesFirst)) == -1) {

                log_debug("base = %d , %s scandir FAILED !!!!! : %s" , base , currentDirectory , strerror(errno));
            }

            log_debug("base = %d , path = %s , [[[ DIRECTORIES ]]] scanCount = %d" , base , path , scanCount);

            for (idx = 0 ; idx < scanCount ; idx++) {

                d_name = namelist[idx]->d_name;
                d_type = namelist[idx]->d_type;


                log_info("[DIR] [[%s%s]]     idx/scanCount = %d/%d, type = %s(%d)", currentDirectory, d_name, idx+1 , scanCount , (d_type==DT_REG) ? "DT_REG" : ((d_type==DT_DIR) ? "DT_DIR" : "DT_UNKNOWN") , d_type);
                if (d_type == DT_DIR) {

                    if (_process_dir(info, new_len, path, d_name, process_file , depth+1) < 0) {

                        log_error("ERROR: unrecoverable error parsing dir, exit \"%s\".", path);

                        path[new_len - 1] = '\0';
                        r = -5;

                        goto end;
                    }
                }
                else if (d_type == DT_UNKNOWN) {

                    if (_process_unknown(info, new_len, path, d_name, process_file , depth) < 0) {

                        log_error("ERROR: unrecoverable error parsing DT_UNKNOWN, exit \"%s\".", path);

                        path[new_len - 1] = '\0';
                        r = -6;

                        goto end;
                    }
                }

            }               /* for (idx = 0 ; idx < scanCount ; idx++) */

        #else               /* else of #if defined(SEPARATE_FILES_FROM_DIRECTORIES_PROCESSING) */

            if ((scanCount = scandir(path , &namelist , scandirFilter , alphaSortCaseInsensitiveFilesFirst)) == -1) {

                log_debug("base = %d , %s scandir FAILED !!!!! : %s" , base , path , strerror(errno));
            }

            //log_debug("base = %d , path = %s , scanCount = %d" , base , path , scanCount);

            for (idx = 0 ; idx < scanCount ; idx++) {

                d_name = namelist[idx]->d_name;
                d_type = namelist[idx]->d_type;

                //log_debug("d_name = %-32s \t d_type = %s ( %d )" , d_name , (d_type==DT_REG) ? "DT_REG" : ((d_type==DT_DIR) ? "DT_DIR" : "DT_UNKNOWN") , d_type);

                if (d_type == DT_REG) {

                    if (process_file(info, new_len, path, d_name , depth) < 0) {

                        log_error("ERROR: unrecoverable error parsing file, exit \"%s\".", path);

                        path[new_len - 1] = '\0';
                        r = -4;

                        #ifndef PATCH_LGE
                            goto end;
                        #else
                            continue;
                        #endif
                    }
                }
                else if (d_type == DT_DIR) {

                    if (_process_dir(info, new_len, path, d_name, process_file , depth+1) < 0) {

                        log_error("ERROR: unrecoverable error parsing dir, exit \"%s\".", path);

                        path[new_len - 1] = '\0';
                        r = -5;

                        goto end;
                    }
                }
                else if (d_type == DT_UNKNOWN) {

                    if (_process_unknown(info, new_len, path, d_name, process_file , depth) < 0) {

                        log_error("ERROR: unrecoverable error parsing DT_UNKNOWN, exit \"%s\".", path);

                        path[new_len - 1] = '\0';
                        r = -6;

                        goto end;
                    }
                }

            }				/* for (idx = 0 ; idx < scanCount ; idx++) */

        #endif              /* End of #if defined(SEPARATE_FILES_FROM_DIRECTORIES_PROCESSING) */

        //log_debug("base = %d , path = %s , depth = %d" , base , path , depth);

    }               /* if (!lms->stop_processing) */

#else              /* else of #if defined(ENABLE_LIMITATION_OF_FILE_SCAN) */

    r = 0;
    while ((de = readdir(dir)) != NULL && !lms->stop_processing) {

        log_debug("path = %s , name = %s , de->d_name = %s , de->d_type = %s ( %d )" , path , name , de->d_name , (de->d_type==DT_REG) ? "DT_REG" : ((de->d_type==DT_DIR) ? "DT_DIR" : "DT_UNKNOWN") , de->d_type);

        if (de->d_name[0] == '.')
            continue;

        if (de->d_type == DT_REG) {

            if (process_file(info, new_len, path, de->d_name , depth) < 0) {

                log_error("ERROR: unrecoverable error parsing file, exit \"%s\".", path);

                path[new_len - 1] = '\0';
                r = -4;

                #ifndef PATCH_LGE
                    goto end;
                #else
                    continue;
                #endif
            }
        }
        else if (de->d_type == DT_DIR) {

            if (_process_dir(info, new_len, path, de->d_name, process_file , depth+1) < 0) {

                log_error("ERROR: unrecoverable error parsing dir, exit \"%s\".", path);

                path[new_len - 1] = '\0';
                r = -5;

                goto end;
            }
        }
        else if (de->d_type == DT_UNKNOWN) {

            if (_process_unknown(info, new_len, path, de->d_name, process_file , depth) < 0) {

                log_error("ERROR: unrecoverable error parsing DT_UNKNOWN, exit \"%s\".", path);

                path[new_len - 1] = '\0';
                r = -6;

                goto end;
            }
        }
    }

#endif              /* End of #if defined(ENABLE_LIMITATION_OF_FILE_SCAN) */


end:

    if (device) {

        log_info("device scan stop path : %s", device_path);

        report_device(info, path, new_len, LMS_SCANNER_DEVICE_STOPPED);

        free(device_path);
    }

    #if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)
    	// Free memory.
        if (namelist != NULL) {

    		for (idx = 0 ; idx < scanCount ; idx++) {
    			free(namelist[idx]);
    		}

    		free(namelist);
    		namelist = NULL;
		}
    #else
        closedir(dir);
    #endif

    //log_debug("path = %s , name = %s , depth = %d .......... [[[ END ]]]" , path , name , depth);

    return r;
}

static int
_lms_process_check_valid(lms_t *lms, const char *path)
{
    if (!lms)
        return -1;

    if (!path)
        return -2;

    if (lms->is_processing) {
        log_error("ERROR: is already processing.");
        return -3;
    }

    if (!lms->parsers) {
        log_error("ERROR: no plugins registered.");
        return -4;
    }

    return 0;
}

static int
_process_trigger(struct cinfo *info, const char *top_path, process_file_callback_t process_file)
{
    char path[PATH_SIZE + 2], *bname;
    lms_t *lms = info->lms;
    int len = 0;
    int r = 0;
    size_t path_len = 0;

    log_info("    [ pid : %d ] , top_path = %s ..... [[ START ]]", getpid() , top_path);

    if (realpath(top_path, path) == NULL) {
        perror("realpath");
        return -1;
    }

    log_debug("    [ pid : %d ] , path = %s", getpid() , path);

    /* search '/' backwards, split dirname and basename, note realpath usage */

    path_len = strlen(path);
    if (path_len > PATH_SIZE)
      log_error("ERROR: path_size overflow");
    else
      len = (int)path_len;
    for (; len >= 0 && path[len] != '/'; len--);
    len++;
    bname = strdup(path + len);
    if (bname == NULL) {
        perror("strdup");
        return -3;
    }

    lms->is_processing = 1;
    lms->stop_processing = 0;
    r = _process_unknown(info, len, path, bname, process_file , 0);
    lms->is_processing = 0;
    lms->stop_processing = 0;
    free(bname);

    log_info("    [ pid : %d ] , top_path = %s ..... [[ END ]]", getpid() , top_path);

    return r;
}

/**
 * Process the given directory or file.
 *
 * This will add or update media found in the given directory or its children.
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @param top_path top directory or file to scan.
 *
 * @return On success 0 is returned.
 */
int
lms_process(lms_t *lms, const char *top_path)
{
    struct pinfo pinfo;
    int r;

    log_info("    [ pid : %d ] , top_path = %s ..... [[ START ]]", getpid() , top_path);

    r = _lms_process_check_valid(lms, top_path);
    if (r < 0)
        return r;

    pinfo.common.lms = lms;

    if (lms_create_pipes(&pinfo) != 0) {
        r = -1;
        goto end;
    }

    if (lms_create_slave(&pinfo, _slave_work) != 0) {
        r = -2;
        goto close_pipes;
    }

    r = _process_trigger(&pinfo.common, top_path, _process_file);

    lms_finish_slave(&pinfo, _master_send_finish);

close_pipes:
    lms_close_pipes(&pinfo);

end:
    log_info("    [ pid : %d ] , top_path = %s ..... [[ END ]]", getpid() , top_path);

    return r;
}

/**
 * Process the given directory or file *without fork()-ing* into child process.
 *
 * This will add or update media found in the given directory or its children.
 * Note that if a parser hangs during the process, this call will also hang.
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @param top_path top directory or file to scan.
 *
 * @return On success 0 is returned.
 */
int
lms_process_single_process(lms_t *lms, const char *top_path)
{
    struct sinfo sinfo;
    int r;

    r = _lms_process_check_valid(lms, top_path);
    if (r < 0)
        return r;

    sinfo.common.lms = lms;
    sinfo.commit_counter = 0;
    sinfo.total_committed = 0;

    r = _db_and_parsers_setup(sinfo.common.lms, &sinfo.db, &sinfo.parser_match);
    if (r < 0)
        return r;

    r = lms_db_update_id_get(sinfo.db->handle);
    if (r < 0) {
        log_error("ERROR: could not get global update id.");
        goto done;
    }

    sinfo.common.update_id = r + 1;

    lms_db_begin_transaction(sinfo.db->transaction_begin);

    r = _process_trigger(&sinfo.common, top_path, _process_file_single_process);

    /* Check only if there are remaining commits to do */
    if (sinfo.commit_counter) {
        sinfo.total_committed += sinfo.commit_counter;
        lms_db_update_id_set(sinfo.db->handle, sinfo.common.update_id);
    }

    lms_db_end_transaction(sinfo.db->transaction_commit);

done:
    free(sinfo.parser_match);
    lms_parsers_finish(lms, sinfo.db->handle);
    _db_close(sinfo.db);
    return r;
}

void
lms_stop_processing(lms_t *lms)
{
    if (!lms)
        return;
    if (!lms->is_processing)
        return;

    lms->stop_processing = 1;
}
