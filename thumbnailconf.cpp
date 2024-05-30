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

#include "thumbnail_conf.h"

#include <map>

#include "player_logger.h"


namespace genivimedia {


static const std::string kGroupConfiguration("configuration");

GKeyFile* Thumbnail_Conf::keyfile_ = nullptr;
unsigned int Thumbnail_Conf::coverart_width_ = 160;
unsigned int Thumbnail_Conf::coverart_height_ = 160;
std::string Thumbnail_Conf::key_string_ = {};

bool Thumbnail_Conf::ParseFile(const std::string& path) {
  GError* error = nullptr;
  keyfile_ = g_key_file_new();
  if (!g_key_file_load_from_file(keyfile_, path.c_str(), G_KEY_FILE_NONE, &error)) {
    LOG_ERROR("%s", error->message);
    return false;
  }

  LoadConfiguration();

  return true;
}

unsigned int Thumbnail_Conf::GetCoverWidth() {
  return coverart_width_;
}

unsigned int Thumbnail_Conf::GetCoverHeight() {
  return coverart_height_;
}


void Thumbnail_Conf::LoadConfiguration() {
  unsigned int width;
  unsigned int height;
  gboolean ret;

  ret = GetGroupKeyInt(kGroupConfiguration.c_str(), "coverart_width", (gint *)&width);
  if (ret) {
    LOG_INFO("coverart_width_: %d", width);
    coverart_width_= width;
  }else{
    coverart_width_= 160;
  }

  ret = GetGroupKeyInt(kGroupConfiguration.c_str(), "coverart_height", (gint *)&height);
  if (ret) {
    LOG_INFO("coverart_height_: %d", height);
    coverart_height_ = height;
  }else{
    coverart_height_= 160;
  }
}

gboolean Thumbnail_Conf::GetGroupKeyInt(const char* group, const char* key, gint* value){
    GError *error = NULL;

    gint iValue = 0;
    iValue =   g_key_file_get_integer (keyfile_,
                                        group,
                                        key,
                                        &error);

    if (error != NULL){
        g_warning("unable to read file : %s\n", error->message);
        g_error_free(error);
        *value = 0;
        return FALSE;
    }

    *value = iValue;

    return TRUE;
}


}  // namespace genivimedia

