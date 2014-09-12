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

Interface *interface = NULL;

enum IOCTL_CMD {
  IOCTL_CMD_GET_VERSION,
  IOCTL_CMD_GET_ELEMENTS_SIZE,
  IOCTL_CMD_GET_ELEMENTS,
  IOCTL_CMD_GET_CONTEXT_INDEX,
  IOCTL_CMD_SECURE_BUFFER,
  IOCTL_CMD_TRY_SECURE_BUFFER,
  IOCTL_CMD_RELEASE_BUFFER,
  IOCTL_CMD_INVOKE_API_AND_GET_DATA,
};

typedef struct {
  uint32_t  api_index;
  uint32_t  ctx_index;
  uint32_t  mem_offset;
  int32_t  buffer_size;
} __attribute__((packed)) IOCTL_Data;

#define BRILLCODEC_KEY         'B'
#define IOCTL_RW(CMD)           (_IOWR(BRILLCODEC_KEY, CMD, IOCTL_Data))

#define CODEC_META_DATA_SIZE    256
#define GET_OFFSET(buffer)      ((uint32_t)buffer - (uint32_t)device_mem)
#define SMALLDATA               0

#define OFFSET_PICTURE_BUFFER   0x100

static inline bool can_use_new_decode_api(void) {
    if (CHECK_VERSION(3)) {
        return true;
    }
    return false;
}

static int
invoke_device_api(int fd, int32_t ctx_index, int32_t api_index,
                          uint32_t *mem_offset, int32_t buffer_size)
{
  IOCTL_Data ioctl_data = { 0, };
  int ret = -1;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ioctl_data.api_index = api_index;
  ioctl_data.ctx_index = ctx_index;
  if (mem_offset) {
    ioctl_data.mem_offset = *mem_offset;
  }
  ioctl_data.buffer_size = buffer_size;

  ret = ioctl(fd, IOCTL_RW(IOCTL_CMD_INVOKE_API_AND_GET_DATA), &ioctl_data);

  if (mem_offset) {
    *mem_offset = ioctl_data.mem_offset;
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return ret;
}

static int
secure_device_mem (int fd, guint ctx_id, guint buf_size, gpointer* buffer)
{
  int ret = 0;
  IOCTL_Data data;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);
  data.ctx_index = ctx_id;
  data.buffer_size = buf_size;

  ret = ioctl (fd, IOCTL_RW(IOCTL_CMD_SECURE_BUFFER), &data);

  *buffer = (gpointer)((uint32_t)device_mem + data.mem_offset);
  GST_DEBUG ("device_mem %p, offset_size 0x%x", device_mem, data.mem_offset);

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
  ret = ioctl (fd, IOCTL_RW(IOCTL_CMD_RELEASE_BUFFER), &offset);
  if (ret < 0) {
    GST_ERROR ("failed to release buffer\n");
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

static int
get_context_index (int fd)
{
  int ctx_index;

  if (ioctl (fd, IOCTL_RW(IOCTL_CMD_GET_CONTEXT_INDEX), &ctx_index) < 0) {
    GST_ERROR ("failed to get a context index, %d", fd);
    return -1;
  }

  return ctx_index;
}

static void
buffer_free (gpointer start)
{
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  release_device_mem (device_fd, start);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

static void
buffer_free2 (gpointer start)
{
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  release_device_mem (device_fd, start - OFFSET_PICTURE_BUFFER);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

static inline void fill_size_header(void *buffer, size_t size)
{
  *((uint32_t *)buffer) = (uint32_t)size;
}

//
// Interface
// INIT / DEINIT
//

static int
init (CodecContext *ctx, CodecElement *codec, CodecDevice *dev)
{
  int opened = 0;
  gpointer buffer = NULL;
  int ret;
  uint32_t mem_offset;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  if (get_context_index(dev->fd) < 0) {
    return -1;
  }
  GST_DEBUG ("get context index: %d, %d", ctx->index, dev->fd);

  /* buffer size is 0. It means that this function is required to
   * use small size.
  */
  if (secure_device_mem(dev->fd, ctx->index, 0, &buffer) < 0) {
    GST_ERROR ("failed to get a memory block");
    return -1;
  }

  codec_init_data_to (ctx, codec, buffer);

  mem_offset = GET_OFFSET(buffer);
  ret = invoke_device_api (dev->fd, ctx->index, CODEC_INIT, &mem_offset, SMALLDATA);

  if (ret < 0) {
    return -1;
  }

  opened =
    codec_init_data_from (ctx, codec->media_type, device_mem + mem_offset);

  if (opened < 0) {
    GST_ERROR ("failed to open Context for %s", codec->name);
  } else {
    ctx->codec = codec;
  }

  release_device_mem(dev->fd, device_mem + mem_offset);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return opened;
}

static void
deinit (CodecContext *ctx, CodecDevice *dev)
{
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  GST_INFO ("close context %d", ctx->index);
  invoke_device_api (dev->fd, ctx->index, CODEC_DEINIT, NULL, -1);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

//
// Interface
// VIDEO DECODE / ENCODE
//

struct video_decode_input {
    int32_t inbuf_size;
    int32_t idx;
    int64_t in_offset;
    uint8_t inbuf;          // for pointing inbuf address
} __attribute__((packed));

struct video_decode_output {
    int32_t len;
    int32_t got_picture;
    uint8_t data;           // for pointing data address
} __attribute__((packed));

static int
decode_video (GstMaruDec *marudec, uint8_t *inbuf, int inbuf_size,
                    gint idx, gint64 in_offset, GstBuffer **out_buf, int *have_data)
{
  CodecContext *ctx = marudec->context;
  CodecDevice *dev = marudec->dev;
  int len = 0, ret = 0;
  gpointer buffer = NULL;
  uint32_t mem_offset;
  size_t size = sizeof(inbuf_size) + sizeof(idx) + sizeof(in_offset) + inbuf_size;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ret = secure_device_mem(dev->fd, ctx->index, size, &buffer);
  if (ret < 0) {
    GST_ERROR ("failed to get available memory to write inbuf");
    return -1;
  }

  fill_size_header(buffer, size);
  struct video_decode_input *decode_input = buffer + size;
  decode_input->inbuf_size = inbuf_size;
  decode_input->idx = idx;
  decode_input->in_offset = in_offset;
  memcpy(&decode_input->inbuf, inbuf, inbuf_size);

  mem_offset = GET_OFFSET(buffer);

  marudec->is_using_new_decode_api = (can_use_new_decode_api() && (ctx->video.pix_fmt != -1));
  if (marudec->is_using_new_decode_api) {
    int picture_size = gst_maru_avpicture_size (ctx->video.pix_fmt,
        ctx->video.width, ctx->video.height);
    if (picture_size < 0) {
      // can not enter here...
      GST_ERROR ("Can not enter here. Check about it !!!");
      picture_size = SMALLDATA;
    }
    ret = invoke_device_api(dev->fd, ctx->index, CODEC_DECODE_VIDEO_AND_PICTURE_COPY, &mem_offset, picture_size);
  } else {
    ret = invoke_device_api(dev->fd, ctx->index, CODEC_DECODE_VIDEO, &mem_offset, SMALLDATA);
  }

  if (ret < 0) {
    GST_ERROR ("Invoke API failed");
    return -1;
  }

  struct video_decode_output *decode_output = device_mem + mem_offset;
  len = decode_output->len;
  *have_data = decode_output->got_picture;
  memcpy(&ctx->video, &decode_output->data, sizeof(VideoData));

  GST_DEBUG_OBJECT (marudec, "after decode: len %d, have_data %d",
        len, *have_data);

  if (len >= 0 && *have_data > 0 && marudec->is_using_new_decode_api) {
    marudec->is_last_buffer = ret;
    marudec->mem_offset = mem_offset;
  } else {
    release_device_mem(dev->fd, device_mem + mem_offset);
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

static GstFlowReturn
buffer_alloc_and_copy (GstPad *pad, guint64 offset, guint size,
                  GstCaps *caps, GstBuffer **buf)
{
  bool is_last_buffer = 0;
  uint32_t mem_offset;
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

    mem_offset = 0;

    GST_DEBUG ("buffer_and_copy. ctx_id: %d", ctx->index);

    int ret = invoke_device_api(dev->fd, ctx->index, CODEC_PICTURE_COPY, &mem_offset, size);
    if (ret < 0) {
      GST_DEBUG ("failed to get available buffer");
      return GST_FLOW_ERROR;
    }
    is_last_buffer = ret;
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
      GST_BUFFER_FREE_FUNC (*buf) = buffer_free2;
    } else {
      buffer = (gpointer)(device_mem + mem_offset);
      GST_BUFFER_FREE_FUNC (*buf) = buffer_free;
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

struct video_encode_input {
    int32_t inbuf_size;
    int64_t in_timestamp;
    uint8_t inbuf;          // for pointing inbuf address
} __attribute__((packed));

struct video_encode_output {
    int32_t len;
    int32_t coded_frame;
    int32_t key_frame;
    uint8_t data;           // for pointing data address
} __attribute__((packed));

static int
encode_video (CodecContext *ctx, uint8_t *outbuf,
                    int out_size, uint8_t *inbuf,
                    int inbuf_size, int64_t in_timestamp,
                    int *coded_frame, int *is_keyframe,
                    CodecDevice *dev)
{
  int len = 0, ret = 0;
  gpointer buffer = NULL;
  uint32_t mem_offset;
  size_t size = sizeof(inbuf_size) + sizeof(in_timestamp) + inbuf_size;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ret = secure_device_mem(dev->fd, ctx->index, size, &buffer);
  if (ret < 0) {
    GST_ERROR ("failed to small size of buffer");
    return -1;
  }

  fill_size_header(buffer, size);
  struct video_encode_input *encode_input = buffer + size;
  encode_input->inbuf_size = inbuf_size;
  encode_input->in_timestamp = in_timestamp;
  memcpy(&encode_input->inbuf, inbuf, inbuf_size);

  mem_offset = GET_OFFSET(buffer);

  // FIXME: how can we know output data size ?
  ret = invoke_device_api(dev->fd, ctx->index, CODEC_ENCODE_VIDEO, &mem_offset, SMALLDATA);

  if (ret < 0) {
    GST_ERROR ("Invoke API failed");
    return -1;
  }

  GST_DEBUG ("encode_video. mem_offset = 0x%x", mem_offset);

  struct video_encode_output *encode_output = device_mem + mem_offset;
  len = encode_output->len;
  *coded_frame = encode_output->coded_frame;
  *is_keyframe = encode_output->key_frame;
  memcpy(outbuf, &encode_output->data, sizeof(VideoData));

  release_device_mem(dev->fd, device_mem + mem_offset);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

//
// Interface
// AUDIO DECODE / ENCODE
//

static int
decode_audio (CodecContext *ctx, int16_t *samples,
                    int *have_data, uint8_t *in_buf,
                    int in_size, CodecDevice *dev)
{
  int len = 0, ret = 0;
  gpointer buffer = NULL;
  uint32_t mem_offset;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ret = secure_device_mem(dev->fd, ctx->index, in_size, &buffer);
  if (ret < 0) {
    GST_ERROR ("failed to get available memory to write inbuf");
    return -1;
  }

  GST_DEBUG ("decode_audio 1. in_buffer size %d", in_size);
  codec_decode_audio_data_to (in_size, in_buf, buffer);

  mem_offset = GET_OFFSET(buffer);

  ret = invoke_device_api(dev->fd, ctx->index, CODEC_DECODE_AUDIO, &mem_offset, SMALLDATA);

  if (ret < 0) {
    return -1;
  }

  GST_DEBUG ("decode_audio 2. ctx_id: %d, buffer = 0x%x",
    ctx->index, device_mem + mem_offset);

  len = codec_decode_audio_data_from (have_data, samples,
    &ctx->audio, device_mem + mem_offset);

  GST_DEBUG ("decode_audio 3. ctx_id: %d len: %d", ctx->index, len);

  release_device_mem(dev->fd, device_mem + mem_offset);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

static int
encode_audio (CodecContext *ctx, uint8_t *out_buf,
                    int max_size, uint8_t *in_buf,
                    int in_size, int64_t timestamp,
                    CodecDevice *dev)
{
  int ret = 0;
  gpointer buffer = NULL;
  uint32_t mem_offset;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ret = secure_device_mem(dev->fd, ctx->index, in_size, &buffer);
  if (ret < 0) {
    return -1;
  }

  codec_encode_audio_data_to (in_size, max_size, in_buf, timestamp, buffer);

  mem_offset = GET_OFFSET(buffer);

  ret = invoke_device_api(dev->fd, ctx->index, CODEC_ENCODE_AUDIO, &mem_offset, SMALLDATA);

  if (ret < 0) {
    return -1;
  }

  GST_DEBUG ("encode_audio. mem_offset = 0x%x", mem_offset);

  ret = codec_encode_audio_data_from (out_buf, device_mem + mem_offset);

  release_device_mem(dev->fd, device_mem + mem_offset);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return ret;
}

//
// Interface
// MISC
//

static void
flush_buffers (CodecContext *ctx, CodecDevice *dev)
{
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  GST_DEBUG ("flush buffers of context: %d", ctx->index);
  invoke_device_api (dev->fd, ctx->index, CODEC_FLUSH_BUFFERS, NULL, -1);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

static int
get_device_version (int fd)
{
  uint32_t device_version;
  int ret;

  ret = ioctl (fd, IOCTL_RW(IOCTL_CMD_GET_VERSION), &device_version);
  if (ret < 0) {
    return ret;
  }

  return device_version;
}

static int
prepare_elements (int fd, GList *elements)
{
  uint32_t size = 0;
  int ret, elem_cnt, i;
  CodecElement *elem;

  ret = ioctl (fd, IOCTL_RW(IOCTL_CMD_GET_ELEMENTS_SIZE), &size);
  if (ret < 0) {
    return ret;
  }

  elem = g_malloc(size);

  ret = ioctl (fd, IOCTL_RW(IOCTL_CMD_GET_ELEMENTS), elem);
  if (ret < 0) {
    g_free (elem);
    return ret;
  }

  elem_cnt = size / sizeof(CodecElement);
  for (i = 0; i < elem_cnt; i++) {
    elements = g_list_append (elements, &elem[i]);
  }

  return 0;
}

// Interfaces
Interface *interface_version_3 = &(Interface) {
  .init = init,
  .deinit = deinit,
  .decode_video = decode_video,
  .decode_audio = decode_audio,
  .encode_video = encode_video,
  .encode_audio = encode_audio,
  .flush_buffers = flush_buffers,
  .buffer_alloc_and_copy = buffer_alloc_and_copy,
  .get_device_version = get_device_version,
  .prepare_elements = prepare_elements,
};
