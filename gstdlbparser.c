#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdlbparse.h"

GST_DEBUG_CATEGORY_STATIC (dlb_ac3_parse_debug_category);
#define GST_CAT_DEFAULT dlb_ac3_parse_debug_category


/* prototypes */
static void gst_dlb_ac3_parse_set_property (GObject * obj, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dlb_ac3_parse_get_property (GObject * obj, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean dlb_ac3_parse_start (GstBaseParse * parse);
static gboolean dlb_ac3_parse_stop (GstBaseParse * parse);
static GstFlowReturn dlb_ac3_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);
static GstFlowReturn dlb_ac3_parse_pre_push_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);

#define AC3_SAMPLES_PER_BLOCK 256

/*properties */
enum {
  PROP_0,
  PROP_NOTIFY_ATMOS
};

/* pad templates */
static GstStaticPadTemplate dlb_ac3_parse_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ac3, framed = (boolean) true, "
        " channels = (int) [ 1, 6 ], rate = (int) [ 8000, 48000 ], "
        " alignment = (string) { frame }; "
        "audio/x-eac3, framed = (boolean) true, "
        " channels = (int) [ 1, 8 ], rate = (int) [ 8000, 48000 ], "
        " alignment = (string) { frame }; ")
    );

static GstStaticPadTemplate dlb_ac3_parse_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ac3; " "audio/x-eac3; " "audio/ac3; "
        "audio/eac3; " "audio/x-private1-ac3")
    );


/* class initialization */
G_DEFINE_TYPE_WITH_CODE (DlbAc3Parse, dlb_ac3_parse, GST_TYPE_BASE_PARSE,
    GST_DEBUG_CATEGORY_INIT (dlb_ac3_parse_debug_category, "dlbparse", 0,
        "debug category for Dolby Atmos parse element"));

static void
dlb_ac3_parse_class_init (DlbAc3ParseClass * klass)
{
  GstBaseParseClass *base_parse_class = GST_BASE_PARSE_CLASS (klass);
  GObjectClass *gobject_class = (GObjectClass *)klass;

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &dlb_ac3_parse_src_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &dlb_ac3_parse_sink_template);

  gobject_class->set_property = gst_dlb_ac3_parse_set_property;
  gobject_class->get_property = gst_dlb_ac3_parse_get_property;

  g_object_class_install_property (gobject_class, PROP_NOTIFY_ATMOS,
      g_param_spec_string ("notify-atmos", "Notify Atmos",
          "Playing content is ATMOS(Only read this property to know current content is ATMOS)",
          NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Dolby Atmos Parser for DCX",
      "Codec/Parser/Audio",
      "Parse EAC3 and AC3 audio stream",
      "LG Multimedia Team");

  base_parse_class->start = GST_DEBUG_FUNCPTR (dlb_ac3_parse_start);
  base_parse_class->stop = GST_DEBUG_FUNCPTR (dlb_ac3_parse_stop);
  base_parse_class->handle_frame =
      GST_DEBUG_FUNCPTR (dlb_ac3_parse_handle_frame);
  base_parse_class->pre_push_frame =
      GST_DEBUG_FUNCPTR (dlb_ac3_parse_pre_push_frame);
}

static void
dlb_ac3_parse_init (DlbAc3Parse * ac3parse)
{
  GST_PAD_SET_ACCEPT_INTERSECT (GST_BASE_PARSE_SINK_PAD (ac3parse));
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_BASE_PARSE_SINK_PAD (ac3parse));

  ac3parse->tag_published = FALSE;
}

static void gst_dlb_ac3_parse_set_property (GObject * obj, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  DlbAc3Parse *ac3parse = DLB_AC3_PARSE (obj);

  switch (prop_id) {
    case PROP_NOTIFY_ATMOS:
       ac3parse->notify_atmos = g_value_get_string (value);

       if (ac3parse->notify_atmos == NULL) {
         GST_DEBUG_OBJECT (ac3parse, "Current content is not ATMOS");
       };
       break;
     default:
       G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
       break;
  }
}

static void gst_dlb_ac3_parse_get_property (GObject * obj, guint prop_id, GValue * value, GParamSpec * pspec)
{
  DlbAc3Parse *ac3parse = DLB_AC3_PARSE (obj);

  switch (prop_id) {
    case PROP_NOTIFY_ATMOS:
      g_value_set_string (value, ac3parse->notify_atmos);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
  }

}

static gboolean
dlb_ac3_parse_start (GstBaseParse * parse)
{
  DlbAc3Parse *ac3parse = DLB_AC3_PARSE (parse);
  guint frmsize = 0;

  GST_DEBUG_OBJECT (ac3parse, "start");

  ac3parse->parser = dlb_audio_parser_new (DLB_AUDIO_PARSER_TYPE_AC3);
  ac3parse->stream_info.data_type = 0;
  ac3parse->stream_info.channels = 0;
  ac3parse->stream_info.sample_rate = 0;
  ac3parse->stream_info.framesize = 0;
  ac3parse->stream_info.samples = 0;
  ac3parse->stream_info.object_audio = 0;
  ac3parse->notify_atmos = NULL;

  frmsize = dlb_audio_parser_query_min_frame_size (ac3parse->parser);
  gst_base_parse_set_min_frame_size (parse, frmsize);

  return TRUE;
}

static gboolean
dlb_ac3_parse_stop (GstBaseParse * parse)
{
  DlbAc3Parse *ac3parse = DLB_AC3_PARSE (parse);
  GST_DEBUG_OBJECT (ac3parse, "stop");

  if (ac3parse->notify_atmos) {
    g_free (ac3parse->notify_atmos);
    ac3parse->notify_atmos = NULL;
  }
  dlb_audio_parser_free (ac3parse->parser);

  return TRUE;
}

static GstFlowReturn
dlb_ac3_parse_handle_frame (GstBaseParse * parse, GstBaseParseFrame * frame,
    gint * skipsize)
{
  DlbAc3Parse *ac3parse = DLB_AC3_PARSE (parse);
  GstMapInfo map;
  dlb_audio_parser_info info;
  dlb_audio_parser_status status;

  gboolean eac,objected;
  gsize offset, frmsize;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (ac3parse, "handle_frame");

  gst_buffer_map (frame->buffer, &map, GST_MAP_READ);

  if (G_UNLIKELY (GST_BASE_PARSE_DRAINING (parse))) {
    GST_DEBUG_OBJECT (parse, "draining");
    dlb_audio_parser_draining_set (ac3parse->parser, 1);
  }
  status = dlb_audio_parser_parse (ac3parse->parser, map.data, map.size,
      &info, &offset);

  if (status == DLB_AUDIO_PARSER_STATUS_OUT_OF_SYNC && offset != 0) {
    GST_DEBUG_OBJECT (parse, "out-of-sync: skipping %zd bytes to frame start",
        offset);

    *skipsize = (gint) offset;
    goto cleanup;
  } else if (status == DLB_AUDIO_PARSER_STATUS_NEED_MORE_DATA) {

    frmsize = dlb_audio_parser_query_min_frame_size (ac3parse->parser);
    GST_DEBUG_OBJECT (parse, "need-more-data: %zd", frmsize);

    gst_base_parse_set_min_frame_size (parse, frmsize);

    *skipsize = (gint) offset;
    goto cleanup;
  } else if (status != DLB_AUDIO_PARSER_STATUS_OK) {
    GST_WARNING_OBJECT (parse, "header-error: %d, skipping %zd bytes", status,
        offset);

    *skipsize = offset;
    goto cleanup;
  }

  eac = info.data_type == DATA_TYPE_EAC3;
  objected = info.object_audio == 1;
  frmsize = info.framesize;

  if (objected) {
    if (!ac3parse->notify_atmos) {
      /* emit to notify the current content is ATMOS */
      ac3parse->notify_atmos = g_strdup ("E-AC-JOC");
      g_object_notify (G_OBJECT (ac3parse), "notify-atmos");
    }
    GST_LOG_OBJECT (ac3parse, "found EAC-3-JOC frame of size %zd", frmsize);
  } else {
    GST_LOG_OBJECT (ac3parse, "found %sAC-3 frame of size %zd", eac ? "E-" : "", frmsize);
  }

  /* detect current stream type and send caps if needed */
  if (G_UNLIKELY (ac3parse->stream_info.sample_rate != info.sample_rate
          || ac3parse->stream_info.channels != info.channels
          || ac3parse->stream_info.data_type != info.data_type)) {

    GstCaps *caps = gst_caps_new_simple (eac ? "audio/x-eac3" : "audio/x-ac3",
        "framed", G_TYPE_BOOLEAN, TRUE, "rate", G_TYPE_INT, info.sample_rate,
        "channels", G_TYPE_INT, info.channels, "alignment", G_TYPE_STRING,
        "frame", NULL);

    gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
    gst_caps_unref (caps);

    ac3parse->stream_info = info;
    gst_base_parse_set_frame_rate (parse, info.sample_rate, info.samples, 0, 0);
  }

  /* Update frame rate if number of samples per frame has changed */
  if (G_UNLIKELY (ac3parse->stream_info.samples != info.samples))
    gst_base_parse_set_frame_rate (parse, info.sample_rate, info.samples, 0, 0);

cleanup:
  gst_buffer_unmap (frame->buffer, &map);

  if (status == DLB_AUDIO_PARSER_STATUS_OK) {
    ret = gst_base_parse_finish_frame (parse, frame, frmsize);
  }

  return ret;
}

static GstFlowReturn
dlb_ac3_parse_pre_push_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  DlbAc3Parse *ac3parse = DLB_AC3_PARSE (parse);
  GstTagList *taglist;
  gint64 bitrate_ = 0;
  gdouble frame_duration_ = 0;
  GstCaps *caps;

  if (ac3parse->tag_published)
    return GST_FLOW_OK;

  caps = gst_pad_get_current_caps (GST_BASE_PARSE_SRC_PAD (parse));
  if (G_UNLIKELY (caps == NULL)) {
    if (GST_PAD_IS_FLUSHING (GST_BASE_PARSE_SRC_PAD (parse))) {
      GST_INFO_OBJECT (parse, "Src pad is flushing");
      return GST_FLOW_FLUSHING;
    } else {
      GST_INFO_OBJECT (parse, "Src pad is not negotiated!");
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  if (ac3parse->stream_info.samples > 0 && ac3parse->stream_info.sample_rate > 0) {
    frame_duration_ = ((gdouble)ac3parse->stream_info.samples/ac3parse->stream_info.sample_rate);
    bitrate_ = 8 * (ac3parse->stream_info.framesize/frame_duration_);
  }
  taglist = gst_tag_list_new_empty ();
  gst_pb_utils_add_codec_description_to_tag_list (taglist,
      GST_TAG_AUDIO_CODEC, caps);
  gst_caps_unref (caps);

  GST_DEBUG("Calculated bitrate value : %lld", bitrate_);
  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE,
      GST_TAG_BITRATE, bitrate_, NULL);
  gst_base_parse_merge_tags (parse, taglist, GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (taglist);

  /* also signals the end of first-frame processing */
  ac3parse->tag_published = TRUE;

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  #ifdef DLB_AUDIO_PARSER_OPEN_DYNLIB
  if (dlb_audio_parser_try_open_dynlib())
    return FALSE;
  #endif

  return gst_element_register (plugin, "dlbparse", GST_RANK_PRIMARY + 2,
      DLB_TYPE_AC3_PARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dlbparse,
    "Dolby Atmos Parser for DCX",
    plugin_init, VERSION, "LGPL", "LG GStreamer Plugins", "Unknown package origin")
