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
#include "gstmaruinterface.h"

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

gboolean gst_maruviddec_register (GstPlugin *plugin, GList *element);
gboolean gst_maruvidenc_register (GstPlugin *plugin, GList *element);
gboolean gst_maruauddec_register (GstPlugin *plugin, GList *element);
gboolean gst_maruaudenc_register (GstPlugin *plugin, GList *element);

static GList *elements = NULL;
static gboolean codec_element_init = FALSE;
static GMutex gst_maru_mutex;

int device_version;

static gboolean
gst_maru_codec_element_init ()
{
  int fd = 0, ret = TRUE;
  void *buffer = MAP_FAILED;

  codec_element_init = TRUE;

  fd = open (CODEC_DEV, O_RDWR);
  if (fd < 0) {
    perror ("[gst-maru] failed to open codec device");
    GST_ERROR ("failed to open codec device");
    ret = FALSE;
    goto out;
  }

  // get device version
  // if version 3
  device_version = interface_version_3->get_device_version(fd);
  if (device_version < 0) {
    perror ("[gst-maru] Incompatible device version");
    GST_ERROR ("Incompatible device version");
    ret = FALSE;
    goto out;
  }

  // interface mapping
  if (device_version >= 3 && device_version < 4) {
    interface = interface_version_3;
  } else {
    perror ("[gst-maru] Incompatible device version");
    GST_ERROR ("Incompatible device version");
    ret = FALSE;
    goto out;
  }

  // prepare elements
  if ((elements = interface->prepare_elements(fd)) == NULL) {
    perror ("[gst-maru] cannot prepare elements");
    GST_ERROR ("cannot prepare elements");
    ret = FALSE;
    goto out;
  }

  // try to mmap device memory
  buffer = mmap (NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (buffer == MAP_FAILED) {
    perror ("[gst-maru] memory mapping failure");
    GST_ERROR ("memory mapping failure");
    ret = FALSE;
  }

out:
  if (buffer != MAP_FAILED) {
    munmap (buffer, 4096);
  }
  if (fd >= 0) {
    close (fd);
  }

  return ret;
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
  if (!gst_maruviddec_register (plugin, elements)) {
    GST_ERROR ("failed to register decoder elements");
    return FALSE;
  }
  if (!gst_maruauddec_register (plugin, elements)) {
    GST_ERROR ("failed to register decoder elements");
    return FALSE;
  }
  if (!gst_maruvidenc_register (plugin, elements)) {
    GST_ERROR ("failed to register encoder elements");
    return FALSE;
  }
  if (!gst_maruaudenc_register (plugin, elements)) {
    GST_ERROR ("failed to register encoder elements");
    return FALSE;
  }
  return TRUE;
}

#ifndef PACKAGE
#define PACKAGE "gst-plugins-emulator"
#endif

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  tizen-emul,
  "Codecs for Tizen Emulator",
  plugin_init,
  "1.2.0",
  "LGPL",
  "gst-plugins-emulator",
  "http://www.tizen.org"
)
