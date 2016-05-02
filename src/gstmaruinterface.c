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

#include <inttypes.h>
#include "gstmaru.h"
#include "gstmaruinterface.h"
#include "gstmaruutils.h"
#include "gstmarumem.h"
#include "gstmarudevice.h"

Interface *interface = NULL;

enum IOCTL_CMD {
  IOCTL_CMD_GET_VERSION = 192, //BASE_VIDIOC_PRIVATE
  IOCTL_CMD_GET_ELEMENTS_SIZE,
  IOCTL_CMD_GET_ELEMENTS,
  IOCTL_CMD_GET_CONTEXT_INDEX,
  IOCTL_CMD_SECURE_BUFFER,
  IOCTL_CMD_TRY_SECURE_BUFFER,
  IOCTL_CMD_RELEASE_BUFFER,
  IOCTL_CMD_INVOKE_API_AND_GET_DATA,
  IOCTL_CMD_GET_PROFILE_STATUS,
};

typedef struct {
  uint32_t  api_index;
  uint32_t  ctx_index;
  uintptr_t  mem_offset;
  int32_t  buffer_size;
} __attribute__((packed)) IOCTL_Data;

#define BRILLCODEC_KEY         'V' //Same as VIDEO DEVICE KEY
#define IOCTL_RW(CMD)           (_IOWR(BRILLCODEC_KEY, CMD, IOCTL_Data))

#define CODEC_META_DATA_SIZE    256
#define GET_OFFSET(buffer)      ((uintptr_t)buffer - (uintptr_t)device_mem)
#define SMALLDATA               0

#define OFFSET_PICTURE_BUFFER   0x100

static void
query_capability(int fd)
{
  GST_DEBUG (" >> Enter");
  struct v4l2_capability cap;
  memset (&cap, 0, sizeof (cap));
  if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0 ) {
    perror ("[gst-interface] failed to query capability");
	GST_ERROR ("cannot query capability");
  } else {
    GST_INFO (" capabilities = %d", cap.capabilities);
  }
  GST_DEBUG (" >> Leave");
}

static int
invoke_device_api(int fd, int32_t ctx_index, int32_t api_index,
                          uintptr_t *mem_offset, int32_t buffer_size)
{
  GST_DEBUG (" >> Enter");
  IOCTL_Data ioctl_data = { 0, };
  int ret = -1;

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

  GST_DEBUG (" >> Leave");
  return ret;
}

static int
secure_device_mem (int fd, guint ctx_id, guint buf_size, gpointer* buffer)
{
  GST_DEBUG (" >> Enter");
  int ret = 0;
  IOCTL_Data data;

  data.ctx_index = ctx_id;
  data.buffer_size = buf_size;

  ret = ioctl (fd, IOCTL_RW(IOCTL_CMD_SECURE_BUFFER), &data);

  *buffer = (gpointer)(device_mem + data.mem_offset);
  GST_DEBUG ("device_mem %p, offset_size 0x%"PRIXPTR, device_mem, data.mem_offset);

  GST_DEBUG (" >> Leave");
  return ret;
}

static void
release_device_mem (int fd, gpointer start)
{
  GST_DEBUG (" >> Enter");
  int ret;
  uint32_t offset = start - device_mem;

  GST_DEBUG ("release device_mem start: %p, offset: 0x%x", start, offset);
  ret = ioctl (fd, IOCTL_RW(IOCTL_CMD_RELEASE_BUFFER), &offset);
  if (ret < 0) {
    GST_ERROR ("failed to release buffer\n");
  }
  GST_DEBUG (" >> Leave");
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
// TODO: check this code is needed
#if 0
static void
buffer_free (gpointer start)
{
  release_device_mem (device_fd, start);
}

static void
buffer_free2 (gpointer start)
{
  release_device_mem (device_fd, start - OFFSET_PICTURE_BUFFER);
}
#endif
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
  uintptr_t mem_offset;

  GST_DEBUG (" >> Enter");
  if ((ctx->index = get_context_index(dev->fd)) <= 0) {
    GST_ERROR ("failed to get a context index");
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
    GST_ERROR ("invoke_device_api failed");
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

  GST_DEBUG (" >> Leave");
  return opened;
}

static void
deinit (CodecContext *ctx, CodecDevice *dev)
{
  GST_INFO ("close context %d", ctx->index);
  invoke_device_api (dev->fd, ctx->index, CODEC_DEINIT, NULL, -1);
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
decode_video (GstMaruVidDec *marudec, uint8_t *inbuf, int inbuf_size,
                    gint idx, gint64 in_offset, GstBuffer **out_buf, int *have_data)
{
  GST_DEBUG (" >> Enter");
  CodecContext *ctx = marudec->context;
  CodecDevice *dev = marudec->dev;
  int len = 0, ret = 0;
  gpointer buffer = NULL;
  uintptr_t mem_offset;
  size_t size = sizeof(inbuf_size) + sizeof(idx) + sizeof(in_offset) + inbuf_size;

  ret = secure_device_mem(dev->fd, ctx->index, size, &buffer);
  if (ret < 0) {
    GST_ERROR ("failed to get available memory to write inbuf");
    return -1;
  }

  fill_size_header(buffer, size);
  struct video_decode_input *decode_input = buffer + sizeof(int32_t);
  decode_input->inbuf_size = inbuf_size;
  decode_input->idx = idx;
  decode_input->in_offset = in_offset;
  memcpy(&decode_input->inbuf, inbuf, inbuf_size);

  mem_offset = GET_OFFSET(buffer);

  marudec->is_using_new_decode_api =  (ctx->video.pix_fmt != -1);
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
    // in case of this, a decoded frame is not given from codec device.
    ret = invoke_device_api(dev->fd, ctx->index, CODEC_DECODE_VIDEO, &mem_offset, SMALLDATA);
  }

  if (ret < 0) {
    GST_ERROR ("invoke API failed");
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

  GST_DEBUG (" >> Leave");
  return len;
}

GstFlowReturn
alloc_and_copy (GstMaruVidDec *marudec, guint64 offset, guint size,
                  GstCaps *caps, GstBuffer **buf)
{
  GST_DEBUG (" >> enter");
//  bool is_last_buffer = 0;
  uintptr_t mem_offset;
  CodecContext *ctx;
  CodecDevice *dev;
  GstMapInfo mapinfo;

  ctx = marudec->context;
  dev = marudec->dev;

  if (marudec->is_using_new_decode_api) {
//    is_last_buffer = marudec->is_last_buffer;
    mem_offset = marudec->mem_offset;
  } else {
    ctx = marudec->context;

    mem_offset = 0;

    int ret = invoke_device_api(dev->fd, ctx->index, CODEC_PICTURE_COPY, &mem_offset, size);
    if (ret < 0) {
      GST_DEBUG ("failed to get available buffer");
      return GST_FLOW_ERROR;
    }
//    is_last_buffer = ret;
  }

//  gpointer *buffer = NULL;
// FIXME: consider to check last_buffer
//  is_last_buffer = 1;
//  if (is_last_buffer) {

// FIXME: we must aligned buffer offset.
//    buffer = g_malloc (size);
  gst_buffer_map (*buf, &mapinfo, GST_MAP_READWRITE);

  if (marudec->is_using_new_decode_api) {
    memcpy (mapinfo.data, device_mem + mem_offset + OFFSET_PICTURE_BUFFER, size);
  } else {
    memcpy (mapinfo.data, device_mem + mem_offset, size);
  }
  release_device_mem(dev->fd, device_mem + mem_offset);

  GST_DEBUG ("secured last buffer!! Use heap buffer");
/*
} else {
    // address of "device_mem" and "opaque" is aleady aligned.
    if (marudec->is_using_new_decode_api) {
      buffer = (gpointer)(device_mem + mem_offset + OFFSET_PICTURE_BUFFER);
      //GST_BUFFER_FREE_FUNC (*buf) = buffer_free2;
    } else {
      buffer = (gpointer)(device_mem + mem_offset);
      //GST_BUFFER_FREE_FUNC (*buf) = buffer_free;
    }

    GST_DEBUG ("device memory start: 0x%p, offset 0x%"PRIXPTR, (void *) buffer, mem_offset);
  }
*/
  gst_buffer_unmap (*buf, &mapinfo);

  GST_DEBUG (" >> leave");
  return GST_FLOW_OK;
}

static GstFlowReturn
buffer_alloc_and_copy (GstPad *pad, guint64 offset, guint size,
                  GstCaps *caps, GstBuffer **buf)
{
  GST_DEBUG (" >> enter");
//  bool is_last_buffer = 0;
  uintptr_t mem_offset;
  GstMaruDec *marudec;
  CodecContext *ctx;
  CodecDevice *dev;
  GstMapInfo mapinfo;

  marudec = (GstMaruDec *)gst_pad_get_element_private(pad);
  ctx = marudec->context;
  dev = marudec->dev;

  if (marudec->is_using_new_decode_api) {
//    is_last_buffer = marudec->is_last_buffer;
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
//    is_last_buffer = ret;
  }

  gpointer *buffer = NULL;
// FIXME: consider to check last_buffer
//  is_last_buffer = 1;
//  if (is_last_buffer) {
// FIXME: we must aligned buffer offset.
    buffer = g_malloc (size);

    if (marudec->is_using_new_decode_api) {
      memcpy (buffer, device_mem + mem_offset + OFFSET_PICTURE_BUFFER, size);
    } else {
      memcpy (buffer, device_mem + mem_offset, size);
    }
    release_device_mem(dev->fd, device_mem + mem_offset);

    GST_DEBUG ("secured last buffer!! Use heap buffer");
/*  } else {
    // address of "device_mem" and "opaque" is aleady aligned.
    if (marudec->is_using_new_decode_api) {
      buffer = (gpointer)(device_mem + mem_offset + OFFSET_PICTURE_BUFFER);
      //GST_BUFFER_FREE_FUNC (*buf) = buffer_free2;
    } else {
      buffer = (gpointer)(device_mem + mem_offset);
      //GST_BUFFER_FREE_FUNC (*buf) = buffer_free;
    }


    GST_DEBUG ("device memory start: 0x%p, offset 0x%"PRIXPTR, (void *) buffer, mem_offset);
  }
*/
  *buf = gst_buffer_new ();
  //*buf = gst_buffer_new_and_alloc (size);
  gst_buffer_map (*buf, &mapinfo, GST_MAP_READWRITE);
  mapinfo.data = (guint8 *)buffer;
  mapinfo.size = size;
  GST_BUFFER_OFFSET (*buf) = offset;
  gst_buffer_unmap (*buf, &mapinfo);

  GST_DEBUG (" >> leave");
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
  uintptr_t mem_offset;
  size_t size = sizeof(inbuf_size) + sizeof(in_timestamp) + inbuf_size;

  ret = secure_device_mem(dev->fd, ctx->index, size, &buffer);
  if (ret < 0) {
    GST_ERROR ("failed to small size of buffer");
    return -1;
  }

  fill_size_header(buffer, size);
  struct video_encode_input *encode_input = buffer + sizeof(int32_t);
  encode_input->inbuf_size = inbuf_size;
  encode_input->in_timestamp = in_timestamp;
  memcpy(&encode_input->inbuf, inbuf, inbuf_size);
  GST_DEBUG ("insize: %d, inpts: %lld", encode_input->inbuf_size,(long long) encode_input->in_timestamp);

  mem_offset = GET_OFFSET(buffer);

  ret = invoke_device_api(dev->fd, ctx->index, CODEC_ENCODE_VIDEO, &mem_offset, SMALLDATA);

  if (ret < 0) {
    GST_ERROR ("Invoke API failed");
    return -1;
  }

  GST_DEBUG ("encode_video. mem_offset = 0x%"PRIXPTR, mem_offset);

  struct video_encode_output *encode_output = device_mem + mem_offset;
  len = encode_output->len;
  *coded_frame = encode_output->coded_frame;
  *is_keyframe = encode_output->key_frame;
  memcpy(outbuf, &encode_output->data, len);

  release_device_mem(dev->fd, device_mem + mem_offset);

  return len;
}

//
// Interface
// AUDIO DECODE / ENCODE
//

struct audio_decode_input {
    int32_t inbuf_size;
    uint8_t inbuf;          // for pointing inbuf address
} __attribute__((packed));

struct audio_decode_output {
    int32_t len;
    int32_t got_frame;
    uint8_t data;           // for pointing data address
} __attribute__((packed));

struct audio_encode_input {
    int32_t inbuf_size;
    uint8_t inbuf;          // for pointing inbuf address
} __attribute__((packed));

struct audio_encode_output {
    int32_t len;
    uint8_t data;           // for pointing data address
} __attribute__((packed));

static int
decode_audio (CodecContext *ctx, int16_t *samples,
                    int *have_data, uint8_t *inbuf,
                    int inbuf_size, CodecDevice *dev)
{
  int len = 0, ret = 0;
  gpointer buffer = NULL;
  uintptr_t mem_offset;
  size_t size = sizeof(inbuf_size) + inbuf_size;

  ret = secure_device_mem(dev->fd, ctx->index, size, &buffer);
  if (ret < 0) {
    GST_ERROR ("failed to get available memory to write inbuf");
    return -1;
  }

  GST_DEBUG ("decode_audio. in_buffer size %d", inbuf_size);

  fill_size_header(buffer, size);
  struct audio_decode_input *decode_input = buffer + sizeof(int32_t);
  decode_input->inbuf_size = inbuf_size;
  memcpy(&decode_input->inbuf, inbuf, inbuf_size);

  mem_offset = GET_OFFSET(buffer);

  ret = invoke_device_api(dev->fd, ctx->index, CODEC_DECODE_AUDIO, &mem_offset, SMALLDATA);

  if (ret < 0) {
    return -1;
  }

  GST_DEBUG ("decode_audio. ctx_id: %d, buffer = 0x%p",
    ctx->index, device_mem + mem_offset);

  struct audio_decode_output *decode_output = device_mem + mem_offset;
  len = decode_output->len;
  *have_data = decode_output->got_frame;
  memcpy(&ctx->audio, &decode_output->data, sizeof(AudioData));

  memcpy (samples, device_mem + mem_offset + OFFSET_PICTURE_BUFFER, len);

  GST_DEBUG ("decode_audio. sample_fmt %d sample_rate %d, channels %d, ch_layout %" PRIu64 ", len %d",
          ctx->audio.sample_fmt, ctx->audio.sample_rate, ctx->audio.channels,
          ctx->audio.channel_layout, len);

  release_device_mem(dev->fd, device_mem + mem_offset);

  return len;
}

static int
encode_audio (CodecContext *ctx, uint8_t *outbuf,
                    int max_size, uint8_t *inbuf,
                    int inbuf_size, int64_t timestamp,
                    CodecDevice *dev)
{
  int len = 0, ret = 0;
  gpointer buffer = NULL;
  uintptr_t mem_offset;
  size_t size = sizeof(inbuf_size) + inbuf_size;

  ret = secure_device_mem(dev->fd, ctx->index, inbuf_size, &buffer);
  if (ret < 0) {
    GST_ERROR ("failed to get available memory to write inbuf");
    return -1;
  }

  fill_size_header(buffer, size);
  struct audio_encode_input *encode_input = buffer + sizeof(int32_t);
  encode_input->inbuf_size = inbuf_size;
  memcpy(&encode_input->inbuf, inbuf, inbuf_size);

  mem_offset = GET_OFFSET(buffer);

  ret = invoke_device_api(dev->fd, ctx->index, CODEC_ENCODE_AUDIO, &mem_offset, SMALLDATA);

  if (ret < 0) {
    return -1;
  }

  GST_DEBUG ("encode_audio. mem_offset = 0x%"PRIXPTR, mem_offset);

  struct audio_encode_output *encode_output = device_mem + mem_offset;
  len = encode_output->len;
  if (len > 0) {
    memcpy (outbuf, &encode_output->data, len);
  }

  GST_DEBUG ("encode_audio. len: %d", len);

  release_device_mem(dev->fd, device_mem + mem_offset);

  return len;
}

//
// Interface
// MISC
//

static void
flush_buffers (CodecContext *ctx, CodecDevice *dev)
{
  GST_DEBUG ("flush buffers of context: %d", ctx->index);
  invoke_device_api (dev->fd, ctx->index, CODEC_FLUSH_BUFFERS, NULL, -1);
}
/*
static int
get_dmabuf_fd (int fd)
{
  struct v4l2_requestbuffers reqbuf;
  memset(&reqbuf, 0, sizeof(reqbuf));
  reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  reqbuf.memory = V4L2_MEMORY_DMABUF;
  reqbuf.count = 1;

  if (ioctl (fd, VIDIOC_REQBUFS, &reqbuf) == -1 ) {
    if (errno == EINVAL)
		perror("DMABUF streaming is not supported");
	else
		perror("VIDIOC_REQUEST");
  }
  return 1;
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
*/

static GList *
prepare_elements (int fd)
{
  uint32_t size = 0;
  int ret, elem_cnt, i;
  GList *elements = NULL;
  CodecElement *elem;

  //ret = ioctl (fd, IOCTL_RW(IOCTL_CMD_GET_ELEMENTS_SIZE), &size);
  ret = ioctl (fd, IOCTL_CMD_GET_ELEMENTS_SIZE, &size);
  if (ret < 0) {
    GST_ERROR ("get_elements_size failed");
    return NULL;
  }

  elem = g_malloc(size);
  //FIXME
  //An elem variable should be deallocated, will be fixed.

  //ret = ioctl (fd, IOCTL_RW(IOCTL_CMD_GET_ELEMENTS), elem);
  ret = ioctl (fd, IOCTL_CMD_GET_ELEMENTS, elem);
  if (ret < 0) {
    GST_ERROR ("get_elements failed");
    g_free (elem);
    return NULL;
  }

  elem_cnt = size / sizeof(CodecElement);
  for (i = 0; i < elem_cnt; i++) {
    elements = g_list_append (elements, &elem[i]);
  }

  return elements;
}

static int
get_profile_status (int fd)
{
  uint8_t profile_status;
  int ret;

  ret = ioctl (fd, IOCTL_RW(IOCTL_CMD_GET_PROFILE_STATUS), &profile_status);
  if (ret < 0) {
    return ret;
  }

  return profile_status;
}

// Interfaces
Interface *interface_version_4 = &(Interface) {
  .init = init,
  .deinit = deinit,
  .decode_video = decode_video,
  .decode_audio = decode_audio,
  .encode_video = encode_video,
  .encode_audio = encode_audio,
  .flush_buffers = flush_buffers,
  .buffer_alloc_and_copy = buffer_alloc_and_copy,
  .prepare_elements = prepare_elements,
  .query_capability = query_capability,
  .get_profile_status = get_profile_status,
};
