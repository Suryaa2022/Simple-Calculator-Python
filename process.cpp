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

#include <boost/property_tree/json_parser.hpp>
#include <boost/regex.hpp>
#include <boost/format.hpp>


#include "videoextractcontrol.h"
#include "extractor_api.h"
#include "player_logger.h"
#include "common.h"

using namespace std;
using namespace boost::property_tree;

namespace genivimedia{
VideoExtractControl::VideoExtractControl(DbusService *dbusSvc, DBManager *dbManager,  GAsyncQueue *CbQueue)
{
  ThumbnailHandler callback = std::bind(&VideoExtractControl::HandleEvent, this,  std::placeholders::_1);
  Extractor_Init(callback);

  mDbusSvc = dbusSvc;
  mCbQueue = CbQueue;
  mDbManager = dbManager;

  m_filename = new string();
}

VideoExtractControl::~VideoExtractControl()
{
  Extractor_UnInit();
  delete m_filename;
}

void VideoExtractControl::HandleEvent (const std::string& data) {
  sendStatus(data);
}

int VideoExtractControl::startExtract(const char *path, char **oPath, void *info)
{
  char *title_norm = NULL;
  char *title_down = NULL;
  char *title_checksum = NULL;
#if 0 //SWMD
  string inPath;
#endif
  string outfile;
  string prefixPath = "thumbnail://";

  title_norm = g_utf8_normalize ((gchar *)path, -1, G_NORMALIZE_NFKD);
  if (title_norm){
    title_down = g_utf8_strdown (title_norm, -1);
    title_checksum = checksumForData (G_CHECKSUM_SHA256,
                    (const guchar *) title_down, strlen (title_down));

#if 1 //temp
    *m_filename = string(path);
    LOG_INFO("file_name[%s]",  m_filename->c_str());
#else
    inPath = string(path);
#endif
    prefixPath.append(path);
    outfile = string((char *)title_checksum);
  }
  else{
    LOG_ERROR("[VideoExtractControl]Fail to normalization, Can't extract [%s]", path);
    return -1;
  }

  Extractor_Start(prefixPath, outfile);
  *oPath = (char *)calloc(strlen(outfile.c_str())+1, sizeof(char));
  if (*oPath == NULL){
    LOG_ERROR("[ViedExtractControl] Fail to allocate memory \n");
    return -1;
  }
 
  return 0;  
}

void VideoExtractControl::stopExtract()
{
  Extractor_Stop();
}

void VideoExtractControl::sendStatus(const string& status)
{
  GError *error = NULL;
  char strOK[] = "ThumbnailDone";
  
  ptree tree;
  std::stringstream stream(status);
  boost::property_tree::read_json(stream, tree);
  auto iter = tree.begin();
  string type = iter->first.c_str();
  ptree info = iter->second;

  string findPrefix = "thumbnail://";
  string filename;
  string thumbnailfilename;
  unsigned int duration=0;
  
  if (type.compare(strOK) == 0) {

    filename = *m_filename;
    if(filename.empty()){
      filename = info.get<string>("filename");

      if(filename.find(findPrefix) == 0)
        filename.erase(0, findPrefix.length());
    }
     
    thumbnailfilename = info.get<string>("thumbnailfilename");
    duration = info.get<int>("duration");
    
    LOG_INFO("filename-%s, thumbnailfilename-%s, duration-%d, playtime-default 0",filename.c_str(), 
      thumbnailfilename.c_str(), duration);
    mDbManager->addThumbnailInfo(filename.c_str(), thumbnailfilename.c_str(), duration);

    g_async_queue_push(mCbQueue, (gpointer)"ThumbnailDone");
   
  } else if (type.compare("Error") == 0) {
    filename = info.get<string>("ErrorURL");
    if(filename.find(findPrefix) == 0)
      filename.erase(0, findPrefix.length());

    int error_code = info.get<int>("ErrorCode");
    if (error_code == 700)
      LOG_ERROR("ERROR_THUMBNAIL_EXTRACT");

    if (error_code == 200 || error_code ==201){
      LOG_WARN("Ignored Error(%d)",error_code);
    }else{
      g_async_queue_push(mCbQueue, (gpointer)"Error");
    }
  }else{
    LOG_INFO("Unknown");
    g_async_queue_push(mCbQueue, (gpointer)"Unknown");
  }

  g_dbus_connection_emit_signal(mDbusSvc->mConnection,
                    NULL,
                    BUS_PATH,
                    BUS_IFACE,
                    "ExtractorEvent",
                    g_variant_new("(sssii)",
                    type.c_str(),
                    filename.c_str(),
                    thumbnailfilename.c_str(),
                    duration,
                    0),
                    &error);

}

gchar * VideoExtractControl::checksumForData (GChecksumType  checksum_type,
                             const guchar *data, int length)
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

string VideoExtractControl::makeStatus(char *status, char *iPath, char *oPath, int duration, int playtime)
{
  using boost::property_tree::ptree;

#if 0 //temp
  int error_code = -100;   

  boost::property_tree::ptree sub_tree;
  sub_tree.put(kThumbnailGivenFilename, iPath);
  sub_tree.put(kThumbnailSaveFilename, oPath);
  sub_tree.put(kThumbnailDuration, duration);
  sub_tree.put(kThumbnailPlaytime, playtime);

  boost::property_tree::ptree tree;
  if (strcmp(status, "ThumbnailDone") ==0)  
    tree.add_child(kKeyThumbnail, sub_tree);
  else
    sub_tree.put(kErrorCode, error_code);
  
  std::string json = GenerateJson(tree);

  LOG_INFO("makestatus [%s]", json.c_str());
  return json;
#else
  std::string s = (boost::format("{\"%1%\":{\"filename\": \"%2%\",\"thumbnailfilename\":\"%3%\",\"duration\":\"%4%\",\"playtime\": \"%5%\"}}") 
    % status % iPath % oPath %duration %playtime).str();

  return s;
#endif
}

#if 0
string VideoExtractControl::GenerateJson(const boost::property_tree::ptree &tree) {
  using boost::property_tree::json_parser::write_json;

  std::stringstream stream;
  boost::regex replaceRegex("\"(null|true|false|-?[0-9]+(\\.[0-9]+)?)\"");
  write_json(stream, tree, false);
  return boost::regex_replace(stream.str(), replaceRegex, "$1");
}
#endif

void VideoExtractControl::sendDBInfo(const string& status)
{
  GError *error = NULL;
  char strOK[] = "ThumbnailDone";

  ptree tree;
  std::stringstream stream(status);
  boost::property_tree::read_json(stream, tree);
  auto iter = tree.begin();
  string type = iter->first.c_str();
  ptree info = iter->second;

  string findPrefix = "thumbnail://";
  string filename;
  string thumbnailfilename;
  unsigned int duration=0;
  unsigned int playtime=0;

  if (type.compare(strOK) == 0) {
    filename = info.get<string>("filename");

    if(filename.find(findPrefix) == 0)
      filename.erase(0, findPrefix.length());

    thumbnailfilename = info.get<string>("thumbnailfilename");
    duration = info.get<int>("duration");
    playtime = info.get<int>("playtime");

    LOG_INFO("filename-%s, thumbnailfilename-%s, duration-%d, playtime-%d",filename.c_str(), 
      thumbnailfilename.c_str(), duration, playtime);
  }

  g_dbus_connection_emit_signal(mDbusSvc->mConnection,
                    NULL,
                    BUS_PATH,
                    BUS_IFACE,
                    "ExtractorEvent",
                    g_variant_new("(sssii)",
                    type.c_str(),
                    filename.c_str(),
                    thumbnailfilename.c_str(),
                    duration,
                    playtime),
                    &error);

}

}//namespace genivimedia
