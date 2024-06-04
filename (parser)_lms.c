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

#include <dlfcn.h>
#ifdef HAVE_MAGIC_H
#include <magic.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <locale.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "lightmediascanner.h"
#include "lightmediascanner_private.h"
#include "lightmediascanner_plugin.h"
#include "lightmediascanner_logger.h"

#define DEFAULT_SLAVE_TIMEOUT 1000
#define DEFAULT_COMMIT_INTERVAL 100

#ifdef HAVE_MAGIC_H
static magic_t _magic_handle;

static void
_magic_handle_clean(void) {
   magic_close(_magic_handle);
}

static int
_magic_handle_setup(void) {
    if (_magic_handle)
      return 1;

    _magic_handle = magic_open(MAGIC_MIME_TYPE | MAGIC_PRESERVE_ATIME);
    if (!_magic_handle) {
        log_error("ERROR: failed magic_open(): %s",
                magic_error(_magic_handle));
        return 0;
    }

    if (magic_load(_magic_handle, NULL) != 0) {
        log_error("ERROR: failed magic_load() - %s",
                magic_error(_magic_handle));
        magic_close(_magic_handle);
        _magic_handle = NULL;
        return 0;
    }

   if (atexit(_magic_handle_clean) != 0) {
       log_error("ERROR: atexit is failed");
       return 0;
   }
   return 1;
}
#endif

int
lms_mime_type_get_from_path(const char *path, struct lms_string_size *mime) {
#ifdef HAVE_MAGIC_H
   const char *s;
   if (!path || !mime) return 0;
   if (!_magic_handle_setup()) return 0;
   s = magic_file(_magic_handle, path);
   if (!s) return 0;
   mime->str = (char *)s;

   size_t str_len = strlen(s);
   if (str_len > UINT_MAX)
   {
     log_error("ERROR: unsigned integer operation may overflow");
     return 0;
   }
   else
   {
     mime->len = str_len;
   }

   return 1;
#else
   return 0;
#endif
}

int
lms_mime_type_get_from_fd(int fd, struct lms_string_size *mime) {
#ifdef HAVE_MAGIC_H
   const char *s;
   if (fd < 0 || !mime) return 0;
   if (!_magic_handle_setup()) return 0;
   s = magic_descriptor(_magic_handle, fd);
   if (!s) return 0;
   mime->str = (char *)s;

   size_t str_len = strlen(s);
   if (str_len > UINT_MAX)
   {
     log_error("ERROR: unsigned integer operation may overflow");
     return 0;
   }
   else
   {
     mime->len = str_len;
   }

   return 1;
#else
   return 0;
#endif
}

static int
_parser_load(struct parser *p, const char *so_path)
{
    lms_plugin_t *(*plugin_open)(void);
    char *errmsg;

    log_info("so_path = %s", so_path);

    memset(p, 0, sizeof(*p));
    p->dl_handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (p->dl_handle == NULL) {
        log_error("ERROR: p->dl_handle is null");
        return -2;
    } else {
        errmsg = dlerror();
        if (errmsg) {
            log_error("ERROR: could not dlopen() %s", errmsg);
            return -1;
        }
    }

    plugin_open = dlsym(p->dl_handle, "lms_plugin_open");
    errmsg = dlerror();
    if (errmsg) {
        log_error("ERROR: could not find plugin entry point %s",
                errmsg);
        return -2;
    }
    p->so_path = strdup(so_path);
    if (!p->so_path) {
        perror("strdup");
        return -3;
    }
    p->plugin = plugin_open();
    if (!p->plugin) {
        log_error("ERROR: plugin \"%s\" failed to init.", so_path);
        return -4;
    }
    return 0;
}

static int
_parser_unload(struct parser *p)
{
    int r;

    r = 0;
    if (p->plugin) {
        if (p->plugin->close(p->plugin) != 0) {
            log_error("ERROR: plugin \"%s\" failed to deinit.",
                    p->so_path);
            r -= 1;
        }
    }
    if (p->dl_handle) {
        char *errmsg;
        dlclose(p->dl_handle);
        errmsg = dlerror();
        if (errmsg) {
            log_error("ERROR: could not dlclose() plugin \"%s\": %s",
                    errmsg, p->so_path);
            r -= 1;
        }
    }
    if (p->so_path)
      free(p->so_path);
    return r;
}


/***********************************************************************
 * Public API.
 ***********************************************************************/
/**
 * Create new Light Media Scanner instance.
 *
 * @param db_path path to database file.
 * @return allocated data on success or NULL on failure.
 * @ingroup LMS_API
 */
lms_t *
lms_new(const char *db_path)
{
    lms_t *lms;

    lms = calloc(1, sizeof(lms_t));
    if (!lms) {
        perror("calloc");
        return NULL;
    }
    lms->cs_conv = lms_charset_conv_new();
    if (!lms->cs_conv) {
        free(lms);
        return NULL;
    }
    lms->commit_interval = DEFAULT_COMMIT_INTERVAL;
    lms->slave_timeout = DEFAULT_SLAVE_TIMEOUT;
    lms->db_path = strdup(db_path);
    if (!lms->db_path) {
        perror("strdup");
        lms_charset_conv_free(lms->cs_conv);
        free(lms);
        return NULL;
    }
    return lms;
}

/**
 * Free existing Light Media Scanner instance.
 *
 * @param lms previously allocated Light Media Scanner instance.
 *
 * @return On success 0 is returned.
 * @ingroup LMS_API
 */
int
lms_free(lms_t *lms)
{
    int i;

    if (!lms)
        return 0;
    if (lms->is_processing)
        return -1;
    if (lms->parsers) {
        for (i = 0; i < lms->n_parsers; i++)
            _parser_unload(lms->parsers + i);
        free(lms->parsers);
    }
    if (lms->progress.data && lms->progress.free_data)
        lms->progress.free_data(lms->progress.data);
    free(lms->db_path);
    lms_charset_conv_free(lms->cs_conv);
    g_list_free_full(lms->completed_scan_paths, g_free);
    free(lms);
    return 0;
}

/**
 * Set callback to be used to report progress (check and process).
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @param cb function to call when files are processed or NULL to unset.
 * @param data data to give to cb when it's called, may be NULL.
 * @param free_data function to call to free @a data when lms is freed or
 *        new progress data is set.
 */
void
lms_set_progress_callback(lms_t *lms, lms_progress_callback_t cb, const void *data, lms_free_callback_t free_data)
{
    if (!lms) {
        if (data && free_data)
            free_data((void *)data);
        return;
    }
    if (lms->progress.data && lms->progress.free_data)
        lms->progress.free_data(lms->progress.data);
    lms->progress.cb = cb;
    lms->progress.data = (void *)data;
    lms->progress.free_data = free_data;
}

#ifdef PATCH_LGE
//cid:12384222
void
lms_set_progress_device_callback(lms_t *lms, lms_progress_device_callback_t cb, void *data, lms_free_callback_t free_data)
{
    if (!lms) {
        if (data && free_data)
            free_data((void *)data);
        g_debug("[%s:%d]fodler calllback not registered", __FUNCTION__, __LINE__);
        return;
    }

    if (lms->progress_device.data && lms->progress_device.free_data)
        lms->progress_device.free_data(lms->progress_device.data);

    lms->progress_device.cb = cb;
    lms->progress_device.data = data;
    lms->progress_device.free_data = free_data;
}
#endif

static int
_plugin_sort(const struct parser *a, const struct parser *b)
{
    int ret = 0;
    if (__builtin_ssub_overflow(a->plugin->order, b->plugin->order, &ret)) {
        log_error("ERROR : (a->plugin->order - b->plugin->order) operation may overflow");
        return 0;
    }

    return ret;
}

/**
 * Add parser plugin given it's shared object path.
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @param so_path path to shared object (usable by dlopen(3)).
 *
 * @return On success the LMS handle to plugin is returned, NULL on error.
 * @ingroup LMS_API
 */
lms_plugin_t *
lms_parser_add(lms_t *lms, const char *so_path)
{
    struct parser *parser;

    log_debug("so_path = %s", so_path);

    if (!lms)
        return NULL;

    if (!so_path)
        return NULL;

    if (lms->is_processing) {
        log_error("ERROR: do not add parsers while it's processing.");
        return NULL;
    }

    if (lms->n_parsers + 1 <= 0)
    {
        log_error("ERROR: unsigned long  operation may overflow");
        return NULL;
    }
    else
    {
      lms->parsers = realloc(lms->parsers,
                           (unsigned long)(lms->n_parsers + 1) * sizeof(struct parser));
    }
    if (!lms->parsers) {
        perror("realloc");
        return NULL;
    }

    parser = lms->parsers + lms->n_parsers;
    if (_parser_load(parser, so_path) != 0) {
        _parser_unload(parser);
        return NULL;
    }

    lms->n_parsers++;
    qsort(lms->parsers, lms->n_parsers, sizeof(struct parser),
          (comparison_fn_t)_plugin_sort);
    return parser->plugin;
}

static int
lms_parser_find(char *buf, int buf_size, const char *name)
{
    int r;

    if (buf_size <= 0)
    {
      log_error("ERROR: Unsigned long may overlow");
    }
    else
    {
        r = snprintf(buf, (size_t)buf_size, "%s/%s.so", PLUGINSDIR, name);
        if (r >= buf_size)
          return 0;
    }

    return 1;
}
/**
 * Add parser plugin given it's name.
 *
 * This will look at default plugin path by the file named @p name (plus
 * the required shared object extension).
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @param name plugin name.
 *
 * @return On success the LMS handle to plugin is returned, NULL on error.
 * @ingroup LMS_API
 */
lms_plugin_t *
lms_parser_find_and_add(lms_t *lms, const char *name)
{
    char so_path[PATH_MAX];

    log_info("name = %s", name);

    if (!lms)
        return NULL;
    if (!name)
        return NULL;

    if (sizeof(so_path) > INT_MAX)
    {
        log_error("ERROR: Integer operation may overflow");
    }
    else
    {
        if (!lms_parser_find(so_path, (int)sizeof(so_path), name))
          return NULL;
    }

    log_info("name = %s , so_path = %s", name , so_path);

    return lms_parser_add(lms, so_path);
}

int
lms_parser_del_int(lms_t *lms, int i)
{
    struct parser *parser;

    parser = lms->parsers + i;
    _parser_unload(parser);
    if (__builtin_ssub_overflow(lms->n_parsers, 1, &lms->n_parsers)) {
        log_error("ERROR:  lms->n_parsers may overflow");
        return 0;
    }

    if (lms->n_parsers == 0) {
        free(lms->parsers);
        lms->parsers = NULL;
        return 0;
    } else {
        int dif;

        dif = lms->n_parsers - i;
        if (dif)
            memmove(parser, parser + 1, dif * sizeof(struct parser));

        lms->parsers = realloc(lms->parsers,
                               lms->n_parsers * sizeof(struct parser));
        if (!lms->parsers) {
            lms->n_parsers = 0;
            return -1;
        }

        return 0;
    }
}

/**
 * Delete previously added parser, making it unavailable for future operations.
 *
 * @param lms previously allocated Light Media Scanner instance.
 *
 * @return On success 0 is returned.
 * @ingroup LMS_API
 */
int
lms_parser_del(lms_t *lms, lms_plugin_t *handle)
{
    int i;

    if (!lms)
        return -1;
    if (!handle)
        return -2;
    if (!lms->parsers)
        return -3;
    if (lms->is_processing) {
        return -4;
    }

    for (i = 0; i < lms->n_parsers; i++)
        if (lms->parsers[i].plugin == handle)
            return lms_parser_del_int(lms, i);
    return -3;
}
/**
 * Checks if Light Media Scanner is being used in a processing operation lile
 * lms_process() or lms_check().
 *
 * @param lms previously allocated Light Media Scanner instance.
 *
 * @return 1 if it is processing, 0 if it's not, -1 on error.
 * @ingroup LMS_API
 */
int
lms_is_processing(const lms_t *lms)
{
    if (!lms) {
        log_error("ERROR: lms_is_processing(NULL)");
        return -1;
    }
    return lms->is_processing;
}
/**
 * Get the database path given at creation time.
 *
 * @param lms previously allocated Light Media Scanner instance.
 *
 * @return path to database.
 * @ingroup LMS_API
 */
const char *
lms_get_db_path(const lms_t *lms)
{
    if (!lms) {
        log_error("ERROR: lms_get_db_path(NULL)");
        return NULL;
    }
    return lms->db_path;
}
/**
 * Get the maximum amount of milliseconds the slave can take to serve one file.
 *
 * If a slave takes more than this amount of milliseconds, it will be killed
 * and the scanner will continue with the next file.
 *
 * @param lms previously allocated Light Media Scanner instance.
 *
 * @return -1 on error or time in milliseconds otherwise.
 * @ingroup LMS_API
 */
int
lms_get_slave_timeout(const lms_t *lms)
{
    if (!lms) {
        log_error("ERROR: lms_get_slave_timeout(NULL)");
        return -1;
    }
    return lms->slave_timeout;
}
/**
 * Set the maximum amount of milliseconds the slave can take to serve one file.
 *
 * If a slave takes more than this amount of milliseconds, it will be killed
 * and the scanner will continue with the next file.
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @param ms time in milliseconds.
 * @ingroup LMS_API
 */
void lms_set_slave_timeout(lms_t *lms, int ms)
{
    if (!lms) {
        log_error("ERROR: lms_set_slave_timeout(NULL, %d)", ms);
        return;
    }
    lms->slave_timeout = ms;
}
/**
 * Get the number of files served between database transactions.
 *
 * This is used as an optimization to database access: doing database commits
 * take some time and can slow things down too much, so you can choose to just
 * commit after some number of files are processed, this is the commit_interval.
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @return (unsigned int)-1 on error, value otherwise.
 * @ingroup LMS_API
 */
int
lms_get_commit_interval(const lms_t *lms)
{
    if (!lms) {
        log_error("ERROR: lms_get_commit_interval(NULL)");
        return -1;
    }

    if (lms->commit_interval > INT_MAX) {
        log_error("ERROR: lms->commit_interval overflow");
        return -1;
    }

    return lms->commit_interval;
}

/**
 * Set the number of files served between database transactions.
 *
 * This is used as an optimization to database access: doing database commits
 * take some time and can slow things down too much, so you can choose to just
 * commit after @p transactions files are processed.
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @param transactions number of files (transactions) to process between
 *        commits.
 * @ingroup LMS_API
 */
void
lms_set_commit_interval(lms_t *lms, unsigned int transactions)
{
    if (!lms) {
        log_error("ERROR: lms_set_commit_interval(NULL, %u)",
                transactions);
        return;
    }
    lms->commit_interval = transactions;
}

void
lms_set_commit_duration(lms_t *lms, double duration)
{
    if (!lms) {
        log_error("ERROR: lms_set_commit_duration(NULL, %lf)",
                duration);
        return;
    }

    lms->commit_duration = duration;
}

void
lms_set_lms_target(lms_t *lms, int lms_target)
{
    if (!lms) {
        log_error("ERROR: lms_set_lms_target(NULL, %d)", lms_target);

        return;
    }

    lms->lmsTarget = (LMS_TARGET)lms_target;
}

#if defined(ENABLE_LIMIT_NUMBERS_OF_FILE_SCAN)

    void lms_set_maxFileScanCount(lms_t *lms, int maxFileScanCount)
    {
        if (!lms) {
            log_error("[[[ ERROR ]]] maxFileScanCount = %d)", maxFileScanCount);

            return;
        }

        lms->maxFileScanCount = maxFileScanCount;
    }

    void lms_set_currentFileScanCount(lms_t *lms, int currentFileCount)
    {
        if (!lms) {
            log_error("[[[ ERROR ]]] currentFileCount = %d)", currentFileCount);

            return;
        }

        lms->currentFileCount = currentFileCount;
    }

    void lms_increase_currentFileScanCount(lms_t *lms, int currentFileCount)
    {
        if (!lms) {
            log_error("[[[ ERROR ]]] currentFileCount = %d)", currentFileCount);

            return;
        }

        if (__builtin_sadd_overflow(lms->currentFileCount, 1, &lms->currentFileCount)) {
            log_error("ERROR: lms->currentFileCount may overflow");
        }
    }

    void lms_set_isPrintDirectoryStructure(lms_t *lms, int isPrint)
    {
        if (!lms) {
            log_error("ERROR: lms_set_isPrintDirectoryStructure(NULL, %d)", isPrint);

            return;
        }

        lms->isPrintDirectoryStructure = isPrint;
    }

#endif

/**
 * Clear the completed scan path.
 *
 * This is used as an optimization to scan data: Clearing completed completed_scan_paths.
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @ingroup LMS_API
 */
void
lms_clear_completed_scan_path(lms_t *lms)
{
    if (!lms) {
        log_error("ERROR: lms_clear_completed_scan_path(NULL)");
        return;
    }
    g_list_free_full(lms->completed_scan_paths, g_free);
}
/**
 * Set the completed scan path.
 *
 * This is used as an optimization to scan data: saving completed pending_urgent_scan
 * to skip completed path at next normal scan.
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @param path pending_completed_scan path.
 * @ingroup LMS_API
 */
void
lms_set_completed_scan_path(lms_t *lms, char *path)
{
    if (!lms) {
        log_error("ERROR: lms_set_pending_completed_scan(NULL, %s)", path);
        return;
    }
    lms->completed_scan_paths = g_list_append(lms->completed_scan_paths, path);
}

/**
 * Clear the completed scan path.
 *
 * This is used as an optimization to scan data: Clearing device_scan_paths.
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @ingroup LMS_API
 */
void
lms_clear_device_scan_path(lms_t *lms)
{
    if (!lms) {
        log_error("ERROR: lms_clear_device_scan_path(NULL)");
        return;
    }

    g_list_free_full(lms->device_scan_paths, g_free);
}


/**
 * Set the completed scan path.
 *
 * This is used as an optimization to scan data: saving device_scan_scan
 * to check currunt scanning path.
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @param path pending_completed_scan path.
 * @ingroup LMS_API
 */
void
lms_set_device_scan_path(lms_t *lms, char *path)
{
    if (!lms) {
        log_error("ERROR: lms_set_pending_device_scan(NULL, %s)", path);
        return;
    }

    lms->device_scan_paths = g_list_append(lms->device_scan_paths, path);
}

/**
 * Register a new charset encoding to be used.
 *
 * All database text strings are in UTF-8, so one needs to register new
 * encodings in order to convert to it.
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @param charset charset name as understood by iconv_open(3).
 *
 * @return On success 0 is returned.
 * @ingroup LMS_API
 */
int
lms_charset_add(lms_t *lms, const char *charset)
{
    if (!lms) {
        log_error("ERROR: lms_charset_add(NULL)");
        return -1;
    }

    return lms_charset_conv_add(lms->cs_conv, charset);
}
/**
 * Forget about registered charset encoding.
 *
 * All database text strings are in UTF-8, so one needs to register new
 * encodings in order to convert to it.
 *
 * @param lms previously allocated Light Media Scanner instance.
 * @param charset charset name as understood by iconv_open(3).
 *
 * @return On success 0 is returned.
 * @ingroup LMS_API
 */
int
lms_charset_del(lms_t *lms, const char *charset)
{
    if (!lms) {
        log_error("ERROR: lms_charset_del(NULL)");
        return -1;
    }

    return lms_charset_conv_del(lms->cs_conv, charset);
}
/**
 * List all known parsers on the system.
 *
 * No information is retrieved, you might like to call lms_parser_info()
 * on the callback path.
 *
 * @param cb function to call for each path found. If it returns 0,
 *        it stops iteraction.
 * @param data extra data to pass to @a cb on every call.
 */
void
lms_parsers_list(int (*cb)(void *data, const char *path), const void *data)
{
    void *datap = (void *)data;
    char path[PATH_MAX] = PLUGINSDIR;
    int base;
    DIR *d;
    struct dirent *de;

    log_info("[ pid : %d ] , path = %s" , getpid() , path);

    if (!cb)
        return;
    base = sizeof(PLUGINSDIR) - 1;
    if (base + sizeof("/.so") >= PATH_MAX) {
        log_error("ERROR: path is too long '%s'", path);
        return;
    }
    d = opendir(path);
    if (!d) {
        log_error("ERROR: could not open directory %s: %s",
                path, strerror(errno));
        return;
    }

    path[base] = '/';
    base++;

    while ((de = readdir(d)) != NULL) {
        size_t len;

        if (de->d_name[0] == '.')
            continue;

        len = strlen(de->d_name);
        if (len < 3 || memcmp(de->d_name + len - 3, ".so", 3) != 0)
           continue;

        memcpy(path + base, de->d_name, len + 1); /* copy \0 */
        if (!cb(datap, path))
            break;
    }
    closedir(d);
}

struct lms_parsers_list_by_category_data {
    const char *category;
    int (*cb)(void *data, const char *path, const struct lms_parser_info *info);
    void *data;
};

static int
_lms_parsers_list_by_category(void *data, const char *path)
{
    struct lms_parsers_list_by_category_data *d = data;
    struct lms_parser_info *info;
    int r;

    info = lms_parser_info(path);
    if (!info)
        return 1;

    r = 1;
    if (info->categories) {
        const char * const *itr;
        for (itr = info->categories; *itr != NULL; itr++)
            if (strcmp(d->category, *itr) == 0) {
                r = d->cb(d->data, path, info);
                break;
            }
    }
    lms_parser_info_free(info);
    return r;
}
/**
 * List all known parsers of a given category.
 *
 * Since we need information to figure out parser category, these are
 * passed as argument to callback, but you should NOT modify or reference it
 * after callback function returns since it will be released after that.
 *
 * @param category which category to match.
 * @param cb function to call for each path found. If it returns 0,
 *        it stops iteraction.
 * @param data extra data to pass to @a cb on every call.
 */
void
lms_parsers_list_by_category(const char *category, int (*cb)(void *data, const char *path, const struct lms_parser_info *info), const void *data)
{
    struct lms_parsers_list_by_category_data d;

    if (!category || !cb)
        return;

    d.category = category;
    d.cb = cb;
    d.data = (void *)data;

    lms_parsers_list(_lms_parsers_list_by_category, &d);
}

static int
_lms_string_array_count(const char * const *array, int *size)
{
    int count;
    size_t align_overflow;

    *size = 0;
    if (!array)
        return 0;

    count = 0;
    size_t len_value = 0;
    for (; *array != NULL; array++) {
        len_value = sizeof(char *) + strlen(*array) + 1;
        if (len_value > INT_MAX){
            log_error("ERROR: len_value may overflow");
            return 0;
        } else {
          *size += (int)len_value;
        }
        if (__builtin_sadd_overflow(count, 1, &count)){
            log_error("ERROR: count add opr overflow");
            return 0;
        }
    }
    if (count) {
        /* count NULL terminator */
        if (__builtin_sadd_overflow(count, 1, &count)){
            log_error("ERROR: count add opr overflow");
            return 0;
        }
        *size += sizeof(char *);
    }

    align_overflow = (size_t)(*size) % sizeof(char *);
    if (align_overflow){
       size_t overflow_len = sizeof(char *) - align_overflow;
       if (overflow_len > INT_MAX) {
           log_error("ERROR: integer operation overflow");
           return 0;
       } else {
           *size += (int)overflow_len;
       }
    }
    return count;
}

static void
_lms_string_array_copy(char **dst, const char * const *src, int count)
{
    char *d;

    d = (char *)(dst + count);

    for (; count > 1; count--, dst++, src++) {
        size_t len = 0;

        len = strlen(*src) + 1;
        *dst = d;
        memcpy(*dst, *src, len);
        d += len;
    }

    *dst = NULL;
}

/**
 * Get parser information.
 *
 * Information can be used to let user choose parsers on Graphical User
 * Interfaces.
 *
 * @param so_path full path to module.
 * @see lms_parser_info_find()
 */
struct lms_parser_info *
lms_parser_info(const char *so_path)
{
    const struct lms_plugin_info *(*plugin_info)(void);
    const struct lms_plugin_info *pinfo;
    struct lms_parser_info *ret;
    const char *errmsg;
    void *dl_handle;
    size_t len = 0;
    size_t path_len = 0;
    size_t uri_len = 0;
    size_t desc_len = 0;
    size_t ver_len = 0;
    size_t name_len = 0;
    int cats_count, cats_size, authors_count, authors_size;

    if (!so_path)
        return NULL;

    dl_handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (dl_handle == NULL) {
        log_error("ERROR: dl_handle is null");
        return NULL;
    } else {
        errmsg = dlerror();
        if (errmsg) {
            log_error("ERROR: could not dlopen() %s", errmsg);
            if (dl_handle)
                dlclose(dl_handle);
            return NULL;
        }
    }

    ret = NULL;
    plugin_info = dlsym(dl_handle, "lms_plugin_info");
    errmsg = dlerror();
    if (errmsg) {
        log_error("ERROR: could not find plugin info function %s",
                errmsg);
        goto close_and_exit;
    }

    if (!plugin_info) {
        log_error("ERROR: lms_plugin_info is NULL");
        goto close_and_exit;
    }

    pinfo = plugin_info();
    if (!pinfo) {
        log_error("ERROR: lms_plugin_info() returned NULL");
        goto close_and_exit;
    }

    path_len = strlen(so_path) + 1;
    name_len = pinfo->name ? strlen(pinfo->name) + 1 : 0;
    desc_len = pinfo->description ? strlen(pinfo->description) + 1 : 0;
    ver_len =  pinfo->version ? strlen(pinfo->version) + 1 : 0;
    uri_len =  pinfo->uri ? strlen(pinfo->uri) + 1 : 0;

    cats_count = _lms_string_array_count(pinfo->categories, &cats_size);
    authors_count = _lms_string_array_count(pinfo->authors, &authors_size);

    size_t sum1;
    if (__builtin_uaddl_overflow(path_len, name_len, &sum1))
    {
        log_error("ERROR1: unsigned long operation overflow");
        goto close_and_exit;
    }
    else
    {
        size_t sum2;
        if (__builtin_uaddl_overflow(sum1, desc_len, &sum2))
        {
            log_error("ERROR2: unsigned long operation overflow");
            goto close_and_exit;
        }
        else
        {
            size_t sum3;
            if (__builtin_uaddl_overflow(sum2, ver_len, &sum3))
            {
                log_error("ERROR3: unsigned long operation overflow");
                goto close_and_exit;
            }
            else
            {
                size_t sum4;
                if (__builtin_uaddl_overflow(sum3,  uri_len, &sum4))
                {
                    log_error("ERROR4: unsigned long operation overflow");
                    goto close_and_exit;
                }
                else
                {
                    size_t sum5;
                    if (cats_size < 0) {
                        log_error("ERROR: cats_size may underflow");
                        goto close_and_exit;
                    }
                    if (__builtin_uaddl_overflow(sum4, (size_t)cats_size, &sum5))
                    {
                        log_error("ERROR5: unsigned long  operation overflow");
                        goto close_and_exit;
                    }
                    else
                    {
                        if (authors_size < 0) {
                            log_error("ERROR: authors_size may underflow");
                            goto close_and_exit;
                        }
                        if (__builtin_uaddl_overflow(sum5, (size_t)authors_size, &len))
                        {
                            log_error("ERROR6: unsigned long  operation overflow");
                            goto close_and_exit;
                        }
                        else
                        {
                            ret = malloc(sizeof(*ret) + len);
                        }
                    }
                }
            }
        }
    }

    if (!ret)
    {
        log_error("ERROR: could not allocate: %s", strerror(errno));
        goto close_and_exit;
    }

    len = 0;

    if (cats_count) {
        ret->categories = (const char * const *)
            ((char *)ret + sizeof(*ret) + len);
        _lms_string_array_copy(
            (char **)ret->categories, pinfo->categories, cats_count);
        len += cats_size;
    } else
        ret->categories = NULL;

    if (authors_count) {
        ret->authors = (const char * const *)
            ((char *)ret + sizeof(*ret) + len);
        _lms_string_array_copy(
            (char **)ret->authors, pinfo->authors, authors_count);
        len += authors_size;
    } else
        ret->authors = NULL;

    ret->path = (char *)ret + sizeof(*ret) + len;
    memcpy((char *)ret->path, so_path, path_len);
    len += path_len;

    if (pinfo->name) {
        ret->name = (char *)ret + sizeof(*ret) + len;
        memcpy((char *)ret->name, pinfo->name, name_len);
        len += name_len;
    } else
        ret->name = NULL;

    if (pinfo->description) {
        ret->description = (char *)ret + sizeof(*ret) + len;
        memcpy((char *)ret->description, pinfo->description, desc_len);
        len += desc_len;
    } else
        ret->description = NULL;

    if (pinfo->version) {
        ret->version = (char *)ret + sizeof(*ret) + len;
        memcpy((char *)ret->version, pinfo->version, ver_len);
        len += ver_len;
    } else
        ret->version = NULL;

    if (pinfo->uri) {
        ret->uri = (char *)ret + sizeof(*ret) + len;
        memcpy((char *)ret->uri, pinfo->uri, uri_len);
        if (UINT_MAX - len < uri_len) {
            log_error("ERROR: len += uri_len may wrap");
            if (ret) {
                free(ret);
                ret = NULL;
            }
            goto close_and_exit;
        }
        len += uri_len;
    } else
        ret->uri = NULL;

  close_and_exit:
    dlclose(dl_handle);
    return ret;
}
/**
 * Find parser by name and get its information.
 *
 * Information can be used to let user choose parsers on Graphical User
 * Interfaces.
 *
 * @param name name of .so to find the whole so_path and retrieve information.
 * @see lms_parser_info()
 */
struct lms_parser_info *
lms_parser_info_find(const char *name)
{
    char so_path[PATH_MAX];

    if (!name)
        return NULL;


    if (sizeof(so_path) > INT_MAX)
    {
        log_error("ERROR: Integer operation may overflow");
    }
    else
    {
        if (!lms_parser_find(so_path, (int)sizeof(so_path), name))
          return NULL;
    }

    return lms_parser_info(so_path);
}
/**
 * Free previously returned information.
 *
 * @note it is safe to call with NULL.
 */
void
lms_parser_info_free(struct lms_parser_info *info)
{
    free(info);
}

void lms_set_country(lms_t *lms, lms_country_t country)
{
    if (!lms) {
        log_error("ERROR: lms_set_country_code(NULL, %d)", country);
        return;
    }
    lms->country = country;
}

void lms_set_chardet_level(lms_t *lms, int level)
{
    if (!lms) {
        log_error("ERROR: lms_set_chardet_level(NULL, %d)", level);
        return;
    }
    lms->chardet_level = level;
}

void lms_set_mutex(lms_t *lms, pthread_mutex_t *mtx) {
    if (!lms) {
        return;
    }

    lms->mtx = mtx;
}

int lms_open_database(const char* db_path) {
    sqlite3 *handle;
    char *errmsg = NULL;
    int r = 0;

    r = sqlite3_open(db_path, &handle);

    if (r != SQLITE_OK) {

        log_error("ERROR: could not open DB \"%s\": %s , r = %d" , db_path, sqlite3_errmsg(handle) , r);

        return r;
    }

    r = sqlite3_exec(handle,
                     "PRAGMA journal_mode = WAL;"
                      "PRAGMA wal_autocheckpoint = 1;",
                     NULL, NULL, &errmsg);

    if (r != SQLITE_OK) {

        log_error("ERROR: set journal mode to WAL: %s , r = %d" , errmsg , r);

        sqlite3_free(errmsg);

        // OYK_2019_05_29 : Close the DB connection.
        //                  Refer to [GENSIX-44943].
        sqlite3_close(handle);

        return r;
    }

    errmsg = NULL;
    r = sqlite3_exec(handle,
                     "CREATE TABLE IF NOT EXISTS lms_internal ("
                     "tab TEXT NOT NULL UNIQUE, "
                     "version INTEGER NOT NULL"
                     ")",
                     NULL, NULL, &errmsg);

    if (r != SQLITE_OK) {

        log_error("ERROR: could not create 'lms_internal' table: %s , r = %d" , errmsg , r);

        sqlite3_free(errmsg);

        // OYK_2019_05_29 : Close the DB connection.
        //                  Refer to [GENSIX-44943].
        sqlite3_close(handle);

        return r;
    }

    sqlite3_close(handle);
    return r;
}

void lms_delete_database(const char* db_path) {
    char *shm = NULL;
    char *wal = NULL;
    int result = -1;

    if (g_file_test(db_path, G_FILE_TEST_EXISTS)) {
        shm = g_strdup_printf("%s-shm", db_path);
        wal = g_strdup_printf("%s-wal", db_path);

        log_info("INFO: delete database to recover");
        result = g_remove(db_path);
        if (result != 0) {
            log_info("WARNING: could not delete %s", db_path);
        }

        result = g_remove(shm);

        if (result != 0) {
            log_info("WARNING: could not delete %s", shm);
        }

        result = g_remove(wal);
        if (result != 0) {
            log_info("WARNING: could not delete %s", wal);
        }
        g_free(shm);
        g_free(wal);
    }
}

int lms_create_database(const char* db_path) {

    int result = 0;

    log_info("db_path = %s" , db_path);

    // OYK_2019_05_29 : Delete and create the database if the database has problems.
    //                  Refer to [GENSIX-44943].
    //if (lms_open_database(db_path) == SQLITE_CORRUPT) {
    if ((result = lms_open_database(db_path)) != SQLITE_OK) {

        log_error("[[[ ERROR ]]] database corrupted. db_path = %s , result = %d" , db_path , result);

        lms_delete_database(db_path);
        result = lms_open_database(db_path);
    }

    return result;
}
