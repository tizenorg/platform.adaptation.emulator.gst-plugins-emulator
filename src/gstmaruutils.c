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

#include "gstmaruutils.h"
#include <gst/audio/audio-channels.h>
#include <gst/pbutils/codec-utils.h>

typedef struct
{
  GstVideoFormat format;
  enum PixelFormat pixfmt;
} PixToFmt;

/* FIXME : FILLME */
static const PixToFmt pixtofmttable[] = {
  /* GST_VIDEO_FORMAT_I420, */
  {GST_VIDEO_FORMAT_I420, PIX_FMT_YUV420P},
  /* Note : this should use a different chroma placement */
  {GST_VIDEO_FORMAT_I420, PIX_FMT_YUVJ420P},

  /* GST_VIDEO_FORMAT_YV12, */
  /* GST_VIDEO_FORMAT_YUY2, */
  {GST_VIDEO_FORMAT_YUY2, PIX_FMT_YUYV422},
  /* GST_VIDEO_FORMAT_UYVY, */
  {GST_VIDEO_FORMAT_UYVY, PIX_FMT_UYVY422},
  /* GST_VIDEO_FORMAT_AYUV, */
  /* GST_VIDEO_FORMAT_RGBx, */
  /* GST_VIDEO_FORMAT_BGRx, */
  /* GST_VIDEO_FORMAT_xRGB, */
  /* GST_VIDEO_FORMAT_xBGR, */
  /* GST_VIDEO_FORMAT_RGBA, */
  {GST_VIDEO_FORMAT_RGBA, PIX_FMT_RGBA},
  /* GST_VIDEO_FORMAT_BGRA, */
  {GST_VIDEO_FORMAT_BGRA, PIX_FMT_BGRA},
  /* GST_VIDEO_FORMAT_ARGB, */
  {GST_VIDEO_FORMAT_ARGB, PIX_FMT_ARGB},
  /* GST_VIDEO_FORMAT_ABGR, */
  {GST_VIDEO_FORMAT_ABGR, PIX_FMT_ABGR},
  /* GST_VIDEO_FORMAT_RGB, */
  {GST_VIDEO_FORMAT_RGB, PIX_FMT_RGB24},
  /* GST_VIDEO_FORMAT_BGR, */
  {GST_VIDEO_FORMAT_BGR, PIX_FMT_BGR24},
  /* GST_VIDEO_FORMAT_Y41B, */
  {GST_VIDEO_FORMAT_Y41B, PIX_FMT_YUV411P},
  /* GST_VIDEO_FORMAT_Y42B, */
  {GST_VIDEO_FORMAT_Y42B, PIX_FMT_YUV422P},
  {GST_VIDEO_FORMAT_Y42B, PIX_FMT_YUVJ422P},
  /* GST_VIDEO_FORMAT_YVYU, */
  /* GST_VIDEO_FORMAT_Y444, */
  {GST_VIDEO_FORMAT_Y444, PIX_FMT_YUV444P},
  {GST_VIDEO_FORMAT_Y444, PIX_FMT_YUVJ444P},
  /* GST_VIDEO_FORMAT_v210, */
  /* GST_VIDEO_FORMAT_v216, */
  /* GST_VIDEO_FORMAT_NV12, */
  {GST_VIDEO_FORMAT_NV12, PIX_FMT_NV12},
  /* GST_VIDEO_FORMAT_NV21, */
  {GST_VIDEO_FORMAT_NV21, PIX_FMT_NV21},
  /* GST_VIDEO_FORMAT_GRAY8, */
  {GST_VIDEO_FORMAT_GRAY8, PIX_FMT_GRAY8},
  /* GST_VIDEO_FORMAT_GRAY16_BE, */
  {GST_VIDEO_FORMAT_GRAY16_BE, PIX_FMT_GRAY16BE},
  /* GST_VIDEO_FORMAT_GRAY16_LE, */
  {GST_VIDEO_FORMAT_GRAY16_LE, PIX_FMT_GRAY16LE},
  /* GST_VIDEO_FORMAT_v308, */
  /* GST_VIDEO_FORMAT_Y800, */
  /* GST_VIDEO_FORMAT_Y16, */
  /* GST_VIDEO_FORMAT_RGB16, */
  {GST_VIDEO_FORMAT_RGB16, PIX_FMT_RGB565},
  /* GST_VIDEO_FORMAT_BGR16, */
  /* GST_VIDEO_FORMAT_RGB15, */
  {GST_VIDEO_FORMAT_RGB15, PIX_FMT_RGB555},
  /* GST_VIDEO_FORMAT_BGR15, */
  /* GST_VIDEO_FORMAT_UYVP, */
  /* GST_VIDEO_FORMAT_A420, */
  {GST_VIDEO_FORMAT_A420, PIX_FMT_YUVA420P},
  /* GST_VIDEO_FORMAT_RGB8_PALETTED, */
  {GST_VIDEO_FORMAT_RGB8P, PIX_FMT_PAL8},
  /* GST_VIDEO_FORMAT_YUV9, */
  {GST_VIDEO_FORMAT_YUV9, PIX_FMT_YUV410P},
  /* GST_VIDEO_FORMAT_YVU9, */
  /* GST_VIDEO_FORMAT_IYU1, */
  /* GST_VIDEO_FORMAT_ARGB64, */
  /* GST_VIDEO_FORMAT_AYUV64, */
  /* GST_VIDEO_FORMAT_r210, */
  {GST_VIDEO_FORMAT_I420_10LE, PIX_FMT_YUV420P10LE},
  {GST_VIDEO_FORMAT_I420_10BE, PIX_FMT_YUV420P10BE},
  {GST_VIDEO_FORMAT_I422_10LE, PIX_FMT_YUV422P10LE},
  {GST_VIDEO_FORMAT_I422_10BE, PIX_FMT_YUV422P10BE},
  {GST_VIDEO_FORMAT_Y444_10LE, PIX_FMT_YUV444P10LE},
  {GST_VIDEO_FORMAT_Y444_10BE, PIX_FMT_YUV444P10BE},
};

gint
gst_maru_smpfmt_depth (int32_t smp_fmt)
{
  GST_DEBUG (" >> ENTER ");
  gint depth = -1;

  switch (smp_fmt) {
  case SAMPLE_FMT_U8:
  case SAMPLE_FMT_U8P:
    depth = 1;
    break;
  case SAMPLE_FMT_S16:
  case SAMPLE_FMT_S16P:
    depth = 2;
    break;
  case SAMPLE_FMT_S32:
  case SAMPLE_FMT_FLT:
  case SAMPLE_FMT_S32P:
  case SAMPLE_FMT_FLTP:
    depth = 4;
    break;
  case SAMPLE_FMT_DBL:
  case SAMPLE_FMT_DBLP:
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
  CH_LOW_FREQUENCY, GST_AUDIO_CHANNEL_POSITION_LFE1}, {
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

static guint64
gst_ffmpeg_channel_positions_to_layout (GstAudioChannelPosition * pos,
    gint channels)
{
  gint i, j;
  guint64 ret = 0;
  gint channels_found = 0;

  if (!pos)
    return 0;

  for (i = 0; i < channels; i++) {
    for (j = 0; j < G_N_ELEMENTS (_ff_to_gst_layout); j++) {
      if (_ff_to_gst_layout[j].gst == pos[i]) {
        ret |= _ff_to_gst_layout[j].ff;
        channels_found++;
        break;
      }
    }
  }

  if (channels_found != channels)
    return 0;
  return ret;
}

gboolean
gst_ffmpeg_channel_layout_to_gst (guint64 channel_layout, gint channels,
    GstAudioChannelPosition * pos)
{
  guint nchannels = 0;
  gboolean none_layout = FALSE;

  if (channel_layout == 0) {
    nchannels = channels;
    none_layout = TRUE;
  } else {
    guint i, j;

    for (i = 0; i < 64; i++) {
      if ((channel_layout & (G_GUINT64_CONSTANT (1) << i)) != 0) {
        nchannels++;
      }
    }

    if (nchannels != channels) {
      GST_ERROR ("Number of channels is different (%u != %u)", channels,
          nchannels);
      nchannels = channels;
      none_layout = TRUE;
    } else {

      for (i = 0, j = 0; i < G_N_ELEMENTS (_ff_to_gst_layout); i++) {
        if ((channel_layout & _ff_to_gst_layout[i].ff) != 0) {
          pos[j++] = _ff_to_gst_layout[i].gst;

          if (_ff_to_gst_layout[i].gst == GST_AUDIO_CHANNEL_POSITION_NONE)
            none_layout = TRUE;
        }
      }

      if (j != nchannels) {
        GST_WARNING
            ("Unknown channels in channel layout - assuming NONE layout");
        none_layout = TRUE;
      }
    }
  }

  if (!none_layout
      && !gst_audio_check_valid_channel_positions (pos, nchannels, FALSE)) {
    GST_ERROR ("Invalid channel layout %" G_GUINT64_FORMAT
        " - assuming NONE layout", channel_layout);
    none_layout = TRUE;
  }

  if (none_layout) {
    if (nchannels == 1) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
    } else if (nchannels == 2) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
    } else {
      guint i;

      for (i = 0; i < nchannels; i++)
        pos[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
    }
  }

  return TRUE;
}

static gboolean
_gst_value_list_contains (const GValue * list, const GValue * value)
{
  guint i, n;
  const GValue *tmp;

  n = gst_value_list_get_size (list);
  for (i = 0; i < n; i++) {
    tmp = gst_value_list_get_value (list, i);
    if (gst_value_compare (value, tmp) == GST_VALUE_EQUAL)
      return TRUE;
  }

  return FALSE;
}

static void
gst_maru_video_set_pix_fmts (GstCaps * caps, const int32_t *fmts)
{
  GValue va = { 0, };
  GValue v = { 0, };
  GstVideoFormat format;
    gint i;

  if (!fmts || fmts[0] == -1) {

    g_value_init (&va, GST_TYPE_LIST);
    g_value_init (&v, G_TYPE_STRING);
    for (i = 0; i <= PIX_FMT_NB; i++) {
      format = gst_maru_pixfmt_to_videoformat (i);
      if (format == GST_VIDEO_FORMAT_UNKNOWN)
        continue;
      g_value_set_string (&v, gst_video_format_to_string (format));
      gst_value_list_append_value (&va, &v);
    }
    gst_caps_set_value (caps, "format", &va);
    g_value_unset (&v);
    g_value_unset (&va);
    return;
  }

  /* Only a single format */
  g_value_init (&va, GST_TYPE_LIST);
  g_value_init (&v, G_TYPE_STRING);
  i = 0;
  while (i < 4) {
    format = gst_maru_pixfmt_to_videoformat (fmts[i]);
    if (format != GST_VIDEO_FORMAT_UNKNOWN) {
      g_value_set_string (&v, gst_video_format_to_string (format));
      /* Only append values we don't have yet */
      if (!_gst_value_list_contains (&va, &v))
        gst_value_list_append_value (&va, &v);
    }
    i++;
  }
  if (gst_value_list_get_size (&va) == 1) {
    /* The single value is still in v */
    gst_caps_set_value (caps, "format", &v);
  } else if (gst_value_list_get_size (&va) > 1) {
    gst_caps_set_value (caps, "format", &va);
  }
  g_value_unset (&v);
  g_value_unset (&va);
}

static gboolean
caps_has_field (GstCaps * caps, const gchar * field)
{
  guint i, n;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    if (gst_structure_has_field (s, field))
      return TRUE;
  }

  return FALSE;
}

GstCaps*
gst_maru_codectype_to_video_caps (CodecContext *ctx, const char *name,
    gboolean encode, CodecElement *codec)
{
  GST_DEBUG (" >> ENTER ");
  GstCaps *caps;

  if (ctx) {
    GST_DEBUG ("context: %p, codec: %s, encode: %d, pixel format: %d",
        ctx, name, encode, ctx->video.pix_fmt);
  } else {
    GST_DEBUG ("context: %p, codec: %s, encode: %d",
        ctx, name, encode);
  }

  if (ctx) {
    caps = gst_maru_pixfmt_to_caps (ctx->video.pix_fmt, ctx, name);
  } else {
    caps =
        gst_maru_video_caps_new (ctx, name, "video/x-raw", NULL);
    if (!caps_has_field (caps, "format"))
      gst_maru_video_set_pix_fmts (caps, codec ? codec->pix_fmts : NULL);
  }

  return caps;
}

GstCaps *
gst_maru_codectype_to_audio_caps (CodecContext *ctx, const char *name,
    gboolean encode, CodecElement *codec)
{
  GST_DEBUG (" >> ENTER ");
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
      int8_t sample_fmt = -1;

      sample_fmt = codec->sample_fmts[i];
      if (!strcmp(name, "aac") && encode) {
        sample_fmt = SAMPLE_FMT_S16;
        GST_DEBUG ("convert sample_fmt. codec %s, encode %d, sample_fmt %d",
                  name, encode, sample_fmt);
      }

      temp =
          gst_maru_smpfmt_to_caps (sample_fmt, ctx, name);
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
  GST_DEBUG (" >> ENTER ");
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
  GST_DEBUG (" >> ENTER ");
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
    const gchar *format;

    if ((format = gst_structure_get_string (str, "format"))) {
        if (g_str_equal (format, "YUY2"))
          ctx->video.pix_fmt = PIX_FMT_YUYV422;
        else if (g_str_equal (format, "I420"))
          ctx->video.pix_fmt = PIX_FMT_YUV420P;
        else if (g_str_equal (format, "A420"))
          ctx->video.pix_fmt = PIX_FMT_YUVA420P;
        else if (g_str_equal (format, "Y41B"))
          ctx->video.pix_fmt = PIX_FMT_YUV411P;
        else if (g_str_equal (format, "Y42B"))
          ctx->video.pix_fmt = PIX_FMT_YUV422P;
        else if (g_str_equal (format, "YUV9"))
          ctx->video.pix_fmt = PIX_FMT_YUV410P;
        else {
          GST_WARNING ("couldn't convert format %s" " to a pixel format",
              format);
        }
    }
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
  GST_DEBUG (" >> ENTER ");
  GstStructure *str;
  gint depth = 0, width = 0, endianness = 0;
  gboolean signedness = FALSE;
  const gchar *name;

  g_return_if_fail (gst_caps_get_size (caps) == 1);
  str = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (str, "channels", &ctx->audio.channels);
  gst_structure_get_int (str, "rate", &ctx->audio.sample_rate);
  gst_structure_get_int (str, "block_align", &ctx->audio.block_align);
  gst_structure_get_int (str, "bitrate", &ctx->bit_rate);

  if (!raw) {
    return;
  }

  name = gst_structure_get_name (str);
  if (!name) {
    GST_ERROR ("Couldn't get audio sample format from caps %" GST_PTR_FORMAT, caps);
    return;
  }

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
  GST_DEBUG (" >> ENTER ");
  GstStructure *structure;
  const GValue *value;
  GstBuffer *buf;

  if (!ctx || !gst_caps_get_size (caps)) {
    return;
  }

  structure = gst_caps_get_structure (caps, 0);

  if ((value = gst_structure_get_value (structure, "codec_data"))) {
    GstMapInfo mapinfo;
    guint size;
    guint8 *data;

    buf = (GstBuffer *) gst_value_get_buffer (value);
    gst_buffer_map (buf, &mapinfo, GST_MAP_READ);
    size = mapinfo.size;
    data = mapinfo.data;
    gst_buffer_unmap (buf, &mapinfo);
    GST_DEBUG ("extradata: %p, size: %d", data, size);

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
    GST_DEBUG ("no extra data");
  }

  if ((strcmp (name, "mpeg4") == 0)) {
    const gchar *mime = gst_structure_get_name (structure);
    if (!mime) {
      GST_ERROR ("Couldn't get mime type from caps %" GST_PTR_FORMAT, caps);
      return;
    }

    if (!strcmp (mime, "video/x-divx")) {
      ctx->codec_tag = GST_MAKE_FOURCC ('D', 'I', 'V', 'X');
    } else if (!strcmp (mime, "video/x-xvid")) {
      ctx->codec_tag = GST_MAKE_FOURCC ('X', 'V', 'I', 'D');
    } else if (!strcmp (mime, "video/x-3ivx")) {
      ctx->codec_tag = GST_MAKE_FOURCC ('3', 'I', 'V', '1');
    } else if (!strcmp (mime, "video/mpeg")) {
      ctx->codec_tag = GST_MAKE_FOURCC ('m', 'p', '4', 'v');
    }
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

#define CODEC_NAME_BUFFER_SIZE 32

void
gst_maru_caps_to_codecname (const GstCaps *caps,
                            gchar *codec_name,
                            CodecContext *context)
{
  GST_DEBUG (" >> ENTER ");
  const gchar *mimetype;
  const GstStructure *str;
  int media_type = AVMEDIA_TYPE_UNKNOWN;

  str = gst_caps_get_structure (caps, 0);

  mimetype = gst_structure_get_name (str);
  if (!mimetype) {
    GST_ERROR ("Couldn't get mimetype from caps %" GST_PTR_FORMAT, caps);
    return;
  }

  if (!strcmp (mimetype, "video/x-h263")) {
    const gchar *h263version = gst_structure_get_string (str, "h263version");
    if (h263version && !strcmp (h263version, "h263p")) {
      g_strlcpy (codec_name, "h263p", CODEC_NAME_BUFFER_SIZE);
    } else {
      g_strlcpy (codec_name, "h263", CODEC_NAME_BUFFER_SIZE);
    }
    media_type = AVMEDIA_TYPE_VIDEO;
  } else if (!strcmp (mimetype, "video/mpeg")) {
    gboolean sys_strm;
    gint mpegversion;

    if (gst_structure_get_boolean (str, "systemstream", &sys_strm) &&
        gst_structure_get_int (str, "mpegversion", &mpegversion) &&
        !sys_strm) {
      switch (mpegversion) {
        case 1:
          g_strlcpy (codec_name, "mpeg1video", CODEC_NAME_BUFFER_SIZE);
          break;
        case 2:
          g_strlcpy (codec_name, "mpeg2video", CODEC_NAME_BUFFER_SIZE);
          break;
        case 4:
          g_strlcpy (codec_name, "mpeg4", CODEC_NAME_BUFFER_SIZE);
          break;
      }
    }

    media_type = AVMEDIA_TYPE_VIDEO;
  } else if (!strcmp (mimetype, "video/x-wmv")) {
    gint wmvversion = 0;

    if (gst_structure_get_int (str, "wmvversion", &wmvversion)) {
      switch (wmvversion) {
        case 1:
          g_strlcpy (codec_name, "wmv1", CODEC_NAME_BUFFER_SIZE);
          break;
        case 2:
          g_strlcpy (codec_name, "wmv2", CODEC_NAME_BUFFER_SIZE);
          break;
        case 3:
        {
          g_strlcpy (codec_name, "wmv3", CODEC_NAME_BUFFER_SIZE);
          const gchar *format;
          if ((format = gst_structure_get_string (str, "format"))) {
            if ((g_str_equal (format, "WVC1")) || (g_str_equal (format, "WMVA"))) {
              g_strlcpy (codec_name, "vc1", CODEC_NAME_BUFFER_SIZE);
            }
          }
        }
          break;
      }
    }

    media_type = AVMEDIA_TYPE_VIDEO;
  } else if (!strcmp (mimetype, "audio/mpeg")) {
    gint layer = 0;
    gint mpegversion = 0;

    if (gst_structure_get_int (str, "mpegversion", &mpegversion)) {
      switch (mpegversion) {
      case 2:
      case 4:
        g_strlcpy (codec_name, "aac", CODEC_NAME_BUFFER_SIZE);
        break;
      case 1:
        if (gst_structure_get_int (str, "layer", &layer)) {
          switch(layer) {
            case 1:
              g_strlcpy (codec_name, "mp1", CODEC_NAME_BUFFER_SIZE);
              break;
            case 2:
              g_strlcpy (codec_name, "mp2", CODEC_NAME_BUFFER_SIZE);
              break;
            case 3:
              g_strlcpy (codec_name, "mp3", CODEC_NAME_BUFFER_SIZE);
              break;
          }
        }
        break;
      }
    }

    media_type = AVMEDIA_TYPE_AUDIO;
  } else if (!strcmp (mimetype, "audio/x-wma")) {
    gint wmaversion = 0;

    if (gst_structure_get_int (str, "wmaversion", &wmaversion)) {
      switch (wmaversion) {
      case 1:
        g_strlcpy (codec_name, "wmav1", CODEC_NAME_BUFFER_SIZE);
        break;
      case 2:
        g_strlcpy (codec_name, "wmav2", CODEC_NAME_BUFFER_SIZE);
        break;
      case 3:
        g_strlcpy (codec_name, "wmapro", CODEC_NAME_BUFFER_SIZE);
        break;
      }
    }

    media_type = AVMEDIA_TYPE_AUDIO;
  } else if (!strcmp (mimetype, "audio/x-ac3")) {
    g_strlcpy (codec_name, "ac3", CODEC_NAME_BUFFER_SIZE);
    media_type = AVMEDIA_TYPE_AUDIO;
  } else if (!strcmp (mimetype, "audio/x-msmpeg")) {
    gint msmpegversion = 0;

    if (gst_structure_get_int (str, "msmpegversion", &msmpegversion)) {
      switch (msmpegversion) {
      case 41:
        g_strlcpy (codec_name, "msmpeg4v1", CODEC_NAME_BUFFER_SIZE);
        break;
      case 42:
        g_strlcpy (codec_name, "msmpeg4v2", CODEC_NAME_BUFFER_SIZE);
        break;
      case 43:
        g_strlcpy (codec_name, "msmpeg4", CODEC_NAME_BUFFER_SIZE);
        break;
      }
    }

    media_type = AVMEDIA_TYPE_VIDEO;
  } else if (!strcmp (mimetype, "video/x-h264")) {
    g_strlcpy (codec_name, "h264", CODEC_NAME_BUFFER_SIZE);
    media_type = AVMEDIA_TYPE_VIDEO;
  }

  if (context != NULL) {
    gst_maru_caps_with_codecname (codec_name, media_type, caps, context);
  }

  if (codec_name != NULL) {
    GST_DEBUG ("The %s belongs to the caps %" GST_PTR_FORMAT, codec_name, caps);
  } else {
    GST_WARNING ("Couldn't figure out the name for caps %" GST_PTR_FORMAT, caps);
  }
}

void
gst_maru_caps_with_codectype (int media_type, const GstCaps *caps, CodecContext *ctx)
{
  GST_DEBUG (" >> ENTER ");
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
  GST_DEBUG (" >> ENTER ");
  GstCaps *caps = NULL;
  va_list var_args;
  gint i;

  GST_DEBUG ("context: %p, name: %s, mimetype: %s", ctx, name, mimetype);

  if (ctx != NULL && ctx->video.width != -1) {
    gint num, denom;

    caps = gst_caps_new_simple (mimetype,
      "width", G_TYPE_INT, ctx->video.width,
      "height", G_TYPE_INT, ctx->video.height, NULL);

    num = ctx->video.fps_d / ctx->video.ticks_per_frame;
    denom = ctx->video.fps_n;

    if (!denom) {
      GST_DEBUG ("invalid framerate: %d/0, -> %d/1", num, num);
      denom = 1;
    }
    if (gst_util_fraction_compare (num, denom, 1000, 1) > 0) {
      GST_DEBUG ("excessive framerate: %d/%d, -> 0/1", num, denom);
      num = 0;
      denom = 1;
    }
    GST_DEBUG ("setting framerate: %d/%d", num, denom);
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
      GST_DEBUG ("default caps");
    }
  }

  /* no fixed caps or special restrictions applied;
   * default unfixed setting */
  if (!caps) {
    GST_DEBUG ("Creating default caps");
    caps = gst_caps_new_empty_simple (mimetype);
  }

  va_start (var_args, fieldname);
  gst_caps_set_simple_valist (caps, fieldname, var_args);
  va_end (var_args);

  return caps;
}

GstCaps *
gst_maru_audio_caps_new (CodecContext *ctx, const char *name,
        const char *mimetype, const char *fieldname, ...)
{
  GST_DEBUG (" >> ENTER ");

  GstCaps *caps = NULL;
  gint i;
  va_list var_args;

  if (ctx != NULL && ctx->audio.channels != -1) {
    GstAudioChannelPosition pos[64];
    guint64 mask;

    caps = gst_caps_new_simple (mimetype,
            "rate", G_TYPE_INT, ctx->audio.sample_rate,
            "channels", G_TYPE_INT, ctx->audio.channels, NULL);

    if (ctx->audio.channels > 1 &&
        gst_ffmpeg_channel_layout_to_gst (ctx->audio.channel_layout,
            ctx->audio.channels, pos) &&
        gst_audio_channel_positions_to_mask (pos, ctx->audio.channels, FALSE,
            &mask)) {
      gst_caps_set_simple (caps, "channel-mask", GST_TYPE_BITMASK, mask, NULL);
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
//TODO Not use the mono type at the moment.
/*    if (maxchannels == 1) {
      caps = gst_caps_new_simple(mimetype,
              "channels", G_TYPE_INT, maxchannels, NULL);
    } else {
      caps = gst_caps_new_simple(mimetype,
              "channels", GST_TYPE_INT_RANGE, 1, maxchannels, NULL);
    }
*/
    caps = gst_caps_new_simple(mimetype,
			"channels", GST_TYPE_INT_RANGE, 1, maxchannels, NULL);
    if (n_rates) {
      GValue list = { 0, };
      //GstStructure *structure;

      g_value_init(&list, GST_TYPE_LIST);
      for (i = 0; i < n_rates; i++) {
        GValue v = { 0, };

        g_value_init(&v, G_TYPE_INT);
        g_value_set_int(&v, rates[i]);
        gst_value_list_append_value(&list, &v);
        g_value_unset(&v);
      }
      gst_caps_set_value (caps, "rate", &list);
      g_value_unset(&list);
    } else {
      gst_caps_set_simple(caps, "rate", GST_TYPE_INT_RANGE, 4000, 96000, NULL);
    }
  }

  va_start (var_args, fieldname);
  gst_caps_set_simple_valist (caps, fieldname, var_args);
  va_end (var_args);

  return caps;
}

GstCaps *
gst_maru_pixfmt_to_caps (enum PixelFormat pix_fmt, CodecContext *ctx, const char *name)
{
  GST_DEBUG (" >> ENTER ");
  GstCaps *caps = NULL;
  GstVideoFormat format;

  format = gst_maru_pixfmt_to_videoformat (pix_fmt);

  if (format != GST_VIDEO_FORMAT_UNKNOWN) {
    caps = gst_maru_video_caps_new (ctx, name, "video/x-raw",
        "format", G_TYPE_STRING, gst_video_format_to_string (format), NULL);
  }

  if (caps != NULL) {
    GST_DEBUG ("caps for pix_fmt=%d: %" GST_PTR_FORMAT, pix_fmt, caps);
  } else {
    GST_DEBUG ("No caps found for pix_fmt=%d", pix_fmt);
  }

  return caps;
}

GstVideoFormat
gst_maru_pixfmt_to_videoformat (enum PixelFormat pixfmt)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (pixtofmttable); i++)
    if (pixtofmttable[i].pixfmt == pixfmt)
      return pixtofmttable[i].format;

  return GST_VIDEO_FORMAT_UNKNOWN;
}

enum PixelFormat
gst_maru_videoformat_to_pixfmt (GstVideoFormat format)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (pixtofmttable); i++)
    if (pixtofmttable[i].format == format)
      return pixtofmttable[i].pixfmt;
  return PIX_FMT_NONE;
}

void
gst_maru_videoinfo_to_context (GstVideoInfo * info, CodecContext * context)
{
  gint i, bpp = 0;

  context->video.width = GST_VIDEO_INFO_WIDTH (info);
  context->video.height = GST_VIDEO_INFO_HEIGHT (info);
  for (i = 0; i < GST_VIDEO_INFO_N_COMPONENTS (info); i++)
    bpp += GST_VIDEO_INFO_COMP_DEPTH (info, i);
  context->video.bpp = bpp;

  context->video.ticks_per_frame = 1;
  if (GST_VIDEO_INFO_FPS_N (info) == 0) {
    GST_DEBUG ("Using 25/1 framerate");
    context->video.fps_d = 25;
    context->video.fps_n = 1;
  } else {
    context->video.fps_d = GST_VIDEO_INFO_FPS_N (info);
    context->video.fps_n = GST_VIDEO_INFO_FPS_D (info);
  }

  context->video.par_n = GST_VIDEO_INFO_PAR_N (info);
  context->video.par_d = GST_VIDEO_INFO_PAR_D (info);

  context->video.pix_fmt =
      gst_maru_videoformat_to_pixfmt (GST_VIDEO_INFO_FORMAT (info));
}

GstCaps *
gst_maru_smpfmt_to_caps (int8_t sample_fmt, CodecContext *ctx, const char *name)
{
  GstCaps *caps = NULL;
  GstAudioFormat format;

  format = gst_maru_smpfmt_to_audioformat (sample_fmt);

  if (format != GST_AUDIO_FORMAT_UNKNOWN) {
    caps = gst_maru_audio_caps_new (ctx, name, "audio/x-raw",
        "format", G_TYPE_STRING, gst_audio_format_to_string (format),
        "layout", G_TYPE_STRING, "interleaved", NULL);
    GST_LOG ("caps for sample_fmt=%d: %" GST_PTR_FORMAT, sample_fmt, caps);
  } else {
    GST_LOG ("No caps found for sample_fmt=%d", sample_fmt);
  }

  return caps;
}

GstCaps *
gst_maru_codecname_to_caps (const char *name, CodecContext *ctx, gboolean encode)
{
  GST_DEBUG (" >> ENTER");
  GstCaps *caps = NULL;

  GST_DEBUG ("codec: %s, context: %p, encode: %d", name, ctx, encode);

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
  } else if (strcmp (name, "mpeg2video") == 0) {
    if (encode) {
      caps = gst_maru_video_caps_new (ctx, name, "video/mpeg",
            "mpegversion", G_TYPE_INT, 2,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
    } else {
      caps = gst_caps_new_simple ("video/mpeg",
            "mpegversion", GST_TYPE_INT_RANGE, 1, 2,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
    }
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
  } else if ((strcmp (name, "h264") == 0) || (strcmp (name, "libx264") == 0)) {
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
                "wmvversion", G_TYPE_INT, 3, "format",
                G_TYPE_STRING, "WVC1", NULL);
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
    GST_ERROR("failed to new caps for %s", name);
  }

  if (caps != NULL) {
    GST_DEBUG ("caps is NOT null");
    if (ctx && ctx->codecdata_size > 0) {
      GST_DEBUG ("codec_data size %d", ctx->codecdata_size);

      GstBuffer *data = gst_buffer_new_and_alloc (ctx->codecdata_size);

      gst_buffer_fill (data, 0, ctx->codecdata, ctx->codecdata_size);
      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, data, NULL);
      gst_buffer_unref (data);
    }
    GST_DEBUG ("caps for codec %s %" GST_PTR_FORMAT, name, caps);
  } else {
    GST_DEBUG ("No caps found for codec %s", name);
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
  GST_DEBUG (" >> ENTER ");
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
  GST_DEBUG (" >> ENTER ");
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
  GST_DEBUG (" >> ENTER ");
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

GstAudioFormat
gst_maru_smpfmt_to_audioformat(int32_t sample_fmt)
{
    switch (sample_fmt) {
    case SAMPLE_FMT_U8:
    case SAMPLE_FMT_U8P:
      return GST_AUDIO_FORMAT_U8;
      break;
    case SAMPLE_FMT_S16:
    case SAMPLE_FMT_S16P:
      return GST_AUDIO_FORMAT_S16;
      break;
    case SAMPLE_FMT_S32:
    case SAMPLE_FMT_S32P:
      return GST_AUDIO_FORMAT_S32;
      break;
    case SAMPLE_FMT_FLT:
    case SAMPLE_FMT_FLTP:
      return GST_AUDIO_FORMAT_F32;
      break;
    case SAMPLE_FMT_DBL:
    case SAMPLE_FMT_DBLP:
      return GST_AUDIO_FORMAT_F64;
      break;
    default:
      /* .. */
      return GST_AUDIO_FORMAT_UNKNOWN;
      break;
  }
}

gboolean
gst_maru_channel_layout_to_gst (guint64 channel_layout, gint channels,
    GstAudioChannelPosition * pos)
{
  guint nchannels = 0;
  gboolean none_layout = FALSE;

  if (channel_layout == 0) {
    nchannels = channels;
    none_layout = TRUE;
  } else {
    guint i, j;

    for (i = 0; i < 64; i++) {
      if ((channel_layout & (G_GUINT64_CONSTANT (1) << i)) != 0) {
        nchannels++;
      }
    }

    if (nchannels != channels) {
      GST_ERROR ("Number of channels is different (%u != %u)", channels,
          nchannels);
      nchannels = channels;
      none_layout = TRUE;
    } else {

      for (i = 0, j = 0; i < G_N_ELEMENTS (_ff_to_gst_layout); i++) {
        if ((channel_layout & _ff_to_gst_layout[i].ff) != 0) {
          pos[j++] = _ff_to_gst_layout[i].gst;

          if (_ff_to_gst_layout[i].gst == GST_AUDIO_CHANNEL_POSITION_NONE)
            none_layout = TRUE;
        }
      }

      if (j != nchannels) {
        GST_WARNING
            ("Unknown channels in channel layout - assuming NONE layout");
        none_layout = TRUE;
      }
    }
  }

  if (!none_layout
      && !gst_audio_check_valid_channel_positions (pos, nchannels, FALSE)) {
    GST_ERROR ("Invalid channel layout %" G_GUINT64_FORMAT
        " - assuming NONE layout", channel_layout);
    none_layout = TRUE;
  }

  if (none_layout) {
    if (nchannels == 1) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
    } else if (nchannels == 2) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
    } else {
      guint i;

      for (i = 0; i < nchannels; i++)
        pos[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
    }
  }

  return TRUE;
}

void
gst_maru_audioinfo_to_context (GstAudioInfo *info, CodecContext *context)
{
  const CodecElement *codec = context->codec;
  enum SampleFormat smpl_fmts[4];
  enum SampleFormat smpl_fmt = -1;
  int i;

  context->audio.channels = info->channels;
  context->audio.sample_rate = info->rate;
  context->audio.channel_layout =
      gst_ffmpeg_channel_positions_to_layout (info->position, info->channels);

  if (!codec) {
    GST_ERROR ("invalid codec");
    return ;
  }

  for (i = 0; i < 4; i++) {
    smpl_fmts[i] = codec->sample_fmts[i];
  }
  i = 0;
  switch (info->finfo->format) {
    case GST_AUDIO_FORMAT_F32:
      if (smpl_fmts[0] != -1) {
        while (smpl_fmts[i] != -1) {
          if (smpl_fmts[i] == SAMPLE_FMT_FLT) {
            smpl_fmt = smpl_fmts[i];
            break;
          } else if (smpl_fmts[i] == SAMPLE_FMT_FLTP) {
            smpl_fmt = smpl_fmts[i];
          }

          i++;
        }
      } else {
        smpl_fmt = SAMPLE_FMT_FLT;
      }
      break;
    case GST_AUDIO_FORMAT_F64:
      if (smpl_fmts[0] != -1) {
        while (smpl_fmts[i] != -1) {
          if (smpl_fmts[i] == SAMPLE_FMT_DBL) {
            smpl_fmt = smpl_fmts[i];
            break;
          } else if (smpl_fmts[i] == SAMPLE_FMT_DBLP) {
            smpl_fmt = smpl_fmts[i];
          }

          i++;
        }
      } else {
        smpl_fmt = SAMPLE_FMT_DBL;
      }
      break;
    case GST_AUDIO_FORMAT_S32:
      if (smpl_fmts[0] != -1) {
        while (smpl_fmts[i] != -1) {
          if (smpl_fmts[i] == SAMPLE_FMT_S32) {
            smpl_fmt = smpl_fmts[i];
            break;
          } else if (smpl_fmts[i] == SAMPLE_FMT_S32P) {
            smpl_fmt = smpl_fmts[i];
          }

          i++;
        }
      } else {
        smpl_fmt = SAMPLE_FMT_S32;
      }
      break;
    case GST_AUDIO_FORMAT_S16:
      if (smpl_fmts[0] != -1) {
        while (smpl_fmts[i] != -1) {
          if (smpl_fmts[i] == SAMPLE_FMT_S16) {
            smpl_fmt = smpl_fmts[i];
            break;
          } else if (smpl_fmts[i] == SAMPLE_FMT_S16P) {
            smpl_fmt = smpl_fmts[i];
          }

          i++;
        }
      } else {
        smpl_fmt = SAMPLE_FMT_S16;
      }
      break;
    case GST_AUDIO_FORMAT_U8:
      if (smpl_fmts[0] != -1) {
        while (smpl_fmts[i] != -1) {
          if (smpl_fmts[i] == SAMPLE_FMT_U8) {
            smpl_fmt = smpl_fmts[i];
            break;
          } else if (smpl_fmts[i] == SAMPLE_FMT_U8P) {
            smpl_fmt = smpl_fmts[i];
          }

          i++;
        }
      } else {
        smpl_fmt = SAMPLE_FMT_U8;
      }
      break;
    default:
      break;
  }

  g_assert (smpl_fmt != -1);

  context->audio.sample_fmt = smpl_fmt;
}
