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

#include "thumbnail_db_api.h"

static char *db_path = NULL;
static lms_db_thumbnail_t *s_ldt = NULL;
static char *lmsDB_path = NULL;

bool _make_dir(char *pathname, gint mode) {
  if (!g_file_test(pathname, G_FILE_TEST_EXISTS)) {
      char *dname = g_path_get_dirname(pathname);
      if (!g_file_test(dname, (GFileTest)(G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))) {
          if (g_mkdir_with_parents(dname, mode) != 0) {
              g_error("Couldn't create directory %s", dname);
              g_free(dname);
              return false;
          }
      }
      g_free(dname);
  }

  return true;
}

bool thumbnail_db_init(void) {
 // struct lms_thumbnail_info info;
  char *thumbnail_path=NULL;

  g_debug("init_db");
  if (!db_path)
      db_path = g_strdup_printf("%s/lightmediascannerd/thumbnail_db.sqlite3",
                                g_get_user_config_dir());
  if(!_make_dir(db_path, 0755))
    return false;

   lmsDB_path = g_strdup_printf("%s/lightmediascannerd/db.sqlite3",
                                g_get_user_config_dir());

  thumbnail_path = g_strdup_printf("%s/media-manager-thumbnail/",
                                g_get_user_cache_dir());
  if(!_make_dir(thumbnail_path, 0744))
    return false;

  g_free(thumbnail_path);
  
  return true;
}


bool thumbnail_db_uninit(void){
  g_free(db_path);
  g_free(s_ldt);

  return true;
}

lms_db_thumbnail_t *_db_open(char* path)
{
  lms_db_thumbnail_t* ldt;
  int ret;
  
  ldt = lms_db_thumbnail_open(path);
  ret = lms_db_thumbnail_start(ldt);
  if(ret<0)
    return NULL;
  
  return ldt;
}

int _db_close(lms_db_thumbnail_t* ldt)
{
  int ret;
  
  ret = lms_db_thumbnail_free(ldt);
  if(ret<0)
    return ret;

  ret = lms_db_thumbnail_close(ldt);

  return ret;
}

bool thumbnail_db_open(void)
{
  if(s_ldt){    // db already openned
    //g_debug("already openned");
    return true;
  }
  
  s_ldt = _db_open(db_path);
  if(!s_ldt)
    return false;
  
  return true;
}

bool thumbnail_db_close(void)
{
  int ret;
  
  if(!s_ldt)
    return true;
  
  ret = _db_close(s_ldt);
  if(ret<0)
    return false;

  s_ldt = NULL;
  return true;  
}

bool thumbnail_db_add(struct lms_thumbnail_info *info)
{
  int ret = -1;

  if(!s_ldt)
  	return false;
  
  ret = lms_db_thumbnail_add(s_ldt, info);

  if(ret<0)
    return false;

  return true; 
}

bool thumbnail_db_get(struct lms_thumbnail_info *info)
{
  int ret;

  if(!s_ldt)
  	return false;
  
  ret = lms_db_thumbnail_get(s_ldt, info);

  if(ret<0)
    return false;

  return true; 
}

bool thumbnail_db_set_playtime(char *path, unsigned int playtime)
{
  int ret;

  if(!db_path)
    return false;
  
  ret = lms_db_thumbnail_update_playtime(db_path, path, playtime);

  if(ret<0)
    return false;

  return true; 
}


bool thumbnail_db_set_playng(char *path)
{
  int ret;

  if(!db_path)
    return false;

  ret = lms_db_thumbnail_update_playng(db_path, path);

  if(ret<0)
    return false;

  return true;
}

bool thumbnail_db_delete_lru_indexes(void)
{
  if(!db_path)
    return false;

  do_delete_lru_indexes(db_path);

  return true; 
}

bool thumbnail_coverart_getPathByAlbum(const char *albumName, const char *artistName, char **filePath)
{
  int ret;

  ret = lms_db_thumbnail_getPathbyAlbum(lmsDB_path, albumName, artistName, filePath);

  if(ret<0)
    return false;

  return true;
}

bool thumbnail_coverart_db_add(struct lms_coverart_info *info)
{
  int ret;

  ret = lms_db_coverart_add(s_ldt, info);

  if(ret<0)
    return false;

  return true;
}


bool thumbnail_coverart_db_get(struct lms_coverart_info *info)
{
  int ret;

  ret = lms_db_coverart_get(s_ldt, info);

  if(ret<0)
    return false;

  return true;
}
