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

#include "extractor_api.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <mutex>
#include <string>

std::mutex extractor_mutex;
static Extractor* extractor_instance = nullptr;

bool Extractor_Init(ThumbnailHandler callback) {
  std::lock_guard < std::mutex > lock (extractor_mutex);
  if (!extractor_instance) {
    extractor_instance = new Extractor(callback);
    return true;
  } else {
    return false;
  }
}

bool Extractor_Start(std::string uri, const std::string& filename) {
  std::lock_guard < std::mutex > lock (extractor_mutex);
  if (extractor_instance) {
    boost::property_tree::ptree sub_tree;
    sub_tree.put("filename", filename.c_str());
    sub_tree.put("mediatype", "video");
    boost::property_tree::ptree tree;
    tree.add_child("Option", sub_tree);
    std::stringstream stream;
    write_json(stream, tree, false);
    std::string json = stream.str();
    extractor_instance->Start(uri, json);
    return true;
  } else
    return false;
}

bool Extractor_Stop() {
  std::lock_guard < std::mutex > lock (extractor_mutex);
  if (extractor_instance) {
    extractor_instance->Stop();
    return true;
  } else
    return false;
}

bool Extractor_UnInit() {
 std::lock_guard < std::mutex > lock (extractor_mutex);
  if (extractor_instance) {
    delete extractor_instance;
    extractor_instance = nullptr;
    return true;
  } else {
    return false;
  }
}

