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

#include "gstmaru.h"
#include "gstmaruinterface.h"
#include "gstmaruutils.h"
#include "gstmarumem.h"
#include "gstmarudevice.h"

extern int device_fd;
extern gpointer device_mem;

typedef struct _CodecHeader {
  int32_t   api_index;
  uint32_t  mem_offset;
} CodecHeader;

typedef struct _CodecBufferId {
  uint32_t  buffer_index;
  uint32_t  buffer_size;
} CodecBufferId;

typedef struct _CodecIOParams {
  int32_t   api_index;
  int32_t   ctx_index;
  uint32_t  mem_offset;

  CodecBufferId buffer_id;
} CodecIOParams;


#define CODEC_META_DATA_SIZE    256
#define GET_OFFSET(buffer)      ((uint32_t)buffer - (uint32_t)device_mem)
#define SMALLDATA               0

#define OFFSET_PICTURE_BUFFER   0x100

static int
_codec_invoke_qemu(int32_t ctx_index, int32_t api_index,
                          uint32_t mem_offset, int fd, CodecBufferId *buffer_id)
{
  CodecIOParams ioparam = { 0, };
  int ret = -1;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ioparam.api_index = api_index;
  ioparam.ctx_index = ctx_index;
  ioparam.mem_offset = mem_offset;

  if (CHECK_VERSION(3)) {
    if (buffer_id) {
      ioparam.buffer_id.buffer_index = buffer_id->buffer_index;
      ioparam.buffer_id.buffer_size = buffer_id->buffer_size;
    }

    ret = ioctl(fd, CODEC_CMD_INVOKE_API_AND_RELEASE_BUFFER, &ioparam);

    if (buffer_id) {
      buffer_id->buffer_index = ioparam.buffer_id.buffer_index;
      buffer_id->buffer_size = ioparam.buffer_id.buffer_size;
    }
  } else {
    if (ioctl(fd, CODEC_CMD_INVOKE_API_AND_RELEASE_BUFFER, &ioparam) < 0) {
      return -1;
    }
    if (buffer_id) {
      ret = ioctl(fd, CODEC_CMD_PUT_DATA_INTO_BUFFER, buffer_id);
    }
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return ret;
}

static int
secure_device_mem (int fd, guint ctx_id, guint buf_size, gpointer* buffer)
{
  int ret = 0;
  CodecBufferId opaque;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);
  opaque.buffer_index = ctx_id;
  opaque.buffer_size = buf_size;

  ret = ioctl (fd, CODEC_CMD_SECURE_BUFFER, &opaque);
  /* ioctl: CODEC_CMD_SECURE_BUFFER
   *  - sets device memory offset into opaque.buffer_size
   */
  *buffer = (gpointer)((uint32_t)device_mem + opaque.buffer_size);
  GST_DEBUG ("device_mem %p, offset_size 0x%x", device_mem, opaque.buffer_size);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return ret;
}

static void
release_device_mem (int fd, gpointer start)
{
  int ret;
  uint32_t offset = start - device_mem;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  GST_DEBUG ("release device_mem start: %p, offset: 0x%x", start, offset);
  ret = ioctl (fd, CODEC_CMD_RELEASE_BUFFER, &offset);
  if (ret < 0) {
    GST_ERROR ("failed to release buffer\n");
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

static void
codec_buffer_free (gpointer start)
{
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  release_device_mem (device_fd, start);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

static void
codec_buffer_free2 (gpointer start)
{
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  release_device_mem (device_fd, start - OFFSET_PICTURE_BUFFER);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

GstFlowReturn
codec_buffer_alloc_and_copy (GstPad *pad, guint64 offset, guint size,
                  GstCaps *caps, GstBuffer **buf)
{
  bool is_last_buffer = 0;
  int mem_offset;
  CodecBufferId opaque;
  GstMaruDec *marudec;
  CodecContext *ctx;
  CodecDevice *dev;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  *buf = gst_buffer_new ();

  marudec = (GstMaruDec *)gst_pad_get_element_private(pad);
  ctx = marudec->context;
  dev = marudec->dev;

  if (marudec->is_using_new_decode_api) {
    is_last_buffer = marudec->is_last_buffer;
    mem_offset = marudec->mem_offset;
  } else {
    ctx = marudec->context;

    opaque.buffer_index = ctx->index;
    opaque.buffer_size = size;

    GST_DEBUG ("buffer_and_copy. ctx_id: %d", ctx->index);

    int ret = _codec_invoke_qemu(ctx->index, CODEC_PICTURE_COPY, 0, dev->fd, &opaque);
    if (ret < 0) {
      GST_DEBUG ("failed to get available buffer");
      return GST_FLOW_ERROR;
    }
    is_last_buffer = ret;
    mem_offset = opaque.buffer_size;
  }

  gpointer *buffer = NULL;
  if (is_last_buffer) {
    // FIXME: we must aligned buffer offset.
    buffer = g_malloc (size);

    GST_BUFFER_FREE_FUNC (*buf) = g_free;

    if (marudec->is_using_new_decode_api) {
      memcpy (buffer, device_mem + mem_offset + OFFSET_PICTURE_BUFFER, size);
    } else {
      memcpy (buffer, device_mem + mem_offset, size);
    }
    release_device_mem(dev->fd, device_mem + mem_offset);

    GST_DEBUG ("secured last buffer!! Use heap buffer");
  } else {
    // address of "device_mem" and "opaque" is aleady aligned.
    if (marudec->is_using_new_decode_api) {
      buffer = (gpointer)(device_mem + mem_offset + OFFSET_PICTURE_BUFFER);
      GST_BUFFER_FREE_FUNC (*buf) = codec_buffer_free2;
    } else {
      buffer = (gpointer)(device_mem + mem_offset);
      GST_BUFFER_FREE_FUNC (*buf) = codec_buffer_free;
    }


    GST_DEBUG ("device memory start: 0x%p, offset 0x%x", (intptr_t)buffer, mem_offset);
  }

  GST_BUFFER_DATA (*buf) = GST_BUFFER_MALLOCDATA (*buf) = (void *)buffer;
  GST_BUFFER_SIZE (*buf) = size;
  GST_BUFFER_OFFSET (*buf) = offset;

  if (caps) {
    gst_buffer_set_caps (*buf, caps);
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return GST_FLOW_OK;
}

int
codec_init (CodecContext *ctx, CodecElement *codec, CodecDevice *dev)
{
  int opened = 0;
  gpointer buffer = NULL;
  CodecBufferId opaque;
  int ret;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  if (ioctl(dev->fd, CODEC_CMD_GET_CONTEXT_INDEX, &ctx->index) < 0) {
    GST_ERROR ("failed to get a context index");
    return -1;
  }
  GST_DEBUG ("get context index: %d", ctx->index);

  /* buffer size is 0. It means that this function is required to
   * use small size.
  */
  if (secure_device_mem(dev->fd, ctx->index, 0, &buffer) < 0) {
    GST_ERROR ("failed to get a memory block");
    return -1;
  }

  codec_init_data_to (ctx, codec, buffer);

  opaque.buffer_index = ctx->index;
  opaque.buffer_size = SMALLDATA;

  ret = _codec_invoke_qemu (ctx->index, CODEC_INIT, GET_OFFSET(buffer), dev->fd, &opaque);

  if (ret < 0) {
    return -1;
  }

  opened =
    codec_init_data_from (ctx, codec->media_type, device_mem + opaque.buffer_size);

  if (opened < 0) {
    GST_ERROR ("failed to open Context for %s", codec->name);
  } else {
    ctx->codec = codec;
  }

  release_device_mem(dev->fd, device_mem + opaque.buffer_size);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return opened;
}

void
codec_deinit (CodecContext *ctx, CodecDevice *dev)
{
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  GST_INFO ("close context %d", ctx->index);
  _codec_invoke_qemu (ctx->index, CODEC_DEINIT, 0, dev->fd, NULL);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

void
codec_flush_buffers (CodecContext *ctx, CodecDevice *dev)
{
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  GST_DEBUG ("flush buffers of context: %d", ctx->index);
  _codec_invoke_qemu (ctx->index, CODEC_FLUSH_BUFFERS, 0, dev->fd, NULL);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

int
codec_decode_video (GstMaruDec *marudec, uint8_t *in_buf, int in_size,
                    gint idx, gint64 in_offset, GstBuffer **out_buf, int *have_data)
{
  CodecContext *ctx = marudec->context;
  CodecDevice *dev = marudec->dev;
  int len = 0, ret = 0;
  gpointer buffer = NULL;
  CodecBufferId opaque = { 0, };

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ret = secure_device_mem(dev->fd, ctx->index, in_size, &buffer);
  if (ret < 0) {
    GST_ERROR ("failed to get available memory to write inbuf");
    return -1;
  }

  codec_decode_video_data_to (in_size, idx, in_offset, in_buf, buffer);

  opaque.buffer_index = ctx->index;

  marudec->is_using_new_decode_api = (can_use_new_decode_api() && (ctx->video.pix_fmt != -1));

  if (marudec->is_using_new_decode_api) {
    int picture_size = gst_maru_avpicture_size (ctx->video.pix_fmt,
        ctx->video.width, ctx->video.height);
    if (picture_size < 0) {
      GST_ERROR ("Check about it"); // FIXME
      opaque.buffer_size = SMALLDATA;
    } else {
      opaque.buffer_size = picture_size;
    }
    ret = _codec_invoke_qemu(ctx->index, CODEC_DECODE_VIDEO2, GET_OFFSET(buffer), dev->fd, &opaque);
  } else {
    opaque.buffer_size = SMALLDATA;
    ret = _codec_invoke_qemu(ctx->index, CODEC_DECODE_VIDEO, GET_OFFSET(buffer), dev->fd, &opaque);
  }

  if (ret < 0) {
    // FIXME:
    return -1;
  }
  len = codec_decode_video_data_from (have_data, &ctx->video, device_mem + opaque.buffer_size);

  GST_DEBUG_OBJECT (marudec, "after decode: len %d, have_data %d",
        len, *have_data);
  if (len < 0 || *have_data <= 0) {
    GST_DEBUG_OBJECT (marudec, "return flow %d, out %p, len %d",
        ret, *out_buf, len);

    release_device_mem(dev->fd, device_mem + opaque.buffer_size);

    return len;
  }

  if (marudec->is_using_new_decode_api) {
    marudec->is_last_buffer = ret;
    marudec->mem_offset = opaque.buffer_size;
  } else {
    release_device_mem(dev->fd, device_mem + opaque.buffer_size);
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

int
codec_decode_audio (CodecContext *ctx, int16_t *samples,
                    int *have_data, uint8_t *in_buf,
                    int in_size, CodecDevice *dev)
{
  int len = 0, ret = 0;
  gpointer buffer = NULL;
  CodecBufferId opaque;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ret = secure_device_mem(dev->fd, ctx->index, in_size, &buffer);
  if (ret < 0) {
    GST_ERROR ("failed to get available memory to write inbuf");
    return -1;
  }

  GST_DEBUG ("decode_audio 1. in_buffer size %d", in_size);
  codec_decode_audio_data_to (in_size, in_buf, buffer);

  opaque.buffer_index = ctx->index;
  opaque.buffer_size = SMALLDATA;

  ret = _codec_invoke_qemu(ctx->index, CODEC_DECODE_AUDIO, GET_OFFSET(buffer), dev->fd, &opaque);

  if (ret < 0) {
    return -1;
  }

  GST_DEBUG ("decode_audio 2. ctx_id: %d, buffer = 0x%x",
    ctx->index, device_mem + opaque.buffer_size);

  len = codec_decode_audio_data_from (have_data, samples,
    &ctx->audio, device_mem + opaque.buffer_size);

  GST_DEBUG ("decode_audio 3. ctx_id: %d len: %d", ctx->index, len);

  release_device_mem(dev->fd, device_mem + opaque.buffer_size);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

int
codec_encode_video (CodecContext *ctx, uint8_t *out_buf,
                    int out_size, uint8_t *in_buf,
                    int in_size, int64_t in_timestamp,
                    int *coded_frame, int *is_keyframe,
                    CodecDevice *dev)
{
  int len = 0, ret = 0;
  gpointer buffer = NULL;
  CodecBufferId opaque;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ret = secure_device_mem(dev->fd, ctx->index, in_size, &buffer);
  if (ret < 0) {
    GST_ERROR ("failed to small size of buffer");
    return -1;
  }

  codec_encode_video_data_to (in_size, in_timestamp, in_buf, buffer);

  opaque.buffer_index = ctx->index;
  opaque.buffer_size = SMALLDATA;

  // FIXME: how can we know output data size ?
  ret = _codec_invoke_qemu(ctx->index, CODEC_ENCODE_VIDEO, GET_OFFSET(buffer), dev->fd, &opaque);

  if (ret < 0) {
    return -1;
  }

  GST_DEBUG ("encode_video. mem_offset = 0x%x", opaque.buffer_size);

  len = codec_encode_video_data_from (out_buf, coded_frame, is_keyframe, device_mem + opaque.buffer_size);

  release_device_mem(dev->fd, device_mem + opaque.buffer_size);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

int
codec_encode_audio (CodecContext *ctx, uint8_t *out_buf,
                    int max_size, uint8_t *in_buf,
                    int in_size, int64_t timestamp,
                    CodecDevice *dev)
{
  int ret = 0;
  gpointer buffer = NULL;
  CodecBufferId opaque;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ret = secure_device_mem(dev->fd, ctx->index, in_size, &buffer);
  if (ret < 0) {
    return -1;
  }

  codec_encode_audio_data_to (in_size, max_size, in_buf, timestamp, buffer);

  opaque.buffer_index = ctx->index;
  opaque.buffer_size = SMALLDATA;

  ret = _codec_invoke_qemu(ctx->index, CODEC_ENCODE_AUDIO, GET_OFFSET(buffer), dev->fd, &opaque);

  if (ret < 0) {
    return -1;
  }

  GST_DEBUG ("encode_audio. mem_offset = 0x%x", opaque.buffer_size);

  ret = codec_encode_audio_data_from (out_buf, device_mem + opaque.buffer_size);

  release_device_mem(dev->fd, device_mem + opaque.buffer_size);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return ret;
}
