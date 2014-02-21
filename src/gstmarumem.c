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

#include "gstmarumem.h"

/*
 *  codec data such as codec name, longname, media type and etc.
 */
static int
_codec_info_data (CodecElement *codec, gpointer buffer)
{
  int size = sizeof(size);

  CODEC_LOG (DEBUG, "enter, %s\n", __func__);

  CODEC_LOG (DEBUG, "type: %d, name: %s\n", codec->codec_type, codec->name);
  memcpy (buffer + size, &codec->codec_type, sizeof(codec->codec_type));
  size += sizeof(codec->codec_type);

  memcpy (buffer + size, codec->name, sizeof(codec->name));
  size += sizeof(codec->name);

  CODEC_LOG (DEBUG, "leave, %s\n", __func__);

  return size;
}

void
codec_init_data_to (CodecContext *ctx, CodecElement *codec, gpointer buffer)
{
  int size = 0;

  CODEC_LOG (DEBUG, "enter, %s\n", __func__);

  size = _codec_info_data (codec, buffer);

  CODEC_LOG (INFO, "context_id: %d, name: %s, media type: %s\n",
    ctx->index, codec->name, codec->media_type ? "AUDIO" : "VIDEO");

  memcpy (buffer + size, ctx, sizeof(CodecContext) - 12);
  size += (sizeof(CodecContext) - 12);
  memcpy (buffer + size, ctx->codecdata, ctx->codecdata_size);
  size += ctx->codecdata_size;

  // data length
  size -= sizeof(size);
  memcpy (buffer, &size, sizeof(size));

  CODEC_LOG (DEBUG, "leave, %s\n", __func__);
}

int
codec_init_data_from (CodecContext *ctx, int media_type, gpointer buffer)
{
  int ret = 0, size = 0;

  CODEC_LOG (DEBUG, "after init. read data from device.\n");

  memcpy (&ret, buffer, sizeof(ret));
  size = sizeof(ret);
  if (ret < 0) {
    return ret;
  } else {
    if (media_type == AVMEDIA_TYPE_AUDIO) {
      memcpy (&ctx->audio.sample_fmt, buffer + size, sizeof(ctx->audio.sample_fmt));
      size += sizeof(ctx->audio.sample_fmt);

      memcpy (&ctx->audio.frame_size, buffer + size, sizeof(ctx->audio.frame_size));
      size += sizeof(ctx->audio.frame_size);

      memcpy(&ctx->audio.bits_per_sample_fmt, buffer + size, sizeof(ctx->audio.bits_per_sample_fmt));
#if 0
      // TODO: check!!
      memcpy (&ctx->audio, buffer + size, sizeof(ctx->audio));
#endif
    } else {
      CODEC_LOG (DEBUG, "video type\n");
    }
  }

  return ret;
}

void
codec_decode_video_data_to (int in_size, int idx, int64_t in_offset,
                          uint8_t *in_buf, gpointer buffer)
{
  int size = 0;

  size = sizeof(size);
  memcpy (buffer + size, &in_size, sizeof(in_size));
  size += sizeof(in_size);
  memcpy (buffer + size, &idx, sizeof(idx));
  size += sizeof(idx);
  memcpy (buffer + size, &in_offset, sizeof(in_offset));
  size += sizeof(in_offset);

  if (in_size > 0) {
    memcpy (buffer + size, in_buf, in_size);
    size += in_size;
  }

  CODEC_LOG (DEBUG, "decode_video. inbuf_size: %d\n", in_size);

  size -= sizeof(size);
  memcpy (buffer, &size, sizeof(size));
}

int
codec_decode_video_data_from (int *got_picture_ptr, VideoData *video, gpointer buffer)
{
  int size = 0, len = 0;

  memcpy (&len, buffer, sizeof(len));
  size = sizeof(len);
  memcpy (got_picture_ptr, buffer + size, sizeof(*got_picture_ptr));
  size += sizeof(*got_picture_ptr);
  memcpy (video, buffer + size, sizeof(VideoData));

  CODEC_LOG (DEBUG, "decode_video. len: %d, have_data: %d\n", len, *got_picture_ptr);

  return len;
}

void
codec_decode_audio_data_to (int in_size, uint8_t *in_buf, gpointer buffer)
{
  int size = 0;

  size = sizeof(size);
  memcpy (buffer + size, &in_size, sizeof(in_size));
  size += sizeof(in_size);
  if (in_size > 0) {
    memcpy (buffer + size, in_buf, in_size);
    size += in_size;
  }

  size -= sizeof(size);
  memcpy (buffer, &size, sizeof(size));
}

int
codec_decode_audio_data_from (int *have_data, int16_t *samples,
                              AudioData *audio, gpointer buffer)
{
  int len = 0, size = 0;

  memcpy (&len, buffer, sizeof(len));
  size = sizeof(len);
  memcpy (have_data, buffer + size, sizeof(*have_data));
  size += sizeof(*have_data);

  CODEC_LOG (DEBUG, "decode_audio. len %d, have_data %d\n",
            len, (*have_data));

  if (*have_data) {
    memcpy (&audio->sample_rate, buffer + size, sizeof(audio->sample_rate));
    size += sizeof(audio->sample_rate);

    memcpy (&audio->channels, buffer + size, sizeof(audio->channels));
    size += sizeof(audio->channels);

    memcpy (&audio->channel_layout, buffer + size, sizeof(audio->channel_layout));
    size += sizeof(audio->channel_layout);

    CODEC_LOG (DEBUG, "decode_audio. sample_rate %d, channels %d, ch_layout %lld\n", audio->sample_rate, audio->channels, audio->channel_layout);

    memcpy (samples, buffer + size, (*have_data));
  }

  return len;
}

void
codec_encode_video_data_to (int in_size, int64_t in_timestamp,
                            uint8_t *in_buf, gpointer buffer)
{
  int size = 0;

  size = sizeof(size);
  memcpy (buffer + size, &in_size, sizeof(in_size));
  size += sizeof(in_size);
  memcpy (buffer + size, &in_timestamp, sizeof(in_timestamp));
  size += sizeof(in_timestamp);
  if (in_size > 0) {
    memcpy (buffer + size, in_buf, in_size);
    size += in_size;
  }

  size -= sizeof(size);
  memcpy (buffer, &size, sizeof(size));
}

int
codec_encode_video_data_from (uint8_t *out_buf, gpointer buffer)
{
  int len = 0, size = 0;

  memcpy (&len, buffer, sizeof(len));
  size = sizeof(len);

  CODEC_LOG (DEBUG, "encode_video. outbuf size: %d\n", len);
  if (len > 0) {
    memcpy (out_buf, buffer + size, len);
  }

  return len;
}

void
codec_encode_audio_data_to (int in_size, int max_size, uint8_t *in_buf, gpointer buffer)
{
  int size = 0;

  size = sizeof(size);
  memcpy (buffer + size, &in_size, sizeof(in_size));
  size += sizeof(in_size);
  memcpy (buffer + size, &max_size, sizeof(max_size));
  size += sizeof(max_size);

  if (in_size > 0) {
    memcpy (buffer + size, in_buf, in_size);
    size += in_size;
  }

  size -= sizeof(size);
  memcpy (buffer, &size, sizeof(size));
}

int
codec_encode_audio_data_from (uint8_t *out_buf, gpointer buffer)
{
  int len = 0, size = 0;

  memcpy (&len, buffer, sizeof(len));
  size = sizeof(len);
  if (len > 0) {
    memcpy (out_buf, buffer + size, len);
  }

  CODEC_LOG (DEBUG, "encode_audio. outbuf size: %d\n", len);

  return len;
}
