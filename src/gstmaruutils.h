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

#ifndef __GST_MARU_UTIL_H__
#define __GST_MARU_UTIL_H__

#include "gstmaru.h"

// FFmpeg
#include "audioconvert.h"

/* Audio channel masks */
#define CH_FRONT_LEFT            AV_CH_FRONT_LEFT
#define CH_FRONT_RIGHT           AV_CH_FRONT_RIGHT
#define CH_FRONT_CENTER          AV_CH_FRONT_CENTER
#define CH_LOW_FREQUENCY         AV_CH_LOW_FREQUENCY
#define CH_BACK_LEFT             AV_CH_BACK_LEFT
#define CH_BACK_RIGHT            AV_CH_BACK_RIGHT
#define CH_FRONT_LEFT_OF_CENTER  AV_CH_FRONT_LEFT_OF_CENTER
#define CH_FRONT_RIGHT_OF_CENTER AV_CH_FRONT_RIGHT_OF_CENTER
#define CH_BACK_CENTER           AV_CH_BACK_CENTER
#define CH_SIDE_LEFT             AV_CH_SIDE_LEFT
#define CH_SIDE_RIGHT            AV_CH_SIDE_RIGHT
#define CH_TOP_CENTER            AV_CH_TOP_CENTER
#define CH_TOP_FRONT_LEFT        AV_CH_TOP_FRONT_LEFT
#define CH_TOP_FRONT_CENTER      AV_CH_TOP_FRONT_CENTER
#define CH_TOP_FRONT_RIGHT       AV_CH_TOP_FRONT_RIGHT
#define CH_TOP_BACK_LEFT         AV_CH_TOP_BACK_LEFT
#define CH_TOP_BACK_CENTER       AV_CH_TOP_BACK_CENTER
#define CH_TOP_BACK_RIGHT        AV_CH_TOP_BACK_RIGHT
#define CH_STEREO_LEFT           AV_CH_STEREO_LEFT
#define CH_STEREO_RIGHT          AV_CH_STEREO_RIGHT

/** Channel mask value used for AVCodecContext.request_channel_layout
    to indicate that the user requests the channel order of the decoder output
    to be the native codec channel order. */
#define CH_LAYOUT_NATIVE         AV_CH_LAYOUT_NATIVE

/* Audio channel convenience macros */
#define CH_LAYOUT_MONO           AV_CH_LAYOUT_MONO
#define CH_LAYOUT_STEREO         AV_CH_LAYOUT_STEREO
#define CH_LAYOUT_2_1            AV_CH_LAYOUT_2_1
#define CH_LAYOUT_SURROUND       AV_CH_LAYOUT_SURROUND
#define CH_LAYOUT_4POINT0        AV_CH_LAYOUT_4POINT0
#define CH_LAYOUT_2_2            AV_CH_LAYOUT_2_2
#define CH_LAYOUT_QUAD           AV_CH_LAYOUT_QUAD
#define CH_LAYOUT_5POINT0        AV_CH_LAYOUT_5POINT0
#define CH_LAYOUT_5POINT1        AV_CH_LAYOUT_5POINT1
#define CH_LAYOUT_5POINT0_BACK   AV_CH_LAYOUT_5POINT0_BACK
#define CH_LAYOUT_5POINT1_BACK   AV_CH_LAYOUT_5POINT1_BACK
#define CH_LAYOUT_7POINT0        AV_CH_LAYOUT_7POINT0
#define CH_LAYOUT_7POINT1        AV_CH_LAYOUT_7POINT1
#define CH_LAYOUT_7POINT1_WIDE   AV_CH_LAYOUT_7POINT1_WIDE
#define CH_LAYOUT_STEREO_DOWNMIX AV_CH_LAYOUT_STEREO_DOWNMIX

GstCaps *gst_maru_codectype_to_video_caps (CodecContext *ctx, const char *name,
    gboolean encode, CodecElement *codec);

GstCaps *gst_maru_codectype_to_audio_caps (CodecContext *ctx, const char *name,
    gboolean encode, CodecElement *codec);


GstCaps *gst_maru_codectype_to_caps (int media_type, CodecContext *ctx,
    const char *name, gboolean encode);

void gst_maru_caps_with_codecname (const char *name, int media_type,
    const GstCaps *caps, CodecContext *ctx);

void gst_maru_caps_with_codectype (int media_type, const GstCaps *caps, CodecContext *ctx);

GstCaps *gst_maru_video_caps_new (CodecContext *ctx, const char *name,
        const char *mimetype, const char *fieldname, ...);

GstCaps *gst_maru_audio_caps_new (CodecContext *ctx, const char *name,
        const char *mimetype, const char *fieldname, ...);

GstCaps *gst_maru_pixfmt_to_caps (enum PixelFormat pix_fmt, CodecContext *ctx, const char *name);

GstCaps *gst_maru_smpfmt_to_caps (int8_t sample_fmt, CodecContext *ctx, const char *name);

GstCaps *gst_maru_codecname_to_caps (const char *name, CodecContext *ctx, gboolean encode);

void gst_maru_init_pix_fmt_info (void);

int gst_maru_avpicture_size (int pix_fmt, int width, int height);

int gst_maru_align_size (int buf_size);

gint gst_maru_smpfmt_depth (int smp_fmt);

#endif
