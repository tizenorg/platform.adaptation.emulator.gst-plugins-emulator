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

#include "gstmaruutils.h"
#include <gst/audio/multichannel.h>
#include <gst/pbutils/codec-utils.h>

gint
gst_maru_smpfmt_depth (int smp_fmt)
{
  gint depth = -1;

  switch (smp_fmt) {
  case SAMPLE_FMT_U8:
    depth = 1;
    break;
  case SAMPLE_FMT_S16:
    depth = 2;
    break;
  case SAMPLE_FMT_S32:
  case SAMPLE_FMT_FLT:
    depth = 4;
    break;
  case SAMPLE_FMT_DBL:
    depth = 8;
    break;
  default:
    GST_ERROR ("Unhandled sample format !");
    break;
  }

  return depth;
}

// FFmpeg
static const struct
{
  guint64 ff;
  GstAudioChannelPosition gst;
} _ff_to_gst_layout[] = {
  {
  CH_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT}, {
  CH_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}, {
  CH_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER}, {
  CH_LOW_FREQUENCY, GST_AUDIO_CHANNEL_POSITION_LFE}, {
  CH_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT}, {
  CH_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
  CH_FRONT_LEFT_OF_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER}, {
  CH_FRONT_RIGHT_OF_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER}, {
  CH_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_REAR_CENTER}, {
  CH_SIDE_LEFT, GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT}, {
  CH_SIDE_RIGHT, GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}, {
  CH_TOP_CENTER, GST_AUDIO_CHANNEL_POSITION_NONE}, {
  CH_TOP_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_NONE}, {
  CH_TOP_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_NONE}, {
  CH_TOP_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_NONE}, {
  CH_TOP_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_NONE}, {
  CH_TOP_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_NONE}, {
  CH_TOP_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_NONE}, {
  CH_STEREO_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT}, {
  CH_STEREO_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}
};

static GstAudioChannelPosition *
gst_ff_channel_layout_to_gst (guint64 channel_layout, guint channels)
{
  guint nchannels = 0, i, j;
  GstAudioChannelPosition *pos = NULL;
  gboolean none_layout = FALSE;

  for (i = 0; i < 64; i++) {
    if ((channel_layout & (G_GUINT64_CONSTANT (1) << i)) != 0) {
      nchannels++;
    }
  }

  if (channel_layout == 0) {
    nchannels = channels;
    none_layout = TRUE;
  }

  if (nchannels != channels) {
    GST_ERROR ("Number of channels is different (%u != %u)", channels,
        nchannels);
    return NULL;
  }

  pos = g_new (GstAudioChannelPosition, nchannels);

  for (i = 0, j = 0; i < G_N_ELEMENTS (_ff_to_gst_layout); i++) {
    if ((channel_layout & _ff_to_gst_layout[i].ff) != 0) {
      pos[j++] = _ff_to_gst_layout[i].gst;

      if (_ff_to_gst_layout[i].gst == GST_AUDIO_CHANNEL_POSITION_NONE) {
        none_layout = TRUE;
      }
    }
  }

  if (j != nchannels) {
    GST_WARNING ("Unknown channels in channel layout - assuming NONE layout");
    none_layout = TRUE;
  }

  if (!none_layout && !gst_audio_check_channel_positions (pos, nchannels)) {
    GST_ERROR ("Invalid channel layout %" G_GUINT64_FORMAT
      " - assuming NONE layout", channel_layout);
    none_layout = TRUE;
  }

  if (none_layout) {
    if (nchannels == 1) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_MONO;
    } else if (nchannels == 2) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
    } else if (channel_layout == 0) {
      g_free (pos);
      pos = NULL;
    } else {
      for (i = 0; i < nchannels; i++) {
        pos[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
      }
    }
  }

  if (nchannels == 1 && pos[0] == GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER) {
    GST_DEBUG ("mono common case; won't set channel positions");
    g_free (pos);
    pos = NULL;
  } else if (nchannels == 2 && pos[0] == GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT
    && pos[1] == GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT) {
    GST_DEBUG ("stereo common case; won't set channel positions");
    g_free (pos);
    pos = NULL;
  }

  return pos;
}

GstCaps*
gst_maru_codectype_to_video_caps (CodecContext *ctx, const char *name,
    gboolean encode, CodecElement *codec)
{
  GstCaps *caps;

  GST_DEBUG ("context: %p, codec: %s, encode: %d, pixel format: %d",
      ctx, name, encode, ctx->video.pix_fmt);

  if (ctx) {
    caps = gst_maru_pixfmt_to_caps (ctx->video.pix_fmt, ctx, name);
  } else {
    GstCaps *temp;
    enum PixelFormat i;
    CodecContext ctx;

    caps = gst_caps_new_empty ();
    for (i = 0; i <= PIX_FMT_NB; i++) {
      temp = gst_maru_pixfmt_to_caps (i, encode ? &ctx : NULL, name);
      if (temp != NULL) {
        gst_caps_append (caps, temp);
      }
    }
  }

  return caps;
}

GstCaps *
gst_maru_codectype_to_audio_caps (CodecContext *ctx, const char *name,
    gboolean encode, CodecElement *codec)
{
  GstCaps *caps = NULL;

  GST_DEBUG ("context: %p, codec: %s, encode: %d, codec: %p",
      ctx, name, encode, codec);

  if (ctx) {
    caps = gst_maru_smpfmt_to_caps (ctx->audio.sample_fmt, ctx, name);
  } else if (codec && codec->sample_fmts[0] != -1){
    GstCaps *temp;
    int i;

    caps = gst_caps_new_empty ();
    for (i = 0; codec->sample_fmts[i] != -1; i++) {
      temp =
          gst_maru_smpfmt_to_caps (codec->sample_fmts[i], ctx, name);
      if (temp != NULL) {
        gst_caps_append (caps, temp);
      }
    }
  } else {
    GstCaps *temp;
    int i;
    CodecContext ctx = {{0}, {0}, 0};

    ctx.audio.channels = -1;
    caps = gst_caps_new_empty ();
    for (i = 0; i <= SAMPLE_FMT_DBL; i++) {
      temp = gst_maru_smpfmt_to_caps (i, encode ? &ctx : NULL, name);
      if (temp != NULL) {
        gst_caps_append (caps, temp);
      }
    }
  }

  return caps;
}

GstCaps*
gst_maru_codectype_to_caps (int media_type, CodecContext *ctx,
    const char *name, gboolean encode)
{
  GstCaps *caps;

  switch (media_type) {
  case AVMEDIA_TYPE_VIDEO:
    caps =
        gst_maru_codectype_to_video_caps (ctx, name, encode, NULL);
    break;
  case AVMEDIA_TYPE_AUDIO:
    caps =
        gst_maru_codectype_to_audio_caps (ctx, name, encode, NULL);
   break;
  default:
    caps = NULL;
    break;
  }

  return caps;
}

void
gst_maru_caps_to_pixfmt (const GstCaps *caps, CodecContext *ctx, gboolean raw)
{
  GstStructure *str;
  const GValue *fps;
  const GValue *par = NULL;

  GST_DEBUG ("converting caps %" GST_PTR_FORMAT, caps);
  g_return_if_fail (gst_caps_get_size (caps) == 1);
  str = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (str, "width", &ctx->video.width);
  gst_structure_get_int (str, "height", &ctx->video.height);
  gst_structure_get_int (str, "bpp", &ctx->video.bpp);

  fps = gst_structure_get_value (str, "framerate");
  if (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps)) {
    ctx->video.fps_d = gst_value_get_fraction_numerator (fps);
    ctx->video.fps_n = gst_value_get_fraction_denominator (fps);
  ctx->video.ticks_per_frame = 1;

    GST_DEBUG ("setting framerate %d/%d = %lf",
        ctx->video.fps_d, ctx->video.fps_n,
        1. * ctx->video.fps_d / ctx->video.fps_n);
  }

  par = gst_structure_get_value (str, "pixel-aspect-ratio");
  if (par && GST_VALUE_HOLDS_FRACTION (par)) {
    ctx->video.par_n = gst_value_get_fraction_numerator (par);
    ctx->video.par_d = gst_value_get_fraction_denominator (par);
  }

  if (!raw) {
    return;
  }

  g_return_if_fail (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps));

  if (strcmp (gst_structure_get_name (str), "video/x-raw-yuv") == 0) {
    guint32 fourcc;

    if (gst_structure_get_fourcc (str, "format", &fourcc)) {
    switch (fourcc) {
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
        ctx->video.pix_fmt = PIX_FMT_YUYV422;
        break;
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
        ctx->video.pix_fmt = PIX_FMT_YUV420P;
        break;
      case GST_MAKE_FOURCC ('A', '4', '2', '0'):
        ctx->video.pix_fmt = PIX_FMT_YUVA420P;
        break;
      case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
        ctx->video.pix_fmt = PIX_FMT_YUV411P;
        break;
      case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
        ctx->video.pix_fmt = PIX_FMT_YUV422P;
        break;
      case GST_MAKE_FOURCC ('Y', 'U', 'V', '9'):
        ctx->video.pix_fmt = PIX_FMT_YUV410P;
        break;
      }
    }
//    printf ("get pixel format: %d, fourcc: %d\n", ctx->video.pix_fmt, fourcc);
  } else if (strcmp (gst_structure_get_name (str), "video/x-raw-rgb") == 0) {
    gint bpp = 0, rmask = 0, endianness = 0;

    if (gst_structure_get_int (str, "bpp", &bpp) &&
      gst_structure_get_int (str, "endianness", &endianness)) {
      if (gst_structure_get_int (str, "red_mask", &rmask)) {
        switch (bpp) {
        case 32:
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
          if (rmask == 0x00ff0000) {
#else
          if (rmask == 0x00ff0000) {
#endif
            ctx->video.pix_fmt = PIX_FMT_RGB32;
          }
          break;
        case 24:
          if (rmask == 0x0000FF) {
            ctx->video.pix_fmt = PIX_FMT_BGR24;
          } else {
            ctx->video.pix_fmt = PIX_FMT_RGB24;
          }
          break;
        case 16:
          if (endianness == G_BYTE_ORDER) {
            ctx->video.pix_fmt = PIX_FMT_RGB565;
          }
          break;
        case 15:
          if (endianness == G_BYTE_ORDER) {
            ctx->video.pix_fmt = PIX_FMT_RGB555;
          }
          break;
        default:
          break;
        }
      }
    } else {
      if (bpp == 8) {
        ctx->video.pix_fmt = PIX_FMT_PAL8;
        // get palette
      }
    }
  } else if (strcmp (gst_structure_get_name (str), "video/x-raw-gray") == 0) {
    gint bpp = 0;

    if (gst_structure_get_int (str, "bpp", &bpp)) {
      switch (bpp) {
      case 8:
        ctx->video.pix_fmt = PIX_FMT_GRAY8;
        break;
      }
    }
  }
}

void
gst_maru_caps_to_smpfmt (const GstCaps *caps, CodecContext *ctx, gboolean raw)
{
  GstStructure *str;
  gint depth = 0, width = 0, endianness = 0;
  gboolean signedness = FALSE;
  const gchar *name;

  g_return_if_fail (gst_caps_get_size (caps) == 1);
  str = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (str, "channels", &ctx->audio.channels);
  gst_structure_get_int (str, "rate", &ctx->audio.sample_rate);
  gst_structure_get_int (str, "block_align", &ctx->audio.block_align);
//  gst_structure_get_int (str, "bitrate", &ctx->audio.bit_rate);
  gst_structure_get_int (str, "bitrate", &ctx->bit_rate);

  if (!raw) {
    return;
  }

  name = gst_structure_get_name (str);

  if (!strcmp (name, "audio/x-raw-float")) {
    if (gst_structure_get_int (str, "width", &width) &&
      gst_structure_get_int (str, "endianness", &endianness)) {
      if (endianness == G_BYTE_ORDER) {
        if (width == 32) {
          ctx->audio.sample_fmt = SAMPLE_FMT_FLT;
        } else if (width == 64) {
          ctx->audio.sample_fmt = SAMPLE_FMT_DBL;
        }
      }
    }
  } else {
    if (gst_structure_get_int (str, "width", &width) &&
      gst_structure_get_int (str, "depth", &depth) &&
      gst_structure_get_boolean (str, "signed", &signedness) &&
      gst_structure_get_int (str, "endianness", &endianness)) {
      if ((endianness == G_BYTE_ORDER) && (signedness == TRUE)) {
        if ((width == 16) && (depth == 16)) {
          ctx->audio.sample_fmt = SAMPLE_FMT_S16;
        } else if ((width == 32) && (depth == 32)) {
          ctx->audio.sample_fmt = SAMPLE_FMT_S32;
        }
      }
    }
  }
}

void
gst_maru_caps_with_codecname (const char *name, int media_type,
    const GstCaps *caps, CodecContext *ctx)
{
  GstStructure *structure;
  const GValue *value;
  const GstBuffer *buf;

  if (!ctx || !gst_caps_get_size (caps)) {
    return;
  }

  structure = gst_caps_get_structure (caps, 0);

  if ((value = gst_structure_get_value (structure, "codec_data"))) {
    guint size;
    guint8 *data;

    buf = GST_BUFFER_CAST (gst_value_get_mini_object (value));
    size = GST_BUFFER_SIZE (buf);
    data = GST_BUFFER_DATA (buf);
    GST_DEBUG ("extradata: %p, size: %d\n", data, size);

    if (ctx->codecdata) {
      g_free (ctx->codecdata);
    }

    ctx->codecdata =
        g_malloc0 (GST_ROUND_UP_16 (size + FF_INPUT_BUFFER_PADDING_SIZE));
    memcpy (ctx->codecdata, data, size);
    ctx->codecdata_size = size;

    if ((strcmp (name, "vc1") == 0) && size > 0 && data[0] == 0) {
      ctx->codecdata[0] = (guint8) size;
    }
  } else if (ctx->codecdata == NULL) {
    ctx->codecdata_size = 0;
    ctx->codecdata = g_malloc0 (GST_ROUND_UP_16(FF_INPUT_BUFFER_PADDING_SIZE));
    GST_DEBUG ("no extra data.\n");
  }

  if ((strcmp (name, "mpeg4") == 0)) {
    const gchar *mime = gst_structure_get_name (structure);

    if (!strcmp (mime, "video/x-divx")) {
      ctx->codec_tag = GST_MAKE_FOURCC ('D', 'I', 'V', 'X');
    } else if (!strcmp (mime, "video/x-xvid")) {
      ctx->codec_tag = GST_MAKE_FOURCC ('X', 'V', 'I', 'D');
    } else if (!strcmp (mime, "video/x-3ivx")) {
      ctx->codec_tag = GST_MAKE_FOURCC ('3', 'I', 'V', '1');
    } else if (!strcmp (mime, "video/mpeg")) {
      ctx->codec_tag = GST_MAKE_FOURCC ('m', 'p', '4', 'v');
    }
#if 0
  } else if (strcmp (name, "h263p") == 0) {
    gboolean val;

    if (!gst_structure_get_boolean (structure, "annex-f", &val) || val) {
      ctx->flags |= CODEC_FLAG_4MV;
    } else {
      ctx->flags &= ~CODEC_FLAG_4MV;
    }
    if ((!gst_structure_get_boolean (structure, "annex-i", &val) || val) &&
      (!gst_structure_get_boolean (structure, "annex-t", &val) || val)) {
      ctx->flags |= CODEC_FLAG_AC_PRED;
    } else {
      ctx->flags &= ~CODEC_FLAG_AC_PRED;
    }
    if ((!gst_structure_get_boolean (structure, "annex-j", &val) || val)) {
      ctx->flags |= CODEC_FLAG_LOOP_FILTER;
    } else {
      ctx->flags &= ~CODEC_FLAG_LOOP_FILTER;
    }
#endif
  } else {
    // TODO
  }

  if (!gst_caps_is_fixed (caps)) {
    return;
  }

  switch (media_type) {
  case AVMEDIA_TYPE_VIDEO:
    gst_maru_caps_to_pixfmt (caps, ctx, FALSE);
  // get_palette
    break;
  case AVMEDIA_TYPE_AUDIO:
    gst_maru_caps_to_smpfmt (caps, ctx, FALSE);
    break;
  default:
    break;
  }
}

void
gst_maru_caps_to_codecname (const GstCaps *caps, gchar *codec_name, CodecContext *context)
{
  const gchar *mimetype;
  const GstStructure *str;

  str = gst_caps_get_structure (caps, 0);

  mimetype = gst_structure_get_name (str);

  if (!strcmp (mimetype, "video/x-wmv")) {
    gint wmvversion = 0;

    if (gst_structure_get_int (str, "wmvversion", &wmvversion)) {
      switch (wmvversion) {
        case 1:
          g_strlcpy(codec_name, "wmv1", 32);
          break;
        case 2:
          g_strlcpy(codec_name, "wmv2", 32);
          break;
        case 3:
        {
          guint32 fourcc;

          g_strlcpy(codec_name, "wmv3", 32);

          if (gst_structure_get_fourcc (str, "format", &fourcc)) {
            if ((fourcc == GST_MAKE_FOURCC ('W', 'V', 'C', '1')) ||
                (fourcc == GST_MAKE_FOURCC ('W', 'M', 'V', 'A'))) {
              g_strlcpy(codec_name, "vc1", 32);
            }
          }
        }
          break;
      }
    }
  }

#if 0
  if (context != NULL) {
    if (video == TRUE) {
      context->codec_type = CODEC_TYPE_VIDEO;
    } else if (audio == TRUE) {
      context->codec_type = CODEC_TYPE_AUDIO;
    } else {
      context->codec_type = CODEC_TYPE_UNKNOWN;
    }
    context->codec_id = id;
    gst_maru_caps_with_codecname (name, context->codec_type, caps, context);
  }
#endif

  if (codec_name != NULL) {
    GST_DEBUG ("The %s belongs to the caps %" GST_PTR_FORMAT, codec_name, caps);
  } else {
    GST_WARNING ("Couldn't figure out the name for caps %" GST_PTR_FORMAT, caps);
  }
}

void
gst_maru_caps_with_codectype (int media_type, const GstCaps *caps, CodecContext *ctx)
{
  if (ctx == NULL) {
    return;
  }

  switch (media_type) {
  case AVMEDIA_TYPE_VIDEO:
    gst_maru_caps_to_pixfmt (caps, ctx, TRUE);
    break;
  case AVMEDIA_TYPE_AUDIO:
    gst_maru_caps_to_smpfmt (caps, ctx, TRUE);
    break;
  default:
    break;
  }
}

GstCaps *
gst_maru_video_caps_new (CodecContext *ctx, const char *name,
        const char *mimetype, const char *fieldname, ...)
{
  GstStructure *structure = NULL;
  GstCaps *caps = NULL;
  va_list var_args;
  gint i;

  GST_LOG ("context: %p, name: %s, mimetype: %s", ctx, name, mimetype);

  if (ctx != NULL && ctx->video.width != -1) {
    gint num, denom;

    caps = gst_caps_new_simple (mimetype,
      "width", G_TYPE_INT, ctx->video.width,
      "height", G_TYPE_INT, ctx->video.height, NULL);

    num = ctx->video.fps_d / ctx->video.ticks_per_frame;
    denom = ctx->video.fps_n;

    if (!denom) {
      GST_LOG ("invalid framerate: %d/0, -> %d/1", num, num);
    }
    if (gst_util_fraction_compare (num, denom, 1000, 1) > 0) {
      GST_LOG ("excessive framerate: %d/%d, -> 0/1", num, denom);
      num = 0;
      denom = 1;
    }
    GST_LOG ("setting framerate: %d/%d", num, denom);
    gst_caps_set_simple (caps,
      "framerate", GST_TYPE_FRACTION, num, denom, NULL);
  } else {
    if (strcmp (name, "h263") == 0) {
      /* 128x96, 176x144, 352x288, 704x576, and 1408x1152. slightly reordered
       * because we want automatic negotiation to go as close to 320x240 as
       * possible. */
      const static gint widths[] = { 352, 704, 176, 1408, 128 };
      const static gint heights[] = { 288, 576, 144, 1152, 96 };
      GstCaps *temp;
      gint n_sizes = G_N_ELEMENTS (widths);

      caps = gst_caps_new_empty ();
      for (i = 0; i < n_sizes; i++) {
        temp = gst_caps_new_simple (mimetype,
            "width", G_TYPE_INT, widths[i],
            "height", G_TYPE_INT, heights[i],
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

        gst_caps_append (caps, temp);
      }
    } else if (strcmp (name, "none") == 0) {
      GST_LOG ("default caps");
    }
  }

  /* no fixed caps or special restrictions applied;
   * default unfixed setting */
  if (!caps) {
    GST_DEBUG ("Creating default caps");
    caps = gst_caps_new_simple (mimetype,
            "width", GST_TYPE_INT_RANGE, 16, 4096,
            "height", GST_TYPE_INT_RANGE, 16, 4096,
             "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    va_start (var_args, fieldname);
    structure = gst_caps_get_structure (caps, i);
    gst_structure_set_valist (structure, fieldname, var_args);
    va_end (var_args);
  }

  return caps;
}

GstCaps *
gst_maru_audio_caps_new (CodecContext *ctx, const char *name,
        const char *mimetype, const char *fieldname, ...)
{
  GstStructure *structure = NULL;
  GstCaps *caps = NULL;
  gint i;
  va_list var_args;

  if (ctx != NULL && ctx->audio.channels != -1) {
    GstAudioChannelPosition *pos;
    guint64 channel_layout = ctx->audio.channel_layout;

    if (channel_layout == 0) {
      const guint64 default_channel_set[] = {
        0, 0, CH_LAYOUT_SURROUND, CH_LAYOUT_QUAD, CH_LAYOUT_5POINT0,
        CH_LAYOUT_5POINT1, 0, CH_LAYOUT_7POINT1
      };

      if (strcmp (name, "ac3") == 0) {
        if (ctx->audio.channels > 0 &&
          ctx->audio.channels < G_N_ELEMENTS (default_channel_set)) {
          channel_layout = default_channel_set[ctx->audio.channels - 1];
        }
      } else {
        // TODO
      }
    }

    caps = gst_caps_new_simple (mimetype,
            "rate", G_TYPE_INT, ctx->audio.sample_rate,
            "channels", G_TYPE_INT, ctx->audio.channels, NULL);

    pos = gst_ff_channel_layout_to_gst (channel_layout, ctx->audio.channels);
    if (pos != NULL) {
      gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
      g_free (pos);
    }
  } else {
    gint maxchannels = 2;
    const gint *rates = NULL;
    gint n_rates = 0;

    if (strcmp (name, "aac") == 0) {
      maxchannels = 6;
    } else if (g_str_has_prefix(name, "ac3")) {
      const static gint l_rates[] = { 48000, 44100, 32000 };
      maxchannels = 6;
      n_rates = G_N_ELEMENTS (l_rates);
      rates = l_rates;
    } else {
     // TODO
    }

    if (maxchannels == 1) {
      caps = gst_caps_new_simple(mimetype,
              "channels", G_TYPE_INT, maxchannels, NULL);
    } else {
      caps = gst_caps_new_simple(mimetype,
              "channels", GST_TYPE_INT_RANGE, 1, maxchannels, NULL);
    }

    if (n_rates) {
      GValue list = { 0, };
      GstStructure *structure;

      g_value_init(&list, GST_TYPE_LIST);
      for (i = 0; i < n_rates; i++) {
        GValue v = { 0, };

        g_value_init(&v, G_TYPE_INT);
        g_value_set_int(&v, rates[i]);
        gst_value_list_append_value(&list, &v);
        g_value_unset(&v);
      }
      structure = gst_caps_get_structure(caps, 0);
      gst_structure_set_value(structure, "rate", &list);
      g_value_unset(&list);
    } else {
      gst_caps_set_simple(caps, "rate", GST_TYPE_INT_RANGE, 4000, 96000, NULL);
    }
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    va_start (var_args, fieldname);
    structure = gst_caps_get_structure (caps, i);
    gst_structure_set_valist (structure, fieldname, var_args);
    va_end (var_args);
  }

  return caps;
}

GstCaps *
gst_maru_pixfmt_to_caps (enum PixelFormat pix_fmt, CodecContext *ctx, const char *name)
{
  GstCaps *caps = NULL;

  int bpp = 0, depth = 0, endianness = 0;
  gulong g_mask = 0, r_mask = 0, b_mask = 0, a_mask = 0;
  guint32 fmt = 0;

  switch (pix_fmt) {
  case PIX_FMT_YUV420P:
    fmt = GST_MAKE_FOURCC ('I', '4', '2', '0');
    break;
  case PIX_FMT_YUYV422:
    fmt = GST_MAKE_FOURCC ('A', '4', '2', '0');
    break;
  case PIX_FMT_RGB24:
    bpp = depth = 24;
    endianness = G_BIG_ENDIAN;
    r_mask = 0xff0000;
    g_mask = 0x00ff00;
    b_mask = 0x0000ff;
    break;
  case PIX_FMT_BGR24:
    bpp = depth = 24;
    endianness = G_BIG_ENDIAN;
    r_mask = 0x0000ff;
    g_mask = 0x00ff00;
    b_mask = 0xff0000;
    break;
  case PIX_FMT_YUV422P:
    fmt = GST_MAKE_FOURCC ('Y', '4', '2', 'B');
    break;
  case PIX_FMT_YUV444P:
    fmt = GST_MAKE_FOURCC ('Y', '4', '4', '4');
    break;
  case PIX_FMT_RGB32:
    bpp = 32;
    depth = 32;
    endianness = G_BIG_ENDIAN;
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    r_mask = 0x00ff0000;
    g_mask = 0x0000ff00;
    b_mask = 0x000000ff;
    a_mask = 0xff000000;
#else
    r_mask = 0x00ff0000;
    g_mask = 0x0000ff00;
    b_mask = 0x000000ff;
    a_mask = 0xff000000;
#endif
    break;
  case PIX_FMT_YUV410P:
    fmt = GST_MAKE_FOURCC ('Y', 'U', 'V', '9');
    break;
  case PIX_FMT_YUV411P:
    fmt = GST_MAKE_FOURCC ('Y', '4', '1', 'b');
    break;
  case PIX_FMT_RGB565:
    bpp = depth = 16;
    endianness = G_BYTE_ORDER;
    r_mask = 0xf800;
    g_mask = 0x07e0;
    b_mask = 0x001f;
    break;
  case PIX_FMT_RGB555:
    bpp = 16;
    depth = 15;
    endianness = G_BYTE_ORDER;
    r_mask = 0x7c00;
    g_mask = 0x03e0;
    b_mask = 0x001f;
    break;
  default:
    break;
  }

  if (caps == NULL) {
    if (bpp != 0) {
      if (r_mask != 0) {
        if (a_mask) {
        caps = gst_maru_video_caps_new (ctx, name, "video/x-raw-rgb",
                "bpp", G_TYPE_INT, bpp,
                "depth", G_TYPE_INT, depth,
                "red_mask", G_TYPE_INT, r_mask,
                "green_mask", G_TYPE_INT, g_mask,
                "blue_mask", G_TYPE_INT, b_mask,
                "alpha_mask", G_TYPE_INT, a_mask,
                "endianness", G_TYPE_INT, endianness, NULL);
        } else {
          caps = gst_maru_video_caps_new (ctx, name, "video/x-raw-rgb",
                  "bpp", G_TYPE_INT, bpp,
                  "depth", G_TYPE_INT, depth,
                  "red_mask", G_TYPE_INT, r_mask,
                  "green_mask", G_TYPE_INT, g_mask,
                  "blue_mask", G_TYPE_INT, b_mask,
                  "alpha_mask", G_TYPE_INT, a_mask,
                  "endianness", G_TYPE_INT, endianness, NULL);
        }
      } else {
        caps = gst_maru_video_caps_new (ctx, name, "video/x-raw-rgb",
                  "bpp", G_TYPE_INT, bpp,
                  "depth", G_TYPE_INT, depth,
                  "endianness", G_TYPE_INT, endianness, NULL);
        if (caps && ctx) {
          // set paletee
        }
      }
    } else if (fmt) {
      caps = gst_maru_video_caps_new (ctx, name, "video/x-raw-yuv",
               "format", GST_TYPE_FOURCC, fmt, NULL);
    }
  }

  if (caps != NULL) {
    GST_DEBUG ("caps for pix_fmt=%d: %", GST_PTR_FORMAT, pix_fmt, caps);
  } else {
    GST_LOG ("No caps found for pix_fmt=%d", pix_fmt);
  }

  return caps;
}

GstCaps *
gst_maru_smpfmt_to_caps (int8_t sample_fmt, CodecContext *ctx, const char *name)
{
  GstCaps *caps = NULL;

  int bpp = 0;
  gboolean integer = TRUE;
  gboolean signedness = FALSE;

  switch (sample_fmt) {
  case SAMPLE_FMT_S16:
    signedness = TRUE;
    bpp = 16;
    break;
  case SAMPLE_FMT_S32:
    signedness = TRUE;
    bpp = 32;
    break;
  case SAMPLE_FMT_FLT:
    integer = FALSE;
    bpp = 32;
    break;
  case SAMPLE_FMT_DBL:
    integer = FALSE;
    bpp = 64;
    break;
  default:
    break;
  }

  if (bpp) {
    if (integer) {
      caps = gst_maru_audio_caps_new (ctx, name, "audio/x-raw-int",
          "signed", G_TYPE_BOOLEAN, signedness,
          "endianness", G_TYPE_INT, G_BYTE_ORDER,
          "width", G_TYPE_INT, bpp, "depth", G_TYPE_INT, bpp, NULL);
    } else {
      caps = gst_maru_audio_caps_new (ctx, name, "audio/x-raw-float",
          "endianness", G_TYPE_INT, G_BYTE_ORDER,
          "width", G_TYPE_INT, bpp, NULL);
    }
  }

  if (caps != NULL) {
    GST_LOG ("caps for sample_fmt=%d: %" GST_PTR_FORMAT, sample_fmt, caps);
  } else {
    GST_LOG ("No caps found for sample_fmt=%d", sample_fmt);
  }

  return caps;
}

GstCaps *
gst_maru_codecname_to_caps (const char *name, CodecContext *ctx, gboolean encode)
{
  GstCaps *caps = NULL;

  GST_LOG ("codec: %s, context: %p, encode: %d", name, ctx, encode);

  if (strcmp (name, "mpegvideo") == 0) {
    caps = gst_maru_video_caps_new (ctx, name, "video/mpeg",
                "mpegversion", G_TYPE_INT, 1,
                "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
  } else if (strcmp (name, "h263") == 0) {
    if (encode) {
      caps = gst_maru_video_caps_new (ctx, name, "video/x-h263",
                  "variant", G_TYPE_STRING, "itu", NULL);
    } else {
      caps = gst_maru_video_caps_new (ctx, "none", "video/x-h263",
                  "variant", G_TYPE_STRING, "itu", NULL);
    }
  } else if (strcmp (name, "h263p") == 0) {
    caps = gst_maru_video_caps_new (ctx, name, "video/x-h263",
              "variant", G_TYPE_STRING, "itu",
              "h263version", G_TYPE_STRING, "h263p", NULL);
#if 0
    if (encode && ctx) {
      gst_caps_set_simple (caps,
        "annex-f", G_TYPE_BOOLEAN, ctx->flags & CODEC_FLAG_4MV,
        "annex-j", G_TYPE_BOOLEAN, ctx->flags & CODEC_FLAG_LOOP_FILTER,
        "annex-i", G_TYPE_BOOLEAN, ctx->flags & CODEC_FLAG_AC_PRED,
        "annex-t", G_TYPE_BOOLEAN, ctx->flags & CODEC_FLAG_AC_PRED,
        NULL);
    }
#endif
  } else if (strcmp (name, "mpeg4") == 0) {
    if (encode && ctx != NULL) {
      // TODO
    switch (ctx->codec_tag) {
    case GST_MAKE_FOURCC ('D', 'I', 'V', 'X'):
      caps = gst_maru_video_caps_new (ctx, name, "video/x-divx",
        "divxversion", G_TYPE_INT, 5, NULL);
      break;
    case GST_MAKE_FOURCC ('m', 'p', '4', 'v'):
    default:
      caps = gst_maru_video_caps_new (ctx, name, "video/mpeg",
        "systemstream", G_TYPE_BOOLEAN, FALSE,
        "mpegversion", G_TYPE_INT, 4, NULL);
      break;
    }
    } else {
      caps = gst_maru_video_caps_new (ctx, name, "video/mpeg",
            "mpegversion", G_TYPE_INT, 4,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      if (encode) {
        caps = gst_maru_video_caps_new (ctx, name, "video/mpeg",
            "mpegversion", G_TYPE_INT, 4,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      } else {
        gst_caps_append (caps, gst_maru_video_caps_new (ctx, name,
            "video/x-divx", "divxversion", GST_TYPE_INT_RANGE, 4, 5, NULL));
        gst_caps_append (caps, gst_maru_video_caps_new (ctx, name,
            "video/x-xvid", NULL));
        gst_caps_append (caps, gst_maru_video_caps_new (ctx, name,
            "video/x-3ivx", NULL));
      }
    }
  } else if (strcmp (name, "h264") == 0) {
      caps = gst_maru_video_caps_new (ctx, name, "video/x-h264", NULL);
  } else if (g_str_has_prefix(name, "msmpeg4")) {
    // msmpeg4v1,m msmpeg4v2, msmpeg4
    gint version;

    if (strcmp (name, "msmpeg4v1") == 0) {
      version = 41;
    } else if (strcmp (name, "msmpeg4v2") == 0) {
      version = 42;
    } else {
      version = 43;
    }

    caps = gst_maru_video_caps_new (ctx, name, "video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, version, NULL);
    if (!encode && !strcmp (name, "msmpeg4")) {
       gst_caps_append (caps, gst_maru_video_caps_new (ctx, name,
            "video/x-divx", "divxversion", G_TYPE_INT, 3, NULL));
    }
  } else if (strcmp (name, "wmv3") == 0) {
    caps = gst_maru_video_caps_new (ctx, name, "video/x-wmv",
                "wmvversion", G_TYPE_INT, 3, NULL);
  } else if (strcmp (name, "vc1") == 0) {
    caps = gst_maru_video_caps_new (ctx, name, "video/x-wmv",
                "wmvversion", G_TYPE_INT, 3, "format", GST_TYPE_FOURCC,
                GST_MAKE_FOURCC ('W', 'V', 'C', '1'),  NULL);
#if 0
  } else if (strcmp (name, "vp3") == 0) {
    mime_type = g_strdup ("video/x-vp3");
  } else if (strcmp (name, "vp8") == 0) {
    mime_type = g_strdup ("video/x-vp8");
#endif
  } else if (strcmp (name, "aac") == 0) {
    caps = gst_maru_audio_caps_new (ctx, name, "audio/mpeg", NULL);
    if (!encode) {
        GValue arr = { 0, };
        GValue item = { 0, };

        g_value_init (&arr, GST_TYPE_LIST);
        g_value_init (&item, G_TYPE_INT);
        g_value_set_int (&item, 2);
        gst_value_list_append_value (&arr, &item);
        g_value_set_int (&item, 4);
        gst_value_list_append_value (&arr, &item);
        g_value_unset (&item);

        gst_caps_set_value (caps, "mpegversion", &arr);
        g_value_unset (&arr);

        g_value_init (&arr, GST_TYPE_LIST);
        g_value_init (&item, G_TYPE_STRING);
        g_value_set_string (&item, "raw");
        gst_value_list_append_value (&arr, &item);
        g_value_set_string (&item, "adts");
        gst_value_list_append_value (&arr, &item);
        g_value_set_string (&item, "adif");
        gst_value_list_append_value (&arr, &item);
        g_value_unset (&item);

        gst_caps_set_value (caps, "stream-format", &arr);
        g_value_unset (&arr);
    } else {
      gst_caps_set_simple (caps, "mpegversion", G_TYPE_INT, 4,
        "stream-format", G_TYPE_STRING, "raw",
        "base-profile", G_TYPE_STRING, "lc", NULL);

        if (ctx && ctx->codecdata_size > 0) {
          gst_codec_utils_aac_caps_set_level_and_profile (caps,
            ctx->codecdata, ctx->codecdata_size);
        }
    }
  } else if (strcmp (name, "ac3") == 0) {
    caps = gst_maru_audio_caps_new (ctx, name, "audio/x-ac3", NULL);
  } else if (strcmp (name, "mp3") == 0) {
    if (encode) {
      caps = gst_maru_audio_caps_new (ctx, name, "audio/mpeg",
              "mpegversion", G_TYPE_INT, 1,
              "layer", GST_TYPE_INT_RANGE, 1, 3, NULL);
    } else {
      caps = gst_caps_new_simple("audio/mpeg",
              "mpegversion", G_TYPE_INT, 1,
              "layer", GST_TYPE_INT_RANGE, 1, 3, NULL);
    }
  } else if (strcmp (name, "mp3adu") == 0) {
    gchar *mime_type;

    mime_type = g_strdup_printf ("audio/x-gst_ff-%s", name);
    caps = gst_maru_audio_caps_new (ctx, name, mime_type, NULL);

    if (mime_type) {
      g_free(mime_type);
    }
  } else if (g_str_has_prefix(name, "wmav")) {
    gint version = 1;
    if (strcmp (name, "wmav2") == 0) {
      version = 2;
    }
    caps = gst_maru_audio_caps_new (ctx, name, "audio/x-wma", "wmaversion",
          G_TYPE_INT, version, "block_align", GST_TYPE_INT_RANGE, 0, G_MAXINT,
          "bitrate", GST_TYPE_INT_RANGE, 0, G_MAXINT, NULL);
  } else {
    GST_ERROR("failed to new caps for %s.\n", name);
  }

  return caps;
}

typedef struct PixFmtInfo
{
  uint8_t x_chroma_shift;       /* X chroma subsampling factor is 2 ^ shift */
  uint8_t y_chroma_shift;       /* Y chroma subsampling factor is 2 ^ shift */
} PixFmtInfo;

static PixFmtInfo pix_fmt_info[PIX_FMT_NB];

void
gst_maru_init_pix_fmt_info (void)
{
  pix_fmt_info[PIX_FMT_YUV420P].x_chroma_shift = 1,
  pix_fmt_info[PIX_FMT_YUV420P].y_chroma_shift = 1;

  pix_fmt_info[PIX_FMT_YUV422P].x_chroma_shift = 1;
  pix_fmt_info[PIX_FMT_YUV422P].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_YUV444P].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_YUV444P].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_YUYV422].x_chroma_shift = 1;
  pix_fmt_info[PIX_FMT_YUYV422].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_YUV410P].x_chroma_shift = 2;
  pix_fmt_info[PIX_FMT_YUV410P].y_chroma_shift = 2;

  pix_fmt_info[PIX_FMT_YUV411P].x_chroma_shift = 2;
  pix_fmt_info[PIX_FMT_YUV411P].y_chroma_shift = 0;

  /* RGB formats */
  pix_fmt_info[PIX_FMT_RGB24].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_RGB24].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_BGR24].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_BGR24].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_RGB32].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_RGB32].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_RGB565].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_RGB565].y_chroma_shift = 0;

  pix_fmt_info[PIX_FMT_RGB555].x_chroma_shift = 0;
  pix_fmt_info[PIX_FMT_RGB555].y_chroma_shift = 0;
}

int
gst_maru_avpicture_size (int pix_fmt, int width, int height)
{
  int size, w2, h2, size2;
  int stride, stride2;
  int fsize;
  PixFmtInfo *pinfo;

  pinfo = &pix_fmt_info[pix_fmt];

  switch (pix_fmt) {
  case PIX_FMT_YUV420P:
  case PIX_FMT_YUV422P:
  case PIX_FMT_YUV444P:
  case PIX_FMT_YUV410P:
  case PIX_FMT_YUV411P:
    stride = ROUND_UP_4(width);
    h2 = ROUND_UP_X(height, pinfo->y_chroma_shift);
    size = stride * h2;
    w2 = DIV_ROUND_UP_X(width, pinfo->x_chroma_shift);
    stride2 = ROUND_UP_4(w2);
    h2 = DIV_ROUND_UP_X(height, pinfo->y_chroma_shift);
    size2 = stride2 * h2;
    fsize = size + 2 * size2;
    break;
  case PIX_FMT_RGB24:
  case PIX_FMT_BGR24:
    stride = ROUND_UP_4 (width * 3);
    fsize = stride * height;
    break;
  case PIX_FMT_RGB32:
    stride = width * 4;
    fsize = stride * height;
    break;
  case PIX_FMT_RGB555:
  case PIX_FMT_RGB565:
    stride = ROUND_UP_4 (width * 2);
    fsize = stride * height;
    break;
  default:
    fsize = -1;
    break;
  }

  return fsize;
}

int
gst_maru_align_size (int buf_size)
{
  int i, align_size;

  align_size = buf_size / 1024;

  for (i = 0; i < 14; i++) {
    if (align_size < (1 << i)) {
      align_size = 1024 * (1 << i);
      break;
    }
  }

  return align_size;
}
