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

typedef struct _GstMaruDec
{
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;

  CodecContext *context;
  CodecDevice *dev;

  union {
    struct {
      gint width, height;
      gint clip_width, clip_height;
      gint par_n, par_d;
      gint fps_n, fps_d;
      gint old_fps_n, old_fps_d;
      gboolean interlaced;

      enum PixelFormat pix_fmt;
    } video;
    struct {
      gint channels;
      gint samplerate;
      gint depth;
    } audio;
  } format;

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


  /* GstSegment can be used for two purposes:
   * 1. performing seeks (handling seek events)
   * 2. tracking playback regions (handling newsegment events)
   */
  GstSegment segment;

  GstTSInfo ts_info[MAX_TS_MASK + 1];
  gint ts_idx;

  /* reverse playback queue */
  GList *queued;

  // decode result
  bool is_last_buffer;
  int mem_offset;
  bool is_using_new_decode_api;
} GstMaruDec;

typedef struct {
  int
  (*init) (CodecContext *ctx, CodecElement *codec, CodecDevice *dev);
  void
  (*deinit) (CodecContext *ctx, CodecDevice *dev);
  int
  (*decode_video) (GstMaruDec *marudec, uint8_t *in_buf, int in_size,
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
} Interface;

extern Interface *interface;

extern Interface *interface_version_2;
extern Interface *interface_version_3;

#endif /* __GST_MARU_INTERFACE_H__ */