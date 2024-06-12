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

#ifndef _DLB_AC3DEC_H_
#define _DLB_AC3DEC_H_

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>
#include "dcx_api.h"
#include "dlb_buffer.h"
#include <stdbool.h>

G_BEGIN_DECLS
#define DLB_TYPE_AC3DEC   (dlb_ac3dec_get_type())
#define DLB_AC3DEC(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),DLB_TYPE_AC3DEC,DlbAc3Dec))
#define DLB_AC3DEC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),DLB_TYPE_AC3DEC,GstAc3DecClass))
#define DLB_IS_AC3DEC(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),DLB_TYPE_AC3DEC))
#define DLB_IS_AC3DEC_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),DLB_TYPE_AC3DEC))
typedef struct _DlbAc3Dec DlbAc3Dec;
typedef struct _DlbAc3DecClass DlbAc3DecClass;

#define DCX_EXEC_OK               0
#define DCX_EXEC_ERR_GENERAL      1
#define DCX_EXEC_ERR_NOMEM        2
#define DCX_EXEC_ERR_INVAL_PARAM  3
#define DCX_EXEC_ERR_PROC         4

#define DEFAULT_DATATYPE      DLB_BUFFER_FLOAT
#define DCX_FRAME_DECODE_ALL (-1)

#define MAX_OUT_CH 12
typedef enum
{
  PCMINT16 = 0,
  PCMINT24 = 24,
  PCMINT32 = 32,
} PCMWORDTYPE;

/**
 * @brief Command line options.
 *
 * Structure is intended to be filled out by command line parser.
 */
typedef struct dcx_options_s
{
    int outmode;
    int drc_mode;
    int drc_boost;
    int drc_cut;
    int verbose;
    int out_bitdepth;
    int out_datatype;
    int front_stage_mix_enable;
    int flushing;
    int extended_top_side_mode;
    float height_dmx_front_gain;
    float height_dmx_rear_gain;
    int num_frames;
    bool out_2ch_downmix;
} dcx_options;

/* Property values */
typedef struct prop_s {
    int out_mode;
    int out_bitdepth;
    int out_datatype;
    int drc_mode;
    int drc_boost;
    int drc_cut;
    int extended_top_side_mode;
    float height_dmx_front_gain;
    float height_dmx_rear_gain;
    bool out_2ch_downmix;
} prop_options;

struct _DlbAc3Dec
{
  GstAudioDecoder base_ac3dec;

  prop_options prop_vals;              /**< Element properties */

  dlb_buffer *outbuf;
  dcx_options options;              /**< Inital set of cli options for DCX initalization. */

  dcx_query_ip      dcx_init_parameters;
  dcx_pt_op         dcx_output_parameters;
  dcx_query_mem_op  dcx_memory_requirements;

  dcx *p_dcx_state;
  char *p_static_mem;
  char *p_dcx_scratch_mem;
  char *p_output_mem;                        /**< Raw memory block for storing the output PCM data. */
  void *ap_ch_mem[DCX_MAX_NUM_OUT_CHANNELS]; /**< Separate pointers addressing the PCM data per channel. */
  size_t timestamp;                          /**< Accumulated number of generated output samples. */

  GstAllocator *alloc_dec;
  GstAllocationParams *alloc_params;
  guint8 *metadata_buffer;
  GstTagList *tags;

  /* reorder positions from dolby to gstreamer */
  GstAudioChannelPosition gstpos[16];
  GstAudioChannelPosition dlbpos[16];

  /* maximum output block size in bytes */
  gsize max_output_blocksz;

  /* max output channels */
  gint max_channels;

  /* bytes per sample */
  gsize bps;

  /* target layout (depends on downstream source pad peer Caps) */
  GstAudioFormat output_format;

  /* static params */
  int outmode;

  /* dynamic params */
  gint drc_mode;
  //dlb_udc_drc_settings drc;

  gboolean dmx_enable;
};

struct _DlbAc3DecClass
{
  GstAudioDecoderClass base_ac3dec_class;
};

GType dlb_ac3dec_get_type (void);

G_END_DECLS
#endif
