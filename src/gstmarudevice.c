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

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "gstmaruinterface.h"
#include "gstmarudevice.h"

static GMutex gst_avcodec_mutex;

#define CODEC_DEVICE_MEM_SIZE 32 * 1024 * 1024

gpointer device_mem = MAP_FAILED;
int device_fd = -1;
int opened_cnt = 0;

int
gst_maru_codec_device_open (CodecDevice *dev, int media_type)
{
  g_mutex_lock (&gst_avcodec_mutex);

  if (device_fd == -1) {
    if ((device_fd = open(CODEC_DEV, O_RDWR)) < 0) {
      GST_ERROR ("failed to open codec device.");
      g_mutex_unlock (&gst_avcodec_mutex);
      return -1;
    }
    GST_INFO ("succeeded to open %s. %d", CODEC_DEV, device_fd);
  } else {
    GST_DEBUG ("codec device is already opened");
  }
  dev->fd = device_fd;
//  g_mutex_unlock (&gst_avcodec_mutex);
#ifdef ORIG_
  // FIXME
  dev->buf_size = CODEC_DEVICE_MEM_SIZE;
  GST_DEBUG ("mmap_size: %d", dev->buf_size);

  // g_mutex_lock (&gst_avcodec_mutex);
  if (device_mem == MAP_FAILED) {
    device_mem =
      mmap (NULL, CODEC_DEVICE_MEM_SIZE, PROT_READ | PROT_WRITE,
          MAP_SHARED, device_fd, 0);
    if (device_mem == MAP_FAILED) {
      GST_ERROR ("failed to map device memory of codec");
      close (device_fd);
      dev->fd = device_fd = -1;
      g_mutex_unlock (&gst_avcodec_mutex);
      return -1;
    }
    GST_INFO ("succeeded to map device memory: %p", device_mem);
  } else {
    GST_DEBUG ("mapping device memory is already done");
  }
  dev->buf = device_mem;

  opened_cnt++;
  GST_DEBUG ("open count: %d", opened_cnt);
#endif
  g_mutex_unlock (&gst_avcodec_mutex);

  return 0;
}

int
gst_maru_codec_device_close (CodecDevice *dev)
{
  int fd = 0;

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("Failed to get %s fd %d", CODEC_DEV, fd);
    return -1;
  }

  g_mutex_lock (&gst_avcodec_mutex);
  if (opened_cnt > 0) {
    opened_cnt--;
  }
  GST_DEBUG ("open count: %d", opened_cnt);

  if (opened_cnt == 0) {
    GST_INFO ("release device memory %p", device_mem);
    if (munmap(device_mem, CODEC_DEVICE_MEM_SIZE) != 0) {
      GST_ERROR ("failed to release device memory of %s", CODEC_DEV);
    }
    device_mem = MAP_FAILED;

    GST_INFO ("close %s", CODEC_DEV);
    if (close(fd) != 0) {
      GST_ERROR ("failed to close %s fd: %d", CODEC_DEV, fd);
    }
    dev->fd = device_fd = -1;
  }
  dev->buf = MAP_FAILED;
  g_mutex_unlock (&gst_avcodec_mutex);

  return 0;
}

int
gst_maru_avcodec_open (CodecContext *ctx,
                      CodecElement *codec,
                      CodecDevice *dev)
{
  int ret;

  if (gst_maru_codec_device_open (dev, codec->media_type) < 0) {
    GST_ERROR ("failed to open device");
    return -1;
  }

  g_mutex_lock (&gst_avcodec_mutex);
  ret = interface->init (ctx, codec, dev);
  g_mutex_unlock (&gst_avcodec_mutex);

  return ret;
}

int
gst_maru_avcodec_close (CodecContext *ctx, CodecDevice *dev)
{
  int ret = 0;

  if (!ctx || (ctx->index == 0)) {
    GST_INFO ("context is null or closed before");
    return -1;
  }

  if (!dev || (dev->fd < 0)) {
    GST_INFO ("dev is null or fd is closed before");
    return -1;
  }

  GST_DEBUG ("close %d of context", ctx->index);

  g_mutex_lock (&gst_avcodec_mutex);
  interface->deinit (ctx, dev);
  g_mutex_unlock (&gst_avcodec_mutex);

  ret = gst_maru_codec_device_close (dev);

  return ret;
}
