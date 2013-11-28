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
#include "gstmarumem.h"
#include "gstmarudevice.h"

extern int device_fd;
extern gpointer device_mem;

struct mem_info {
    gpointer start;
    uint32_t offset;
    uint32_t size;
};

typedef struct _CodecHeader {
  int32_t   api_index;
  uint32_t  mem_offset;
} CodecHeader;

typedef struct _CodecBufferId {
  uint32_t  buffer_index;
  uint32_t  buffer_size;
} CodecBufferId;

#define CODEC_META_DATA_SIZE    256
#define GET_OFFSET(buffer)      ((uint32_t)buffer - (uint32_t)device_mem)
#define SMALLDATA               0

static void
_codec_invoke_qemu (int32_t ctx_index, int32_t api_index,
                          uint32_t mem_offset, int fd)
{
  CodecIOParams ioparam = { 0 };

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ioparam.api_index = api_index;
  ioparam.ctx_index = ctx_index;
  ioparam.mem_offset = mem_offset;
  if (ioctl (fd, CODEC_CMD_INVOKE_API_AND_RELEASE_BUFFER, &ioparam) < 0) {
    CODEC_LOG (ERR, "failed to invoke codec APIs\n");
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

static int
secure_device_mem (int fd, guint ctx_id, guint buf_size, gpointer* buffer)
{
  int ret = 0;
//  uint32_t opaque = 0;
  CodecBufferId opaque;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);
  opaque.buffer_index = ctx_id;
  opaque.buffer_size = buf_size;

  ret = ioctl (fd, CODEC_CMD_SECURE_BUFFER, &opaque);
  *buffer = (gpointer)((uint32_t)device_mem + opaque.buffer_size);
  CODEC_LOG (DEBUG, "buffer: 0x%x\n", (int)buffer);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return ret;
}

static void
release_device_mem (int fd, gpointer start)
{
  int ret;
  uint32_t offset = start - device_mem;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  CODEC_LOG (DEBUG, "release device_mem start: %p, offset: 0x%x\n", start, offset);
  ret = ioctl (fd, CODEC_CMD_RELEASE_BUFFER, &offset);
  if (ret < 0) {
    CODEC_LOG (ERR, "failed to release buffer\n");
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

GstFlowReturn
codec_buffer_alloc_and_copy (GstPad *pad, guint64 offset, guint size,
                  GstCaps *caps, GstBuffer **buf)
{
  struct mem_info info;
  CodecBufferId opaque;
  int ret = 0;
  GstMaruDec *marudec;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  *buf = gst_buffer_new ();

  marudec = (GstMaruDec *)gst_pad_get_element_private(pad);

  opaque.buffer_index = marudec->context->index;
  opaque.buffer_size = size;

  CODEC_LOG (DEBUG, "buffer_and_copy. ctx_id: %d\n", marudec->context->index);
  _codec_invoke_qemu (marudec->context->index, CODEC_PICTURE_COPY,
                        0, marudec->dev->fd);

  ret = ioctl (marudec->dev->fd, CODEC_CMD_PUT_DATA_INTO_BUFFER, &opaque);

  if (ret < 0) {
    CODEC_LOG (DEBUG, "failed to get available buffer\n");
  } else if (ret == 1) {
    // FIXME: we must aligned buffer offset.
    info.start = g_malloc (size);
    info.offset = 0;

    GST_BUFFER_FREE_FUNC (*buf) = g_free;

    memcpy (info.start, (uint32_t)device_mem + opaque.buffer_size, size);
    release_device_mem(marudec->dev->fd, (uint32_t)device_mem + opaque.buffer_size);

    CODEC_LOG (DEBUG, "we secured last buffer, so we will use heap buffer\n");
  } else {
    // address of "device_mem" and "opaque" is aleady aligned.
    info.start = (gpointer)((uint32_t)device_mem + opaque.buffer_size);
    info.offset = opaque.buffer_size;

    GST_BUFFER_FREE_FUNC (*buf) = codec_buffer_free;

    CODEC_LOG (DEBUG, "device memory start: 0x%p, offset 0x%x\n", info.start, info.offset);
  }

  GST_BUFFER_DATA (*buf) = GST_BUFFER_MALLOCDATA (*buf) = info.start;
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
  int ret = 0, opened = 0, size = 8;
  uint32_t meta_offset = 0;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ret = ioctl(dev->fd, CODEC_CMD_GET_CONTEXT_INDEX, &ctx->index);
  if (ret < 0) {
    GST_ERROR ("failed to get context index\n");
    return -1;
  }
  CODEC_LOG (DEBUG, "get context index: %d\n", ctx->index);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG,
    "init. ctx: %d meta_offset = 0x%x\n", ctx->index, meta_offset);

  _codec_init_meta_to (ctx, codec, device_mem + meta_offset + size);

  _codec_invoke_qemu (ctx->index, CODEC_INIT, 0, dev->fd);

  CODEC_LOG (DEBUG,
    "init. ctx: %d meta_offset = 0x%x, size: %d\n", ctx->index, meta_offset, size);

  opened =
    _codec_init_meta_from (ctx, codec->media_type, device_mem + meta_offset + size);
  ctx->codec = codec;

  CODEC_LOG (DEBUG, "opened: %d\n", opened);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return opened;
}

void
codec_deinit (CodecContext *ctx, CodecDevice *dev)
{
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  CODEC_LOG (INFO, "close. context index: %d\n", ctx->index);
  _codec_invoke_qemu (ctx->index, CODEC_DEINIT, 0, dev->fd);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

void
codec_flush_buffers (CodecContext *ctx, CodecDevice *dev)
{
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  CODEC_LOG (DEBUG, "flush buffers. context index: %d\n", ctx->index);
  _codec_invoke_qemu (ctx->index, CODEC_FLUSH_BUFFERS, 0, dev->fd);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

int
codec_decode_video (CodecContext *ctx, uint8_t *in_buf, int in_size,
                    gint idx, gint64 in_offset, GstBuffer **out_buf,
                    int *got_picture_ptr, CodecDevice *dev)
{
  int len = 0, ret = 0, size = 8;
  gpointer buffer = NULL;
  uint32_t meta_offset = 0;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG, "decode_video. ctx_id: %d meta_offset = 0x%x\n", ctx->index, meta_offset);
  _codec_decode_video_meta_to (in_size, idx, in_offset, device_mem + meta_offset + size);

  ret = secure_device_mem(dev->fd, ctx->index, in_size, &buffer);
  if (ret < 0) {
    CODEC_LOG (ERR,
      "decode_video. failed to get available memory to write inbuf\n");
    return -1;
  }

  _codec_decode_video_inbuf (in_buf, in_size, buffer);
  dev->mem_info.offset = GET_OFFSET(buffer);
  _codec_invoke_qemu (ctx->index, CODEC_DECODE_VIDEO, GET_OFFSET(buffer), dev->fd);

  // after decoding video, no need to get outbuf.
  len =
    _codec_decode_video_meta_from (&ctx->video, got_picture_ptr, device_mem + meta_offset + size);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

int
codec_decode_audio (CodecContext *ctx, int16_t *samples,
                    int *have_data, uint8_t *in_buf,
                    int in_size, CodecDevice *dev)
{
  int len = 0, ret = 0, size = 8;
  gpointer buffer = NULL;
  uint32_t meta_offset = 0;
  CodecBufferId opaque;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG, "decode_audio. ctx_id: %d meta_offset = 0x%x\n", ctx->index, meta_offset);
  _codec_decode_audio_meta_to (in_size, device_mem + meta_offset + size);

  ret = secure_device_mem(dev->fd, ctx->index, in_size, &buffer);
  if (ret < 0) {
    CODEC_LOG (ERR,
      "decode_audio. failed to get available memory to write inbuf\n");
    return -1;
  }

  _codec_decode_audio_inbuf (in_buf, in_size, buffer);
  dev->mem_info.offset = GET_OFFSET(buffer);
  _codec_invoke_qemu (ctx->index, CODEC_DECODE_AUDIO, GET_OFFSET(buffer), dev->fd);

  opaque.buffer_index = ctx->index;
  opaque.buffer_size = SMALLDATA;
  // FIXME: how can we know output data size ?
  ret = ioctl (dev->fd, CODEC_CMD_PUT_DATA_INTO_BUFFER, &opaque);
  if (ret < 0) {
    return -1;
  }
  CODEC_LOG (DEBUG, "after decode_audio. ctx_id: %d, buffer = 0x%x\n",
            ctx->index, (int)device_mem + opaque.buffer_size);

  len =
    _codec_decode_audio_meta_from (&ctx->audio, have_data, device_mem + meta_offset + size);
  if (len > 0) {
    _codec_decode_audio_outbuf (*have_data, samples, device_mem + opaque.buffer_size);
  } else {
    CODEC_LOG (DEBUG, "decode_audio failure. ctx_id: %d\n", ctx->index);
  }
  CODEC_LOG (DEBUG, "decode_audio. ctx_id: %d len: %d\n", ctx->index, len);

  release_device_mem(dev->fd, device_mem + opaque.buffer_size);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

int
codec_encode_video (CodecContext *ctx, uint8_t *out_buf,
                    int out_size, uint8_t *in_buf,
                    int in_size, int64_t in_timestamp, CodecDevice *dev)
{
  int len = 0, ret = 0, size = 8;
  gpointer buffer = NULL;
  uint32_t meta_offset = 0;
  CodecBufferId opaque;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG, "encode_video. meta_offset = 0x%x\n", meta_offset);
  _codec_encode_video_meta_to (in_size, in_timestamp, device_mem + meta_offset + size);

  ret = secure_device_mem(dev->fd, ctx->index, in_size, &buffer);
  if (ret < 0) {
    CODEC_LOG (ERR, "failed to small size of buffer.\n");
    return -1;
  }

  _codec_encode_video_inbuf (in_buf, in_size, buffer);
  dev->mem_info.offset = GET_OFFSET(buffer);
  _codec_invoke_qemu (ctx->index, CODEC_ENCODE_VIDEO, GET_OFFSET(buffer), dev->fd);

  opaque.buffer_index = ctx->index;
  opaque.buffer_size = SMALLDATA;
  // FIXME: how can we know output data size ?
  ret = ioctl (dev->fd, CODEC_CMD_PUT_DATA_INTO_BUFFER, &opaque);
  if (ret < 0) {
    return -1;
  }
  CODEC_LOG (DEBUG, "read, encode_video. mem_offset = 0x%x\n", opaque.buffer_size);

  memcpy (&len, device_mem + meta_offset + size, sizeof(len));

  CODEC_LOG (DEBUG, "encode_video. outbuf size: %d\n", len);
  if (len > 0) {
    memcpy (out_buf, device_mem + opaque.buffer_size, len);
    dev->mem_info.offset = opaque.buffer_size;
  }

  release_device_mem(dev->fd, device_mem + opaque.buffer_size);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

int
codec_encode_audio (CodecContext *ctx, uint8_t *out_buf,
                    int max_size, uint8_t *in_buf,
                    int in_size, CodecDevice *dev)
{
  int len = 0, ret = 0, size = 8;
  gpointer buffer = NULL;
  uint32_t meta_offset = 0;
  CodecBufferId opaque;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG, "encode_audio. meta mem_offset = 0x%x\n", meta_offset);
  _codec_encode_audio_meta_to (max_size, in_size, device_mem + meta_offset + size);

  ret = secure_device_mem(dev->fd, ctx->index, in_size, &buffer);
  if (ret < 0) {
    return -1;
  }

  _codec_encode_audio_inbuf (in_buf, in_size, buffer);
  dev->mem_info.offset = GET_OFFSET(buffer);
  _codec_invoke_qemu (ctx->index, CODEC_ENCODE_AUDIO, GET_OFFSET(buffer), dev->fd);

  opaque.buffer_index = ctx->index;
  opaque.buffer_size = SMALLDATA;
  // FIXME: how can we know output data size ?
  ret = ioctl (dev->fd, CODEC_CMD_PUT_DATA_INTO_BUFFER, &opaque);
  if (ret < 0) {
    return -1;
  }

  CODEC_LOG (DEBUG, "read, encode_video. mem_offset = 0x%x\n", opaque.buffer_size);

  len = _codec_encode_audio_outbuf (out_buf, device_mem + opaque.buffer_size);

  release_device_mem(dev->fd, device_mem + opaque.buffer_size);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}
