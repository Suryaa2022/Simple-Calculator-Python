/**
 * Copyright (C) 2015 by LGE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * @author LGE <@lge.com>
 */

/**
 * @brief
 *
 * matroska file parser.
 */

#include <lightmediascanner_plugin.h>
#include <lightmediascanner_db.h>
#include <lightmediascanner_utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include <matroska/c/libmatroska_t.h>
#include <matroska/c/libmatroska.h>
#include "lms_matroska_wrapper.h"

#ifdef PATCH_LGE //hkchoi
#include <glib.h>
#include <glib/gtypes.h>
#include <gio/gio.h>
#endif

#define DECL_STR(cname, str)                                            \
    static const struct lms_string_size cname = LMS_STATIC_STRING_SIZE(str)

//#define MATROSKA_FREE(pbuffer) if(pbuffer) free(pbuffer)

//DECL_STR(_container_mkv, "mkv");
DECL_STR(_container_mka, "mka");
//DECL_STR(_codec_video_avc, "avc");
//DECL_STR(_codec_video_h264, "h264");

struct plugin {
    struct lms_plugin plugin;
    lms_db_video_t *video_db;
    lms_db_audio_t *audio_db;
};

static const char _name[] = "matroska";

static const struct lms_string_size _exts[] = {
//    LMS_STATIC_STRING_SIZE(".mkv"),
    LMS_STATIC_STRING_SIZE(".mka")
};
static const char *_cats[] = {
    "multimedia",
    "video",
    "audio",
};
static const char *_authors[] = {
    "LGE",
    NULL
};

static sqlite3 *_gDBCtxt = NULL; //for coverart

static void matroska_free(void *pBuffer){
    if(pBuffer){
        free(pBuffer);
        pBuffer = NULL;
    }
}

static gchar *
_checksum_for_data (GChecksumType  checksum_type,
                             const guchar  *data,
                             gsize          length)
{
    GChecksum *checksum;
    static char *retval;

    checksum = g_checksum_new (checksum_type);
    if (!checksum) {
        return NULL;
    }

    g_checksum_update (checksum, data, length);
    retval = g_strdup (g_checksum_get_string (checksum));
    g_checksum_free (checksum);

    return retval;
}

static void _make_checksum_coverart(const char *album, const char *artist, char** csAlbum, char**csArtist){
    const gchar *space_checksum = "7215ee9c7d9dc229d2921a40e899ec5f";
    gchar *artist_norm;
    gchar *album_norm;
    gchar *artist_checksum = NULL;
    gchar *album_checksum = NULL;
    gchar *artist_down = NULL;
    gchar *album_down=NULL;

    if (album==NULL && artist== NULL){
          g_debug("[%s:%d]Can find album and artist name\n", __FUNCTION__, __LINE__);
          if (NULL == _gDBCtxt){
              *csArtist = strdup(space_checksum);
              *csAlbum = strdup(space_checksum);
          }else{
              *csArtist = NULL;
              *csAlbum = NULL;
              return;
          }
      }

    if (artist!=NULL){
        artist_norm = g_utf8_normalize (artist, -1, G_NORMALIZE_NFKD);
        if (artist_norm){
            artist_down = g_utf8_strdown (artist_norm, -1);

            artist_checksum = _checksum_for_data (G_CHECKSUM_MD5,
                          (const guchar *) artist_down,
                          strlen (artist_down));
        }
        else
            artist_checksum = strdup(space_checksum);
    }

    if(album!=NULL){
        album_norm = g_utf8_normalize (album, -1, G_NORMALIZE_NFKD);

        if (album_norm){
            album_down = g_utf8_strdown (album_norm, -1);
            album_checksum = _checksum_for_data (G_CHECKSUM_MD5,
                        (const guchar *) album_down,
                        strlen (album_down));
        }
        else
            album_checksum = strdup(space_checksum);
    }

    if (!artist_checksum && !album_checksum){
        g_warning("Can't save APIC , artist_checksum, album_checksum are NULL \n");
        return;
    }

    *csArtist = artist_checksum;
    *csAlbum = album_checksum;

}


static void _get_matroska_cover_art (matroska_stream_t info, struct lms_string_size *coverat_url) {
    char cache_path[1025]={0,}; //hkchoi, initialization

    gchar *artist_checksum = NULL;
    gchar *album_checksum = NULL;
    gchar *art_filename = NULL;

    int fd=-1;
    int image_type = -1;
    int offset_sig =0;
    const unsigned char png_sig[8] = {0x89, 0x50, 0x4e, 0x47, 0xd, 0xa, 0x1a, 0xa};
    const unsigned char jpg_sig[3] = {0xff, 0xd8, 0xff};
    unsigned char *frame_data = info->coverart_data;
    unsigned int frame_size = (unsigned int)info->coverart_size;


    char *p=NULL;
    if (NULL != _gDBCtxt)
      p = getenv("XDG_CACHE_HOME");
    else
      p = strdup("/tmp");

    if (p) {
          int length = strlen(p);
          int cp_length = (length > 1023 ? 1023: length);

          if (cp_length >= 0 && cp_length < 1024) {
              memcpy (cache_path, p, cp_length);
              cache_path[cp_length + 1] = '\0';
          }

          // [ Static Analysis ] 2577573 : Time of check time of use
          //if (-1 ==access(cache_path, 0)){
              if (-1 == mkdir (cache_path, 0644) && (errno != EEXIST)) {
                  g_warning("[%s:%d]Error : can't make directory [%s]", __FUNCTION__, __LINE__, cache_path);
                  return;
              }
          //}

          // [ Static Analysis ] 7342488 : Calling risky function.
          // Replace strcat(...) with strncat(...).
          //strcat (cache_path, "/media-manager-artwork/");
          strncat(cache_path , "/media-manager-artwork/" , strlen("/media-manager-artwork/") + 1);

          // [ Static Analysis ] 2577573 : Time of check time of use
          //if (-1 == access(cache_path, 0)){
              if (-1 == mkdir (cache_path, 0644) && (errno != EEXIST)) {
                  g_warning("[%s:%d]Error : can't make directory [%s]", __FUNCTION__, __LINE__, cache_path);
                  return;
              }
          //}

      } else {
          int length = strlen(g_get_user_cache_dir());
          int cp_length = (length > 1024 ? 1024: length);

          strncpy (cache_path, g_get_user_cache_dir(), cp_length);

          // [ Static Analysis ] 2577573 : Time of check time of use
          //if (-1 ==access(cache_path, 0)){
              if (-1 == mkdir (cache_path, 0644) && (errno != EEXIST)){
                  g_warning("[%s:%d]Error : can't make directory [%s]", __FUNCTION__, __LINE__, cache_path);
                  return;
              }
          //}

          // [ Static Analysis ] 7342488 : Calling risky function.
          // Replace strcat(...) with strncat(...).
          //strcat (cache_path, "/media-manager-artwork/");
          strncat(cache_path , "/media-manager-artwork/" , strlen("/media-manager-artwork/") + 1);

          // [ Static Analysis ] 2577573 : Time of check time of use
          //if (-1 ==access(cache_path, 0)){
              if (-1 == mkdir (cache_path, 0644) && (errno != EEXIST)) {
                  g_warning("[%s:%d]Error : can't make directory [%s]", __FUNCTION__, __LINE__, cache_path);
                  return;
              }
          //}
      }

    if(!strcmp(info->coverart_mime, "image/png")){
      image_type = 0;
    }else if (!strcmp (info->coverart_mime,"image/jpeg")){
      image_type = 1;
    }else{
      log_error("no mime type [%s]", info->coverart_mime);
      image_type = -1;
    }

    _make_checksum_coverart(info->Album, info->Artist, &album_checksum, &artist_checksum);
    if (image_type ==0){
        if (memcmp(info->coverart_data, png_sig, sizeof(png_sig)) !=0){
            g_warning("not match PNG signature");
            goto exit;
        }
        art_filename = g_strdup_printf ("cover-art-%s-%s.png", album_checksum, artist_checksum);
        offset_sig = sizeof(png_sig);
    }else if (image_type ==1){
        if (memcmp(info->coverart_data, jpg_sig, sizeof(jpg_sig)) !=0){
            g_warning("not match JPEG SOI");
            goto exit;
        }

        art_filename = g_strdup_printf ("cover-art-%s-%s.jpg", album_checksum, artist_checksum);
        offset_sig = sizeof(jpg_sig);
    }

    if ((art_filename == NULL) || (g_strlcat(cache_path, art_filename, 1024) > 1024)) {
        log_warning("art_filename is null or truncation happens");
        goto exit;
    }
    fd =open(cache_path,O_RDWR|O_CREAT, 777);
    if (fd == -1) {
        log_error("Unable to create image file: %s", art_filename);
        goto exit;
    }

    write (fd, frame_data, frame_size);
    close(fd);

    coverat_url->str = strdup (cache_path);
    coverat_url->len = strlen(cache_path);

exit:
    matroska_free(p);
    matroska_free(art_filename);
    matroska_free(album_checksum);
    matroska_free(artist_checksum);
    matroska_free(frame_data);
    return;
}

static void *
_match(struct plugin *p, const char *path, int len, int base)
{
    long i;

    i = lms_which_extension(path, len, _exts, LMS_ARRAY_SIZE(_exts));
    if (i < 0)
      return NULL;
    else
      return (void*)(i + 1);
}

static int
_parse(struct plugin *plugin, struct lms_context *ctxt, const struct lms_file_info *finfo, void *match)
{
    struct lms_audio_info info = { };
    struct lms_string_size *coverat_url = NULL;
    int r =0;

    matroska_stream_t stream = NULL;
    _gDBCtxt = ctxt->db;

    c_string path = (c_string)finfo->path;

    g_debug("[%s:%d] start parsing [%s] ", __FUNCTION__, __LINE__, finfo->path);
    stream = matroska_open_stream_file(path);

    if (stream){
        info.id = finfo->id;
        lms_string_size_strndup(&info.album, stream->Album, -1);
        lms_string_size_strndup(&info.title, stream->Title, -1);
        lms_string_size_strndup(&info.artist, stream->Artist, -1);
        lms_string_size_strndup(&info.genre, stream->Genre, -1);
        lms_string_size_strndup(&info.codec, stream->CodecID, -1);

        info.length = stream->Duration * stream->TimecodeScale / 1000000000;
        info.container = _container_mka;
        info.sampling_rate = (unsigned int)stream->SamplingFreq;
        info.trackno = stream->TrackNo;
        info.channels = stream->Channels;

        if (stream->BPS)
            info.bitrate = atoi(stream->BPS);

#if !(USE_COVERART)
        if (!ctxt->db){
#endif
            if (stream->coverart_data && stream->coverart_size >0){
                coverat_url = (struct lms_string_size *)malloc(sizeof(struct lms_string_size ));
                if (coverat_url == NULL) {
                    r = -1;
                    g_warning("[%s:%d] failed to allocate coverart_url", __FUNCTION__, __LINE__);
                    goto exit;
                }
                _get_matroska_cover_art(stream, coverat_url);
                lms_string_size_dup(&info.album_art_url, coverat_url);
            }
#if !(USE_COVERART)
        }
#endif

    }else{
        r = -1;
        g_warning("[%s:%d] matroska stream is NULL. no info [%s]", __FUNCTION__, __LINE__, finfo->path);
        goto exit;
    }

exit:
    if (!info.title.str)
        lms_name_from_path(&info.title, finfo->path, finfo->path_len,
                           finfo->base, _exts[((long) match) - 1].len,
                           ctxt->cs_conv);


    if(info.title.str)
        lms_charset_conv(ctxt->cs_conv,&info.title.str, &info.title.len);
    if(info.artist.str)
        lms_charset_conv(ctxt->cs_conv,&info.artist.str, &info.artist.len);
    if(info.album.str)
        lms_charset_conv(ctxt->cs_conv,&info.album.str, &info.album.len);
    if(info.genre.str)
        lms_charset_conv(ctxt->cs_conv,&info.genre.str, &info.genre.len);
    if(info.codec.str)
        lms_charset_conv(ctxt->cs_conv,&info.codec.str, &info.codec.len);

    if (ctxt->db == NULL){

        // [ Static Analysis ] 2159305 : Big parameter passed by value
        lms_db_dup_audioInfo(&info, ctxt->audioInfo);
    }
    else{
        r = lms_db_audio_add(plugin->audio_db, &info);
    }

    if (stream){
        matroska_free(stream->Title);
        matroska_free(stream->Album);
        matroska_free(stream->Artist);
        matroska_free(stream->Genre);
        matroska_free(stream->coverart_mime);
        matroska_free(stream->coverart_name);
        matroska_free(stream->CodecID);
        matroska_free(stream->BPS);
        matroska_free(coverat_url);

        free(stream);
    }
    return r;
}

#ifdef PATCH_LGE
static int match_header(struct plugin *p, const char *path, int len)
{
    return -1;
}
#endif


static int
_setup(struct plugin *plugin, struct lms_context *ctxt)
{
#ifdef PATCH_LGE
    if (ctxt->db!=NULL)
#endif
      plugin->audio_db = lms_db_audio_new(ctxt->db);

    if (!plugin->audio_db)
        return -1;
    return 0;
}

static int
_start(struct plugin *plugin, struct lms_context *ctxt)
{
    return lms_db_audio_start(plugin->audio_db);
}

static int
_finish(struct plugin *plugin, struct lms_context *ctxt)
{
    if (plugin->audio_db)
        lms_db_audio_free(plugin->audio_db);
    return 0;
}

static int
_close(struct plugin *plugin)
{
    if (plugin)
        free(plugin);
    return 0;
}

API struct lms_plugin *
lms_plugin_open(void)
{
    struct plugin *plugin;
#ifdef PATCH_LGE
    plugin = calloc(1, sizeof(*plugin));
#else
    plugin = (struct plugin *)malloc(sizeof(*plugin));
#endif
    plugin->plugin.name = _name;
    plugin->plugin.match = (lms_plugin_match_fn_t)_match;
    plugin->plugin.match_header = (lms_plugin_match_header_fn_t)match_header;
    plugin->plugin.parse = (lms_plugin_parse_fn_t)_parse;
    plugin->plugin.close = (lms_plugin_close_fn_t)_close;
    plugin->plugin.setup = (lms_plugin_setup_fn_t)_setup;
    plugin->plugin.start = (lms_plugin_start_fn_t)_start;
    plugin->plugin.finish = (lms_plugin_finish_fn_t)_finish;
    plugin->plugin.order = 0;

    return (struct lms_plugin *)plugin;
}

API const struct lms_plugin_info *
lms_plugin_info(void)
{
    static struct lms_plugin_info info = {
        _name,
        _cats,
        "MATROSKA parser",
        PACKAGE_VERSION,
        _authors,
        "http://github.com/profusion/lightmediascanner"
    };

    return &info;
}

