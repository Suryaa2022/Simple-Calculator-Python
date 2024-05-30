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

#include <string>
#include <string.h>
#include <glib.h>

#include "player_logger.h"
#include "dbusservice.h"
#include "process.h"
#include "thumbnail_conf.h"

#ifndef CONF_DIR
#define CONF_DIR "/app/bin"
#endif
static void custom_dlog_handler(const gchar *log_domain, GLogLevelFlags log_level,
	const gchar *message, gpointer user_data)
{
    switch(log_level){
      case G_LOG_LEVEL_ERROR:
        LOG_ERROR("%s", message);
        break;
      case G_LOG_LEVEL_CRITICAL:
        LOG_ERROR("%s", message);
        break;
      case G_LOG_LEVEL_WARNING:
        LOG_WARN("%s", message);
        break;
      case G_LOG_LEVEL_MESSAGE:
        LOG_INFO("%s", message);
        break;
      case G_LOG_LEVEL_INFO:
        LOG_INFO("%s", message);
        break;
      case G_LOG_LEVEL_DEBUG:
        LOG_DEBUG("%s", message);
        break;
      default:
        LOG_DEBUG("%s", message);
        break;
    }

    return;
}

int main(int argc, char **argv) {
  using namespace genivimedia;

//  GSource *source;
//  GMainContext *context;
  MMLog::RegisterLogger(new DltLogger("THUM", "thumbnail extractor service", "THUM-CTX", "thumbnail extractor service context"));
  g_log_set_handler(G_LOG_DOMAIN, G_LOG_LEVEL_MASK, custom_dlog_handler, NULL);

  std::setlocale(LC_ALL, "");

  // temporarily added
  std::string str;
  std::ifstream is("/tmp/session_appmgr");
  std::getline(is, str);
  setenv("DBUS_SESSION_BUS_ADDRESS", str.c_str(), 1);

  // parse conf
  std::string conf_file;
  conf_file.append(CONF_DIR);
  conf_file.append("/thumbnail-extractor.conf");
  Thumbnail_Conf::ParseFile(conf_file);
  
  GMainLoop* loop = g_main_loop_new(NULL, false);//context

  DbusService *dbussvc = new DbusService(loop);
  dbussvc->registerDbus();
 
  g_main_loop_unref(loop);
  delete dbussvc;
 
  return 0;
}
