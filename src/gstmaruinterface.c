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

#define CODEC_META_DATA_SIZE 256

static int
_codec_header (int32_t api_index, uint32_t mem_offset, uint8_t *device_buf)
{
  CodecHeader header = { 0 };

  CODEC_LOG (DEBUG, "enter, %s\n", __func__);

  header.api_index = api_index;
  header.mem_offset = mem_offset;

  memcpy(device_buf, &header, sizeof(header));

  CODEC_LOG (DEBUG, "leave, %s\n", __func__);

  return sizeof(header);
}

static void
_codec_write_to_qemu (int32_t ctx_index, int32_t api_index,
                          uint32_t mem_offset, int fd)
{
  CodecIOParams ioparam;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  memset(&ioparam, 0, sizeof(ioparam));
  ioparam.api_index = api_index;
  ioparam.ctx_index = ctx_index;
  ioparam.mem_offset = mem_offset;
  if (write (fd, &ioparam, 1) < 0) {
    CODEC_LOG (ERR, "failed to write input data\n");
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

static struct mem_info
secure_device_mem (guint buf_size)
{
  int ret = 0;
  uint32_t opaque = 0;
  struct mem_info info = {0, };

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);
  opaque = buf_size;

  ret = ioctl (device_fd, CODEC_CMD_TRY_SECURE_BUFFER, &opaque);
  if (ret == -1) {
    CODEC_LOG (DEBUG, "failed to get available buffer\n");
    info.start = NULL;
    info.offset = 0;
  } else {
    info.start = (gpointer)((uint32_t)device_mem + opaque);
    info.offset = opaque;
    CODEC_LOG (DEBUG, "acquire device_memory: 0x%x\n", opaque);
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return info;
}

#ifndef USE_HEAP_BUFFER
static void
release_device_mem (gpointer start)
{
  int ret;
  uint32_t offset = start - device_mem;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  CODEC_LOG (DEBUG, "release device_mem start: %p, offset: 0x%x\n", start, offset);
  ret = ioctl (device_fd, CODEC_CMD_RELEASE_BUFFER, &offset);
  if (ret < 0) {
    CODEC_LOG (ERR, "failed to release buffer\n");
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

static void
codec_buffer_free (gpointer start)
{
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  release_device_mem (start);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

GstFlowReturn
codec_buffer_alloc (GstPad *pad, guint64 offset, guint size,
                  GstCaps *caps, GstBuffer **buf)
{
  struct mem_info info;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  *buf = gst_buffer_new ();

  info = secure_device_mem (size);

  if (info.start == NULL) {
    CODEC_LOG (DEBUG, "can not secure memory now, so we will use heap buffer");
    info.start = g_malloc (size);
    GST_BUFFER_FREE_FUNC (*buf) = g_free;
  } else {
    CODEC_LOG (DEBUG, "device memory start: 0x%p, offset 0x%x\n", info.start, info.offset);
    GST_BUFFER_FREE_FUNC (*buf) = codec_buffer_free;
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
#endif

int
codec_init (CodecContext *ctx, CodecElement *codec, CodecDevice *dev)
{
  int fd, ret = 0;
  int opened, size = 0;
  uint8_t *mmapbuf = NULL;
  uint32_t meta_offset = 0;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd.\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = (uint8_t *)dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address.\n");
    return -1;
  }

  ret = ioctl(fd, CODEC_CMD_GET_CONTEXT_INDEX, &ctx->index);
  if (ret < 0) {
    GST_ERROR ("failed to get context index\n");
    return -1;
  }
  CODEC_LOG (DEBUG, "get context index: %d\n", ctx->index);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG,
    "init. ctx: %d meta_offset = 0x%x\n", ctx->index, meta_offset);

  size = 8;
  _codec_init_meta_to (ctx, codec, mmapbuf + meta_offset + size);

  _codec_write_to_qemu (ctx->index, CODEC_INIT, 0, fd);

  CODEC_LOG (DEBUG,
    "init. ctx: %d meta_offset = 0x%x, size: %d\n", ctx->index, meta_offset, size);

  opened =
    _codec_init_meta_from (ctx, codec->media_type, mmapbuf + meta_offset + size);
  ctx->codec= codec;

  CODEC_LOG (DEBUG, "opened: %d\n", opened);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return opened;
}

void
codec_deinit (CodecContext *ctx, CodecDevice *dev)
{
  int fd;
  void *mmapbuf = NULL;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd.\n", CODEC_DEV);
    return;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address.\n");
    return;
  }

  CODEC_LOG (INFO, "close. context index: %d\n", ctx->index);
  _codec_write_to_qemu (ctx->index, CODEC_DEINIT, 0, fd);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

void
codec_flush_buffers (CodecContext *ctx, CodecDevice *dev)
{
  int fd;
  void *mmapbuf = NULL;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd.\n", CODEC_DEV);
    return;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address.\n");
    return;
  }

  CODEC_LOG (DEBUG, "flush buffers. context index: %d\n", ctx->index);
  _codec_write_to_qemu (ctx->index, CODEC_FLUSH_BUFFERS, 0, fd);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

int
codec_decode_video (CodecContext *ctx, uint8_t *in_buf, int in_size,
                    gint idx, gint64 in_offset, GstBuffer **out_buf,
                    int *got_picture_ptr, CodecDevice *dev)
{
  int fd, len = 0;
  int ret, size = 0;
  uint8_t *mmapbuf = NULL;
  uint32_t opaque = 0, meta_offset = 0;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address\n");
    return -1;
  }

  opaque = in_size;

  ret = ioctl (fd, CODEC_CMD_SECURE_BUFFER, &opaque);
  if (ret < 0) {
    CODEC_LOG (ERR,
      "decode_video. failed to get available memory to write inbuf\n");
    return -1;
  }
  CODEC_LOG (DEBUG, "decode_video. mem_offset = 0x%x\n", opaque);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG, "decode_video. meta_offset = 0x%x\n", meta_offset);

  size = 8;
  _codec_decode_video_meta_to (in_size, idx, in_offset, mmapbuf + meta_offset + size);
  _codec_decode_video_inbuf (in_buf, in_size, mmapbuf + opaque);

  dev->mem_info.offset = opaque;

  _codec_write_to_qemu (ctx->index, CODEC_DECODE_VIDEO, opaque, fd);

  // after decoding video, no need to get outbuf.
  len =
    _codec_decode_video_meta_from (&ctx->video, got_picture_ptr, mmapbuf + meta_offset + size);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

void
codec_picture_copy (CodecContext *ctx, uint8_t *pict,
                    uint32_t pict_size, CodecDevice *dev)
{
  int fd, ret = 0;
  uint32_t opaque = 0;
  int is_direct_buffer = 0;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd\n", CODEC_DEV);
    return;
  }

  if (!dev->buf) {
    GST_ERROR ("failed to get mmaped memory address\n");
    return;
  }

  // determine buffer located in device memory or not.
  if ((uint32_t)pict >= (uint32_t)(dev->buf) &&
      (uint32_t)pict < (uint32_t)(dev->buf) + (dev->buf_size)) {
    is_direct_buffer = 1;
  }

  CODEC_LOG (DEBUG, "pict_size: %d\n",  pict_size);

  _codec_write_to_qemu (ctx->index, CODEC_PICTURE_COPY,
                        0, fd);

  if (is_direct_buffer) {
    // if we can use device memory as a output buffer...
#ifdef USE_HEAP_BUFFER
    GST_ERROR ("failed to get mmaped memory address\n");
#else
    dev->mem_info.offset = (uint32_t)pict - (uint32_t)(dev->buf);
    CODEC_LOG (DEBUG, "%d of pict: %p , device_mem: %p\n",
              ctx->index, pict, dev->buf);
    CODEC_LOG (DEBUG, "%d of picture_copy, mem_offset = 0x%x\n",
              ctx->index, dev->mem_info.offset);

    ret = ioctl (fd, CODEC_CMD_USE_DEVICE_MEM, &(dev->mem_info.offset));
    if (ret < 0) {
      CODEC_LOG (ERR, "failed to use device memory\n");
      return;
    }
#endif
  } else {
    // if we can not use device memory as a output buffer,
    // we must copy data from device memory to heap buffer.
    CODEC_LOG (DEBUG, "need a memory\n");
    opaque = pict_size;
    ret = ioctl (fd, CODEC_CMD_GET_DATA_INTO_DEVICE_MEM, &opaque);
    if (ret < 0) {
      CODEC_LOG (ERR, "failed to secure device memory\n");
      return;
    }

    CODEC_LOG (DEBUG, "picture_copy, mem_offset = 0x%x\n",  opaque);

    memcpy (pict, (dev->buf) + opaque, pict_size);

    ret = ioctl(fd, CODEC_CMD_RELEASE_BUFFER, &opaque);

    if (ret < 0) {
      CODEC_LOG (ERR, "failed to release used memory\n");
    }
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

int
codec_decode_audio (CodecContext *ctx, int16_t *samples,
                    int *have_data, uint8_t *in_buf,
                    int in_size, CodecDevice *dev)
{
  int fd, len = 0;
  int ret, size = 0;
  uint8_t *mmapbuf = NULL;
  uint32_t opaque = 0, meta_offset = 0;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR("failed to get %s fd\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = (uint8_t *)dev->buf;
  if (!mmapbuf) {
    GST_ERROR("failed to get mmaped memory address\n");
    return -1;
  }

  opaque = in_size;

  ret = ioctl (fd, CODEC_CMD_SECURE_BUFFER, &opaque);
  if (ret < 0) {
    CODEC_LOG (ERR,
      "decode_audio. failed to get available memory to write inbuf\n");
    return -1;
  }
  CODEC_LOG (DEBUG, "decode_audio. ctx_id: %d mem_offset = 0x%x\n", ctx->index, opaque);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG, "decode_audio. ctx_id: %d meta_offset = 0x%x\n", ctx->index, meta_offset);

  size = 8;
  _codec_decode_audio_meta_to (in_size, mmapbuf + meta_offset + size);
  _codec_decode_audio_inbuf (in_buf, in_size, mmapbuf + opaque);

  dev->mem_info.offset = opaque;
  _codec_write_to_qemu (ctx->index, CODEC_DECODE_AUDIO, opaque, fd);

  opaque = 0; // FIXME: how can we know output data size ?
  ret = ioctl (fd, CODEC_CMD_GET_DATA_INTO_DEVICE_MEM, &opaque);
  if (ret < 0) {
    return -1;
  }
  CODEC_LOG (DEBUG, "after decode_audio. ctx_id: %d mem_offset = 0x%x\n", ctx->index, opaque);

  len =
    _codec_decode_audio_meta_from (&ctx->audio, have_data, mmapbuf + meta_offset + size);
  if (len > 0) {
    _codec_decode_audio_outbuf (*have_data, samples, mmapbuf + opaque);
  } else {
    CODEC_LOG (DEBUG, "decode_audio failure. ctx_id: %d\n", ctx->index);
  }

  memset(mmapbuf + opaque, 0x00, sizeof(len));

  ret = ioctl(fd, CODEC_CMD_RELEASE_BUFFER, &opaque);
  if (ret < 0) {
    CODEC_LOG (ERR, "failed release used memory\n");
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

int
codec_encode_video (CodecContext *ctx, uint8_t *out_buf,
                    int out_size, uint8_t *in_buf,
                    int in_size, int64_t in_timestamp, CodecDevice *dev)
{
  int fd, len = 0;
  int ret, size;
  uint8_t *mmapbuf = NULL;
  uint32_t opaque = 0, meta_offset = 0;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd.\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address.\n");
    return -1;
  }

  opaque = in_size;
  ret = ioctl (fd, CODEC_CMD_SECURE_BUFFER, &opaque);
  if (ret < 0) {
    CODEC_LOG (ERR, "failed to small size of buffer.\n");
    return -1;
  }

  CODEC_LOG (DEBUG, "encode_video. mem_offset = 0x%x\n", opaque);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG, "encode_video. meta_offset = 0x%x\n", meta_offset);

  size = 8;
  meta_offset += size;
  _codec_encode_video_meta_to (in_size, in_timestamp, mmapbuf + meta_offset);
  _codec_encode_video_inbuf (in_buf, in_size, mmapbuf + opaque);

  dev->mem_info.offset = opaque;
  _codec_write_to_qemu (ctx->index, CODEC_ENCODE_VIDEO, opaque, fd);

#ifndef DIRECT_BUFFER
  opaque = 0; // FIXME: how can we know output data size ?
  ret = ioctl (fd, CODEC_CMD_GET_DATA_INTO_DEVICE_MEM, &opaque);
  if (ret < 0) {
    return -1;
  }
  CODEC_LOG (DEBUG, "read, encode_video. mem_offset = 0x%x\n", opaque);

  memcpy (&len, mmapbuf + meta_offset, sizeof(len));
  CODEC_LOG (DEBUG, "encode_video. outbuf size: %d\n", len);
  if (len > 0) {
    memcpy (out_buf, mmapbuf + opaque, len);
    out_buf = mmapbuf + opaque;
  }

  dev->mem_info.offset = opaque;

  ret = ioctl(fd, CODEC_CMD_RELEASE_BUFFER, &opaque);
  if (ret < 0) {
    CODEC_LOG (ERR, "failed release used memory\n");
  }
#else
  dev->mem_info.offset = (uint32_t)pict - (uint32_t)mmapbuf;
  CODEC_LOG (DEBUG, "outbuf: %p , device_mem: %p\n",  pict, mmapbuf);
  CODEC_LOG (DEBUG, "encoded video. mem_offset = 0x%x\n",  dev->mem_info.offset);

  ret = ioctl (fd, CODEC_CMD_USE_DEVICE_MEM, &(dev->mem_info.offset));
  if (ret < 0) {
    // FIXME:
  }
#endif
  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

int
codec_encode_audio (CodecContext *ctx, uint8_t *out_buf,
                    int max_size, uint8_t *in_buf,
                    int in_size, CodecDevice *dev)
{
  int fd, len = 0;
  int ret, size;
  void *mmapbuf = NULL;
  uint32_t opaque = 0, meta_offset = 0;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd.\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address.\n");
    return -1;
  }

  opaque = in_size;

  ret = ioctl (fd, CODEC_CMD_SECURE_BUFFER, &opaque);
  if (ret < 0) {
    return -1;
  }

  CODEC_LOG (DEBUG, "write, encode_audio. mem_offset = 0x%x\n", opaque);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG, "encode_audio. meta mem_offset = 0x%x\n", meta_offset);

  size = _codec_header (CODEC_ENCODE_AUDIO, opaque,
                            mmapbuf + meta_offset);
  _codec_encode_audio_meta_to (max_size, in_size, mmapbuf + meta_offset + size);
  _codec_encode_audio_inbuf (in_buf, in_size, mmapbuf + opaque);

  dev->mem_info.offset = opaque;
  _codec_write_to_qemu (ctx->index, CODEC_ENCODE_AUDIO, opaque, fd);

  opaque = 0; // FIXME: how can we know output data size ?
  ret = ioctl (fd, CODEC_CMD_GET_DATA_INTO_DEVICE_MEM, &opaque);
  if (ret < 0) {
    return -1;
  }

  CODEC_LOG (DEBUG, "read, encode_video. mem_offset = 0x%x\n", opaque);

  len = _codec_encode_audio_outbuf (out_buf, mmapbuf + opaque);
  memset(mmapbuf + opaque, 0x00, sizeof(len));

  ret = ioctl(fd, CODEC_CMD_RELEASE_BUFFER, &opaque);
  if (ret < 0) {
    return -1;
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}
