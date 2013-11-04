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
};

typedef struct _CodecHeader {
  int32_t   api_index;
  uint32_t  mem_offset;
} CodecHeader;

#define SMALL_BUFFER    (256 * 1024)
#define MEDIUM_BUFFER   (2 * 1024 * 1024)
#define LARGE_BUFFER    (4 * 1024 * 1024)

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
  uint32_t mem_offset = 0, cmd = 0;
  struct mem_info info = {0, };

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  if (buf_size < SMALL_BUFFER) {
    cmd = CODEC_CMD_SECURE_SMALL_BUFFER;
    CODEC_LOG (DEBUG, "small buffer size\n");
  } else if (buf_size < MEDIUM_BUFFER) {
    // HD Video(2MB)
    cmd = CODEC_CMD_SECURE_MEDIUM_BUFFER;
    CODEC_LOG (DEBUG, "HD buffer size\n");
  } else {
    // FULL HD Video(4MB)
    cmd = CODEC_CMD_SECURE_LARGE_BUFFER;
    CODEC_LOG (DEBUG, "FULL HD buffer size\n");
  }

  ret = ioctl (device_fd, cmd, &mem_offset);
  if (ret < 0) {
    CODEC_LOG (ERR, "failed to get available buffer\n");
  } else {
    if (mem_offset == (LARGE_BUFFER * 8)) {
      CODEC_LOG (ERR, "acquired memory is over!!\n");
    } else {
      info.start = (gpointer)((uint32_t)device_mem + mem_offset);
      info.offset = mem_offset;
      CODEC_LOG (DEBUG, "acquire device_memory: 0x%x\n", mem_offset);
    }
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return info;
}

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

  CODEC_LOG (DEBUG, "memory start: 0x%p, offset 0x%x\n", info.start, info.offset);

  GST_BUFFER_DATA (*buf) = GST_BUFFER_MALLOCDATA (*buf) = info.start;
  GST_BUFFER_SIZE (*buf) = size;
  GST_BUFFER_FREE_FUNC (*buf) = codec_buffer_free;
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
  uint32_t mem_offset = 0, meta_offset = 0;

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

  ret = ioctl (fd, CODEC_CMD_SECURE_SMALL_BUFFER, &mem_offset);
  if (ret < 0) {
    CODEC_LOG (ERR,
      "decode_video. failed to get available memory to write inbuf\n");
    return -1;
  }
  CODEC_LOG (DEBUG, "decode_video. mem_offset = 0x%x\n", mem_offset);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG, "decode_video. meta_offset = 0x%x\n", meta_offset);

  size = 8;
  _codec_decode_video_meta_to (in_size, idx, in_offset, mmapbuf + meta_offset + size);
  _codec_decode_video_inbuf (in_buf, in_size, mmapbuf + mem_offset);

  dev->mem_info.offset = mem_offset;

  _codec_write_to_qemu (ctx->index, CODEC_DECODE_VIDEO, mem_offset, fd);

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
  void *mmapbuf = NULL;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd\n", CODEC_DEV);
    return;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address\n");
    return;
  }

  CODEC_LOG (DEBUG, "pict_size: %d\n",  pict_size);

  if (pict_size < (SMALL_BUFFER)) {
    dev->mem_info.offset = (uint32_t)pict - (uint32_t)mmapbuf;
    CODEC_LOG (DEBUG, "%d of pict: %p , device_mem: %p\n",
              ctx->index, pict, mmapbuf);
    CODEC_LOG (DEBUG, "%d of picture_copy, mem_offset = 0x%x\n",
              ctx->index, dev->mem_info.offset);
  }

  _codec_write_to_qemu (ctx->index, CODEC_PICTURE_COPY,
                        dev->mem_info.offset, fd);
  if (pict_size < SMALL_BUFFER) {
    CODEC_LOG (DEBUG,
      "set the mem_offset as outbuf: 0x%x\n",  dev->mem_info.offset);
    ret = ioctl (fd, CODEC_CMD_USE_DEVICE_MEM, &(dev->mem_info.offset));
    if (ret < 0) {
    // FIXME:
    }
  } else if (pict_size < MEDIUM_BUFFER) {
    uint32_t mem_offset = 0;
    CODEC_LOG (DEBUG, "need to use medium size of memory\n");

    ret = ioctl (fd, CODEC_CMD_GET_DATA_FROM_MEDIUM_BUFFER, &mem_offset);
    if (ret < 0) {
      return;
    }
    CODEC_LOG (DEBUG, "picture_copy, mem_offset = 0x%x\n",  mem_offset);

    memcpy (pict, mmapbuf + mem_offset, pict_size);

    ret = ioctl(fd, CODEC_CMD_RELEASE_BUFFER, &mem_offset);
    if (ret < 0) {
      CODEC_LOG (ERR, "failed to release used memory\n");
    }
  } else {
    uint32_t mem_offset = 0;
    CODEC_LOG (DEBUG, "need to use large size of memory\n");

    ret = ioctl (fd, CODEC_CMD_GET_DATA_FROM_LARGE_BUFFER, &mem_offset);
    if (ret < 0) {
      return;
    }
    CODEC_LOG (DEBUG, "picture_copy, mem_offset = 0x%x\n",  mem_offset);

    memcpy (pict, mmapbuf + mem_offset, pict_size);

    ret = ioctl(fd, CODEC_CMD_RELEASE_BUFFER, &mem_offset);
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
  uint32_t mem_offset = 0, meta_offset = 0;

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

  ret = ioctl (fd, CODEC_CMD_SECURE_SMALL_BUFFER, &mem_offset);
  if (ret < 0) {
    CODEC_LOG (ERR,
      "decode_audio. failed to get available memory to write inbuf\n");
    return -1;
  }
//  CODEC_LOG (DEBUG, "decode audio. mem_offset = 0x%x\n", mem_offset);
  CODEC_LOG (DEBUG, "decode_audio. ctx_id: %d mem_offset = 0x%x\n", ctx->index, mem_offset);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG, "decode_audio. ctx_id: %d meta_offset = 0x%x\n", ctx->index, meta_offset);

  size = 8;
  _codec_decode_audio_meta_to (in_size, mmapbuf + meta_offset + size);
  _codec_decode_audio_inbuf (in_buf, in_size, mmapbuf + mem_offset);

  dev->mem_info.offset = mem_offset;
  _codec_write_to_qemu (ctx->index, CODEC_DECODE_AUDIO, mem_offset, fd);

  ret = ioctl (fd, CODEC_CMD_GET_DATA_FROM_SMALL_BUFFER, &mem_offset);
  if (ret < 0) {
    return -1;
  }
  CODEC_LOG (DEBUG, "after decode_audio. ctx_id: %d mem_offset = 0x%x\n", ctx->index, mem_offset);

  len =
    _codec_decode_audio_meta_from (&ctx->audio, have_data, mmapbuf + meta_offset + size);
  if (len > 0) {
    _codec_decode_audio_outbuf (*have_data, samples, mmapbuf + mem_offset);
  } else {
    CODEC_LOG (DEBUG, "decode_audio failure. ctx_id: %d\n", ctx->index);
  }

  memset(mmapbuf + mem_offset, 0x00, sizeof(len));

  ret = ioctl(fd, CODEC_CMD_RELEASE_BUFFER, &mem_offset);
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
  uint32_t mem_offset = 0, meta_offset = 0;

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

  if (in_size < SMALL_BUFFER) {
    CODEC_LOG (DEBUG, "use small size of buffer\n");

    ret = ioctl (fd, CODEC_CMD_SECURE_SMALL_BUFFER, &mem_offset);
    if (ret < 0) {
      CODEC_LOG (ERR, "failed to small size of buffer.\n");
      return -1;
    }
  } else if (in_size < MEDIUM_BUFFER) {
    CODEC_LOG (DEBUG, "use medium size of buffer\n");

    ret = ioctl (fd, CODEC_CMD_SECURE_MEDIUM_BUFFER, &mem_offset);
    if (ret < 0) {
      CODEC_LOG (ERR, "failed to small size of buffer.\n");
      return -1;
    }
  } else {
    CODEC_LOG (DEBUG, "use large size of buffer\n");
    ret = ioctl (fd, CODEC_CMD_SECURE_LARGE_BUFFER, &mem_offset);
    if (ret < 0) {
      CODEC_LOG (ERR, "failed to large size of buffer.\n");
      return -1;
    }
  }
  CODEC_LOG (DEBUG, "encode_video. mem_offset = 0x%x\n", mem_offset);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG, "encode_video. meta_offset = 0x%x\n", meta_offset);

  size = 8;
  meta_offset += size;
  _codec_encode_video_meta_to (in_size, in_timestamp, mmapbuf + meta_offset);
  _codec_encode_video_inbuf (in_buf, in_size, mmapbuf + mem_offset);

  dev->mem_info.offset = mem_offset;
  _codec_write_to_qemu (ctx->index, CODEC_ENCODE_VIDEO, mem_offset, fd);

#ifndef DIRECT_BUFFER
  ret = ioctl (fd, CODEC_CMD_GET_DATA_FROM_SMALL_BUFFER, &mem_offset);
  if (ret < 0) {
    return -1;
  }
  CODEC_LOG (DEBUG, "read, encode_video. mem_offset = 0x%x\n", mem_offset);

  memcpy (&len, mmapbuf + meta_offset, sizeof(len));
  CODEC_LOG (DEBUG, "encode_video. outbuf size: %d\n", len);
  if (len > 0) {
    memcpy (out_buf, mmapbuf + mem_offset, len);
    out_buf = mmapbuf + mem_offset;
  }

  dev->mem_info.offset = mem_offset;
#if 0
  len =
    _codec_encode_video_outbuf (out_buf, mmapbuf + mem_offset);
//  memset(mmapbuf + mem_offset, 0x00, sizeof(len));
#endif

#if 1
  ret = ioctl(fd, CODEC_CMD_RELEASE_BUFFER, &mem_offset);
  if (ret < 0) {
    CODEC_LOG (ERR, "failed release used memory\n");
  }
#endif
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
  uint32_t mem_offset = 0, meta_offset = 0;

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

  ret = ioctl (fd, CODEC_CMD_SECURE_SMALL_BUFFER, &mem_offset);
  if (ret < 0) {
    return -1;
  }

  CODEC_LOG (DEBUG, "write, encode_audio. mem_offset = 0x%x\n", mem_offset);

  meta_offset = (ctx->index - 1) * CODEC_META_DATA_SIZE;
  CODEC_LOG (DEBUG, "encode_audio. meta mem_offset = 0x%x\n", meta_offset);

  size = _codec_header (CODEC_ENCODE_AUDIO, mem_offset,
                            mmapbuf + meta_offset);
  _codec_encode_audio_meta_to (max_size, in_size, mmapbuf + meta_offset + size);
  _codec_encode_audio_inbuf (in_buf, in_size, mmapbuf + mem_offset);

  dev->mem_info.offset = mem_offset;
  _codec_write_to_qemu (ctx->index, CODEC_ENCODE_AUDIO, mem_offset, fd);

  ret = ioctl (fd, CODEC_CMD_GET_DATA_FROM_SMALL_BUFFER, &mem_offset);
  if (ret < 0) {
    return -1;
  }

  CODEC_LOG (DEBUG, "read, encode_video. mem_offset = 0x%x\n", mem_offset);

  len = _codec_encode_audio_outbuf (out_buf, mmapbuf + mem_offset);
  memset(mmapbuf + mem_offset, 0x00, sizeof(len));

  ret = ioctl(fd, CODEC_CMD_RELEASE_BUFFER, &mem_offset);
  if (ret < 0) {
    return -1;
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}
