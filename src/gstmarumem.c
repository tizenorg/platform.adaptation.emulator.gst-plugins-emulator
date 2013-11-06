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

#include "gstmarumem.h"

/*
 *  codec data such as codec name, longname, media type and etc.
 */
static int
_codec_info_data (CodecElement *codec, uint8_t *device_buf)
{
  int size = 0;

  CODEC_LOG (DEBUG, "enter, %s\n", __func__);

  CODEC_LOG (DEBUG, "type: %d, name: %s\n", codec->codec_type, codec->name);
  memcpy (device_buf, &codec->codec_type, sizeof(codec->codec_type));
  size = sizeof(codec->codec_type);

  memcpy (device_buf + size, codec->name, sizeof(codec->name));
  size += sizeof(codec->name);

  CODEC_LOG (DEBUG, "leave, %s\n", __func__);

  return size;
}

void
_codec_init_meta_to (CodecContext *ctx,
                      CodecElement *codec,
                      uint8_t *device_buf)
{
  int size = 0;

  CODEC_LOG (DEBUG, "enter, %s\n", __func__);

  size = _codec_info_data (codec, device_buf);

  CODEC_LOG (INFO, "name: %s, media type: %s\n",
    codec->name, codec->media_type ? "AUDIO" : "VIDEO");

  if (codec->media_type == AVMEDIA_TYPE_AUDIO) {
    CODEC_LOG (DEBUG,
      "before init. audio sample_fmt: %d\n", ctx->audio.sample_fmt);
    CODEC_LOG (DEBUG,
      "before init. audio block_align: %d\n", ctx->audio.block_align);
  }

  CODEC_LOG (DEBUG, "init. write data to qemu, size: %d\n", size);
  memcpy (device_buf + size, ctx, sizeof(CodecContext) - 12);
  size += (sizeof(CodecContext) - 12);
  memcpy (device_buf + size, ctx->codecdata, ctx->codecdata_size);

  CODEC_LOG (DEBUG, "leave, %s\n", __func__);
}

int
_codec_init_meta_from (CodecContext *ctx,
                        int media_type,
                        uint8_t *device_buf)
{
  int ret = 0, size = 0;

  CODEC_LOG (DEBUG, "after init. read data from device.\n");

  memcpy (&ret, device_buf, sizeof(ret));
  size = sizeof(ret);
  if (!ret) {
    if (media_type == AVMEDIA_TYPE_AUDIO) {
      AudioData audio = { 0 };

#if 0
      memcpy(&audio, device_buf + size, sizeof(AudioData));
      ctx->audio.sample_fmt = audio.sample_fmt;
      ctx->audio.frame_size = audio.frame_size;
      ctx->audio.bits_per_sample_fmt = audio.bits_per_sample_fmt;
#endif
      CODEC_LOG (INFO,
        "audio sample_fmt: %d\n", *(int *)(device_buf + size));

      memcpy(&ctx->audio.sample_fmt, device_buf + size, sizeof(audio.sample_fmt));
      size += sizeof(audio.sample_fmt);
      memcpy(&ctx->audio.frame_size, device_buf + size, sizeof(audio.frame_size));
      size += sizeof(audio.frame_size);
      memcpy(&ctx->audio.bits_per_sample_fmt, device_buf + size, sizeof(audio.bits_per_sample_fmt));

      CODEC_LOG (INFO,
        "after init. audio sample_fmt: %d\n", ctx->audio.sample_fmt);
    }
  } else {
    CODEC_LOG (ERR, "failed to open codec context\n");
  }

  return ret;
}

void
_codec_decode_video_meta_to (int in_size, int idx, int64_t in_offset, uint8_t *device_buf)
{
  memcpy (device_buf, &in_size, sizeof(in_size));
  memcpy (device_buf + sizeof(in_size), &idx, sizeof(idx));
  memcpy (device_buf + sizeof(idx), &in_offset, sizeof(in_offset));
}

void
_codec_decode_video_inbuf (uint8_t *in_buf, int in_size,
                              uint8_t *device_buf)
{
  int size = 0;

  memcpy(device_buf, &in_size, sizeof(in_size));
  size = sizeof(in_size);
  if (in_size > 0 ) {
    memcpy (device_buf + size, in_buf, in_size);
  }

  CODEC_LOG (DEBUG, "decode_video. inbuf_size: %d\n", in_size);
}


int
_codec_decode_video_meta_from (VideoData *video,
                              int *got_picture_ptr,
                              uint8_t *device_buf)
{
  int len = 0, size = 0;

  CODEC_LOG (DEBUG, "decode_video. read data from qemu.\n");

  memcpy (&len, device_buf, sizeof(len));
  size = sizeof(len);
  memcpy (got_picture_ptr,
    device_buf + size, sizeof(*got_picture_ptr));
  size += sizeof(*got_picture_ptr);
  memcpy (video, device_buf + size, sizeof(VideoData));

  CODEC_LOG (DEBUG, "decode_video. len: %d, have_data: %d\n",
    len, *got_picture_ptr);

  return len;
}

void
_codec_decode_audio_meta_to (int in_size, uint8_t *device_buf)
{
  memcpy (device_buf, &in_size, sizeof(in_size));
}


void
_codec_decode_audio_inbuf (uint8_t *in_buf, int in_size, uint8_t *device_buf)
{
  int size = 0;

  memcpy (device_buf, &in_size, sizeof(in_size));
  size = sizeof(in_size);
  if (in_size > 0) {
    memcpy (device_buf + size, in_buf, in_size);
  }

  CODEC_LOG (DEBUG, "decode_audio. inbuf_size: %d\n", in_size);
}

int
_codec_decode_audio_meta_from (AudioData *audio, int *frame_size_ptr,
                                uint8_t *device_buf)
{
  int len = 0, size = 0;

  CODEC_LOG (DEBUG, "decode_audio. read data from device.\n");

  memcpy (&audio->sample_rate,
    device_buf, sizeof(audio->sample_rate));
  size = sizeof(audio->sample_rate);
  memcpy (&audio->channels,
    device_buf + size, sizeof(audio->channels));
  size += sizeof(audio->channels);
  memcpy (&audio->channel_layout,
    device_buf + size, sizeof(audio->channel_layout));
  size += sizeof(audio->channel_layout);
  memcpy (&len, device_buf + size, sizeof(len));
  size += sizeof(len);
  memcpy (frame_size_ptr, device_buf + size, sizeof(*frame_size_ptr));

  CODEC_LOG (DEBUG, "decode_audio. len: %d, frame_size: %d\n",
          len, (*frame_size_ptr));

  return len;
}

void
_codec_decode_audio_outbuf (int outbuf_size, int16_t *samples, uint8_t *device_buf)
{
  CODEC_LOG (DEBUG, "decode_audio. read outbuf %d\n", outbuf_size);
  memcpy (samples, device_buf, outbuf_size);
}

void
_codec_encode_video_meta_to (int in_size, int64_t in_timestamp, uint8_t *device_buf)
{
  CODEC_LOG (DEBUG, "encode_video. write data to device.\n");

  memcpy (device_buf, &in_size, sizeof(in_size));
  memcpy (device_buf + sizeof(in_size), &in_timestamp, sizeof(in_timestamp));
}

void
_codec_encode_video_inbuf (uint8_t *in_buf, int in_size, uint8_t *device_buf)
{
  int size = 0;

  memcpy ((uint8_t *)device_buf, &in_size, sizeof(in_size));
  size += sizeof(in_size);
  if (in_size > 0) {
    memcpy (device_buf + size, in_buf, in_size);
  }
  CODEC_LOG (DEBUG, "encode_video. inbuf_size: %d\n", in_size);
}

void
_codec_encode_video_outbuf (int len, uint8_t *out_buf, uint8_t *device_buf)
{
//  int len, size;

  CODEC_LOG (DEBUG, "encode_video. read data from device.\n");

//  memcpy (&len, device_buf, sizeof(len));
//  size = sizeof(len);
  memcpy (out_buf, device_buf, len);

//  return len;
}

void
_codec_encode_audio_meta_to (int max_size, int in_size, uint8_t *device_buf)
{
  int size = 0;

  CODEC_LOG (DEBUG, "encode_audio. write data to device.\n");

  memcpy (device_buf, &in_size, sizeof(in_size));
  size = sizeof(in_size);
  memcpy (device_buf + size, &max_size, sizeof(max_size));
}

void
_codec_encode_audio_inbuf (uint8_t *in_buf, int in_size, uint8_t *device_buf)
{
  int size = 0;

  memcpy (device_buf, &in_size, sizeof(in_size));
  size = sizeof(in_size);
  if (in_size > 0) {
    memcpy (device_buf + size, in_buf, in_size);
  }
  CODEC_LOG (DEBUG, "encode_audio. inbuf_size: %d\n", in_size);
}

int
_codec_encode_audio_outbuf (uint8_t *out_buf, uint8_t *device_buf)
{
  int len, size;

  CODEC_LOG (DEBUG, "encode_audio. read data from device\n");

  memcpy (&len, (uint8_t *)device_buf, sizeof(len));
  size = sizeof(len);
  memcpy (out_buf, (uint8_t *)device_buf + size, len);

  return len;
}
