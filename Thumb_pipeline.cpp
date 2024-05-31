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

#include "player/pipeline/thumbnail_pipeline.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <ctime>

#include "logger/player_logger.h"
#include "player/pipeline/conf.h"

namespace genivimedia {

ThumbnailPipeline::ThumbnailPipeline()
  : gst_media_(new GstMedia()),
    event_(new Event(false)),
    timer_(new Timer()),
    filename_(),
    uri_(),
    video_count_(0) {
  LOG_DEBUG("");
}

ThumbnailPipeline::~ThumbnailPipeline() {
  LOG_INFO("");

  if (gst_media_)
    delete gst_media_;
  if (event_)
    delete event_;
  if (timer_)
    delete timer_;
}

bool ThumbnailPipeline::RegisterCallback(EventHandler callback) {
  return event_->RegisterCallback(callback);
}

bool ThumbnailPipeline::Load(const std::string& uri) {
  std::string raw_uri = gst_media_->GetRawURI(uri);
  if (raw_uri.empty()) {
    LOG_INFO("Fail to convert raw uri");
    return false;
  }
  char* caps = nullptr;
#if defined(PLATFORM_NVIDIA)
  caps = g_strdup_printf ("%s%s%s",
                          "video/x-raw,format=RGB,width=",
                          Conf::GetThumbnail(THUMBNAIL_WIDTH),
                          ",height=90");
#else
  caps = g_strdup_printf ("%s%s%s",
                          "video/x-raw,format=RGB,width=",
                          Conf::GetThumbnail(THUMBNAIL_WIDTH),
                          ",pixel-aspect-ratio=1/1");
#endif
  LOG_INFO("uri[%s] with caps[%s]", uri.c_str(), caps);
  uri_ = uri;

  if (!gst_media_->CreateGstUriDecodebin(raw_uri.c_str(), caps)) {
    LOG_ERROR("cannot create Uridecodebin!");
    if (caps)
      g_free(caps);
    return false;
  }

  BusCallback callback = std::bind(&ThumbnailPipeline::BusMessage, this,
                                   std::placeholders::_1,
                                   std::placeholders::_2,
                                   std::placeholders::_3);
  gst_media_->RegisterWatchBus(callback);
  AutoPlugSortCallback autoplugsort_callback = std::bind(&ThumbnailPipeline::HandleAutoplugSort, this,
                                                     std::placeholders::_1,
                                                     std::placeholders::_2,
                                                     std::placeholders::_3,
                                                     std::placeholders::_4,
                                                     std::placeholders::_5);
  gst_media_->RegisterAutoPlugSort(autoplugsort_callback,
                                   gst_bin_get_by_name(GST_BIN(gst_media_->GetPipeline()), "uridecode"));
  AutoPlugSelectCallback autoplugselect_callback = std::bind(&ThumbnailPipeline::HandleAutoplugSelect, this,
                                                     std::placeholders::_1,
                                                     std::placeholders::_2,
                                                     std::placeholders::_3,
                                                     std::placeholders::_4,
                                                     std::placeholders::_5);
  gst_media_->RegisterAutoPlugSelect(autoplugselect_callback,
                                     gst_bin_get_by_name(GST_BIN(gst_media_->GetPipeline()), "uridecode"));
  NoMorePadsCallback nomorepads_callback = std::bind(&ThumbnailPipeline::HandleNoMorePads, this,
                                                     std::placeholders::_1,
                                                     std::placeholders::_2);
  gst_media_->RegisterNoMorePads(nomorepads_callback,
                                 gst_bin_get_by_name(GST_BIN(gst_media_->GetPipeline()), "uridecode"));
  TimerCallback timer_callback = std::bind(&ThumbnailPipeline::CheckConnectVideoPad, this);
  timer_->AddCallback(timer_callback, 10000);

  if (!gst_media_->ChangeStateToPause()) {
    LOG_ERROR("cannot change status into pause.");
    if (caps)
      g_free(caps);
    return false;
  }

  g_free(caps);
  return true;
}

bool ThumbnailPipeline::Load(const std::string& uri, const std::string& option, char slot) {
  using boost::property_tree::ptree;

  ptree tree;
  std::stringstream stream(option);
  try {
    boost::property_tree::read_json(stream, tree);
  } catch (const boost::property_tree::ptree_error& exception) {
    LOG_INFO("Invalid JSON Format - %s", exception.what());
    return false;
  }

  auto iter = tree.begin();
  ptree info = iter->second;
  if (info.get_optional<std::string>("filename"))
    filename_ = info.get<std::string>("filename");
  LOG_INFO("%s",filename_.c_str());
  return Load(uri);
}

bool ThumbnailPipeline::Unload(bool send_event, bool destroy_pipeline) {
  return UnloadInternal(true);
}

bool ThumbnailPipeline::UnloadInternal(bool destroy_pipeline) {
  video_count_ = 0;
  timer_->Stop();
  if ( false == gst_media_->StopGstPipeline()) {
    LOG_ERROR("UnloadInternal Error!");
    return false;
  }
  return true;
}

bool ThumbnailPipeline::Play() {
  LOG_INFO("Play");
  event_->NotifyEventError(ERROR_OPERATION_NOT_SUPPORTED, "", uri_);
  return false;
}

bool ThumbnailPipeline::Pause() {
  LOG_INFO("Pause");
  event_->NotifyEventError(ERROR_OPERATION_NOT_SUPPORTED, "", uri_);
  return false;
}

bool ThumbnailPipeline::FastForward(gdouble rate) {
  LOG_INFO("FastForward");
  event_->NotifyEventError(ERROR_OPERATION_NOT_SUPPORTED, "", uri_);
  return false;
}

bool ThumbnailPipeline::Rewind(gdouble rate) {
  LOG_INFO("Rewind");
  event_->NotifyEventError(ERROR_OPERATION_NOT_SUPPORTED, "", uri_);
  return false;
}

bool ThumbnailPipeline::Seek(gint64 position) {
  LOG_INFO("Seek");
  event_->NotifyEventError(ERROR_OPERATION_NOT_SUPPORTED, "", uri_);
  return false;
}

bool ThumbnailPipeline::SetSubtitleEnable(bool show) {
  LOG_INFO("SetSubtitleEnable");
  event_->NotifyEventError(ERROR_OPERATION_NOT_SUPPORTED, "", uri_);
  return false;
}

bool ThumbnailPipeline::SetSubtitleLanguage(const std::string& language) {
  LOG_INFO("SetSubtitleLanguage");
  event_->NotifyEventError(ERROR_OPERATION_NOT_SUPPORTED, "", uri_);
  return false;
}

bool ThumbnailPipeline::SetSubtitleLanguageIndex(int language) {
  LOG_INFO("SetSubtitleLanguageIndex");
  event_->NotifyEventError(ERROR_OPERATION_NOT_SUPPORTED, "", uri_);
  return false;
}

bool ThumbnailPipeline::SetAudioLanguage(int index) {
  LOG_INFO("SetAudioLanguage");
  event_->NotifyEventError(ERROR_OPERATION_NOT_SUPPORTED, "", uri_);
  return false;
}

bool ThumbnailPipeline::SetPlaybackSpeed(double rate) {
  LOG_INFO("SetPlaybackSpeed");
  event_->NotifyEventError(ERROR_OPERATION_NOT_SUPPORTED, "", uri_);
  return false;
}

bool ThumbnailPipeline::SetAudioMute(bool mute) {
  LOG_INFO("SetAudioMute");
  event_->NotifyEventError(ERROR_OPERATION_NOT_SUPPORTED, "", uri_);
  return false;
}

bool ThumbnailPipeline::SetAudioVolume(double volume) {
  LOG_INFO("SetAudioVolume");
  event_->NotifyEventError(ERROR_OPERATION_NOT_SUPPORTED, "", uri_);
  return false;
}

bool ThumbnailPipeline::Extract() {
  gint64 duration = 0;
  gint64 position = 0;
  gchar* dest = nullptr;
  gchar* format = Conf::GetThumbnail(THUMBNAIL_FORMAT);
  bool ret = true;

  LOG_INFO("Extract");
  duration = gst_media_->GetDuration();
  if (duration != -1)
    position = duration * 5 / 100;
  else
    position = 1 * GST_SECOND;

  dest = g_strdup_printf ("%s%s.%s",
                          Conf::GetThumbnail(THUMBNAIL_PATH),
                          filename_.c_str(),
                          format);
  if (gst_media_->IsSeekable() && !gst_media_->SeekSimple(position)){
    LOG_ERROR("SeekSimple Error!");
    ret = false;
    goto EXIT;
  }
  if (!gst_media_->ExtractThumbnail(const_cast<char*>(dest), const_cast<char*>(format))){
    LOG_ERROR("ExtrctThumbnail Error!");
    ret = false;
    goto EXIT;
  }

  event_->NotifyEventThumbnailDone(uri_.c_str(), dest, duration);

EXIT:
  if (dest)
    g_free(dest);
  if (!ret)
    event_->NotifyEventError(ERROR_THUMBNAIL_EXTRACT, "", uri_);
  return ret;
}

gboolean ThumbnailPipeline::CheckConnectVideoPad() {
  if (video_count_ < 2) {
    LOG_INFO ("Unable to connect decode pad");
    event_->NotifyEventError(ERROR_THUMBNAIL_EXTRACT, "", uri_);
  }
  return false;
}

gboolean ThumbnailPipeline::BusMessage(GstBus* bus, GstMessage* message, gpointer data) {
  gboolean ret = true;
  GstElement* pipeline = gst_media_->GetPipeline();

  if (!pipeline) {
    ret = false;
    goto EXIT;
  }

  if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_APPLICATION)
    HandleBusApplication(message);

  if (GST_MESSAGE_SRC(message) == GST_OBJECT_CAST(pipeline))
    ret = HandleBusPipelineMessage(message);
  else
    ret = HandleBusElementMessage(message);

EXIT:
  return ret;
}

void ThumbnailPipeline::HandleBusChangeState(GstMessage* message) {
  GstState old_state = GST_STATE_NULL;
  GstState new_state = GST_STATE_NULL;
  gst_message_parse_state_changed(message, &old_state, &new_state, nullptr);
  LOG_INFO("[BUS] GST_MESSAGE_STATE_CHANGED [%s -> %s]", gst_element_state_get_name(old_state),
                                                         gst_element_state_get_name(new_state));
  if (old_state == new_state){
    return;
  }

  switch (new_state) {
    case GST_STATE_VOID_PENDING:
      break;
    case GST_STATE_READY:
      break;
    case GST_STATE_NULL:
      break;
    case GST_STATE_PLAYING:
      break;
    case GST_STATE_PAUSED:
      Extract();
      break;
    default:
      break;
  }
}

gboolean ThumbnailPipeline::HandleBusApplication(GstMessage* message) {
  LOG_INFO("[HandleBusApplication] src(%s)", GST_MESSAGE_SRC_NAME(message));
  return true;
}

gboolean ThumbnailPipeline::HandleBusPipelineMessage(GstMessage* message) {
  GError* err = nullptr;
  GError* warn = nullptr;
  gchar* debug = nullptr;

  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_STATE_CHANGED:
      HandleBusChangeState(message);
      break;

    case GST_MESSAGE_ERROR:
      gst_message_parse_error(message, &err, &debug);
      event_->NotifyEventError(ERROR_GST_INTERNAL_ERROR, "", uri_);
      LOG_INFO("[BUS] GST_MESSAGE_ERROR : %s", err->message);
      break;

    case GST_MESSAGE_WARNING:
      gst_message_parse_warning(message, &warn, &debug);
      LOG_INFO("[BUS] GST_MESSAGE_WARNING : %s", warn->message);
      break;

    case GST_MESSAGE_ASYNC_DONE:
      LOG_INFO("[BUS] GST_MESSAGE_ASYNC_DONE");
      break;

    default:
      break;
  }

  if (err)
    g_error_free(err);
  if (warn)
    g_error_free(warn);
  if (debug)
    g_free(debug);

  return true;
}

gboolean ThumbnailPipeline::HandleBusElementMessage(GstMessage* message) {
  GError* err = nullptr;
  GError* warn = nullptr;
  gchar* debug = nullptr;

  if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_WARNING) {
    gst_message_parse_warning(message, &warn, &debug);
    if (warn->code == (int)GST_STREAM_ERROR_CODEC_NOT_FOUND) {
      if (strstr(warn->message, "video")) {
        LOG_ERROR("[HandleBusElementMessage] not supported video");
        event_->NotifyEventError(ERROR_VIDEO_CODEC_NOT_SUPPORTED, "", uri_);
      }
    }
    goto EXIT;
  }
  if (GST_MESSAGE_TYPE(message) != GST_MESSAGE_ERROR)
    return true;

  gst_message_parse_error(message, &err, &debug);
  LOG_ERROR("[HandleBusElementMessage] GST_MESSAGE_ERROR : %s - %d(from %s), %s, %s",
            (err->domain == GST_STREAM_ERROR ? "GST_STREAM_ERROR" :
            (err->domain == GST_CORE_ERROR ? "GST_CORE_ERROR" :
            (err->domain == GST_RESOURCE_ERROR ? "GST_RESOURCE_ERROR" :
            (err->domain == GST_LIBRARY_ERROR ? "GST_LIBRARY_ERROR" : "UNKNOWN")))),
            err->code, (GST_OBJECT_NAME(GST_MESSAGE_SRC(message))), err->message, debug);

  event_->NotifyEventError(ERROR_GST_INTERNAL_ERROR, "", uri_);

EXIT:
  if (warn)
    g_error_free(warn);
  if (err)
    g_error_free(err);
  if (debug)
    g_free(debug);
  return true;
}

GValueArray* ThumbnailPipeline::HandleAutoplugSort(GstElement *bin,GstPad *pad,GstCaps *caps,
                                                   GValueArray *factories, gpointer user_data) {

  GstStructure* caps_str = nullptr;
  char* structure_str = nullptr;
  GstCaps* capbility = nullptr;
  capbility = gst_pad_query_caps(pad, nullptr);
  if (capbility) {
    caps_str = gst_caps_get_structure(capbility, 0);
    structure_str = gst_structure_to_string(caps_str);
    LOG_INFO("capability - %s\n", structure_str);
    if (g_strrstr(structure_str, "video") ||
        g_strrstr(structure_str, "x-3gp") ||
        g_strrstr(structure_str, "vnd.rn-realmedia")) {
      LOG_INFO("count - %d \n", video_count_);
      if (video_count_ == 0) {
        timer_->Start();
        LOG_INFO("thumbnail timer start");
      }
      video_count_++;
      if (video_count_ > 1)
        timer_->Stop();
    }
  }

  if (structure_str)
    g_free(structure_str);
  if (capbility)
    gst_caps_unref(capbility);
  return nullptr;
}

int ThumbnailPipeline::HandleAutoplugSelect(GstElement *bin,GstPad *pad, GstCaps *caps,
                                        GstElementFactory *factory, gpointer data) {
  int select_result = GST_AUTOPLUG_SELECT_TRY;

  return select_result;
}

void ThumbnailPipeline::HandleNoMorePads(GstElement *element, gpointer data) {
  if (video_count_ < 2) {
    LOG_ERROR("No Video");
    event_->NotifyEventError(ERROR_THUMBNAIL_EXTRACT, "", uri_);
  }
}

} // namespace genivimedia

