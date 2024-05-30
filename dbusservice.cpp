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

#include <cstring>
#include <glib-unix.h>

#include "dbusservice.h"
#include "player_logger.h"
#include "common.h"
#include "process.h"

const char introspection_xml[] =
    "<node>"
    "  <interface name=\"com.lge.thumbnailextractor.service\">"
    "    <method name=\"Start\">"
    "      <arg direction=\"in\" type=\"as\" name=\"PathList\" />"
    "    </method>"
    "    <method name=\"Stop\" />"
    "    <method name=\"SetPlaytime\">"
    "      <arg direction=\"in\" type=\"s\" name=\"Path\" />"
    "      <arg direction=\"in\" type=\"i\" name=\"Playtime\" />"
    "    </method>"
    "    <method name=\"SetPlayNG\">"
    "      <arg direction=\"in\" type=\"s\" name=\"Path\" />"
    "    </method>"
    "    <method name=\"StartCoverart\">"
    "      <arg direction=\"in\" type=\"a{ss}\" name=\"AlbumAritstList\" />"
    "    </method>"
    "    <method name=\"StartCoverartEx\">"
    "      <arg direction=\"in\" type=\"as\" name=\"PathList\" />"
    "    </method>"
    "    <method name=\"StopCoverart\" />"
    "    <signal name=\"ExtractorEvent\">"
    "      <arg type=\"s\" name=\"Status\" />"
    "      <arg type=\"s\" name=\"Path\" />"
    "      <arg type=\"s\" name=\"Thumbnail\" />"
    "      <arg type=\"i\" name=\"Duration\" />"
    "      <arg type=\"i\" name=\"Playtime\" />"
    "    </signal>"
    "    <method name=\"GetSingleAudioInfo\">"
    "      <arg direction=\"in\" type=\"s\" name=\"InputPath\" />"
    "    </method>"
    "    <signal name=\"CoverartEvent\">"
    "      <arg type=\"s\" name=\"Status\" />"
    "      <arg type=\"s\" name=\"Path\" />"
    "      <arg type=\"s\" name=\"Album\" />"
    "      <arg type=\"s\" name=\"Artist\" />"
    "      <arg type=\"s\" name=\"Coverart\" />"
    "    </signal>"
    "    <signal name=\"SingleAudioInfoEvent\">"
    "      <arg type=\"s\" name=\"Status\" />"
    "      <arg type=\"s\" name=\"InputPath\" />"
    "      <arg type=\"s\" name=\"Coverart\" />"
    "      <arg type=\"s\" name=\"Title\" />"
    "      <arg type=\"s\" name=\"Album\" />"
    "      <arg type=\"s\" name=\"Artist\" />"
    "      <arg type=\"s\" name=\"Genre\" />"
    "      <arg type=\"i\" name=\"Duration\" />"
    "    </signal>"
    "  </interface>"
    "</node>";

void methodCall(GDBusConnection *conn, const char *sender, const char *opath, const char *iface, const char *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data)
{
  genivimedia::DbusService *dbusObj = (genivimedia::DbusService *)data;

  LOG_DEBUG("methodcall: %s", method);

  if (strcmp(method, "Start") == 0)
    dbusObj->startExtractThumb(inv, data, params);
  else if (strcmp(method, "SetPlaytime") == 0)
    dbusObj->setPlaytime(inv, data, params);
  else if (strcmp(method, "SetPlayNG") == 0)
    dbusObj->setPlayng(inv, data, params);
  else if (strcmp(method, "StartCoverart")==0)
    dbusObj->startExtractCoverart(inv, data, params);
  else if (strcmp(method, "StopCoverart")==0)
    dbusObj->stopExtractCoverart(inv, data, params);
  else if (strcmp(method, "Stop")==0)
    dbusObj->stopExtractThumb(inv, data, params);
  else if (strcmp(method, "GetSingleAudioInfo")==0)
    dbusObj->getAudioInfoFile(inv, data, params);
  else if (strcmp(method, "StartCoverartEx")==0)
    dbusObj->startExtractCoverartEx(inv, data, params);
  else
    LOG_ERROR("No method");
}

static const GDBusInterfaceVTable extractor_vtable = {
        methodCall,
        NULL,
        NULL
};

void OnDBusNameAcquired(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
  GDBusInterfaceInfo *iface;
  unsigned id;

  genivimedia::DbusService *dbusObj = (genivimedia::DbusService *)user_data;

  dbusObj->mConnection = connection;
  iface = g_dbus_node_info_lookup_interface(dbusObj->mIntrospectionData, BUS_IFACE);

  id = g_dbus_connection_register_object(connection,
                                       BUS_PATH,
                                       iface,
                                       &extractor_vtable,
                                       dbusObj, //pData
                                       NULL, //destroy
                                       NULL);

  genivimedia::Process *procObj = new genivimedia::Process(dbusObj->mLoop, dbusObj);

  dbusObj->mThread = procObj->mThread;
  if(id <=0)
    LOG_ERROR("DBUS register failed!!!!");
}

namespace genivimedia{

DbusService::DbusService(GMainLoop *loop)
  : mConnection(NULL),
    mIntrospectionData(NULL),
    mThread(NULL) {
  mLoop = loop;
  mQueue = g_async_queue_new();
}

gboolean OnSigTerm(gpointer data)
{
    GMainLoop *loop = (GMainLoop *)data;

    LOG_DEBUG("got SIGTERM, exit.");
    g_main_loop_quit(loop);
    return FALSE;
}

gboolean OnSigInt(gpointer data)
{
    GMainLoop *loop = (GMainLoop *)data;

    LOG_DEBUG("got SIGINT, exit.");
    g_main_loop_quit(loop);
    return FALSE;
}

void DbusService::registerDbus()
{
  mIntrospectionData = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
 // g_assert(introspection_xml != NULL);
  if(mIntrospectionData == NULL){
    LOG_ERROR("mIntrospectionData is NULL !!!");
    return;
  }

  g_bus_own_name(G_BUS_TYPE_SESSION,
                 "com.lge.thumbnailextractor",
                 G_BUS_NAME_OWNER_FLAGS_NONE,
                 NULL,
                 OnDBusNameAcquired,
                 NULL,
                 this,
                 NULL);

  g_unix_signal_add(SIGTERM, OnSigInt, mLoop);
  g_unix_signal_add(SIGINT, OnSigTerm, mLoop);

  g_main_loop_run(this->mLoop);

  g_thread_join(this->mThread);
}

void DbusService::startExtractThumb(GDBusMethodInvocation *inv, void *data, GVariant *params)
{
  GList *paths = NULL;
  struct Command  *command = new Command();
 
  getParamList(&paths, params);
  if (paths==NULL){
    LOG_ERROR("paths list is NULL");
    goto exit;
  }
 
  //push the pathList
  command->type = Command::START;
  command->paths = paths;
 
  g_async_queue_push(mQueue, (gpointer)command);

exit:
  g_dbus_method_invocation_return_value(inv, NULL);
}


void DbusService::setPlaytime(GDBusMethodInvocation *inv, void *data, GVariant *params)
{
  char *path = NULL;
  unsigned int playtime;
  struct Command  *command = new Command();

  getPlaytime(&path, &playtime, params);

  if (path==NULL){
    LOG_ERROR("path is NULL");
    goto exit;
  }

  //push the playtime
  command->type = Command::SETPLAYTIME;
  command->path = path;
  command->playtime = playtime;
  g_async_queue_push(mQueue, (gpointer)command);

exit:
  g_dbus_method_invocation_return_value(inv, NULL);
}

void DbusService::setPlayng(GDBusMethodInvocation *inv, void *data, GVariant *params)
{
  char *path = NULL;
  struct Command  *command = new Command();

  getPath(&path, params);

  if (path==NULL){
    LOG_ERROR("path is NULL");
    goto exit;
  }


  command->type = Command::SETPLAYNG;
  command->path = path;
  g_async_queue_push(mQueue, (gpointer)command);

exit:
  g_dbus_method_invocation_return_value(inv, NULL);
}

void DbusService::startExtractCoverart(GDBusMethodInvocation *inv, void *data, GVariant *params)
{
  GList *albums = NULL;
  struct Command  *command = new Command();

  getParamArrayList(&albums, params);
  if (albums==NULL){
    LOG_ERROR("paths list is NULL");
    goto exit;
  }

  //push the pathList
  command->type = Command::START_COVERART;
  command->albums = albums;

  g_async_queue_push(mQueue, (gpointer)command);

  exit:
  g_dbus_method_invocation_return_value(inv, NULL);
}

void DbusService::startExtractCoverartEx(GDBusMethodInvocation *inv, void *data, GVariant *params)
{
  GList *paths = NULL;
  struct Command  *command = new Command();

  getParamList(&paths, params);
  if (paths == NULL){
    LOG_ERROR("paths list is NULL");
    goto exit;
  }

  //push the pathList
  command->type = Command::START_COVERART_EX;
  command->paths = paths;

  g_async_queue_push(mQueue, (gpointer)command);

  exit:
  g_dbus_method_invocation_return_value(inv, NULL);
}

void DbusService::getParamList(GList **pathList, GVariant *params)
{
  GVariantIter *itr;
  char *tempPath;
  char *path;

  g_variant_get(params, "(as)", &itr);
  while (g_variant_iter_loop(itr, "s", &tempPath)) {
    path = g_strndup(tempPath, strlen(tempPath));
    *pathList=g_list_append(*pathList, path);
    LOG_DEBUG("path : %s", path);
  }
  g_variant_iter_free(itr);
}

void DbusService::getParamArrayList(GList **arrayList, GVariant *params)
{
  GVariantIter *itr;
  gchar *album;
  gchar *artist;
  gchar *album_name;
  gchar *artist_name;
  GArray *elemArray;

  g_variant_get(params, "(a{ss})", &itr);
  while (g_variant_iter_loop(itr, "{ss}", &album, &artist)) {

    album_name = g_strdup(album);
    artist_name = g_strdup(artist);

    elemArray = g_array_new(TRUE, TRUE, sizeof(char *));
    g_array_append_val(elemArray, album_name);
    g_array_append_val(elemArray, artist_name);

    *arrayList=g_list_append(*arrayList, elemArray);
    LOG_DEBUG("album : [%s], artist: [%s]", album_name, artist_name);
  }
  g_variant_iter_free(itr);
}

void DbusService::getPlaytime(char **path, unsigned int *playtime, GVariant *params)
{
  char *tempPath;

  if(params ==NULL){
    *path=NULL;
    return;
  }

  g_variant_get(params, "(si)", &tempPath, playtime);
  *path = g_strndup(tempPath, strlen(tempPath));
  LOG_DEBUG("path : %s, playtime : %d", tempPath, *playtime);
}

void DbusService::getPath(char **path, GVariant *params)
{
  char *tempPath;

  if(params ==NULL){
    *path=NULL;
    return;
  }

  g_variant_get(params, "(s)", &tempPath);
  *path = g_strndup(tempPath, strlen(tempPath));
  LOG_DEBUG("path : %s", tempPath);
}

void DbusService::stopExtractCoverart(GDBusMethodInvocation *inv, void *data, GVariant *params)
{
  struct Command  *command = new Command();

  command->type = Command::STOP_COVERART;
  g_async_queue_push(mQueue, (gpointer)command);

  g_dbus_method_invocation_return_value(inv, NULL);
}

void DbusService::stopExtractThumb(GDBusMethodInvocation *inv, void *data, GVariant *params)
{
  struct Command  *command = new Command();

  command->type = Command::STOP;
  g_async_queue_push(mQueue, (gpointer)command);

  g_dbus_method_invocation_return_value(inv, NULL);
}

void DbusService::getAudioInfoFile(GDBusMethodInvocation *inv, void *data, GVariant *params)
{
  char *path = NULL;
  struct Command  *command = new Command();

  getPath(&path, params);

  if (path==NULL){
    LOG_ERROR("path is NULL");
    goto exit;
  }

  command->type = Command::GET_AUDIOINFO;
  command->path = path;
  g_async_queue_push(mQueue, (gpointer)command);

  exit:
  g_dbus_method_invocation_return_value(inv, NULL);

}

}//genivimedia
