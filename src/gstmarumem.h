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

#ifndef __GST_MARU_MEM_H__
#define __GST_MARU_MEM_H__

#include "gstmaru.h"

void _codec_init_meta_to (CodecContext *ctx, CodecElement *codec, uint8_t *device_buf);

int _codec_init_meta_from (CodecContext *ctx, int media_type, uint8_t *device_buf);

void _codec_decode_video_meta_to (int in_size, int idx, int64_t in_offset, uint8_t *device_buf);

void _codec_decode_video_inbuf (uint8_t *in_buf, int in_size, uint8_t *device_buf);

int _codec_decode_video_meta_from (VideoData *video, int *got_picture_ptr,
                                  uint8_t *device_buf);

void _codec_decode_audio_meta_to (int in_size, uint8_t *device_buf);


void _codec_decode_audio_inbuf (uint8_t *in_buf, int in_size,
                                  uint8_t *device_buf);

int _codec_decode_audio_meta_from (AudioData *audio, int *frame_size_ptr,
                                  uint8_t *device_buf);

void _codec_decode_audio_outbuf (int outbuf_size, int16_t *samples,
                                  uint8_t *device_buf);

void _codec_encode_video_meta_to (int in_size, int64_t in_timestamp, uint8_t *device_buf);

void _codec_encode_video_inbuf (uint8_t *in_buf, int in_size,
                                  uint8_t *device_buf);

void _codec_encode_video_outbuf (int len, uint8_t *outbuf, uint8_t *device_buf);

void _codec_encode_audio_meta_to (int max_size, int in_size, uint8_t *device_buf);

void _codec_encode_audio_inbuf (uint8_t *in_buf, int in_size, uint8_t *device_buf);

int _codec_encode_audio_outbuf (uint8_t *out_buf, uint8_t *device_buf);

#endif
