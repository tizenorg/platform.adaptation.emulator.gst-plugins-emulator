/*
 * Emulator
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
 */

/* First, include the header file for the plugin, to bring in the
 * object definition and other useful things.
 */

/*
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
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

static gboolean
gst_maru_codec_element_init ()
{
  int fd = 0, size = 0;
  int version = 0;
  int data_length = 0;
  int i, elem_cnt = 0;
  void *buffer = NULL;
  CodecElement *elem = NULL;

  fd = open (CODEC_DEV, O_RDWR);
  if (fd < 0) {
    perror ("[gst-maru] failed to open codec device");
    return FALSE;
  }

  ioctl (fd, CODEC_CMD_GET_VERSION, &version);
  if (version != CODEC_VER) {
    CODEC_LOG (INFO, "version conflict between device: %d, plugin: %d\n",
              version, CODEC_VER);
    close (fd);
    return FALSE;
  }

  buffer = mmap (NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (!buffer) {
    perror ("[gst-maru] failure memory mapping.");
    close (fd);
    return FALSE;
  }

  CODEC_LOG (DEBUG, "request a device to get codec element.\n");
  if (ioctl(fd, CODEC_CMD_GET_ELEMENT, NULL) < 0) {
    perror ("[gst-maru] failed to get codec elements");
    munmap (buffer, 4096);
    close (fd);
    return FALSE;
  }

  memcpy(&data_length, (uint8_t *)buffer, sizeof(data_length));
  size += sizeof(data_length);

  elem = g_malloc0 (data_length);
  if (!elem) {
    CODEC_LOG (ERR, "Failed to allocate memory.\n");
    munmap (buffer, 4096);
    close (fd);
    return FALSE;
  }

  memcpy (elem, (uint8_t *)buffer + size, data_length);

  elem_cnt = data_length / sizeof(CodecElement);
  for (i = 0; i < elem_cnt; i++) {
    codec_element = g_list_append (codec_element, &elem[i]);
  }

  munmap (buffer, 4096);
  close (fd);

  return TRUE;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  GST_DEBUG_CATEGORY_INIT (maru_debug,
      "tizen-maru", 0, "Tizen Emulator Codec Elements");

  gst_maru_init_pix_fmt_info ();

  if (!gst_maru_codec_element_init ()) {
    GST_ERROR ("failed to get codec elements from QEMU");
    return FALSE;
  }

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
#define PACKAGE "gst-plugins-maruator"
#endif

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "tizen-emul",
  "Codecs for Tizen Emulator",
  plugin_init,
  "0.1.1",
  "LGPL",
  "gst-plugins-emulator",
  "http://tizen.org"
)
