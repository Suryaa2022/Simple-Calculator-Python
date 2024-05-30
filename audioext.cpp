// @@@LICENSE
//
// Copyright (C) 2015, LG Electronics, All Right Reserved.
//
// No part of this source code may be communicated, distributed, reproduced
// or transmitted in any form or by any means, electronic or mechanical or
// otherwise, for any purpose, without the prior written permission of
// LG Electronics.
//
// LICENSE@@@

#include <fstream>
#include <gtk/gtk.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/regex.hpp>
#include <boost/format.hpp>
#include <stdlib.h>

#include "audioextractcontrol.h"
#include "player_logger.h"
#include "common.h"
#include "lms_parser.h"
#include "thumbnail_conf.h"

using namespace std;
using namespace boost::property_tree;
using namespace MediaParserLib;
  
namespace genivimedia{

AudioExtractControl::AudioExtractControl(DbusService *dbusSvc, DBManager *dbManager)
{
  mDbusSvc = dbusSvc;
}

AudioExtractControl::~AudioExtractControl(){}

int AudioExtractControl::startExtract(const char *path, char **oPath, void *info)
{
  string savePath;
  string coverartPath;
  GError *error = NULL;

  GdkPixbuf *pixbuf     = NULL;
  GdkPixbuf *scaledbuf  = NULL;
  int ret;
  int nResult = 0;
  gchar *path_down      = NULL;
  gchar *path_checksum  = NULL;

  LOG_DEBUG("input path [%s]", path);
  if (path == NULL)
    return -1;

  string file_path = string(path);
  LmsParser *mParser = new LmsParser(file_path);
  audio_info audioInfo = audio_info();

  mParser->parse();
  audioInfo = mParser->getAudioInfo();
  
  // for singleAudioInfo
  if(info){
    struct AudioInfo *audio_info = (struct AudioInfo *)info;

    if (audioInfo.album)
      audio_info->album = strdup(audioInfo.album);
    else
      audio_info->album = strdup(" ");    
    if (audioInfo.artist)      
      audio_info->artist = strdup(audioInfo.artist);
    else
      audio_info->artist = strdup(" ");
    if (audioInfo.title)    
      audio_info->title = strdup(audioInfo.title);
    else
      audio_info->title = strdup(" ");
    if (audioInfo.genre)    
      audio_info->genre = strdup(audioInfo.genre);
    else
      audio_info->genre = strdup(" ");
    audio_info->duration = audioInfo.duration;
  }

  //2. save coverart 
  if (audioInfo.cover_art_url == NULL){  
    LOG_DEBUG("[%s:%d] cover_art_url null", __FUNCTION__, __LINE__);
    ret =-1;
    if(*oPath){
      free(*oPath);
      *oPath = NULL;
    }
    goto exit;
  } else{
    coverartPath = string(audioInfo.cover_art_url);
    ret =0;
  }

  // scaling & save
  pixbuf = gdk_pixbuf_new_from_file (coverartPath.c_str(), &error);
  if (pixbuf == NULL){
      LOG_ERROR("Failed to load image file [%s] " , coverartPath.c_str());
      ret =-1;
      goto exit;
  }

  savePath = g_strdup_printf("%s/media-manager-coverart/",
                                g_get_user_cache_dir());


  nResult =  access(savePath.c_str(), 0);
  if (nResult ==-1){
    if (g_mkdir_with_parents(savePath.c_str(), 0644) != 0) {
      LOG_ERROR("Couldn't create directory %s", savePath.c_str());
      ret = -1;
      goto exit;
    }
  }

  path_down = g_utf8_strdown (path, -1);
  path_checksum = checksumForData (G_CHECKSUM_SHA256,
                    (const guchar *) path_down, strlen (path_down));

  if (*oPath == NULL){
    savePath.append(path_checksum);
    savePath.append(".jpg");
  }else{
    savePath.replace(0, savePath.length(),*oPath);
  }

  scaledbuf = gdk_pixbuf_scale_simple (pixbuf, Thumbnail_Conf::GetCoverWidth(), Thumbnail_Conf::GetCoverHeight(), GDK_INTERP_BILINEAR);
  if(scaledbuf)
    gdk_pixbuf_save(scaledbuf, savePath.c_str(), "jpeg", &error, "quality", "100", NULL);
  else{
    LOG_ERROR("Error : scaledbuf is NULL ");
    ret =-1;
    goto exit;
  }

  if (*oPath == NULL){
    *oPath = (char *)calloc(strlen(savePath.c_str()) +1, sizeof(char));
    if (*oPath == NULL){
      LOG_ERROR("[ViedExtractControl] Fail to allocate memory \n");
      ret =-1;
      goto exit;
    } else
      strcpy(*oPath , savePath.c_str());
  }

  if (-1 == remove(coverartPath.c_str()))
    LOG_WARN("can't delete non-scaled image file[%s]", coverartPath.c_str());

exit:
  delete(mParser);
  if(pixbuf)
    g_object_unref(pixbuf);
  if(scaledbuf)
  g_object_unref(scaledbuf);
  if(path_checksum)
    free(path_checksum);
  return ret;
}
void AudioExtractControl::stopExtract()
{

}
void AudioExtractControl::sendStatus(const string& status)
{
  GError *error = NULL;
  string filePath;
  string albumName;
  string artistName;
  string savePath;

  char strOK[] = "OK"; 
  ptree tree;

  std::stringstream stream(status);
  boost::property_tree::read_json(stream, tree);
  auto iter = tree.begin();
  string type = iter->first.c_str();

  ptree info = iter->second;
  if (type.compare(strOK) == 0) {
    filePath = info.get<string>("filepath");
    albumName = info.get<string>("albumname");
    artistName = info.get<string>("artistname");
    savePath = info.get<string>("coverartfilename");
    LOG_INFO("filepath-%s, albumname-%s, artistname-%s,thumbnailfilename-%s \n",filePath.c_str(), albumName.c_str(), artistName.c_str(),
      savePath.c_str());

    g_dbus_connection_emit_signal(mDbusSvc->mConnection,
      NULL,
      BUS_PATH,
      BUS_IFACE,
      "CoverartEvent",
      g_variant_new("(sssss)",
      "OK",
      filePath.c_str(),
      albumName.c_str(),
      artistName.c_str(),
      savePath.c_str()),
      &error);
    }else {
      filePath = info.get<string>("filepath");
      albumName = info.get<string>("albumname");
      artistName = info.get<string>("artistname");

      LOG_INFO("filepath-%s, albumname-%s, artistname-%s\n",filePath.c_str(), albumName.c_str(), artistName.c_str());

      g_dbus_connection_emit_signal(mDbusSvc->mConnection,
                      NULL,
                      BUS_PATH,
                      BUS_IFACE,
                      "CoverartEvent",
                      g_variant_new("(sssss)",
                                    "Error",
                                    filePath.c_str(),
                                    albumName.c_str(),
                                    artistName.c_str(),
                                    ""),
                                    &error);
      } 
}

gchar* AudioExtractControl::checksumForData (GChecksumType  checksum_type,
                                 const guchar  *data,
                                 int          length)
{
  GChecksum *checksum;
  char *retval;
  checksum = g_checksum_new (checksum_type);

  if (!checksum) {
    return NULL;
  }

  g_checksum_update (checksum, data, length);
  retval = g_strdup (g_checksum_get_string (checksum));
  g_checksum_free (checksum);
  return retval;
}

std::string AudioExtractControl::makeStatus(InputParamType type, int status, const char* file_path, const char *album_file, const char *artist, const char *oPath, struct AudioInfo *audio_info)
{
  std::string s;

  if (type == ALBUM_NAME_TYPE) {
    if (status == 0){
      s = (boost::format("{\"%1%\":{\"filepath\": \"%2%\",\"albumname\": \"%3%\",\"artistname\": \"%4%\",\"coverartfilename\":\"%5%\"}}")
        % kKeyCoverArtDone % file_path % album_file % artist % oPath ).str();
    } else {
      s = (boost::format("{\"%1%\":{\"filepath\": \"%2%\",\"albumname\": \"%3%\",\"artistname\": \"%4%\"}}")
        % kAudioError % file_path % album_file % artist).str();
    }
  } else if (type == FILE_PATH_TYPE) {
    if (audio_info->title != NULL) {
      s = (boost::format("{\"%1%\":{\"inputfilepath\": \"%2%\",\"coverartfilename\":\"%3%\", \
                                    \"title\":\"%4%\",\"album\":\"%5%\",\"artist\":\"%6%\", \
                                    \"genre\":\"%7%\",\"duration\":\"%8%\"}}")
        % kKeyCoverArtDone % album_file % oPath % audio_info->title % audio_info->album
        % audio_info->artist % audio_info->genre % audio_info->duration).str();
    } else {
      s = (boost::format("{\"%1%\":{\"inputfilepath\": \"%2%\"}}")
        % kAudioError % album_file).str();
    }
  }

  return s;
}

void AudioExtractControl::sendSingleAudioInfoStatus(const string& status)
{
  GError *error = NULL;
  string iPath;
  string savePath;
  string title;
  string album;
  string artist;
  string genre;
  int duration =0;

  char strOK[] = "OK";
  ptree tree;

  char *result = NULL;

  std::stringstream stream(status);
  boost::property_tree::read_json(stream, tree);
  auto iter = tree.begin();
  string type = iter->first.c_str();

  ptree info = iter->second;
  if (type.compare(strOK) == 0) {
    result = strdup("OK");
    iPath = info.get<string>("inputfilepath");
    savePath = info.get<string>("coverartfilename");
    title = info.get<string>("title");
    album = info.get<string>("album");
    artist = info.get<string>("artist");
    genre = info.get<string>("genre");
    duration = info.get<int>("duration");    
  } else {
    result = strdup("Error");
    iPath = info.get<string>("inputfilepath");
    savePath = "";
  }

  LOG_INFO("filename:[%s], thumbnailfilename:[%s], type:[%s] \n",iPath.c_str(),
    savePath.c_str(), type.c_str());

  g_dbus_connection_emit_signal(mDbusSvc->mConnection,
    NULL,
    BUS_PATH,
    BUS_IFACE,
    "SingleAudioInfoEvent",
    g_variant_new("(sssssssi)",
                result,
                iPath.c_str(),
                savePath.c_str(),
                title.c_str(),
                album.c_str(),
                artist.c_str(),
                genre.c_str(),
                duration),
                &error);
}

void AudioExtractControl::cleanup_AudioInfo(struct AudioInfo *audio_info){

  if (audio_info){
    if(audio_info->title){
      free(audio_info->title);
      audio_info->title = NULL;
    }
    if(audio_info->album){
      free(audio_info->album);
      audio_info->album = NULL;
    }
    if(audio_info->artist){
      free(audio_info->artist);
      audio_info->artist = NULL;
    }
    if(audio_info->genre){
      free(audio_info->genre);
      audio_info->genre = NULL;
    }
    free(audio_info);
    audio_info = NULL;
  }
}

void AudioExtractControl::init_AudioInfo(struct AudioInfo *audio_info){

  if (audio_info){
    if(audio_info->title){
      free(audio_info->title);
      audio_info->title = NULL;
    }
    if(audio_info->album){
      free(audio_info->album);
      audio_info->album = NULL;
    }
    if(audio_info->artist){
      free(audio_info->artist);
      audio_info->artist = NULL;
    }
    if(audio_info->genre){
      free(audio_info->genre);
      audio_info->genre = NULL;
    }
  }
}

}//namespace genivimedia
