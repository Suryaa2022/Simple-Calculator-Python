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

#include "dbmanager.h"
#include "dbusservice.h"
#include "lightmediascanner_db.h"

DBManager::DBManager()
{
  thumbnail_db_init();
}

DBManager::~DBManager()
{
  close();
}

bool DBManager::open()
{
  bool ret;
  ret = thumbnail_db_open();
  if(ret)
    thumbnail_db_delete_lru_indexes();
  return ret;
}

bool DBManager::close()
{
  return thumbnail_db_close();
}

bool DBManager::getThumbnailInfo(const char *path, struct lms_thumbnail_info *info)
{
  lms_string_size_strndup(&info->path, path, -1);
  return thumbnail_db_get(info);
}

bool DBManager::addThumbnailInfo(const char *iPath, const char *oPath, unsigned int duration)
{
  struct lms_thumbnail_info info = {-1,};

  lms_string_size_strndup(&info.path, iPath, -1);
  lms_string_size_strndup(&info.thumbnail_url, oPath, -1);
  info.duration = duration;
  info.playtime = 0;

  return thumbnail_db_add(&info);
}

bool DBManager::setPlayTime(char *path, unsigned int playtime)
{
  bool ret =false;

  open();
  ret = thumbnail_db_set_playtime(path, playtime);
  close();

  return ret;
}

bool DBManager::setPlayNg(char *path)
{
  bool ret =false;

  open();
  ret = thumbnail_db_set_playng(path);
  close();

  return ret;
}

bool DBManager::getCovartartInfo(const char *album, const char *artist, struct lms_coverart_info *info)
{
  lms_string_size_strndup(&info->album_name, (char *)album, -1);
  lms_string_size_strndup(&info->artist_name, (char *)artist, -1);
  return thumbnail_coverart_db_get(info);
}

bool DBManager::getCovartartInfo(const char *filepath, struct lms_coverart_info *info)
{
  lms_string_size_strndup(&info->path, (char *)filepath, -1);
  return thumbnail_coverart_db_get(info);
}


bool DBManager::addCovartartInfo(struct lms_coverart_info *info)
{
  return thumbnail_coverart_db_add(info);
}

bool DBManager::findAlbumFile(const char *albumName, const char *artistName, char **filePath)
{
  return thumbnail_coverart_getPathByAlbum(albumName, artistName,  filePath);
}

void DBManager::makeCoverartInfo(const char *filePath, const char *album, const char *artist, const char *oPath,  lms_coverart_info *info)
{
  lms_string_size_strndup(&info->album_name, (char *)album, -1);
  lms_string_size_strndup(&info->artist_name, (char *)artist, -1);
  lms_string_size_strndup(&info->coverart_url, (char *)oPath, -1);
  lms_string_size_strndup(&info->path, (char *)filePath, -1);
}
