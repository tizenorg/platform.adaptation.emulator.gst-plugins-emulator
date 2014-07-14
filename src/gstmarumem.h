/*
 * GStreamer codec plugin for Tizen Emulator.
 *
 * Copyright (C) 2013 - 2014 Samsung Electronics Co., Ltd. All rights reserved.
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

#ifndef __GST_MARU_MEM_H__
#define __GST_MARU_MEM_H__

#include "gstmaru.h"

void codec_init_data_to (CodecContext *, CodecElement *, gpointer);

int codec_init_data_from (CodecContext *, int, gpointer);

void codec_decode_video_data_to (int, int, int64_t, uint8_t *, gpointer);

int codec_decode_video_data_from (int *, VideoData *, gpointer);

void codec_decode_audio_data_to (int, uint8_t *, gpointer);

int codec_decode_audio_data_from (int *, int16_t *, AudioData *, gpointer);

void codec_encode_video_data_to (int, int64_t, uint8_t *, gpointer);

int codec_encode_video_data_from (uint8_t *, int *, int *, gpointer);

void codec_encode_audio_data_to (int, int, uint8_t *, int64_t, gpointer);

int codec_encode_audio_data_from (uint8_t *, gpointer);

#endif
