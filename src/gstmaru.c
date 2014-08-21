/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
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
 */

/* First, include the header file for the plugin, to bring in the
 * object definition and other useful things.
 */

/* Modifications by Samsung Electronics Co., Ltd.
 * 1. Get available Video/Audio Codecs from Qemu
 */

#include "gstmaru.h"
#include "gstmaruutils.h"

GST_DEBUG_CATEGORY (maru_debug);

#define GST_TYPE_MARUDEC \
  (gst_maru_dec_get_type())
#define GST_MARUDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MARUDEC,GstMaruDec))
#define GST_MARUDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MARUDEC,GstMaruDecClass))
#define GST_IS_MARUDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MARUDEC))
#define GST_IS_MARUDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MARUDEC))

gboolean gst_marudec_register (GstPlugin *plugin, GList *element);
gboolean gst_maruenc_register (GstPlugin *plugin, GList *element);

static GList *codec_element = NULL;
static gboolean codec_element_init = FALSE;
static GMutex gst_maru_mutex;

int device_version;

static gboolean
gst_maru_codec_element_init ()
{
  int fd = 0;
  int i, elem_cnt = 0;
  uint32_t data_length = 0;
  void *buffer = MAP_FAILED;
  CodecElement *elem = NULL;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  codec_element_init = TRUE;

  fd = open (CODEC_DEV, O_RDWR);
  if (fd < 0) {
    perror ("[gst-maru] failed to open codec device");
    GST_ERROR ("failed to open codec device");
    return FALSE;
  }

  ioctl (fd, CODEC_CMD_GET_VERSION, &device_version);
  GST_LOG ("pluigin version is [%u]", CODEC_VER);
  GST_LOG ("device version is [%u]", device_version);
  // FIXME: check version
/*
  if (device_version < CODEC_VER) {
    GST_ERROR ("plugin version [%u] is not support device version [%u]", CODEC_VER, device_version);
    close (fd);
    return FALSE;
  }
*/

  buffer = mmap (NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (buffer == MAP_FAILED) {
    perror ("[gst-maru] memory mapping failure");
    GST_ERROR ("memory mapping failure");
    close (fd);
    return FALSE;
  }

  GST_DEBUG ("request a device to get codec element");
  if (ioctl(fd, CODEC_CMD_GET_ELEMENT, &data_length) < 0) {
    perror ("[gst-maru] failed to get codec elements");
    GST_ERROR ("failed to get codec elements");
    munmap (buffer, 4096);
    close (fd);
    return FALSE;
  }

  GST_DEBUG ("total size of codec elements %d", data_length);
  elem = g_malloc0 (data_length);
  if (!elem) {
    GST_ERROR ("failed to allocate memory for codec elements");
    munmap (buffer, 4096);
    close (fd);
    return FALSE;
  }

  if (ioctl(fd, CODEC_CMD_GET_ELEMENT_DATA, elem) < 0) {
    GST_ERROR ("failed to get codec elements");
    munmap (buffer, 4096);
    close (fd);
    return FALSE;
  }

  elem_cnt = data_length / sizeof(CodecElement);
  for (i = 0; i < elem_cnt; i++) {
    codec_element = g_list_append (codec_element, &elem[i]);
  }

  munmap (buffer, 4096);
  close (fd);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return TRUE;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  GST_DEBUG_CATEGORY_INIT (maru_debug,
      "tizen-emul", 0, "Tizen Emulator Codec Elements");

  gst_maru_init_pix_fmt_info ();

  g_mutex_lock (&gst_maru_mutex);
  if (!codec_element_init) {
    if (!gst_maru_codec_element_init ()) {
      g_mutex_unlock (&gst_maru_mutex);

      GST_ERROR ("failed to get codec elements from QEMU");
      return FALSE;
    }
  }
  g_mutex_unlock (&gst_maru_mutex);

  if (!gst_marudec_register (plugin, codec_element)) {
    GST_ERROR ("failed to register decoder elements");
    return FALSE;
  }
  if (!gst_maruenc_register (plugin, codec_element)) {
    GST_ERROR ("failed to register encoder elements");
    return FALSE;
  }

#if 0
  while ((codec_element = g_list_next (codec_element))) {
    g_list_free (codec_element);
  }
#endif

  return TRUE;
}

#ifndef PACKAGE
#define PACKAGE "gst-plugins-emulator"
#endif

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "tizen-emul",
  "Codecs for Tizen Emulator",
  plugin_init,
  "0.3.0",
  "LGPL",
  "gst-plugins-emulator",
  "http://www.tizen.org"
)
