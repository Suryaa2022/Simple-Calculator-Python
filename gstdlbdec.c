// @@@LICENSE
//
// Copyright (C) 2023, LG Electronics, All Right Reserved.
//
// No part of this source code may be communicated, distributed, reproduced
// or transmitted in any form or by any means, electronic or mechanical or
// otherwise, for any purpose, without the prior written permission of
// LG Electronics.
//
// LICENSE@@@

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <gst/gst.h>

#include "gstdlbdec.h"

#include "dlb_buffer.h"

GST_DEBUG_CATEGORY_STATIC (dlb_ac3dec_debug_category);
#define GST_CAT_DEFAULT dlb_ac3dec_debug_category

/* public prototypes */
static void dlb_ac3dec_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void dlb_ac3dec_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void dlb_ac3dec_finalize (GObject * object);
static gboolean dlb_ac3dec_start (GstAudioDecoder * decoder);
static gboolean dlb_ac3dec_stop (GstAudioDecoder * decoder);
static gboolean dlb_ac3dec_set_format (GstAudioDecoder * decoder,
    GstCaps * caps);
static GstFlowReturn dlb_ac3dec_handle_frame (GstAudioDecoder * decoder,
    GstBuffer * inbuf);
static void renegotiate (DlbAc3Dec * ac3dec);

enum
{
  PROP_0,
  PROP_OUT_MODE,
  PROP_2CH_DOWNMIX,
  PROP_OUT_DATATYPE,
  PROP_OUT_BITDEPTH,
  PROP_DRC_MODE,
  PROP_DRC_BOOST,
  PROP_DRC_CUT,
  PROP_EXT_TOP_SIDE_MODE,
  PROP_HEIGHT_FRNT_GN,
  PROP_HEIGHT_REAR_GN,
  PROP_DMX_ENABLE,
};

#define DLB_AC3DEC_SRC_CAPS                                             \
    "audio/x-raw, " \
    "format = (string) { S16LE, S24LE, S32LE }, " \
    "layout = (string) interleaved, " \
    "rate = (int) [1, MAX]," \
    "channels = (int) [1,8];" \

#define DLB_AC3DEC_SINK_CAPS                                            \
    "audio/x-ac3, " \
    "rate = (int) [32000, 48000]; " \
    "audio/x-eac3, " \
    "rate = (int) [32000, 48000]; " \

/* pad templates */
static GstStaticPadTemplate dlb_ac3dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (DLB_AC3DEC_SRC_CAPS)
    );

static GstStaticPadTemplate dlb_ac3dec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (DLB_AC3DEC_SINK_CAPS)
    );

/* class initialization */
G_DEFINE_TYPE_WITH_CODE (DlbAc3Dec, dlb_ac3dec, GST_TYPE_AUDIO_DECODER,
    GST_DEBUG_CATEGORY_INIT (dlb_ac3dec_debug_category, "dlbdec", 0,
        "debug category for Dolby Atmos decoder element"));


static void
dlb_ac3dec_class_init (DlbAc3DecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAudioDecoderClass *audio_decoder_class = GST_AUDIO_DECODER_CLASS (klass);

  gobject_class->set_property = dlb_ac3dec_set_property;
  gobject_class->get_property = dlb_ac3dec_get_property;
  gobject_class->finalize = dlb_ac3dec_finalize;
  audio_decoder_class->start = GST_DEBUG_FUNCPTR (dlb_ac3dec_start);
  audio_decoder_class->stop = GST_DEBUG_FUNCPTR (dlb_ac3dec_stop);
  audio_decoder_class->set_format = GST_DEBUG_FUNCPTR (dlb_ac3dec_set_format);
  audio_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (dlb_ac3dec_handle_frame);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &dlb_ac3dec_src_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &dlb_ac3dec_sink_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Dolby Atmos Decoder for DCX", "Codec/Decoder/Audio",
      "Decode EAC3 and AC3 audio stream",
      "LG Multimedia Team");

  /* install properties */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_OUT_MODE,
      g_param_spec_int ("out-mode", "Output mode", "Output mode for DCX decoder\n \
              \t\t(0) - 5.1 output channel configuration\n \
              \t\t(1) - 7.1 output channel configuration\n \
              \t\t(2) - 5.1.2 output channel configuration\n \
              \t\t(3) - 5.1.4 output channel configuration\n \
              \t\t(4) - 7.1.2 output channel configuration\n \
              \t\t(5) - 7.1.4 output channel configuration",
          DCX_OUTCHANCONFIG_51, DCX_OUTCHANCONFIG_714, DCX_OUTCHANCONFIG_51,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_2CH_DOWNMIX,
      g_param_spec_boolean ("out-2ch-downmix", "Downmix to 2 channels",
          "Downmix the output to 2 channels", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_OUT_DATATYPE,
      g_param_spec_int ("out-datatype", "Output datatype",  "Datatype for DCX decoder",
          G_MININT32, G_MAXINT32, 0,
           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
 
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_OUT_BITDEPTH,
      g_param_spec_int ("out-bit-depth", "Output Bit-Depth", "Output Bit Depth DCX decoder",
          G_MININT32, G_MAXINT32, 0,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DRC_MODE,
      g_param_spec_int ("drc-mode", "Dynamic Range Compression Modes", "Dynamic Range Compression Modes\n \
              \t\t(0) - Turn on dynamic range compression, using an output reference level of -31 dBFS\n \
              \t\t(1) - Turn on dynamic range compression, using an output reference level of -20 dBFS\n \
              \t\t(2) - Turn off dynamic range compression, i.e.dialnorm value from the bitstream is not taken into account" ,
          DCX_COMP_LINE, DCX_COMP_SUPPRESSION, DCX_COMP_RF,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DRC_BOOST,
      g_param_spec_int ("drc-boost", "Dynamic range boost scale factor", "Dynamic range boost scale factor\n \
              \t\t Amount of amplification for low-level signals",
          0, 100, 100,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DRC_CUT,
      g_param_spec_int ("drc-cut", "Dynamic range cut scale factor", "Dynamic range cut scale factor\n \
              \t\t Amount of amplification for high-level signals",
          0, 100, 100,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_EXT_TOP_SIDE_MODE,
      g_param_spec_int ("top-side-mode", "Extended Top Side Modes", "Extended Top Side Modes\n \
              \t\t(0) - Extended top side channel mode off\n \
              \t\t(1) - Extended top side channel mode with 5.1.2 or 7.1.2 for front height speakers\n \
              \t\t(2) - Extended top side channel mode with 5.1.2 or 7.1.2 for rear height speakers\n \
              \t\t(3) - Extended top side channel mode with 5.1.2 or 7.1.2 for middle height speakers",
          DCX_EXT_TOP_SIDE_MODE_OFF, DCX_EXT_TOP_SIDE_MODE_MIDDLE, DCX_EXT_TOP_SIDE_MODE_OFF,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HEIGHT_FRNT_GN,
      g_param_spec_double ("height-frnt-gain", "Front height downmix gain control", "Front height downmix gain control",
          (gdouble)0.5, (gdouble)1.0, (gdouble)0.707,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HEIGHT_REAR_GN,
      g_param_spec_double ("height-rear-gain", "Rear height downmix gain control", "Rear height downmix gain control",
          (gdouble)0.5, (gdouble)1.0, (gdouble)0.707,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE)));

  gst_tag_register ("object-audio", GST_TAG_FLAG_META,
      G_TYPE_BOOLEAN, "object-audio tag",
      "a tag that indicates if object audio is present", NULL);
}

static void
dlb_ac3dec_init (DlbAc3Dec * ac3dec)
{
  GST_DEBUG("dlb_ac3dec_init");
  ac3dec->tags = gst_tag_list_new_empty ();
  ac3dec->dmx_enable = TRUE;

  ac3dec->prop_vals.drc_mode = DCX_COMP_RF;
  ac3dec->prop_vals.drc_boost = 100;
  ac3dec->prop_vals.drc_cut = 100;
  ac3dec->prop_vals.out_datatype = DLB_BUFFER_SHORT_16;
  ac3dec->prop_vals.out_bitdepth = 16;
  ac3dec->prop_vals.out_mode = DCX_OUTCHANCONFIG_51;
  ac3dec->prop_vals.extended_top_side_mode = DCX_EXT_TOP_SIDE_MODE_OFF;
  ac3dec->prop_vals.height_dmx_front_gain = 0.707f;
  ac3dec->prop_vals.height_dmx_rear_gain = 0.707f;
  ac3dec->prop_vals.out_2ch_downmix = FALSE;

  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (ac3dec), TRUE);
  gst_audio_decoder_set_estimate_rate (GST_AUDIO_DECODER (ac3dec), TRUE);
  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER_CAST
      (ac3dec), TRUE);

  GST_PAD_SET_ACCEPT_TEMPLATE (GST_AUDIO_DECODER_SINK_PAD (ac3dec));
}

static void
dlb_ac3dec_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  DlbAc3Dec *ac3dec = DLB_AC3DEC (object);

  GST_OBJECT_LOCK (ac3dec);
  switch (property_id) {
    case PROP_OUT_MODE:
      ac3dec->prop_vals.out_mode = g_value_get_int (value);
      break;
    case PROP_2CH_DOWNMIX:
      ac3dec->prop_vals.out_2ch_downmix = g_value_get_boolean (value);
      break;
    case PROP_OUT_DATATYPE:
      ac3dec->prop_vals.out_datatype = g_value_get_int (value);
      break;
    case PROP_OUT_BITDEPTH:
      ac3dec->prop_vals.out_bitdepth = g_value_get_int (value);
      break;
    case PROP_DRC_MODE:
      ac3dec->prop_vals.drc_mode = g_value_get_int (value);
      break;
    case PROP_DRC_CUT:
      ac3dec->prop_vals.drc_cut = g_value_get_int (value);
      break;
    case PROP_DRC_BOOST:
      ac3dec->prop_vals.drc_boost = g_value_get_int (value);
      break;
    case PROP_EXT_TOP_SIDE_MODE:
      ac3dec->prop_vals.extended_top_side_mode = g_value_get_int (value);
      break;
    case PROP_HEIGHT_FRNT_GN:
      ac3dec->prop_vals.height_dmx_front_gain = g_value_get_double (value);
      break;
    case PROP_HEIGHT_REAR_GN:
      ac3dec->prop_vals.height_dmx_rear_gain = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (ac3dec);
}

static void
dlb_ac3dec_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  DlbAc3Dec *ac3dec = DLB_AC3DEC (object);

  GST_DEBUG (ac3dec, "get_property");

  GST_OBJECT_LOCK (ac3dec);

  switch (property_id) {
    case PROP_OUT_MODE:
      g_value_set_int (value, ac3dec->prop_vals.out_mode);
      break;
    case PROP_2CH_DOWNMIX:
      g_value_set_boolean (value, ac3dec->prop_vals.out_2ch_downmix);
      break;
    case PROP_OUT_DATATYPE:
      g_value_set_int (value, ac3dec->prop_vals.out_datatype);
      break;
    case PROP_OUT_BITDEPTH:
      g_value_set_int (value, ac3dec->prop_vals.out_bitdepth);
      break;
    case PROP_DRC_MODE:
      g_value_set_int (value, ac3dec->prop_vals.drc_mode);
      break;
    case PROP_DRC_CUT:
      g_value_set_int (value, ac3dec->prop_vals.drc_cut);
      break;
    case PROP_DRC_BOOST:
      g_value_set_int (value, ac3dec->prop_vals.drc_boost);
      break;
    case PROP_EXT_TOP_SIDE_MODE:
      g_value_set_int (value, ac3dec->prop_vals.extended_top_side_mode);
      break;
    case PROP_HEIGHT_FRNT_GN:
      g_value_set_double (value, ac3dec->prop_vals.height_dmx_front_gain);
      break;
    case PROP_HEIGHT_REAR_GN:
      g_value_set_double (value, ac3dec->prop_vals.height_dmx_rear_gain);
      break;
/*    case PROP_DMX_ENABLE:
      g_value_set_boolean (value, ac3dec->dmx_enable);
      break;*/
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (ac3dec);
}

static void
dlb_ac3dec_finalize (GObject * object)
{
  DlbAc3Dec *ac3dec = DLB_AC3DEC (object);

  GST_DEBUG (ac3dec, "Finalize");

  g_free (ac3dec->metadata_buffer);
  ac3dec->metadata_buffer = NULL;

  gst_tag_list_unref (ac3dec->tags);

  G_OBJECT_CLASS (dlb_ac3dec_parent_class)->finalize (object);
}

static int dcx_dynamic_opts_update (dcx *p_dcx_state,const dcx_options *options) {
    int ret;
    int drc_mode = options->drc_mode;

    ret = dcx_setprocessparam(p_dcx_state, DCX_CTL_COMPMODE_ID,
            (void*) &drc_mode, sizeof(drc_mode));
    if (ret)
    {
        return DCX_EXEC_ERR_INVAL_PARAM;
    }

    ret = dcx_setprocessparam(p_dcx_state, DCX_CTL_DRCSCALEHIGH_ID,
            (void*) &options->drc_cut, sizeof(options->drc_cut));
    if (ret)
    {
        return DCX_EXEC_ERR_INVAL_PARAM;
    }

    ret = dcx_setprocessparam(p_dcx_state, DCX_CTL_DRCSCALELOW_ID,
            (void*) &options->drc_boost, sizeof(options->drc_boost));
    if (ret)
    {
        return DCX_EXEC_ERR_INVAL_PARAM;
    }

    ret = dcx_setprocessparam(p_dcx_state, DCX_CTL_FRONT_STAGE_MIX_ENABLE_ID,
            (void*) &options->front_stage_mix_enable, sizeof(options->front_stage_mix_enable));
    if (ret)
    {
        return DCX_EXEC_ERR_INVAL_PARAM;
    }
    ret = dcx_setprocessparam(p_dcx_state, DCX_CTL_EXT_HEIGHT_DMX_FRONT_GAIN_ID,
            (void*) &options->height_dmx_front_gain, sizeof(options->height_dmx_front_gain));
    if (ret)
    {
        return DCX_EXEC_ERR_INVAL_PARAM;
    }
    ret = dcx_setprocessparam(p_dcx_state, DCX_CTL_EXT_HEIGHT_DMX_REAR_GAIN_ID,
            (void*) &options->height_dmx_rear_gain, sizeof(options->height_dmx_rear_gain));
    if (ret)
    {
        return DCX_EXEC_ERR_INVAL_PARAM;
    }
    return DCX_EXEC_OK;
}

static int dcx_mem_allocate(DlbAc3Dec *ac3dec,const dcx_query_mem_op *mem_req) {
    /* Allocate static memory */
    ac3dec->p_static_mem = (void *)malloc(mem_req->dcx_static_size);
    if (!ac3dec->p_static_mem)
    {
        goto error;
    }
    /* Allocate dynamic memory */
    ac3dec->p_dcx_scratch_mem = (char *)malloc(mem_req->dcx_dynamic_size);
    if (!ac3dec->p_dcx_scratch_mem)
    {
        goto error;
    }
    /* Allocate output buffer. */
    ac3dec->p_output_mem = (char *)malloc(mem_req->outputbuffersize);
    if (!ac3dec->p_output_mem)
    {
        goto error;
    }
    return DCX_EXEC_OK;
error:
    free(ac3dec->p_static_mem);
    free(ac3dec->p_dcx_scratch_mem);
    free(ac3dec->p_output_mem);

    return DCX_EXEC_ERR_NOMEM;
}

static void dcx_mem_free(DlbAc3Dec *ac3dec) {
    if (!ac3dec)
    {
        return;
    }

    free(ac3dec->p_static_mem);
    free(ac3dec->p_dcx_scratch_mem);
    free(ac3dec->p_output_mem);
}


static void dcx_init_options(DlbAc3Dec *ac3dec, dcx_options *options) {
    memset(options, 0, sizeof(*options));

    /*  - Value from plugin params */
    options->drc_mode = ac3dec->prop_vals.drc_mode;
    options->drc_boost = ac3dec->prop_vals.drc_boost;
    options->drc_cut = ac3dec->prop_vals.drc_cut;
    options->out_datatype = DLB_BUFFER_SHORT_16;
    options->out_bitdepth = 16;
    options->outmode = ac3dec->prop_vals.out_mode;
    options->flushing = 0;
    options->extended_top_side_mode = ac3dec->prop_vals.extended_top_side_mode;
    options->height_dmx_front_gain = ac3dec->prop_vals.height_dmx_front_gain;
    options->height_dmx_rear_gain = ac3dec->prop_vals.height_dmx_rear_gain;
    options->num_frames = DCX_FRAME_DECODE_ALL;
    options->out_2ch_downmix = ac3dec->prop_vals.out_2ch_downmix;
}

static gboolean
dlb_ac3dec_start (GstAudioDecoder * decoder)
{
  DlbAc3Dec *ac3dec = DLB_AC3DEC (decoder);
  GST_DEBUG("dlb_ac3dec_start");

  int ret = 0;

  /* Set the DCX params */
  dcx_init_options(ac3dec, &ac3dec->options);

  ac3dec->dcx_init_parameters.outchanconfig = ac3dec->options.outmode;

  ac3dec->dcx_init_parameters.extended_top_side_mode = ac3dec->options.extended_top_side_mode;
  ret = dcx_query_mem(&ac3dec->dcx_init_parameters, &ac3dec->dcx_memory_requirements);
  if (ret)
  {
      GST_ERROR_OBJECT(ac3dec, "dcx_query_mem failed : %d", ret);
      goto Error;
  }

  ret = dcx_mem_allocate(ac3dec, &ac3dec->dcx_memory_requirements);
  if (ret)
  {
      GST_ERROR_OBJECT(ac3dec, "dcx_mem_allocate failed : %d", ret);
      goto Error;
  }

  ret = dcx_init(
          &ac3dec->dcx_init_parameters,
          ac3dec->p_static_mem,
          ac3dec->p_dcx_scratch_mem,
          &ac3dec->p_dcx_state);
  if (ret)
  {
      GST_ERROR_OBJECT(ac3dec, "dcx_init failed : %d", ret);
      goto Error;
  }

  ret = dcx_dynamic_opts_update(ac3dec->p_dcx_state, &ac3dec->options);
  if (ret)
  {
      GST_ERROR_OBJECT(ac3dec, "dcx_dynamic_opts_update failed : %d", ret);
      goto Error;
  }

  return TRUE;
Error:
  return FALSE;
}

static gboolean
dlb_ac3dec_stop (GstAudioDecoder * decoder)
{
  DlbAc3Dec *ac3dec = DLB_AC3DEC (decoder);
  gboolean res = TRUE;

  GST_DEBUG (ac3dec, "stop");

  /* Close the DCX instance */
  dcx_close(ac3dec->p_dcx_state);

  dcx_mem_free(ac3dec);
  return TRUE;
}

static gboolean
dlb_ac3dec_set_format (GstAudioDecoder * decoder, GstCaps * caps)
{
  DlbAc3Dec *ac3dec = DLB_AC3DEC (decoder);
  GstStructure *s;

  GST_DEBUG (ac3dec, "set_format sink caps: %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);
  g_return_val_if_fail (s, FALSE);
  if (!gst_structure_has_name (s, "audio/x-ac3")
      && !gst_structure_has_name (s, "audio/x-eac3")) {
    GST_WARNING_OBJECT (ac3dec, "Unsupported stream type");
    g_return_val_if_reached (FALSE);
  }

  return TRUE;
}

/**
 * @brief Returns the number of channels for DCX output configuration.
 */
static int dcx_get_num_channels(int outchanconfig)
{
    switch (outchanconfig)
    {
    case DCX_OUTCHANCONFIG_51:
        return 6;
    case DCX_OUTCHANCONFIG_71:
    case DCX_OUTCHANCONFIG_512:
        return 8;
    case DCX_OUTCHANCONFIG_514:
    case DCX_OUTCHANCONFIG_712:
        return 10;
    case DCX_OUTCHANCONFIG_714:
        return 12;
    default:
        return 0;
    };
}


/**
 * @brief Initializes a dlb_buffer using the provided raw memory block.
 *
 * All dlb_buffer struct members are initialized, however the audio data is NOT cleared.
 * The pointers to the PCM data are initialized in a channel-interleaved manner.
 */
static void dcx_setup_dlb_buffer
    (unsigned int nchannels    /**< [in] Number of audio channels. */
    ,int          data_type    /**< [in] PCM data type as defined in dlb_buffer.h */
    ,char        *p_output_mem /**< [in] Memory to be referenced for storing the PCM data.
                                         The pointer must address a memory block large enough
                                         to store all samples of all channels. */
    ,void       **ppdata       /**< [in,out] Memory for pointers into the PCM data per channel. */
    ,dlb_buffer  *p_buf        /**< [out] Buffer descriptor to be initialized. */
    )
{
    unsigned int i;
    size_t bytes_per_sample;

    switch (data_type)
    {
    case DLB_BUFFER_FLOAT:
        bytes_per_sample = sizeof(float);
        break;
    case DLB_BUFFER_INT_LEFT:
        bytes_per_sample = sizeof(int);
        break;
    case DLB_BUFFER_SHORT_16:
        bytes_per_sample = sizeof(short);
        break;
    default:
        bytes_per_sample = 0;
    }

    p_buf->nchannel = nchannels;
    p_buf->nstride = nchannels;

    p_buf->data_type = data_type;
    p_buf->ppdata =  ppdata;
    for (i = 0; i < nchannels; i++)
    {
        ppdata[i] = p_output_mem + i * bytes_per_sample;
    }
}

static GstAudioFormat get_audio_format(int bitdepth) {
    GstAudioFormat audio_format = 0;

    switch (bitdepth)
    {
    case 16:
        audio_format = GST_AUDIO_FORMAT_S16LE;
        break;
    case 24:
        audio_format = GST_AUDIO_FORMAT_S24LE;
        break;
    case 32:
        audio_format = GST_AUDIO_FORMAT_S32LE;
        break;
    default:
        break;
    }
    return audio_format;
}

static gint gst_dlbdec_channels (int outmode, GstAudioChannelPosition * pos, bool out_2ch_downmix)
{
    gint chans = 0;

    /* 2ch downmix then output channel mapping required for 2 ch and ignore outmode */
    if (out_2ch_downmix) {
      if (pos) {
         pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
         pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
       }
       chans += 2;
       return chans;
    }

    switch (outmode) {
        case DCX_OUTCHANCONFIG_51:
            if (pos) { /* L, R, C, LFE, Ls, Rs */
                pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
                pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
                pos[2 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
                pos[3 + chans] = GST_AUDIO_CHANNEL_POSITION_LFE1;
                //pos[4 + chans] = GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT;
                pos[4 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
                //pos[5 + chans] = GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT;
                pos[5 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
            }
            chans += 6;
            break;
        case DCX_OUTCHANCONFIG_71:
            if (pos) { /* L, R, C, LFE, Ls, Rs, Lb, Rb */
                pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
                pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
                pos[2 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
                pos[3 + chans] = GST_AUDIO_CHANNEL_POSITION_LFE1;
                pos[4 + chans] = GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT;
                pos[5 + chans] = GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT;
                pos[6 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
                pos[7 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
            }
            chans += 8;
            break;
        case DCX_OUTCHANCONFIG_512:
            if (pos) { /* L, R, C, LFE, Ls, Rs, Tsl, Tsr */
                pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
                pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
                pos[2 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
                pos[3 + chans] = GST_AUDIO_CHANNEL_POSITION_LFE1;
                pos[4 + chans] = GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT;
                pos[5 + chans] = GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT;
                pos[6 + chans] = GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_LEFT;
                pos[7 + chans] = GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_RIGHT;
            }
            chans += 8;
            break;
        case DCX_OUTCHANCONFIG_514:
            if (pos) {
                /* L, R, C, LFE, Ls, Rs, Tfl, Tfr, Tbl, Tbr */
                pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
                pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
                pos[2 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
                pos[3 + chans] = GST_AUDIO_CHANNEL_POSITION_LFE1;
                pos[4 + chans] = GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT;
                pos[5 + chans] = GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT;
                pos[6 + chans] = GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT;
                pos[7 + chans] = GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT;
                pos[8 + chans] = GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT;
                pos[9 + chans] = GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT;
            }
            chans += 10;
            break;
        case DCX_OUTCHANCONFIG_712:
            if (pos) {
                /* L, R, C, LFE, Ls, Rs, Lb, Rb, Tsl, Tsr */
                pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
                pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
                pos[2 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
                pos[3 + chans] = GST_AUDIO_CHANNEL_POSITION_LFE1;
                pos[4 + chans] = GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT;
                pos[5 + chans] = GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT;
                pos[6 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
                pos[7 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
                pos[8 + chans] = GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_LEFT;
                pos[9 + chans] = GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_RIGHT;
            }
            chans += 10;
            break;
        case DCX_OUTCHANCONFIG_714:
            if (pos) {
                /* L, R, C, LFE, Ls, Rs, Lb, Rb, Tfl, Tfr, Tbl, Tbr */
                pos[0 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
                pos[1 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
                pos[2 + chans] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
                pos[3 + chans] = GST_AUDIO_CHANNEL_POSITION_LFE1;
                pos[4 + chans] = GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT;
                pos[5 + chans] = GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT;
                pos[6 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
                pos[7 + chans] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
                pos[8 + chans] = GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT;
                pos[9 + chans] = GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT;
                pos[10 + chans] = GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT;
                pos[11 + chans] = GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT;
            }
            chans += 12;
            break;
        default:
            break;
    };
    return chans;
}

static void renegotiate (DlbAc3Dec * ac3dec) {
    GstAudioInfo audio_info;
    GstAudioChannelPosition from[MAX_OUT_CH], to[MAX_OUT_CH];
    gint channels = 0;
    GstAudioFormat audio_format = 0;

    audio_format = get_audio_format(ac3dec->options.out_bitdepth);
    channels = gst_dlbdec_channels(ac3dec->dcx_init_parameters.outchanconfig, from, ac3dec->prop_vals.out_2ch_downmix);

    memcpy (to, from, sizeof (GstAudioChannelPosition) * channels);
    gst_audio_channel_positions_to_valid_order (to, channels);

    GST_DEBUG("renegotiate : channels : %d", channels);
    gst_audio_info_init (&audio_info);
    gst_audio_info_set_format (&audio_info, audio_format,
        ac3dec->dcx_output_parameters.pcmsamplerate, channels, to);
    gst_audio_decoder_set_output_format (GST_AUDIO_DECODER (ac3dec), &audio_info);
}

void fill_2ch_downmix_outbuffer(int data_type, short arr_samp_16[], int arr_samp[], int pcmwordtype, char **outdata) {
    char x0, x1;
    /* calculate resultant rear and left channel value for 2 ch downmix*/
    if(data_type != DLB_BUFFER_SHORT_16) {
        /* Result_L = ( 0dB X L + -3dB X C + -3dB X Rear_L ) X -3dB
        = ( 1.0 X L + 0.707 X C + 0.707 X Rear_L ) X 0.707 */
        int res_l = (1.0 * arr_samp[0] + 0.707 * arr_samp[2] + 0.707 * arr_samp[4]) * 0.707;
        /* Result_R = ( 0dB X R + -3dB X C + -3dB X Rear_R ) X -3dB
        = ( 1.0 X R + 0.707 X C + 0.707 X Rear_R ) X 0.707 */
        int res_r = (1.0 * arr_samp[1] + 0.707 * arr_samp[2] + 0.707 * arr_samp[5]) * 0.707;
        x0 = res_l >> 24;
        x1 = (res_l >> 16) & 0xff;
        if (pcmwordtype == PCMINT16) {
            **outdata = x1;
            *outdata += 1;
            **outdata = x0;
            *outdata += 1;
        }
        x0 = res_r >> 24;
        x1 = (res_r >> 16) & 0xff;
        if (pcmwordtype == PCMINT16) {
            **outdata = x1;
            *outdata += 1;
            **outdata = x0;
            *outdata += 1;
        }
    } else {
        short res_l = (1.0 * arr_samp_16[0] + 0.707 * arr_samp_16[2] + 0.707 * arr_samp_16[4]) * 0.707;
        short res_r = (1.0 * arr_samp_16[1] + 0.707 * arr_samp_16[2] + 0.707 * arr_samp_16[5]) * 0.707;
        x1 = res_l & 0xff;
        x0 = (res_l >> 8) & 0xff;
        if (pcmwordtype == PCMINT16) {
            **outdata = x1;
            *outdata += 1;
            **outdata = x0;
            *outdata += 1;
        }
        x1 = res_r & 0xff;
        x0 = (res_r >> 8) & 0xff;
        if (pcmwordtype == PCMINT16) {
            **outdata = x1;
            *outdata += 1;
            **outdata = x0;
            *outdata += 1;
        }
    }
}

int process_pcm_data(DlbAc3Dec *p_pcmoutbuf, const unsigned int nblocks, int channels,
      const dlb_buffer *p_pcmchbfds, GstBuffer *outbuf)
{
    int pcmwordtype = PCMINT16;
    int ch;
    int s;
    int samp;
    short samp_16;
    /* array to store for 2 ch downmix intermediate channel values */
    int arr_samp[channels];
    short arr_samp_16[channels];
    int *p_samp[MAX_OUT_CH];
    short *p_samp_16[MAX_OUT_CH];
    char x0, x1, x2, x3;
    int status = 0;
    static int count = 0;
    GstMapInfo outmap;
    char *outdata;
    gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE);
    outdata = (char *)outmap.data;
    int totalsize=0;

    /* Assign the output buffers */
    if(p_pcmchbfds->data_type != DLB_BUFFER_SHORT_16) {
        for (ch = 0; ch < channels; ch++) {
            p_samp[ch] = (int *) p_pcmchbfds->ppdata[ch];
        }
    } else {
        for (ch = 0; ch < channels; ch++) {
            p_samp_16[ch] = (short *) p_pcmchbfds->ppdata[ch];
        }
    }

    /* The samples are interleaved */
    if(p_pcmchbfds->nstride != 1) {
        for (s = 0; s < (int)(nblocks); s++) {
            for (ch = 0; ch < channels; ch++) {
                if(p_pcmchbfds->data_type != DLB_BUFFER_SHORT_16) {
                    samp = p_samp[ch][s * p_pcmchbfds->nstride];
                    /* Round according to PCM output bits */
                    if (pcmwordtype == PCMINT16) {
                        /* y = (int)(x + 0.5) */
                        if (samp <= INT_MAX - 0x8000) {
                            samp += 0x8000;
                        }
                    } else if (pcmwordtype == PCMINT24) {
                        if (samp <= INT_MAX - 0x80) {
                            samp += 0x80;
                        }
                    }

                    if (p_pcmoutbuf->prop_vals.out_2ch_downmix && channels > 2) {
                        arr_samp[ch] = samp;
                    } else {
                        x0 = samp >> 24;
                        x1 = (samp >> 16) & 0xff;
                        x2 = (samp >> 8) & 0xff;
                        x3 = (samp) & 0xff;
                        if (pcmwordtype == PCMINT16) {
                            *outdata++ = x1;
                            *outdata++ = x0;
                        } else if (pcmwordtype == PCMINT24) {
                            *outdata++ = x2;
                            *outdata++ = x1;
                            *outdata++ = x0;
                        } else {
                            *outdata++ = x3;
                            *outdata++ = x2;
                            *outdata++ = x1;
                            *outdata++ = x0;
                        }
                    }
                } else {
                    samp_16 = p_samp_16[ch][s * p_pcmchbfds->nstride];
                    if (p_pcmoutbuf->prop_vals.out_2ch_downmix && channels > 2) {
                        arr_samp_16[ch] = samp_16;
                    } else {
                        x1 = samp_16 & 0xff;
                        x0 = (samp_16 >> 8) & 0xff;
                        if (pcmwordtype == PCMINT16) {
                            *outdata++ = x1;
                            *outdata++ = x0;
                        } else {
                            //printf(stderr, " Output required 24 bit depth conflict with set the output data type DLB_BUFFER_SHORT_16 ! \n", status);
                        }
                    }
                }
            }
            if (p_pcmoutbuf->prop_vals.out_2ch_downmix && channels > 2) {
                fill_2ch_downmix_outbuffer(p_pcmchbfds->data_type, arr_samp_16, arr_samp, pcmwordtype, &outdata);
            }
        }
    } else {
        /* The samples are non-interleaved */
        for (ch = 0; ch < channels; ch++) {
            for (s = 0; s < (int)(nblocks); s++) {
                if(p_pcmchbfds->data_type != DLB_BUFFER_SHORT_16) {
                    samp = p_samp[ch][s * p_pcmchbfds->nstride];
                    /* Round according to PCM output bits */
                    if (pcmwordtype == PCMINT16) {
                        /* y = (int)(x + 0.5) */
                        if (samp <= INT_MAX - 0x8000) {
                            samp += 0x8000;
                        }
                    } else if (pcmwordtype == PCMINT24) {
                        if (samp <= INT_MAX - 0x80) {
                            samp += 0x80;
                        }
                    }

                    if (p_pcmoutbuf->prop_vals.out_2ch_downmix && channels > 2) {
                        arr_samp[ch] = samp;
                    } else {
                        x0 = samp >> 24;
                        x1 = (samp >> 16) & 0xff;
                        x2 = (samp >> 8) & 0xff;
                        x3 = (samp) & 0xff;

                        if (pcmwordtype == PCMINT16) {
                            *outdata++ = x1;
                            *outdata++ = x0;
                        } else if (pcmwordtype == PCMINT24) {
                            *outdata++ = x2;
                            *outdata++ = x1;
                            *outdata++ = x0;
                        } else {
                            *outdata++ = x3;
                            *outdata++ = x2;
                            *outdata++ = x1;
                            *outdata++ = x0;
                        }
                    }
                } else {
                    samp_16 = p_samp_16[ch][s * p_pcmchbfds->nstride];
                    if (p_pcmoutbuf->prop_vals.out_2ch_downmix && channels > 2) {
                        arr_samp_16[ch] = samp_16;
                    } else {
                        x1 = samp_16 & 0xff;
                        x0 = (samp_16 >> 8) & 0xff;

                        if (pcmwordtype == PCMINT16) {
                            *outdata++ = x1;
                            *outdata++ = x0;
                        } else {
                            //printf(stderr, " Output required 24 bit depth conflict with set the output data type DLB_BUFFER_SHORT_16 ! \n", status);
                        }
                    }
                }
            }
        }
        if (p_pcmoutbuf->prop_vals.out_2ch_downmix && channels > 2) {
            fill_2ch_downmix_outbuffer(p_pcmchbfds->data_type, arr_samp_16, arr_samp, pcmwordtype, &outdata);
        }
    }
    gst_buffer_unmap (outbuf, &outmap);
    return (0);
}

static GstFlowReturn
dlb_ac3dec_handle_frame (GstAudioDecoder * decoder, GstBuffer * inbuf)
{
  DlbAc3Dec *ac3dec = DLB_AC3DEC (decoder);
  GstBuffer *outbuf = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo map;
  dlb_buffer        dcx_output_audiodata;
  size_t bytesconsumed = 0;

  /* no fancy draining */
  if (G_UNLIKELY(!inbuf))
  {
    GST_DEBUG("handle_frame buffer NULL");
    return gst_audio_decoder_finish_frame(ac3dec, NULL, 1);
  }

  if(!gst_buffer_map(inbuf, &map, GST_MAP_READ))
  {
    GST_ERROR_OBJECT(ac3dec, "buffer map failed");
    return GST_FLOW_ERROR;
  }

  if (map.size > 0) {
    /* Feed the decoder. */
    ret = dcx_add_frame(
            ac3dec->p_dcx_state,
            map.size == 0 ? NULL : map.data,
            (unsigned int)map.size,
            (unsigned int *) &bytesconsumed);
    if (ret)
    {
        GST_ERROR_OBJECT(ac3dec, "dcx_add_frame failed : %d", ret);
        ret = GST_FLOW_ERROR;
        goto Error;
    }
    //assert(bytesconsumed == map.size);
    GstMapInfo outmap;

    dcx_setup_dlb_buffer(dcx_get_num_channels(ac3dec->dcx_init_parameters.outchanconfig)
      ,ac3dec->options.out_datatype
      ,ac3dec->p_output_mem
      ,ac3dec->ap_ch_mem
      ,&dcx_output_audiodata
      );

    ret = dcx_process_frame(
           ac3dec->p_dcx_state,
           &ac3dec->dcx_output_parameters,
           &dcx_output_audiodata);
    if (ret)
    {
        GST_ERROR_OBJECT(ac3dec, "dcx_process_frame failed : %d", ret);
        ret = GST_FLOW_ERROR;
        goto Error;
    }

    GST_DEBUG("removed sink event Input format : %d, active channels: %d, samples : %d",ac3dec->dcx_output_parameters.input_format, ac3dec->dcx_output_parameters.active_output_channels, ac3dec->dcx_output_parameters.samples);
    /* for 2ch-downmix and input format is stereo then channels will be 2 */
    int out_channels = 0;
    if (ac3dec->prop_vals.out_2ch_downmix || ac3dec->dcx_output_parameters.input_format == 1) {
        /* Input format/Output channel configuration          5.1 5.1.x 7.1 7.1.x
           ===========================================================================
           Dolby Digital Plus with Stereo DCX_INPUT_STEREO(1) 2.0 2.0   2.0 2.0 */
        out_channels = 2;
    } else {
        out_channels = dcx_get_num_channels(ac3dec->dcx_init_parameters.outchanconfig);
    }

    /* Buffer size = out_channels * (out bitdepth/8) * no. of samples */
    outbuf = gst_buffer_new_and_alloc(sizeof(char) * (ac3dec->options.out_bitdepth/8) * out_channels * ac3dec->dcx_output_parameters.samples);

    int err = 0;
    /* Do additional processing and write output files */
    err =  process_pcm_data(
              ac3dec,
              ac3dec->dcx_output_parameters.samples,
              (ac3dec->dcx_output_parameters.input_format == 1) ? 2 : dcx_get_num_channels(ac3dec->dcx_init_parameters.outchanconfig)/*Output channels*/,
              &dcx_output_audiodata,
              outbuf);
    if (err)
    {
      GST_ERROR_OBJECT(ac3dec, "processpcm data failed : %d", err);
      /* clear output buffer */
      gst_buffer_unref(outbuf);
    }

    renegotiate(ac3dec);
  } else {
    GST_WARNING_OBJECT(ac3dec, "map size is zero");
  }
  gst_buffer_unmap(inbuf, &map);

  ret = gst_audio_decoder_finish_frame (ac3dec, outbuf, 1);
Error:
  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (dlb_ac3dec_debug_category, "dlbdec", 0,
      "debug category for Dolby Atmos decoder element");

  if (!gst_element_register (plugin, "dlbdec", GST_RANK_PRIMARY + 2,
          DLB_TYPE_AC3DEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dlbdec,
    "Dolby Atmos Decoder for Hyundai ccIC27/ccRC/ccIC, using DCX (IDK v1.3.1) by LGE",
    plugin_init, VERSION, "LGPL", "LG GStreamer Plugins", "Unknown package origin")
