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

#ifndef __GST_MARU_H__
#define __GST_MARU_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiodecoder.h>
#include <gst/video/gstvideodecoder.h>
#include "pixfmt.h"

GST_DEBUG_CATEGORY_EXTERN (maru_debug);
#define GST_CAT_DEFAULT maru_debug

G_BEGIN_DECLS

extern int device_version;

enum codec_log_level {
  ERR,
  WARN,
  INFO,
  DEBUG,
};

#define CODEC_DEV   "/dev/brillcodec"
#define CODEC_VER   3

#define CHECK_VERSION(version)        (device_version >= version)

#define CODEC_LOG(level, fmt, ...) \
  do { \
    if (level <= INFO) \
      printf("[gst-maru][%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
  } while (0)

#define FF_INPUT_BUFFER_PADDING_SIZE  8
#define FF_MAX_AUDIO_FRAME_SIZE       192000 // 1 second of 48khz 32bit audio
#define FF_MIN_BUFFER_SIZE            16384

#define GEN_MASK(x) ((1<<(x))-1)
#define ROUND_UP_X(v, x) (((v) + GEN_MASK(x)) & ~GEN_MASK(x))
#define ROUND_UP_2(x) ROUND_UP_X(x, 1)
#define ROUND_UP_4(x) ROUND_UP_X(x, 2)
#define ROUND_UP_8(x) ROUND_UP_X(x, 3)
#define DIV_ROUND_UP_X(v, x) (((v) + GEN_MASK(x)) >> (x))

typedef struct {
  int       fd;
  uint8_t   *buf;
  uint32_t  buf_size;
} CodecDevice;

typedef struct {
  int32_t codec_type;
  int32_t media_type;
  gchar name[32];
  gchar longname[64];
  union {
    int32_t pix_fmts[4];
    int32_t sample_fmts[4];
  };
} __attribute__((packed)) CodecElement;

typedef struct AVRational{
    int num; ///< numerator
    int den; ///< denominator
} AVRational;

typedef struct {
  int32_t width, height;
  int32_t fps_n, fps_d;
  int32_t par_n, par_d;
  int32_t pix_fmt, bpp;
  int32_t ticks_per_frame;
} __attribute__((packed)) VideoData;

typedef struct {
  int32_t channels, sample_rate;
  int32_t block_align, depth;
  int32_t sample_fmt, frame_size;
  int32_t bits_per_sample_fmt, reserved;
  int64_t channel_layout;
} __attribute__((packed)) AudioData;

typedef struct {
  VideoData video;
  AudioData audio;

  int32_t bit_rate;
  int32_t codec_tag;

  int32_t codecdata_size;
  uint8_t *codecdata;

  CodecElement *codec;
  int32_t index;
} CodecContext;

enum CODEC_MEDIA_TYPE {
  AVMEDIA_TYPE_UNKNOWN = -1,
  AVMEDIA_TYPE_VIDEO,
  AVMEDIA_TYPE_AUDIO,
};

enum CODEC_TYPE {
  CODEC_TYPE_UNKNOWN = -1,
  CODEC_TYPE_DECODE,
  CODEC_TYPE_ENCODE,
};

enum SampleFormat {
  SAMPLE_FMT_NONE = -1,
  SAMPLE_FMT_U8,
  SAMPLE_FMT_S16,
  SAMPLE_FMT_S32,
  SAMPLE_FMT_FLT,
  SAMPLE_FMT_DBL,

  SAMPLE_FMT_U8P,
  SAMPLE_FMT_S16P,
  SAMPLE_FMT_S32P,
  SAMPLE_FMT_FLTP,
  SAMPLE_FMT_DBLP,
  SAMPLE_FMT_NB
};

G_END_DECLS
#endif
