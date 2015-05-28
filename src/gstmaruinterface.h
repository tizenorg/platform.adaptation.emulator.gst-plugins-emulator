/*
 * GStreamer codec plugin for Tizen Emulator.
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * KiTae Kim <kt920.kim@samsung.com>
 * SeokYeon Hwang <syeon.hwang@samsung.com>
 * YeongKyoon Lee <yeongkyoon.lee@samsung.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
 */

#ifndef __GST_MARU_INTERFACE_H__
#define __GST_MARU_INTERFACE_H__

#include "gstmaru.h"

#define MAX_TS_MASK 0xff

enum CODEC_FUNC_TYPE {
  CODEC_INIT = 0,
  CODEC_DECODE_VIDEO,
  CODEC_ENCODE_VIDEO,
  CODEC_DECODE_AUDIO,
  CODEC_ENCODE_AUDIO,
  CODEC_PICTURE_COPY,
  CODEC_DEINIT,
  CODEC_FLUSH_BUFFERS,
  CODEC_DECODE_VIDEO_AND_PICTURE_COPY, // version 3
};

typedef struct
{
  gint idx;
  GstClockTime timestamp;
  GstClockTime duration;
  gint64 offset;
} GstTSInfo;

typedef struct _GstMaruVidDec
{
  GstVideoDecoder element;

  CodecContext *context;
  CodecDevice *dev;

  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;

  /* current context */
  enum PixelFormat ctx_pix_fmt;
  gint ctx_width;
  gint ctx_height;
  gint ctx_par_n;
  gint ctx_par_d;
  gint ctx_ticks;
  gint ctx_time_d;
  gint ctx_time_n;
  gint ctx_interlaced;
  GstBuffer *palette;

  gboolean opened;
  gboolean discont;
  gboolean clear_ts;

  /* tracking DTS/PTS */
  GstClockTime next_out;

  /* Qos stuff */
  gdouble proportion;
  GstClockTime earliest_time;
  gint64 processed;
  gint64 dropped;

  GstTSInfo ts_info[MAX_TS_MASK + 1];
  gint ts_idx;

  // decode result
  bool is_last_buffer;
  int mem_offset;
  bool is_using_new_decode_api;

  int max_threads;

  GstCaps *last_caps;
} GstMaruVidDec;

typedef struct _GstMaruDec
{
  GstAudioDecoder parent;
  GstElement element;

  /* decoding */
  CodecContext *context;
  CodecDevice *dev;
  gboolean opened;

  /* prevent reopening the decoder on GST_EVENT_CAPS when caps are same as last time. */
  GstCaps *last_caps;

  /* Stores current buffers to push as GstAudioDecoder wants 1:1 mapping for input/output buffers */
  GstBuffer *outbuf;

  /* current output format */
  GstAudioInfo info;
  GstAudioChannelPosition ffmpeg_layout[64];
  gboolean needs_reorder;

  // decode result
  bool is_last_buffer;
  int mem_offset;
  bool is_using_new_decode_api;
} GstMaruDec;

typedef struct _GstMaruAudDec
{
  GstAudioDecoder parent;

  /* prevent reopening the decoder on GST_EVENT_CAPS when caps are same as last time. */
  GstCaps *last_caps;

  /* Stores current buffers to push as GstAudioDecoder wants 1:1 mapping for input/output buffers */
  GstBuffer *outbuf;

  /* current output format */
  GstAudioInfo info;
  GstAudioChannelPosition layout[64];
  gboolean needs_reorder;

  /* decoding */
  CodecContext *context;
  gboolean opened;

  struct {
    gint channels;
    gint sample_rate;
    gint depth;
  } audio;

  CodecDevice *dev;

  GstTSInfo ts_info[MAX_TS_MASK + 1];
  gint ts_idx;
  // decode result
  bool is_last_buffer;
  int mem_offset;
  bool is_using_new_decode_api;

} GstMaruAudDec;


typedef struct {
  int
  (*init) (CodecContext *ctx, CodecElement *codec, CodecDevice *dev);
  void
  (*deinit) (CodecContext *ctx, CodecDevice *dev);
  int
  (*decode_video) (GstMaruVidDec *marudec, uint8_t *in_buf, int in_size,
                    gint idx, gint64 in_offset, GstBuffer **out_buf, int *have_data);
  int
  (*decode_audio) (CodecContext *ctx, int16_t *samples,
                    int *frame_size_ptr, uint8_t *in_buf,
                    int in_size, CodecDevice *dev);
  int
  (*encode_video) (CodecContext *ctx, uint8_t*out_buf,
                    int out_size, uint8_t *in_buf,
                    int in_size, int64_t in_timestamp,
                    int *coded_frame, int *is_keyframe,
                    CodecDevice *dev);
  int
  (*encode_audio) (CodecContext *ctx, uint8_t *out_buf,
                    int out_size, uint8_t *in_buf,
                    int in_size, int64_t timestamp,
                    CodecDevice *dev);
  void
  (*flush_buffers) (CodecContext *ctx, CodecDevice *dev);
  GstFlowReturn
  (*buffer_alloc_and_copy) (GstPad *pad, guint64 offset,
                    guint size, GstCaps *caps, GstBuffer **buf);
  int
  (*get_device_version) (int fd);
  GList *
  (*prepare_elements) (int fd);
  int
  (*get_profile_status) (int fd);
} Interface;

extern Interface *interface;

extern Interface *interface_version_2;
extern Interface *interface_version_3;

#endif /* __GST_MARU_INTERFACE_H__ */
