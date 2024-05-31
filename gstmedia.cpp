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

#include "player/pipeline/gst_media.h"
#include "player/pipeline/conf.h"

#include <sys/resource.h>
#include "logger/player_logger.h"
#include "player/pipeline/keep_alive.h"

namespace genivimedia {

GstMedia* GstMedia::gst_media_instance_ = nullptr;

GstMedia* GstMedia::Instance() {
  if (gst_media_instance_ == nullptr) {
    gst_media_instance_ = new GstMedia();
  }
  return gst_media_instance_;
}

void GstMedia::Destroy() {
  if (gst_media_instance_) {
    delete gst_media_instance_;
    gst_media_instance_ = nullptr;
  }
}

GstMedia::GstMedia()
  : pipeline_(nullptr),
    uridecodebin_(nullptr),
    decodebin_(nullptr),
    playsink_(nullptr),
    bus_(nullptr),
    bus_signal_id_(0),
    pipeline_element_add_signal_id_(0),
    uridecodebin_element_add_signal_id_(0),
    decodebin_element_add_signal_id_(0),
    playsink_element_add_signal_id_(0),
    uridecodebin_sort_signal_id_(0),
    uridecodebin_select_signal_id_(0),
    uridecodebin_nomorepad_signal_id_(0),
    cache_location_signal_id_(0),
    remote_address_signal_id_(0),
    notify_atmos_signal_id_(0),
    lang_code_({0,}),
    media_type_(""),
    bus_callback_(),
    pipeline_elementadd_callback_(),
    uridecodebin_elementadd_callback_(),
    decodebin_elementadd_callback_(),
    playsink_elementadd_callback_(),
    autoplugsort_callback_(),
    autoplugselect_callback_(),
    nomorepads_callback_(),
    seek_callback_(),
    seek_control_(),
    cache_location_callback_(),
    remote_address_callback_(),
    notify_atmos_callback_(),
    isSeeking_(false) {

}

GstMedia::~GstMedia() {
  LOG_INFO("");

  if (seek_control_)
    delete seek_control_;
}

bool GstMedia::AddElement(const char* factory_name, const char* name) {
  GstElement* element;

  if (!factory_name || !name) {
    return false;
  }

  if (GetElement(name)) {
    return true;
  }

  element = gst_element_factory_make(factory_name, name);
  if (!element) {
    LOG_ERROR("Failed to create factory %s", name);
    return false;
  }

  if (false == gst_bin_add(GST_BIN(pipeline_), element)) {
    LOG_ERROR("Failed to gst_bin_add");
    return false;
  }
  return true;
}

bool GstMedia::AddElement(const GstElementFactory* factory, const char* name) {
  GstElement* element;

  if (!factory || !name) {
    return false;
  }

  element = gst_element_factory_create(const_cast<GstElementFactory*>(factory), name);
  if (!element) {
    LOG_ERROR("Failed to create factory %s", name);
    return false;
  }

  if (false == gst_bin_add(GST_BIN(pipeline_), element)) {
    LOG_ERROR("Failed to gst_bin_add");
    return false;
  }
  return true;
}

bool GstMedia::RemoveElement(const char* element_name) {
  GstElement* element;
  element = GetElement(element_name);

  if (!element) {
    return false;
  }

  if (false == gst_bin_remove(GST_BIN(pipeline_), element)) {
    return false;
  }
  return true;
}

GstElement* GstMedia::CreateElement(const char* factory_name, const char* name) {
  GstElement* element = nullptr;

  if (!factory_name || !name) {
    return nullptr;
  }

  element = gst_element_factory_make(factory_name, name);
  if (!element) {
    LOG_ERROR("Failed to create factory %s", name);
    return nullptr;
  }
  return element;
}

GstElement* GstMedia::GetElement(const char* name) {
  GstElement* element;

  element = gst_bin_get_by_name(GST_BIN(pipeline_), name);
  if (!element) {
    return nullptr;
  }
  return element;
}

bool GstMedia::CreateGstPipeline(const char* pipeline_name) {
  if (pipeline_ != nullptr) {
    LOG_ERROR("Pipeline already existed");
    StopGstPipeline(true);
  }

  pipeline_ = gst_pipeline_new(pipeline_name);
  if (!pipeline_) {
    return false;
  }
  return true;
}

bool GstMedia::CreateGstPlaybin(const char* playbin_name) {
  if (!pipeline_) {
    pipeline_ = gst_element_factory_make ("playbin", playbin_name);
    if (!pipeline_) {
      return false;
    }
  }
  return true;
}

GstElement* GstMedia::CreateAudioSinkBin(const char* audio_sink, char slot, char slot_6ch,
                                         int channel, bool is_dsd, const std::string& media_type) {
    GstElement* audio_sink_bin = nullptr;
    std::string audio_entire_bin;
    std::string audio_alsa_device = " device=";

    if (!audio_sink)
        return nullptr;
    std::string alsa_name = (channel > 5) ? Conf::GetAlsaDeviceType("alsa_5_1")
                                          : Conf::GetAlsaDeviceType(media_type.c_str());
    if (alsa_name.size() == 0) {
        LOG_ERROR("Cannot acquire alsa device name[%s], use default instead", media_type.c_str());
        alsa_name = "default";
    }

    LOG_INFO("Channel=[%d] Alsa device name[%s]", channel, alsa_name.c_str());
    audio_alsa_device.append(alsa_name);

    if (slot == (char)-1) {
        LOG_ERROR("Invalid slot value, use default value 1");
        slot = '1';
    }

    if (channel > 5) {
      if (slot_6ch == '1') { // ccRC case - device name is '6channel2'
        audio_alsa_device.append("2");
      }
    } else if (alsa_name.compare("default") == 0) {
        // Do nothing
        LOG_ERROR("#### Should not be Here ####");
    } else {
        // Ignore face_detection slot
        if (media_type.compare("face_detection") != 0) {
            audio_alsa_device.append((const char*)&slot, 1);
        }
    }
#ifdef PLATFORM_TELECHIPS
    audio_entire_bin = "audioresample ! audio/x-raw, rate=48000 ! ";
#else
    if (is_dsd) {
        audio_entire_bin = "audioresample ! audio/x-raw, rate=48000 ! ";
    } else {
        audio_entire_bin.clear();
    }
#endif
    if (media_type.compare("welaaa_audio_streaming") == 0) {
        audio_entire_bin = "audioresample ! ";
    }
    /* VisualOn will send 5.1 output, but some contents has multi track(AC3 + AAC + ...) in one contents.
     If user selects AAC tracks for playback, we need audioconvert. So we should always add audioconvert in pipeline.
     But AC3/DTS's actual downmix will be done by VisualOn plugin. */
    if (channel > 5) {
        audio_entire_bin.append("audioconvert ! audio/x-raw,channels=6");
    } else {
        audio_entire_bin.append("audioconvert ! audio/x-raw,channels=2"); // for 2channel down mixing
    }

    //add the sampling rate and format for welaaa case
    if (!is_dsd && (media_type.compare("welaaa_audio_streaming") == 0)) {
        audio_entire_bin.append(",format=S16LE,rate=48000");
    }
    audio_entire_bin.append(" ! ");

    audio_entire_bin.append(std::string(audio_sink));
    audio_entire_bin.append(audio_alsa_device);
    audio_entire_bin.append(" buffer-time=80000 latency-time=10000"); // for removing dmix

    LOG_INFO("audio-sink=[%s]", audio_entire_bin.c_str());
    audio_sink_bin = gst_parse_bin_from_description(reinterpret_cast<const gchar*>(audio_entire_bin.c_str()), TRUE, NULL);

    return audio_sink_bin;
}

GstEncodingProfile* GstMedia::CreateEncoderProfile(std::string codec_type, std::string file_type) {
  GstEncodingContainerProfile* prof = nullptr;
  GstCaps* caps = nullptr;

  if (file_type == "MP4")
    caps = gst_caps_from_string("video/quicktime,variant=iso");
  else if (file_type == "OGG")
    caps = gst_caps_from_string("application/ogg");
  else if (file_type == "WAV")
    caps = gst_caps_from_string("audio/x-wav");
  else
    caps = gst_caps_from_string("audio/x-wav");

  prof = gst_encoding_container_profile_new("profile_name", "profile_desc", caps, NULL);
  gst_caps_unref (caps);

  if (codec_type == "AAC")
    caps = gst_caps_from_string("audio/mpeg,mpegversion=4");
  else if (codec_type == "OPUS")
    caps = gst_caps_from_string("audio/x-opus");
  else if (codec_type == "WAV_PCM")
    caps = gst_caps_from_string("audio/x-raw"); // ToDo: CHECK
  else
    caps = gst_caps_from_string("audio/x-wav");

  gst_encoding_container_profile_add_profile(prof, (GstEncodingProfile*)gst_encoding_audio_profile_new(caps, NULL, NULL, 0));
  gst_caps_unref (caps);

  return (GstEncodingProfile*) prof;
}

bool GstMedia::CreateGstUriDecodebin(const char* uri, const char* caps) {
  bool ret = false;
  gchar *descr = nullptr;
  GError *error = nullptr;
#if defined(PLATFORM_NVIDIA)
  descr = g_strdup_printf ("%s%s%s%s%s",
                           "uridecodebin name=uridecode uri=\"",
                           uri,
                           "\" ! nvmediasurfmixer ! videoconvert ! videoscale ! appsink name=sink caps=\"",
                           caps,
                           "\"");
#else
  descr = g_strdup_printf ("%s%s%s%s%s",
                           "uridecodebin name=uridecode uri=\"",
                           uri,
                           "\" ! videoconvert ! videoscale ! appsink name=sink caps=\"",
                           caps,
                           "\"");
#endif
  pipeline_ = gst_parse_launch (descr, &error);
  if (error) {
    LOG_ERROR ("could not construct pipeline: %s\n", error->message);
    goto EXIT;
  } else {
    ret = true;
  }

EXIT:
  if (error)
    g_error_free (error);
  if (descr)
    g_free(descr);
  return ret;
}

bool GstMedia::StopGstPipeline(bool destory_pipeline, bool use_keep_alive) {
  bool ret = false;
  if (!pipeline_) {
    LOG_ERROR("return with null pipeline");
    return ret;
  }

  if (use_keep_alive)
    KeepAlive::Instance()->Start();

  GstStateChangeReturn ret_gst = GST_STATE_CHANGE_SUCCESS;
  if (destory_pipeline) {

    gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_NULL);
    UnRegisterWatchBus();
    if (pipeline_ && GST_IS_ELEMENT(pipeline_)) {
      ret_gst = gst_element_set_state(pipeline_, GST_STATE_NULL);
      LOG_INFO("Destroy set_state : %s", gst_element_state_change_return_get_name(ret_gst));
      ret_gst = gst_element_get_state(pipeline_, nullptr, nullptr, 500 * GST_MSECOND);
      LOG_INFO("Destroy get_state : %s", gst_element_state_change_return_get_name(ret_gst));

      gst_object_unref(GST_OBJECT(pipeline_));
      pipeline_ = nullptr;
      ret = true;
    } else {
      LOG_INFO("Pipeline is already uninitialized ");
    }
  } else {
    ret_gst = gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_READY);
    LOG_INFO("set_state : %s", gst_element_state_change_return_get_name(ret_gst));
    ret_gst = gst_element_get_state(pipeline_, nullptr, nullptr, 500 * GST_MSECOND);
    LOG_INFO("get_state : %s", gst_element_state_change_return_get_name(ret_gst));

    DisconnectAllBinSignal();
    if (bus_signal_id_) {
      g_signal_handler_disconnect(bus_, bus_signal_id_);
      bus_signal_id_ = 0;
    }
    ret = true;
  }

  if (use_keep_alive)
    KeepAlive::Instance()->Stop();

  return ret;
}

bool GstMedia::DisconnectAllBinSignal() {
  if (pipeline_element_add_signal_id_ > 0 && pipeline_ != nullptr) {
    g_signal_handler_disconnect(pipeline_, pipeline_element_add_signal_id_);
    pipeline_element_add_signal_id_ = 0;
  }

  if (uridecodebin_element_add_signal_id_ > 0 && uridecodebin_ != nullptr) {
    g_signal_handler_disconnect(uridecodebin_, uridecodebin_element_add_signal_id_);
    uridecodebin_element_add_signal_id_ = 0;
  }

  if (decodebin_element_add_signal_id_ > 0 && decodebin_ != nullptr) {
    g_signal_handler_disconnect(decodebin_, decodebin_element_add_signal_id_);
    decodebin_element_add_signal_id_ = 0;
  }

  if (playsink_element_add_signal_id_ > 0 && playsink_ != nullptr) {
    g_signal_handler_disconnect(playsink_, playsink_element_add_signal_id_);
    playsink_element_add_signal_id_ = 0;
  }

  if (cache_location_signal_id_ > 0 && pipeline_ != nullptr) {
    g_signal_handler_disconnect(pipeline_, cache_location_signal_id_);
    cache_location_signal_id_=0;
  }

  if (remote_address_signal_id_ > 0 && pipeline_ != nullptr) {
    g_signal_handler_disconnect(pipeline_, remote_address_signal_id_);
    remote_address_signal_id_=0;
  }

  if (notify_atmos_signal_id_ > 0 && pipeline_ != nullptr) {
    g_signal_handler_disconnect(pipeline_, notify_atmos_signal_id_);
    notify_atmos_signal_id_=0;
  }

  if (uridecodebin_) {
    g_signal_handler_disconnect(uridecodebin_, uridecodebin_sort_signal_id_);
    g_signal_handler_disconnect(uridecodebin_, uridecodebin_select_signal_id_);
    g_signal_handler_disconnect(uridecodebin_, uridecodebin_nomorepad_signal_id_);
  }
  return true;
}

bool GstMedia::GetCurPipelineState(GstState* state) {
  GstState cur_state = GST_STATE_NULL;
  if (GST_STATE_CHANGE_FAILURE == gst_element_get_state(pipeline_, &cur_state, nullptr, 0)) {
    LOG_ERROR("Failed to get state of pipeline");
    return false;
  }
  *state = cur_state;
  return true;
}

GstElementFactory* GstMedia::FindElementFactory(GstElementFactoryListType type, GstCaps* caps) {
  GList* element_list = nullptr;
  GList* filtered = nullptr;
  GstElementFactory* factories = nullptr;
  gchar* caps_info = gst_caps_to_string(caps);
  gchar* name;

  element_list = gst_element_factory_list_get_elements(type, GST_RANK_MARGINAL);
  if (!element_list) {
    LOG_ERROR("Failed to find element list");
    goto FAIL;
  }

  element_list = g_list_sort(element_list, (GCompareFunc)gst_plugin_feature_rank_compare_func);
  filtered = gst_element_factory_list_filter(element_list, caps, GST_PAD_SINK, false);
  if (!filtered) {
    LOG_ERROR("Failed to find any element for caps %s", caps_info);
    goto FAIL;
  }

  name = gst_plugin_feature_get_name(reinterpret_cast<GstPluginFeature*>(filtered->data));
  LOG_INFO("Found element %s for caps %s", name, caps_info);
  factories = GST_ELEMENT_FACTORY_CAST(g_list_nth_data(filtered, 0));

FAIL:
  if (element_list)
    gst_plugin_feature_list_free(element_list);
  if (filtered)
    gst_plugin_feature_list_free(filtered);
  g_free(caps_info);
  return factories;
}

bool GstMedia::RegisterWatchBus(BusCallback callback) {
  bus_callback_ = callback;

  if (bus_) {
    bus_signal_id_ = g_signal_connect(bus_,
                                      "message",
                                      G_CALLBACK(BusCallbackFunc),
                                      static_cast<void*>(this));
    return true;
  }

  bus_ = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
  if (!bus_) {
    return false;
  }
  gst_bus_add_signal_watch(bus_);
  bus_signal_id_ = g_signal_connect(bus_,
                                    "message",
                                    G_CALLBACK(BusCallbackFunc),
                                    static_cast<void*>(this));
  if (bus_signal_id_ > 0) {
    return true;
  } else {
    return false;
  }
}

bool GstMedia::UnRegisterWatchBus() {
  if (bus_) {
    gst_bus_set_flushing(bus_, true);
    if (bus_signal_id_) {
      g_signal_handler_disconnect(bus_, bus_signal_id_);
    }
    gst_bus_remove_signal_watch(bus_);
    gst_object_unref(bus_);
    bus_ = nullptr;
    return true;
  } else {
    return false;
  }
}

bool GstMedia::RegisterElementAddBin(ElementAddCallback callback) {
  if (pipeline_element_add_signal_id_ > 0) {
    return true;
  }
  pipeline_elementadd_callback_ = callback;

  pipeline_element_add_signal_id_ = g_signal_connect(pipeline_,
                       "element-added",
                       G_CALLBACK(ElementAddCallbackFunc),
                       static_cast<void*>(this));
  if (pipeline_element_add_signal_id_ > 0) {
    return true;
  } else {
    return false;
  }
}

bool GstMedia::RegisterUriDecodeBinElementAddBin(ElementAddCallback callback, GstElement* element) {
  if (uridecodebin_element_add_signal_id_ > 0) {
    return true;
  }
  uridecodebin_elementadd_callback_ = callback;
  uridecodebin_ = element;

  uridecodebin_element_add_signal_id_ = g_signal_connect(uridecodebin_,
                       "element-added",
                       G_CALLBACK(UriDecodeBinElementAddCallbackFunc),
                       static_cast<void*>(this));
  if (uridecodebin_element_add_signal_id_ > 0) {
    return true;
  } else {
    return false;
  }
}

bool GstMedia::RegisterPlaySinkElementAddBin(ElementAddCallback callback, GstElement* element) {
  if (playsink_element_add_signal_id_ > 0) {
    return true;
  }
  playsink_elementadd_callback_ = callback;
  playsink_ = element;

  playsink_element_add_signal_id_ = g_signal_connect(playsink_,
                       "element-added",
                       G_CALLBACK(PlaySinkElementAddCallbackFunc),
                       static_cast<void*>(this));
  if (playsink_element_add_signal_id_ > 0) {
    return true;
  } else {
    return false;
  }
}

void GstMedia::CacheLocationCallbackFunc(GstObject *gstobject, GstObject *prop_object,
                                         GParamSpec *prop, gpointer data) {
  GstMedia* handler = reinterpret_cast<GstMedia*> (data);
  handler->cache_location_callback_(gstobject, prop_object, prop, data);
}

bool GstMedia::RegisterHandleCacheLocation(CacheLocationCallback callback) {
  if (cache_location_signal_id_ > 0) {
    return true;
  }
  cache_location_callback_ = callback;
  cache_location_signal_id_ = g_signal_connect (pipeline_, "deep-notify::temp-location",
                                                G_CALLBACK (CacheLocationCallbackFunc),
                                                static_cast<void*>(this));
  if (cache_location_signal_id_ > 0) {
    return true;
  } else {
    return false;
  }
}

void GstMedia::RemoteAddressCallbackFunc(GstObject *gstobject, GstObject *prop_object,
                                         GParamSpec *prop, gpointer data) {
  GstMedia* handler = reinterpret_cast<GstMedia*> (data);
  handler->remote_address_callback_(gstobject, prop_object, prop, data);
}

bool GstMedia::RegisterHandleRemoteAddress(RemoteAddressCallback callback) {
  if (remote_address_signal_id_ > 0) {
    return true;
  }
  remote_address_callback_ = callback;
  remote_address_signal_id_ = g_signal_connect (pipeline_, "deep-notify::redirect-uri",
                                                G_CALLBACK (RemoteAddressCallbackFunc),
                                                static_cast<void*>(this));
  if (remote_address_signal_id_ > 0) {
    return true;
  } else {
    return false;
  }
}

void GstMedia::NotifyAtmosCallbackFunc(GstObject *gstobject, GstObject *prop_object,
                                         GParamSpec *prop, gpointer data) {
  GstMedia* handler = reinterpret_cast<GstMedia*> (data);
  handler->notify_atmos_callback_(gstobject, prop_object, prop, data);
}

bool GstMedia::RegisterHandleNotifyAtmos(NotifyAtmosCallback callback) {
  if (notify_atmos_signal_id_ > 0) {
    return true;
  }
  notify_atmos_callback_ = callback;
  notify_atmos_signal_id_ = g_signal_connect (pipeline_, "deep-notify::notify-atmos",
                                                G_CALLBACK (NotifyAtmosCallbackFunc),
                                                static_cast<void*>(this));
  if (notify_atmos_signal_id_ > 0) {
    return true;
  } else {
    return false;
  }
}

bool GstMedia::RegisterDecodeBinElementAddBin(ElementAddCallback callback, GstElement* element) {
  if (decodebin_element_add_signal_id_ > 0) {
    return true;
  }
  decodebin_elementadd_callback_ = callback;
  decodebin_ = element;

  decodebin_element_add_signal_id_ = g_signal_connect(decodebin_,
                       "element-added",
                       G_CALLBACK(DecodeBinElementAddCallbackFunc),
                       static_cast<void*>(this));
  if (decodebin_element_add_signal_id_ > 0) {
    return true;
  } else {
    return false;
  }
}

bool GstMedia::RegisterAutoPlugSort(AutoPlugSortCallback callback, GstElement* element) {
  if (uridecodebin_sort_signal_id_ > 0) {
    return true;
  }
  bool ret = false;
  gchar *element_name = gst_element_get_name(element);
  autoplugsort_callback_ = callback;
  uridecodebin_ = element;
  uridecodebin_sort_signal_id_ = g_signal_connect(gst_bin_get_by_name(GST_BIN(pipeline_), element_name),
                                                  "autoplug-sort",
                                                  G_CALLBACK(AutoSortPlugCallbackFunc),
                                                  static_cast<void*>(this));
  if (uridecodebin_sort_signal_id_ > 0) {
    ret = true;
  }
  g_free(element_name);
  return ret;
}

bool GstMedia::RegisterAutoPlugSelect(AutoPlugSelectCallback callback, GstElement* element) {
  if (uridecodebin_select_signal_id_ > 0) {
    return true;
  }
  bool ret = false;
  gchar *element_name = gst_element_get_name(element);
  autoplugselect_callback_ = callback;
  uridecodebin_ = element;
  uridecodebin_select_signal_id_ = g_signal_connect(gst_bin_get_by_name(GST_BIN(pipeline_), element_name),
                                                    "autoplug-select",
                                                    G_CALLBACK(AutoSelectPlugCallbackFunc),
                                                    static_cast<void*>(this));
  if (uridecodebin_select_signal_id_ > 0) {
    ret = true;
  }
  g_free(element_name);
  return ret;
}

bool GstMedia::RegisterNoMorePads(NoMorePadsCallback callback, GstElement* element) {
  if (uridecodebin_nomorepad_signal_id_ > 0) {
    return true;
  }
  bool ret = false;
  gchar *element_name = gst_element_get_name(element);
  nomorepads_callback_ = callback;
  uridecodebin_ = element;
  uridecodebin_nomorepad_signal_id_ = g_signal_connect(gst_bin_get_by_name(GST_BIN(pipeline_), element_name),
                                                       "no-more-pads",
                                                       G_CALLBACK(NoMorePadsCallbackFunc),
                                                       static_cast<void*>(this));
  if (uridecodebin_nomorepad_signal_id_ > 0)
    ret = true;
  g_free(element_name);
  return ret;
}

void GstMedia::RegisterSeekControl(SeekControlCallback callback, int interval) {
  if (!seek_control_)
    seek_control_ = new SeekControl(interval);
  if (seek_control_) {
    seek_callback_ = callback;
    SeekControlCallback seekcallback = std::bind(&GstMedia::HandleSeekControl, this, std::placeholders::_1);
    seek_control_->RegisterCallback(seekcallback);
  }
}

bool GstMedia::ChangeStateToNull() {
  LOG_INFO("");
  bool ret = false;
  int timeout_ms = 3000;
  GstState state, pending;

  if (!pipeline_) {
    LOG_ERROR("return with null pipeline");
    return ret;
  }

  GstStateChangeReturn ret_gst = gst_element_set_state(pipeline_, GST_STATE_NULL);
  ret_gst = gst_element_get_state (pipeline_, &state, &pending, timeout_ms * GST_MSECOND);

  if (GST_STATE_CHANGE_SUCCESS == ret_gst) {
    LOG_INFO ("state[%d] done", state);
  } else if (GST_STATE_CHANGE_FAILURE == ret_gst) {
    LOG_ERROR("Failed to Null");
    return ret;
  } else if (GST_STATE_CHANGE_NO_PREROLL == ret_gst) {
    LOG_INFO("GST_STATE_CHANGE_NO_PREROLL(live streaming)");
  } else if (GST_STATE_CHANGE_ASYNC == ret_gst) {
    LOG_INFO("GST_STATE_CHANGE_ASYNC, state=[%d], pending=[%d]", state, pending);
  }

  return true;
}

bool GstMedia::ChangeStateToReady() {
  LOG_INFO("");
  bool ret = false;
  int timeout_ms = 3000;
  GstState state, pending;

  if (!pipeline_) {
    LOG_ERROR("return with null pipeline");
    return ret;
  }

  GstStateChangeReturn ret_gst = gst_element_set_state(pipeline_, GST_STATE_READY);
  ret_gst = gst_element_get_state (pipeline_, &state, &pending, timeout_ms * GST_MSECOND);

  if (GST_STATE_CHANGE_SUCCESS == ret_gst) {
    LOG_INFO("state[%d] done", state);
  } else if (GST_STATE_CHANGE_FAILURE == ret_gst) {
    LOG_ERROR("Failed to READY");
    return ret;
  } else if (GST_STATE_CHANGE_NO_PREROLL == ret_gst) {
    LOG_INFO("GST_STATE_CHANGE_NO_PREROLL(live streaming)");
  } else if (GST_STATE_CHANGE_ASYNC == ret_gst) {
    LOG_INFO("GST_STATE_CHANGE_ASYNC, state=[%d], pending=[%d]", state, pending);
  } else {
    LOG_INFO ("state[%d]", state);
  }

  return true;
}


bool GstMedia::ChangeStateToPause() {
  bool ret = false;

  if (!pipeline_) {
    LOG_ERROR("return with null pipeline");
    return ret;
  }

  GstStateChangeReturn ret_gst = gst_element_set_state(pipeline_, GST_STATE_PAUSED);

  if (GST_STATE_CHANGE_FAILURE == ret_gst) {
    LOG_ERROR("Failed to pause");
    return ret;
  } else if (GST_STATE_CHANGE_NO_PREROLL == ret_gst) {
    LOG_INFO("GST_STATE_CHANGE_NO_PREROLL(live streaming)");
  } else {
    ;
  }
  ret = true;
  return ret;
}

bool GstMedia::ChangeStateToPlayNoWait() {
  GstStateChangeReturn ret_gst;
  bool ret = false;

  if (!pipeline_) {
    LOG_ERROR("return with null pipeline");
    return ret;
  }

  ret_gst = gst_element_set_state(pipeline_, GST_STATE_PLAYING);

  if (ret_gst == GST_STATE_CHANGE_FAILURE)
    LOG_ERROR("Failed setting pipeline to state");
  else if (ret_gst == GST_STATE_CHANGE_ASYNC) {
    LOG_INFO("gst state changed async");
    ret = true;
  } else if (ret_gst == GST_STATE_CHANGE_SUCCESS) {
    LOG_INFO("changed state to play");
    ret = true;
  }

  return ret;
}

bool GstMedia::ChangeStateToPlay() {
  GstStateChangeReturn ret_gst;
  GstState state;
  bool ret = false;
  int timeout_ms = 3000;

  if (!pipeline_) {
    LOG_ERROR("return with null pipeline");
    return ret;
  }

  gst_element_set_state(pipeline_, GST_STATE_PLAYING);
  ret_gst = gst_element_get_state (pipeline_, &state, NULL, timeout_ms * GST_MSECOND);

  if (ret_gst == GST_STATE_CHANGE_FAILURE)
    LOG_ERROR("Failed setting pipeline to state");
  else if (ret_gst == GST_STATE_CHANGE_ASYNC) {
    LOG_INFO("gst state changed async");
    ret = true;
  } else if (ret_gst == GST_STATE_CHANGE_SUCCESS) {
    LOG_INFO("changed state to play");
    ret = true;
  }

  return ret;
}

bool GstMedia::Seek(gint64 position, GstSeekFlags flags) {
  bool ret = false;
  if (!pipeline_) {
    LOG_ERROR("return with null pipeline");
    return ret;
  }

  LOG_INFO("Seek with position (%lld) milli-seconds", position);
  GstEvent *seek;
  //GstSeekFlags flags = GstSeekFlags(GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH);
  //GstSeekFlags flags = GstSeekFlags(GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SKIP);
  seek = gst_event_new_seek ((gdouble)1.0,
                              GST_FORMAT_TIME,
                              flags,
                              GST_SEEK_TYPE_SET, (position*GST_MSECOND),
                              GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE);
  if (!gst_element_send_event (pipeline_, seek))
    LOG_ERROR("Error - gst_element_send_event");
  else {
    ret = true;
    if (seek_control_)
      seek_control_->Start();
  }

  return ret;
}

bool GstMedia::SeekDone() {
  if (seek_control_)
    seek_control_->Done();
  return true;
}

bool GstMedia::SeekSimple(gint64 position) {
  gboolean ret = false;
  if (!pipeline_) {
    LOG_ERROR("return with null pipeline");
    return (bool)ret;
  }

  GstSeekFlags flags = GstSeekFlags(GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SKIP | GST_SEEK_FLAG_FLUSH);
  ret = gst_element_seek_simple(pipeline_, GST_FORMAT_TIME, flags, position);

  return (bool)ret;
}

bool GstMedia::SeekSegment(gint64 position, bool flush) {
  gboolean ret = false;
  if (!pipeline_) {
    LOG_ERROR("return with null pipeline");
    return (bool)ret;
  }

  GstSeekFlags flags = (flush == true) ? GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT)
                                       : GstSeekFlags(GST_SEEK_FLAG_SEGMENT);
  ret = gst_element_seek_simple(pipeline_, GST_FORMAT_TIME, flags, position);

  return (bool)ret;
}

bool GstMedia::GetCurPosition(gint64* position) {
  gint64 pts;
  if (!pipeline_)
    return false;

  if (!gst_element_query_position(pipeline_, GST_FORMAT_TIME, &pts))
    return false;
  else {
    *position = pts;
    return true;
  }
}

char* GstMedia::GetLanguage(int index, const gchar* tag){
  if (!pipeline_ || !tag)
    return nullptr;

  GstTagList* tags = nullptr;
  gchar* lang_code = nullptr;
  bool found = false;
  g_signal_emit_by_name(G_OBJECT(pipeline_), tag, index, &tags);
  if (tags) {
    if (gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &lang_code)) {
      found = true;
    } else if (gst_tag_list_get_string(tags, GST_TAG_CODEC, &lang_code)) {
      found = true;
    }
    gst_tag_list_free(tags);
  }

  if (found && lang_code) {
    gchar iso_lang[4] = {'u', 'n', 'd', 0};
    size_t lang_code_size = strlen(lang_code);
    size_t lang_code_dst_size = strlen(iso_lang);

    if (lang_code_size > lang_code_dst_size) {
      strncpy(iso_lang, lang_code, lang_code_dst_size);
      iso_lang[lang_code_dst_size] = '\0';
    } else {
      strncpy(iso_lang, lang_code, lang_code_size);
      iso_lang[lang_code_size] = '\0';
    }
    snprintf(lang_code_, strlen(iso_lang) + 1, "%s", iso_lang);
    g_free(lang_code);
  } else {
    snprintf(lang_code_, 3, "und");
  }

  LOG_INFO("%s : code[%s]", tag, lang_code_);
  return lang_code_;
}

void GstMedia::GetMediaInfo(MediaFileInfo* info, size_t index, const gchar* name) {
  if (!pipeline_ || !info || !name)
    return;
  if (index >= info->audio_.size() &&
      g_strstr_len(name, strlen(name), "audio")) {
    //LOG_ERROR("Invalid audio index value : %u", index);
    return;
  }
  GstTagList* tags = nullptr;
  GstPad* pad = nullptr;

  if(g_strcmp0(name, "get-video-tags") == 0){
    g_signal_emit_by_name(G_OBJECT(pipeline_), name, index, &tags);
    GetFormatTagInfo(info, tags);
    GetVideoTagInfo(info->video_, tags);
  }else if(g_strcmp0(name, "get-audio-tags") == 0){
    g_signal_emit_by_name(G_OBJECT(pipeline_), name, index, &tags);
    GetFormatTagInfo(info, tags);
    GetAudioTagInfo(info->audio_[index], tags);
  }else if(g_strcmp0(name, "get-video-pad") == 0){
    g_signal_emit_by_name(G_OBJECT(pipeline_), name, index, &pad);
    GetVideoPadInfo(info->video_, pad);
  }else if(g_strcmp0(name, "get-audio-pad") == 0){
    g_signal_emit_by_name(G_OBJECT(pipeline_), name, index, &pad);
    GetAudioPadInfo(info->audio_[index], pad);
  }

  if(tags)
    gst_tag_list_free(tags);
  if(pad)
    gst_object_unref(pad);
}

void GstMedia::GetFormatTagInfo(MediaFileInfo* info, GstTagList* tags) {
  gchar* str_type = nullptr;

  if(!tags)
    return;

  if (info->file_format_.empty()) {
    if (gst_tag_list_get_string(tags, GST_TAG_CONTAINER_FORMAT, &str_type)) {
      if (str_type != NULL) {
        info->file_format_ = str_type;
        g_free(str_type);
        LOG_INFO("GST_TAG_CONTAINER_FORMAT-%s", info->file_format_.c_str());
      }
    }
  }
}

void GstMedia::GetVideoTagInfo(MediaFileVideoInfo& info, GstTagList* tags) {
  gchar* str_type = nullptr;
  guint int_type;
  gdouble double_type;

  if(!tags)
    return;

  info.QPEL_ = "No";
  if (gst_tag_list_get_string(tags, GST_TAG_VIDEO_CODEC, &str_type)) {
    if (str_type != NULL) {
      info.codec_id_ = str_type;
      g_free(str_type);
    }
  }

  if (gst_tag_list_get_uint(tags, GST_TAG_BITRATE, &int_type))
    info.bitrate_ = int_type;
  if (gst_tag_exists("video-width") && gst_tag_list_get_uint(tags, "video-width", &int_type))
    info.width_= int_type;
  if (gst_tag_exists("video-height") && gst_tag_list_get_uint(tags, "video-height", &int_type))
    info.height_ = int_type;
  if (gst_tag_exists("video-framerate") && gst_tag_list_get_double (tags, "video-framerate", &double_type))
    info.framerate_= (float)double_type;
  if (gst_tag_exists("video-sprite-warping-points") && gst_tag_list_get_uint(tags, "video-sprite-warping-points", &int_type))
    info.GMC_ = int_type;
  if (gst_tag_exists("video-qpel") && gst_tag_list_get_uint(tags, "video-qpel", &int_type)) {
    if(int_type == 1)
      info.QPEL_ = "Yes";
  }

  LOG_INFO("GST_TAG_VIDEO_CODEC-%s", info.codec_id_.c_str());
  LOG_INFO("GST_TAG_VIDEO_BITRATE-%d", info.bitrate_);
  LOG_INFO("GST_TAG_VIDEO_WIDTH-%d", info.width_);
  LOG_INFO("GST_TAG_VIDEO_HEIGHT-%d", info.height_);
  LOG_INFO("GST_TAG_VIDEO_FRAMERATE-%g", info.framerate_);
  LOG_INFO("GST_TAG_VIDEO_SPRITE_WARPING_POINTS-%d", info.GMC_);
  LOG_INFO("GST_TAG_VIDEO_QPEL-%s", info.QPEL_.c_str());
}

void GstMedia::GetAudioTagInfo(MediaFileAudioInfo& info, GstTagList* tags) {
  gchar* str_type = nullptr;
  guint int_type;

  if(!tags)
    return;

  if (gst_tag_list_get_string(tags, GST_TAG_AUDIO_CODEC, &str_type)) {
    if (str_type != NULL) {
      info.audio_codec_id_ = str_type;
      g_free(str_type);
    }
  }
  if (gst_tag_list_get_uint(tags, GST_TAG_BITRATE, &int_type))
    info.audio_bitrate_ = int_type;

  LOG_INFO("GST_TAG_AUDIO_CODEC-%s", info.audio_codec_id_.c_str());
  LOG_INFO("GST_TAG_AUDIO_BITRATE-%d", info.audio_bitrate_);
}

void GstMedia::GetVideoPadInfo(MediaFileVideoInfo& info, GstPad* pad) {
  GstCaps *caps = nullptr;
  GstStructure *structure = nullptr;
  gchar *caps_str = nullptr;
  gint width = 0;
  gint height = 0;
  gint num = 0;
  gint denom = 0;

  if (!pad)
    return;

  caps = gst_pad_get_current_caps(pad);
  if (!gst_caps_is_fixed(caps))
    return;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int(structure, "width", &width);
  gst_structure_get_int(structure, "height", &height);
  gst_structure_get_fraction(structure, "framerate", &num, &denom);

  if (width > 0 && height > 0) {
    if (info.width_ <= 0 || info.height_ <= 0) {
      info.width_ = width;
      info.height_ = height;
    }
  }
  caps_str = gst_caps_to_string(caps);
  LOG_INFO("GST_CAPS_VIDEO - %s", caps_str);
  LOG_INFO("GST_PAD_VIDEO WIDTH[%d] HEIGHT[%d] Framerate[%d / %d]", width, height, num, denom);
  media_type_ = "video";

  if(caps)
    gst_caps_unref(caps);
  if(caps_str)
    g_free(caps_str);
}

void GstMedia::GetAudioPadInfo(MediaFileAudioInfo& info, GstPad* pad) {
  GstCaps *caps = nullptr;
  GstStructure *structure = nullptr;
  gchar *caps_str = nullptr;
  gint samplerate = 0;

  if(!pad)
    return;

  caps = gst_pad_get_current_caps(pad);
  if(!gst_caps_is_fixed(caps))
    return;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int(structure, "rate", &samplerate);
  caps_str = gst_caps_to_string(caps);
  LOG_INFO("GST_CAPS_AUDIO - %s", caps_str);
  LOG_INFO("GST_CAPS_AUDIO SAMPLERATE[%d]", samplerate);
  media_type_ = "audio";

  if(caps)
    gst_caps_unref(caps);
  if(caps_str)
    g_free(caps_str);
}

bool GstMedia::SetSubtitleEnable(bool show) {
  if (!pipeline_)
    return false;
  LOG_INFO("req-show:%d", show);
  gint flags;
  g_object_get(G_OBJECT(pipeline_), "flags", &flags, nullptr);
  if (show) {
    flags |= GST_PLAY_FLAG_TEXT;
  }
  else {
    flags &= ~GST_PLAY_FLAG_TEXT;
  }
  g_object_set(G_OBJECT(pipeline_), "flags", flags, nullptr);
  LOG_INFO("Set flag value: %.4x",flags);
  return true;
}

bool GstMedia::SetSubtitleLanguageIndex(int index) {
  if (!pipeline_)
    return false;

  int num_text = 0;
  g_object_get(G_OBJECT(pipeline_), "n-text", &num_text, nullptr);
  LOG_INFO("n-text:%d, req-index:%d", num_text, index);
  if (index >= num_text) {
    LOG_INFO("return - index(%d) exceeds n-text(%d)", index, num_text);
    return false;
  }
  SetSubtitleEnable(true);
  g_object_set(G_OBJECT(pipeline_), "current-text", index, nullptr);
  return true;
}

bool GstMedia::SetAudioLanguage(int index) {
  if (!pipeline_)
    return false;

  int num_audio = 0;
  int cur_audio_index = -1;
  g_object_get(G_OBJECT(pipeline_), "current-audio", &cur_audio_index, nullptr);
  g_object_get(G_OBJECT(pipeline_), "n-audio", &num_audio, nullptr);
  LOG_INFO("cur-audio-index:%d, n-audio:%d, req-index:%d", cur_audio_index, num_audio, index);
  if (index >= num_audio) {
    LOG_INFO("return - index(%d) exceeds n-audio(%d)", index, num_audio);
    return false;
  }

  g_object_set(G_OBJECT(pipeline_), "current-audio", index, nullptr);
  return true;
}

bool GstMedia::SetPlaybackSpeed(double rate) {

  LOG_INFO("Set speed with (%lf) rate", rate);
  gint64 position = 0;
  if (GetCurPosition(&position) == true) {
    GstEvent *seek;
    GstSeekFlags flags = GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE);
    if (rate > 0) {
      seek = gst_event_new_seek ((gdouble)rate,
                                  GST_FORMAT_TIME,
                                  flags,
                                  GST_SEEK_TYPE_SET, position,
                                  GST_SEEK_TYPE_NONE, 0);
    } else {
      seek = gst_event_new_seek ((gdouble)rate,
                                  GST_FORMAT_TIME,
                                  flags,
                                  GST_SEEK_TYPE_SET, 0,
                                  GST_SEEK_TYPE_SET, position);
    }
    if (!gst_element_send_event (pipeline_, seek)) {
      LOG_ERROR("Error - gst_element_send_event speed");
      return false;
    }
  } else {
    return false;
  }
  return true;
}

bool GstMedia::SetAudioMute(bool mute) {
  if (!pipeline_)
    return false;
  LOG_INFO("req-mute:%d", mute);
  g_object_set(G_OBJECT(pipeline_), "mute", (gboolean)mute, nullptr);
  return true;
}

bool GstMedia::SetAVoffset(int delay) {
  gint64 av_offset = 0;
  if (!pipeline_)
    return false;
  LOG_INFO("req-avoffset:%d", delay);
  /* scale input delay value in ms to nanosecs */
  av_offset = (gint64)delay * 1000000LL;
  g_object_set(G_OBJECT(pipeline_), "av-offset", (gint64)av_offset, nullptr);
  return true;
}

bool GstMedia::SetAudioVolume(double volume) {
  if (!pipeline_)
    return false;

  LOG_INFO("req-audio-volume:%lf", volume);
  g_object_set(G_OBJECT(pipeline_), "volume", (gdouble)volume, nullptr);
  return true;
}

bool GstMedia::SwitchChannel(bool downmix, char slot, char slot_6ch, bool is_dsd, const std::string& media_type, bool provide_global_clock, GstElement* audio_sink, GstElement* parent) {
  if (!pipeline_)
    return false;
  LOG_INFO("downmix %s", downmix ? "enable" : "disable");
  gint64 position = 0;
  if (GetCurPosition(&position)) {
    LOG_INFO("current position=[%lld]", position);
  }

  bool ret = false;
  int timeout_ms = 3000;
  ret = ChangeStateToReady();
  if(!ret)
   return false;
  
  int mix_channel = 0;
  if(downmix)
    mix_channel = 2;
  else
    mix_channel = 6;

  gchar* audio_sink_ = Conf::GetSink(AUDIO_SINK);
  if (strlen(audio_sink_)) {
    std::string audio_property(audio_sink_);
    provide_global_clock ? audio_property.append(" provide-clock=true") : audio_property.append(" provide-clock=false");
    audio_sink = CreateAudioSinkBin(audio_property.c_str(), slot, slot_6ch, mix_channel, is_dsd, media_type);
    if (audio_sink) {
      SetProperty<GstElement*>(parent, "audio-sink", const_cast<GstElement*>(audio_sink));
    } else {
      LOG_ERROR("Failed to create new audio sink");
      return false;
    }
  }
  LOG_INFO("#### Create new sink done");
  ret = ChangeStateToPlay();
  if(!ret)
   return false;
  if (SeekSimple(position)) {
    LOG_INFO("#### Seeking...");
  }
  //GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline_), GST_DEBUG_GRAPH_SHOW_ALL, "CHANNEL_V_PIPELINE");
  return true;
}

bool GstMedia::SetTag(const std::string& key, const std::string& value) {
  GstElement* tag_setter_element = nullptr;
  GstTagSetter *tagsetter = nullptr;
  std::string padded_value = "";

  if (!pipeline_) {
    return false;
  }

  tag_setter_element = gst_bin_get_by_interface(GST_BIN(pipeline_), GST_TYPE_TAG_SETTER);
  tagsetter = GST_TAG_SETTER (tag_setter_element);

  //add padding if case of even number of length
  if(!(value.length() % 2)) {
    padded_value = value + " ";
  } else {
    padded_value = value;
  }
  gst_tag_setter_add_tags (tagsetter, GST_TAG_MERGE_REPLACE,
                              key.c_str(), padded_value.c_str(), NULL);

  gst_object_unref (tag_setter_element);
  LOG_INFO("setTag, Key[%s], Value[%s]", key.c_str(), value.c_str());
  return true;
}

gint64 GstMedia::GetDuration() {
  gint64 dur = 0;
  if (!pipeline_)
    return dur;

  gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &dur);
  LOG_INFO("duration : %ld (sec)", dur/GST_SECOND);
  return dur;
}

bool GstMedia::IsSeekable() {
  if (!pipeline_)
    return false;

  GstQuery *query = gst_query_new_seeking(GST_FORMAT_TIME);
  gboolean is_seekable = false;
  if (gst_element_query(pipeline_, query))
    gst_query_parse_seeking(query, nullptr, &is_seekable, nullptr, nullptr);
  LOG_INFO("seekable : %s", is_seekable ? "true" : "false");
  gst_query_unref(query);
  return (bool)is_seekable;
}

bool GstMedia::GetSeekStatus() {
  return isSeeking_;
}

void GstMedia::SetSeekStatus(bool status) {
  isSeeking_ = status;
}

bool GstMedia::ExtractThumbnail(const char* uri, const char* format) {

  GstElement *sink;
  gint width;
  gint height;
  GstSample *sample = nullptr;
  GError *error = nullptr;
  GdkPixbuf *pixbuf;
  gboolean res;
  GstMapInfo map;

  sink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
  if (!sink) {
    LOG_ERROR ("could not get sink element! \n");
    return false;
  }

  g_signal_emit_by_name (sink, "pull-preroll", &sample, nullptr);
  gst_object_unref (sink);

  if (sample) {
    GstBuffer *buffer;
    GstCaps *caps;
    GstStructure *s;

    caps = gst_sample_get_caps (sample);
    if (!caps) {
      LOG_ERROR ("could not get snapshot format\n");
      return false;
    }

    s = gst_caps_get_structure (caps, 0);
    res = gst_structure_get_int (s, "width", &width);
    res |= gst_structure_get_int (s, "height", &height);
    if (!res) {
      LOG_ERROR ("could not get snapshot dimension\n");
      return false;
    }

    buffer = gst_sample_get_buffer (sample);
    res = gst_buffer_map (buffer, &map, GST_MAP_READ);
    pixbuf = gdk_pixbuf_new_from_data (map.data,
                                       GDK_COLORSPACE_RGB, false, 8, width, height,
                                       GST_ROUND_UP_4 (width * 3), nullptr, nullptr);

    if (g_strcmp0(format,"jpeg") == 0) {
      if (!gdk_pixbuf_save (pixbuf, uri, format, &error, "quality", "100", nullptr))
        LOG_ERROR ("failed to save pixbuf");
    }
    else {
      if (!gdk_pixbuf_save (pixbuf, uri, format, &error, nullptr))
        LOG_ERROR ("failed to save pixbuf");
    }

    gst_buffer_unmap (buffer, &map);
  }
  else {
    LOG_ERROR ("could not make thumbnail\n");
    return false;
  }


  return true;
}

std::string GstMedia::GetRawURI(const std::string& uri) {
  static const std::string kFilePrefix("file://");
  static const std::string kThumbnailPrefix("thumbnail://");
  static const std::string kHTTPPrefix("http://");
  static const std::string kHTTPSPrefix("https://");

  bool found_string = false;
  bool found_http = false;
  std::string uri_http;
  std::string open_uri;
  std::size_t found = uri.find(kFilePrefix);
  std::size_t http_found = uri.find(kHTTPPrefix);
  std::size_t https_found = uri.find(kHTTPSPrefix);

  if (found != std::string::npos) {
    open_uri = uri.substr(found+kFilePrefix.size());
    found_string = true;
  } else if (http_found != std::string::npos || https_found != std::string::npos) {
    found_http = true;
  } else {
    found = uri.find(kThumbnailPrefix);
    if (found != std::string::npos) {
      open_uri = uri.substr(found+kThumbnailPrefix.size());
      found_string = true;
    }
  }

  if (found_string) {
    gchar *raw_uri = gst_filename_to_uri(open_uri.c_str(), nullptr);
    std::string ret_string((raw_uri == nullptr) ? "" : raw_uri);
    if (raw_uri)
      g_free(raw_uri);
    return ret_string;
  } else if (found_http) {
    uri_http = uri;
    return uri_http;
  } else {
    return std::string("");
  }
}

bool GstMedia::IsLGsrcValidSamplerate(int samplerate) {
  std::vector<int> support_sample_rate = Conf::GetSampleRate(SAMPLE_RATE);

  if (support_sample_rate.empty() || std::find(support_sample_rate.begin(), support_sample_rate.end(), samplerate)
      != support_sample_rate.end())
  {
      return true;
  }
  LOG_ERROR ("LGSRC cannot support %d Hz samplerate", samplerate);
  return false;
}

bool GstMedia::IsMtpFile(const std::string& uri) {
  bool ret = false;
  std::string open_uri = uri;
  std::size_t found = open_uri.find("file:///media/mtp");
  if (found != std::string::npos)
    ret = true;
  return ret;
}

void GstMedia::SetTaskPriority(const gchar* name, gint priority) {
  gchar* dir_path = nullptr;
  GDir *process_dir = nullptr;
  GError *error = nullptr;
  const gchar *task_filename = nullptr;

  dir_path = g_strdup_printf ("/proc/%d/task", getpid ());
  if (!dir_path){
    LOG_ERROR ("g_strdup_printf() failed in path");
    goto _EXIT;
  }
  process_dir = g_dir_open (dir_path, 0, &error);
  if (!process_dir) {
    if (error) {
      if (error->message) {
        LOG_ERROR ("g_dir_open() failed in path-%s", error->message);
      } else {
        LOG_ERROR ("g_dir_open() failed in path");
      }
      g_clear_error (&error);
    }
    goto _EXIT;
  }

  LOG_INFO ("try to increase priority of %s", name);

  while ((task_filename = g_dir_read_name (process_dir))) {
    // try to find 'taskname' in /proc/{pid}/task/{tid}/comm files
    SetTaskPriorityInternal(name, task_filename, dir_path, process_dir, priority);
  }

_EXIT:
  if (dir_path)
    g_free (dir_path);
  if (process_dir)
    g_dir_close (process_dir);
  return;
}

void GstMedia::PrintGstDot(const gchar* name) {
  if (Conf::GetFeatures(SUPPORT_GST_DOT)) // Debug purpose
    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline_), GST_DEBUG_GRAPH_SHOW_ALL, name);
}

gboolean GstMedia::BusCallbackFunc(GstBus* bus, GstMessage* message, gpointer data) {
  GstMedia* handler = reinterpret_cast<GstMedia*> (data);
  return handler->bus_callback_(bus, message, data);
}

void GstMedia::ElementAddCallbackFunc(GstBin* bin, GstElement *element, gpointer data) {
  GstMedia* handler = reinterpret_cast<GstMedia*> (data);
  handler->pipeline_elementadd_callback_(bin, element, data);
}

void GstMedia::UriDecodeBinElementAddCallbackFunc(GstBin* bin, GstElement *element, gpointer data) {
  GstMedia* handler = reinterpret_cast<GstMedia*> (data);
  handler->uridecodebin_elementadd_callback_(bin, element, data);
}
void GstMedia::PlaySinkElementAddCallbackFunc(GstBin* bin, GstElement *element, gpointer data) {
  GstMedia* handler = reinterpret_cast<GstMedia*> (data);
  handler->playsink_elementadd_callback_(bin, element, data);
}
void GstMedia::DecodeBinElementAddCallbackFunc(GstBin* bin, GstElement *element, gpointer data) {
  GstMedia* handler = reinterpret_cast<GstMedia*> (data);
  handler->decodebin_elementadd_callback_(bin, element, data);
}

GValueArray* GstMedia::AutoSortPlugCallbackFunc(GstElement *bin,GstPad *pad,GstCaps *caps,
                                               GValueArray *factories, gpointer data) {
  GstMedia* handler = reinterpret_cast<GstMedia*> (data);
  return handler->autoplugsort_callback_(bin, pad, caps, factories, data);
}

int GstMedia::AutoSelectPlugCallbackFunc(GstElement *bin,GstPad *pad,GstCaps *caps,
                                         GstElementFactory *factory, gpointer data) {
  GstMedia* handler = reinterpret_cast<GstMedia*> (data);
  return handler->autoplugselect_callback_(bin, pad, caps, factory, data);
}

void GstMedia::NoMorePadsCallbackFunc(GstElement *element, gpointer data) {
  GstMedia* handler = reinterpret_cast<GstMedia*> (data);
  return handler->nomorepads_callback_(element, data);
}

void GstMedia::HandleSeekControl(int message) {
  if (seek_callback_)
    seek_callback_(message);
}

void GstMedia::SetTaskPriorityInternal(const gchar* name, const gchar* task_filename,
                                       const gchar* dir_path, GDir* directory, gint priority) {
  FILE *fp = nullptr;
  size_t size;
  ssize_t readsize;
  gchar *line_str = nullptr;
  gchar* file_path = nullptr;

  file_path = g_strdup_printf ("%s/%s/comm", dir_path, task_filename);
  if (!file_path)
    return;
  fp = fopen (file_path, "r");
  if (fp) {
    readsize = getdelim (&line_str, &size, '\n', fp);
    if ((readsize > 0) &&
        (nullptr != g_strrstr (line_str, name))) {

      LOG_INFO ("found  task (%s:%d)\n", name, atoi (task_filename));
      if (setpriority (PRIO_PROCESS, atoi (task_filename), priority) != -1)
        LOG_INFO ("set the (tid:%s) priority to %d\n", name, priority);
    }
    if (line_str)
      g_free (line_str);
    fclose (fp);
  } else {
    LOG_ERROR ("failed to open [%s] \n", file_path);
  }
  g_free (file_path);
}


} // namespace genivimedia

