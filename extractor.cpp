aa
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <memory>
#include <string>
#include "player/media_player.h"

using namespace genivimedia;

Extractor::Extractor(ThumbnailHandler callback)
  : player_(),
    callback_(callback) {

#if 0  // To regist DLT_logger at Playerengine with this Information.
  boost::property_tree::ptree sub_tree;
  sub_tree.put("app_name", "THUM");
  sub_tree.put("app_desc", "thumbnail extractor service");
  sub_tree.put("context_name", "THUM-CTX");
  sub_tree.put("context_desc", "thumbnail extractor service context");

  boost::property_tree::ptree tree;
  tree.add_child("LogInfo", sub_tree);
  std::stringstream stream;
  write_json(stream, tree, false);
  std::string json = stream.str();
#endif

  player_ = new MediaPlayer(false);
  player_->RegisterCallback(callback_);
}

Extractor::~Extractor() {
  if (player_)
    delete player_;
}

void Extractor::Start(const std::string &uri, const std::string& option) {
  player_->SetURI(uri, option);
}

void Extractor::Stop() {
  player_->Stop();
}

