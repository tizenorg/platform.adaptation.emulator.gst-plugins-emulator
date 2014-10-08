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

#include "gstmarudevice.h"
#include "gstmaruutils.h"
#include "gstmaruinterface.h"
#include <gst/base/gstadapter.h>

#define GST_MARUENC_PARAMS_QDATA g_quark_from_static_string("maruenc-params")

enum
{
  ARG_0,
  ARG_BIT_RATE
};

typedef struct _GstMaruEnc
{
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;

  CodecContext *context;
  CodecDevice *dev;
  gboolean opened;
  GstClockTime adapter_ts;
  guint64 adapter_consumed;
  GstAdapter *adapter;
  gboolean discont;

  // cache
  gulong bitrate;
  gint gop_size;
  gulong buffer_size;

  guint8 *working_buf;
  gulong working_buf_size;

  GQueue *delay;

} GstMaruEnc;

typedef struct _GstMaruEncClass
{
  GstElementClass parent_class;

  CodecElement *codec;
  GstPadTemplate *sinktempl;
  GstPadTemplate *srctempl;
  GstCaps *sinkcaps;
} GstMaruEncClass;

static GstElementClass *parent_class = NULL;

static void gst_maruenc_base_init (GstMaruEncClass *klass);
static void gst_maruenc_class_init (GstMaruEncClass *klass);
static void gst_maruenc_init (GstMaruEnc *maruenc);
static void gst_maruenc_finalize (GObject *object);

static gboolean gst_maruenc_setcaps (GstPad *pad, GstCaps *caps);
static GstCaps *gst_maruenc_getcaps (GstPad *pad);

static GstCaps *gst_maruenc_get_possible_sizes (GstMaruEnc *maruenc,
  GstPad *pad, const GstCaps *caps);

static GstFlowReturn gst_maruenc_chain_video (GstPad *pad, GstBuffer *buffer);
static GstFlowReturn gst_maruenc_chain_audio (GstPad *pad, GstBuffer *buffer);

static gboolean gst_maruenc_event_video (GstPad *pad, GstEvent *event);
static gboolean gst_maruenc_event_src (GstPad *pad, GstEvent *event);

GstStateChangeReturn gst_maruenc_change_state (GstElement *element, GstStateChange transition);

#define DEFAULT_VIDEO_BITRATE   300000
#define DEFAULT_VIDEO_GOP_SIZE  15
#define DEFAULT_AUDIO_BITRATE   128000

#define DEFAULT_WIDTH 352
#define DEFAULT_HEIGHT 288

/*
 * Implementation
 */
static void
gst_maruenc_base_init (GstMaruEncClass *klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
    GstPadTemplate *sinktempl = NULL, *srctempl = NULL;
    GstCaps *sinkcaps = NULL, *srccaps = NULL;
    CodecElement *codec;
    gchar *longname, *classification, *description;

    codec =
        (CodecElement *)g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
         GST_MARUENC_PARAMS_QDATA);

    longname = g_strdup_printf ("%s Encoder", codec->longname);
    classification = g_strdup_printf ("Codec/Encoder/%s",
            (codec->media_type == AVMEDIA_TYPE_VIDEO) ? "Video" : "Audio");
    description = g_strdup_printf ("%s Encoder", codec->name);

    gst_element_class_set_details_simple (element_class,
            longname,
            classification,
            description,
            "Kitae Kim <kt920.kim@samsung.com>");

    g_free (longname);
    g_free (classification);


  if (!(srccaps = gst_maru_codecname_to_caps (codec->name, NULL, TRUE))) {
    GST_DEBUG ("Couldn't get source caps for encoder '%s'", codec->name);
    srccaps = gst_caps_new_simple ("unknown/unknown", NULL);
  }

  switch (codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    sinkcaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv; video/x-raw-gray");
    break;
  case AVMEDIA_TYPE_AUDIO:
    sinkcaps = gst_maru_codectype_to_audio_caps (NULL, codec->name, TRUE, codec);
    break;
  default:
    GST_LOG("unknown media type");
    break;
  }

  if (!sinkcaps) {
      GST_DEBUG ("Couldn't get sink caps for encoder '%s'", codec->name);
      sinkcaps = gst_caps_new_simple ("unknown/unknown", NULL);
  }

  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
          GST_PAD_ALWAYS, sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
          GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->codec = codec;
  klass->sinktempl = sinktempl;
  klass->srctempl = srctempl;
  klass->sinkcaps = NULL;
}

static void
gst_maruenc_set_property (GObject *object,
  guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstMaruEnc *maruenc;

  maruenc = (GstMaruEnc *) (object);

  if (maruenc->opened) {
    GST_WARNING_OBJECT (maruenc,
      "Can't change properties one decoder is setup !");
    return;
  }

  switch (prop_id) {
    case ARG_BIT_RATE:
      maruenc->bitrate = g_value_get_ulong (value);
      break;
    default:
      break;
  }
}

static void
gst_maruenc_get_property (GObject *object,
  guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMaruEnc *maruenc;

  maruenc = (GstMaruEnc *) (object);

  switch (prop_id) {
    case ARG_BIT_RATE:
      g_value_set_ulong (value, maruenc->bitrate);
      break;
    default:
      break;
  }
}

static void
gst_maruenc_class_init (GstMaruEncClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_maruenc_set_property;
  gobject_class->get_property = gst_maruenc_get_property;

  if (klass->codec->media_type == AVMEDIA_TYPE_VIDEO) {
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
      g_param_spec_ulong ("bitrate", "Bit Rate",
        "Target VIDEO Bitrate", 0, G_MAXULONG, DEFAULT_VIDEO_BITRATE,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  } else if (klass->codec->media_type == AVMEDIA_TYPE_AUDIO) {
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
      g_param_spec_ulong ("bitrate", "Bit Rate",
        "Target Audio Bitrate", 0, G_MAXULONG, DEFAULT_AUDIO_BITRATE,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  }

  gstelement_class->change_state = gst_maruenc_change_state;

  gobject_class->finalize = gst_maruenc_finalize;
}

static void
gst_maruenc_init (GstMaruEnc *maruenc)
{
  GstMaruEncClass *oclass;
  oclass = (GstMaruEncClass*) (G_OBJECT_GET_CLASS(maruenc));

  maruenc->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_setcaps_function (maruenc->sinkpad,
    GST_DEBUG_FUNCPTR(gst_maruenc_setcaps));
  gst_pad_set_getcaps_function (maruenc->sinkpad,
    GST_DEBUG_FUNCPTR(gst_maruenc_getcaps));

  maruenc->srcpad = gst_pad_new_from_template (oclass->srctempl, "src");
  gst_pad_use_fixed_caps (maruenc->srcpad);

#if 0
  maruenc->file = NULL;
#endif
  maruenc->delay = g_queue_new ();

  // instead of AVCodecContext
  maruenc->context = g_malloc0 (sizeof(CodecContext));
  maruenc->context->video.pix_fmt = PIX_FMT_NONE;
  maruenc->context->audio.sample_fmt = SAMPLE_FMT_NONE;

  maruenc->opened = FALSE;

  maruenc->dev = g_malloc0 (sizeof(CodecDevice));
  if (!maruenc->dev) {
    GST_ERROR_OBJECT (maruenc, "failed to allocate CodecDevice");
    // TODO: error handling
  }

  if (oclass->codec->media_type == AVMEDIA_TYPE_VIDEO) {
    gst_pad_set_chain_function (maruenc->sinkpad, gst_maruenc_chain_video);
    gst_pad_set_event_function (maruenc->sinkpad, gst_maruenc_event_video);
    gst_pad_set_event_function (maruenc->srcpad, gst_maruenc_event_src);

    maruenc->bitrate = DEFAULT_VIDEO_BITRATE;
    maruenc->buffer_size = 512 * 1024;
    maruenc->gop_size = DEFAULT_VIDEO_GOP_SIZE;

  } else if (oclass->codec->media_type == AVMEDIA_TYPE_AUDIO){
    gst_pad_set_chain_function (maruenc->sinkpad, gst_maruenc_chain_audio);
    maruenc->bitrate = DEFAULT_AUDIO_BITRATE;
  }

  gst_element_add_pad (GST_ELEMENT (maruenc), maruenc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (maruenc), maruenc->srcpad);

  // TODO: need to know what adapter does.
  maruenc->adapter = gst_adapter_new ();
}

static void
gst_maruenc_finalize (GObject *object)
{
  // Deinit Decoder
  GstMaruEnc *maruenc = (GstMaruEnc *) object;

  if (maruenc->opened) {
    gst_maru_avcodec_close (maruenc->context, maruenc->dev);
    maruenc->opened = FALSE;
  }

  if (maruenc->context) {
    g_free (maruenc->context);
    maruenc->context = NULL;
  }

  if (maruenc->dev) {
    g_free (maruenc->dev);
    maruenc->dev = NULL;
  }

  g_queue_free (maruenc->delay);
#if 0
  g_free (maruenc->filename);
#endif

  g_object_unref (maruenc->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_maruenc_get_possible_sizes (GstMaruEnc *maruenc, GstPad *pad,
  const GstCaps *caps)
{
  GstCaps *othercaps = NULL;
  GstCaps *tmpcaps = NULL;
  GstCaps *intersect = NULL;
  guint i;

  othercaps = gst_pad_peer_get_caps (maruenc->srcpad);

  if (!othercaps) {
    return gst_caps_copy (caps);
  }

  intersect = gst_caps_intersect (othercaps,
    gst_pad_get_pad_template_caps (maruenc->srcpad));
  gst_caps_unref (othercaps);

  if (gst_caps_is_empty (intersect)) {
    return intersect;
  }

  if (gst_caps_is_any (intersect)) {
    return gst_caps_copy (caps);
  }

  tmpcaps = gst_caps_new_empty ();

  for (i = 0; i <gst_caps_get_size (intersect); i++) {
    GstStructure *s = gst_caps_get_structure (intersect, i);
    const GValue *height = NULL;
    const GValue *width = NULL;
    const GValue *framerate = NULL;
    GstStructure *tmps;

    height = gst_structure_get_value (s, "height");
    width = gst_structure_get_value (s, "width");
    framerate = gst_structure_get_value (s, "framerate");

    tmps = gst_structure_new ("video/x-rwa-rgb", NULL);
    if (width) {
      gst_structure_set_value (tmps, "width", width);
    }
    if (height) {
      gst_structure_set_value (tmps, "height", height);
    }
    if (framerate) {
      gst_structure_set_value (tmps, "framerate", framerate);
    }
    gst_caps_merge_structure (tmpcaps, gst_structure_copy (tmps));

    gst_structure_set_name (tmps, "video/x-raw-yuv");
    gst_caps_merge_structure (tmpcaps, gst_structure_copy (tmps));

    gst_structure_set_name (tmps, "video/x-raw-gray");
    gst_caps_merge_structure (tmpcaps, tmps);
  }
  gst_caps_unref (intersect);

  intersect = gst_caps_intersect (caps, tmpcaps);
  gst_caps_unref (tmpcaps);

  return intersect;
}

static GstCaps *
gst_maruenc_getcaps (GstPad *pad)
{
  GstMaruEnc *maruenc = (GstMaruEnc *) GST_PAD_PARENT (pad);
  GstMaruEncClass *oclass =
    (GstMaruEncClass *) G_OBJECT_GET_CLASS (maruenc);
  CodecContext *ctx = NULL;
  enum PixelFormat pixfmt;
  GstCaps *caps = NULL;
  GstCaps *finalcaps = NULL;
  gint i;

  GST_DEBUG_OBJECT (maruenc, "getting caps");

  if (!oclass->codec) {
    GST_ERROR_OBJECT (maruenc, "codec element is null.");
    return NULL;
  }

  if (oclass->codec->media_type == AVMEDIA_TYPE_AUDIO) {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

    GST_DEBUG_OBJECT (maruenc, "audio caps, return template %" GST_PTR_FORMAT,
      caps);
    return caps;
  }

  // cached
  if (oclass->sinkcaps) {
    caps = gst_maruenc_get_possible_sizes (maruenc, pad, oclass->sinkcaps);
    GST_DEBUG_OBJECT (maruenc, "return cached caps %" GST_PTR_FORMAT, caps);
    return caps;
  }

  GST_DEBUG_OBJECT (maruenc, "probing caps");
  i = pixfmt = 0;

  for (pixfmt = 0;; pixfmt++) {
    GstCaps *tmpcaps;

    if ((pixfmt = oclass->codec->pix_fmts[i++]) == PIX_FMT_NONE) {
      GST_DEBUG_OBJECT (maruenc,
          "At the end of official pixfmt for this codec, breaking out");
      break;
    }

    GST_DEBUG_OBJECT (maruenc,
        "Got an official pixfmt [%d], attempting to get caps", pixfmt);
    tmpcaps = gst_maru_pixfmt_to_caps (pixfmt, NULL, oclass->codec->name);
    if (tmpcaps) {
      GST_DEBUG_OBJECT (maruenc, "Got caps, breaking out");
      if (!caps) {
        caps = gst_caps_new_empty ();
      }
      gst_caps_append (caps, tmpcaps);
      continue;
    }

    GST_DEBUG_OBJECT (maruenc,
        "Couldn't figure out caps without context, trying again with a context");

    GST_DEBUG_OBJECT (maruenc, "pixfmt: %d", pixfmt);
    if (pixfmt >= PIX_FMT_NB) {
      GST_WARNING ("Invalid pixfmt, breaking out");
      break;
    }

    ctx = g_malloc0 (sizeof(CodecContext));
    if (!ctx) {
      GST_DEBUG_OBJECT (maruenc, "no context");
      break;
    }

    ctx->video.width = DEFAULT_WIDTH;
    ctx->video.height = DEFAULT_HEIGHT;
    ctx->video.fps_n = 1;
    ctx->video.fps_d = 25;
    ctx->video.ticks_per_frame = 1;
    ctx->bit_rate = DEFAULT_VIDEO_BITRATE;

    ctx->video.pix_fmt = pixfmt;

    GST_DEBUG ("Attempting to open codec");
    if (gst_maru_avcodec_open (ctx, oclass->codec, maruenc->dev) >= 0
        && ctx->video.pix_fmt == pixfmt) {
      ctx->video.width = -1;
      if (!caps) {
        caps = gst_caps_new_empty ();
      }
      tmpcaps = gst_maru_codectype_to_caps (oclass->codec->media_type, ctx,
          oclass->codec->name, TRUE);
      if (tmpcaps) {
        gst_caps_append (caps, tmpcaps);
      } else {
        GST_LOG_OBJECT (maruenc,
            "Couldn't get caps for codec: %s", oclass->codec->name);
      }
      gst_maru_avcodec_close (ctx, maruenc->dev);
    } else {
      GST_DEBUG_OBJECT (maruenc, "Opening codec failed with pixfmt: %d", pixfmt);
    }

#if 0
    if (ctx->priv_data) {
      gst_maru_avcodec_close (ctx, maruenc->dev);
    }
#endif
    g_free (ctx);
  }

  if (!caps) {
    caps = gst_maruenc_get_possible_sizes (maruenc, pad,
      gst_pad_get_pad_template_caps (pad));
    GST_DEBUG_OBJECT (maruenc, "probing gave nothing, "
      "return template %" GST_PTR_FORMAT, caps);
    return caps;
  }

  GST_DEBUG_OBJECT (maruenc, "probed caps gave %" GST_PTR_FORMAT, caps);
  oclass->sinkcaps = gst_caps_copy (caps);

  finalcaps = gst_maruenc_get_possible_sizes (maruenc, pad, caps);
  gst_caps_unref (caps);

  return finalcaps;
}

static gboolean
gst_maruenc_setcaps (GstPad *pad, GstCaps *caps)
{
  GstMaruEnc *maruenc;
  GstMaruEncClass *oclass;
  GstCaps *other_caps;
  GstCaps *allowed_caps;
  GstCaps *icaps;
  enum PixelFormat pix_fmt;

  maruenc = (GstMaruEnc *) (gst_pad_get_parent (pad));
  oclass = (GstMaruEncClass *) (G_OBJECT_GET_CLASS (maruenc));

  if (maruenc->opened) {
    gst_maru_avcodec_close (maruenc->context, maruenc->dev);
    maruenc->opened = FALSE;

    gst_pad_set_caps (maruenc->srcpad, NULL);
  }

  maruenc->context->bit_rate = maruenc->bitrate;
  GST_DEBUG_OBJECT (maruenc, "Setting context to bitrate %lu, gop_size %d",
      maruenc->bitrate, maruenc->gop_size);

#if 0

  // user defined properties
  maruenc->context->gop_size = maruenc->gop_size;
  maruenc->context->lmin = (maruenc->lmin * FF_QP2LAMBDA + 0.5);
  maruenc->context->lmax = (maruenc->lmax * FF_QP2LAMBDA + 0.5);

  // some other defaults
  maruenc->context->b_frame_strategy = 0;
  maruenc->context->coder_type = 0;
  maruenc->context->context_model = 0;
  maruenc->context->scenechange_threshold = 0;
  maruenc->context->inter_threshold = 0;

  if (maruenc->interlaced) {
    maruenc->context->flags |=
      CODEC_FLAG_INTERLACED_DCT | CODEC_FLAG_INTERLACED_ME;
    maruenc->picture->interlaced_frame = TRUE;

    maruenc->picture->top_field_first = TRUE;
  }
#endif

  gst_maru_caps_with_codectype (oclass->codec->media_type, caps, maruenc->context);

  if (!maruenc->context->video.fps_d) {
    maruenc->context->video.fps_d = 25;
    maruenc->context->video.fps_n = 1;
  } else if (!strcmp(oclass->codec->name ,"mpeg4")
      && (maruenc->context->video.fps_d > 65535)) {
      maruenc->context->video.fps_n =
        (gint) gst_util_uint64_scale_int (maruenc->context->video.fps_n,
            65535, maruenc->context->video.fps_d);
      maruenc->context->video.fps_d = 65535;
      GST_LOG_OBJECT (maruenc, "MPEG4 : scaled down framerate to %d / %d",
          maruenc->context->video.fps_d, maruenc->context->video.fps_n);
  }

  pix_fmt = maruenc->context->video.pix_fmt;

  // open codec
  if (gst_maru_avcodec_open (maruenc->context,
      oclass->codec, maruenc->dev) < 0) {
    GST_DEBUG_OBJECT (maruenc, "maru_%senc: Failed to open codec",
        oclass->codec->name);
    return FALSE;
  }

  if (pix_fmt != maruenc->context->video.pix_fmt) {
    gst_maru_avcodec_close (maruenc->context, maruenc->dev);
    GST_DEBUG_OBJECT (maruenc,
      "maru_%senc: AV wants different colorspace (%d given, %d wanted)",
      oclass->codec->name, pix_fmt, maruenc->context->video.pix_fmt);
    return FALSE;
  }

  if (oclass->codec->media_type == AVMEDIA_TYPE_VIDEO
    && pix_fmt == PIX_FMT_NONE) {
    GST_DEBUG_OBJECT (maruenc, "maru_%senc: Failed to determine input format",
      oclass->codec->name);
    return FALSE;
  }

  GST_DEBUG_OBJECT (maruenc, "picking an output format.");
  allowed_caps = gst_pad_get_allowed_caps (maruenc->srcpad);
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (maruenc, "but no peer, using template caps");
    allowed_caps =
      gst_caps_copy (gst_pad_get_pad_template_caps (maruenc->srcpad));
  }

  GST_DEBUG_OBJECT (maruenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);
  gst_maru_caps_with_codecname (oclass->codec->name,
    oclass->codec->media_type, allowed_caps, maruenc->context);

  other_caps =
  gst_maru_codecname_to_caps (oclass->codec->name, maruenc->context, TRUE);
  if (!other_caps) {
    GST_DEBUG ("Unsupported codec - no caps found");
    gst_maru_avcodec_close (maruenc->context, maruenc->dev);
    return FALSE;
  }

  icaps = gst_caps_intersect (allowed_caps, other_caps);
  gst_caps_unref (allowed_caps);
  gst_caps_unref (other_caps);
  if (gst_caps_is_empty (icaps)) {
    gst_caps_unref (icaps);
    return FALSE;
  }

  if (gst_caps_get_size (icaps) > 1) {
    GstCaps *newcaps;

    newcaps =
      gst_caps_new_full (gst_structure_copy (gst_caps_get_structure (icaps,
              0)), NULL);
    gst_caps_unref (icaps);
    icaps = newcaps;
  }

  if (!gst_pad_set_caps (maruenc->srcpad, icaps)) {
    gst_maru_avcodec_close (maruenc->context, maruenc->dev);
    gst_caps_unref (icaps);
    return FALSE;
  }
  gst_object_unref (maruenc);

  maruenc->opened = TRUE;

  return TRUE;
}

static void
gst_maruenc_setup_working_buf (GstMaruEnc *maruenc)
{
  guint wanted_size =
      maruenc->context->video.width * maruenc->context->video.height * 6 +
      FF_MIN_BUFFER_SIZE;

  if (maruenc->working_buf == NULL ||
    maruenc->working_buf_size != wanted_size) {
    if (maruenc->working_buf) {
      g_free (maruenc->working_buf);
    }
    maruenc->working_buf_size = wanted_size;
    maruenc->working_buf = g_malloc0 (maruenc->working_buf_size);
  }
  maruenc->buffer_size = wanted_size;
}

GstFlowReturn
gst_maruenc_chain_video (GstPad *pad, GstBuffer *buffer)
{
  GstMaruEnc *maruenc = (GstMaruEnc *) (GST_PAD_PARENT (pad));
  GstBuffer *outbuf = NULL;
  gint ret_size = 0, frame_size = 0;
  int coded_frame = 0, is_keyframe = 0;
  uint32_t mem_offset = 0;

  GST_DEBUG_OBJECT (maruenc,
      "Received buffer of time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

#if 0
  GST_OBJECT_LOCK (maruenc);
  force_keyframe = maruenc->force_keyframe;
  maruenc->force_keyframe = FALSE;
  GST_OBJECT_UNLOCK (maruenc);

  if (force_keyframe) {
    maruenc->picture->pict_type = FF_I_TYPE;
  }
#endif

  frame_size = gst_maru_avpicture_size (maruenc->context->video.pix_fmt,
      maruenc->context->video.width, maruenc->context->video.height);
  g_return_val_if_fail (frame_size == GST_BUFFER_SIZE (buffer),
      GST_FLOW_ERROR);

#if 0
  pts = gst_maru_time_gst_to_ff (GST_BUFFER_TIMESTAMP (buffer) /
    maruenc->context.video.ticks_per_frame,
    maruenc->context.video.fps_n, maruen->context.video.fps_d);
#endif

  gst_maruenc_setup_working_buf (maruenc);

  ret_size =
    interface->encode_video (maruenc->context, maruenc->working_buf,
                maruenc->working_buf_size, GST_BUFFER_DATA (buffer),
                GST_BUFFER_SIZE (buffer), GST_BUFFER_TIMESTAMP (buffer),
                &coded_frame, &is_keyframe,
                maruenc->dev);

  if (ret_size < 0) {
    GstMaruEncClass *oclass =
      (GstMaruEncClass *) (G_OBJECT_GET_CLASS (maruenc));
    GST_ERROR_OBJECT (maruenc,
        "maru_%senc: failed to encode buffer", oclass->codec->name);
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  g_queue_push_tail (maruenc->delay, buffer);
  if (ret_size) {
    buffer = g_queue_pop_head (maruenc->delay);
  } else {
    return GST_FLOW_OK;
  }

#if 0
  if (maruenc->file && maruenc->context->stats_out) {
    if (fprintf (maruenc->file, "%s", maruenc->context->stats_out) < 0) {
      GST_ELEMENT_ERROR (maruenc, RESOURCE, WRITE,
        (("Could not write to file \"%s\"."), maruenc->filename),
        GST_ERROR_SYSTEM);
    }
  }
#endif

  // mem_offset = maruenc->dev->mem_info.offset;
  // working_buf = maruenc->dev->buf + mem_offset;

  GST_DEBUG_OBJECT (maruenc, "encoded video. mem_offset = 0x%x",  mem_offset);

  outbuf = gst_buffer_new_and_alloc (ret_size);
  memcpy (GST_BUFFER_DATA (outbuf), maruenc->working_buf, ret_size);
  // memcpy (GST_BUFFER_DATA (outbuf), working_buf, ret_size);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buffer);

#if 0
  ret = ioctl(maruenc->dev->fd, CODEC_CMD_RELEASE_BUFFER, &mem_offset);
  if (ret < 0) {
    GST_ERROR_OBJECT (maruenc, "failed to release used buffer");
  }
#endif

  if (coded_frame) {
    if (!is_keyframe) {
      GST_DEBUG_OBJECT (maruenc, "this frame is not a keyframe");

      /* GST_BUFFER_FLAG_DELTA_UNIT
       * - this unit cannot be decoded independently.
       */
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    }
  } else {
    GST_WARNING_OBJECT (maruenc, "codec did not provide keyframe info");
  }
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (maruenc->srcpad));

  gst_buffer_unref (buffer);

#if 0
  if (force_keyframe) {
    gst_pad_push_event (maruenc->srcpad,
      gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
      gst_structure_new ("GstForceKeyUnit", "timestamp",
                         G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (outbuf), NULL)));
  }
#endif

  return gst_pad_push (maruenc->srcpad, outbuf);
}

GstFlowReturn
gst_maruenc_encode_audio (GstMaruEnc *maruenc, guint8 *audio_in,
  guint in_size, guint max_size, GstClockTime timestamp,
  GstClockTime duration, gboolean discont)
{
  GstBuffer *outbuf;
  guint8 *audio_out;
  gint res;
  GstFlowReturn ret;

  outbuf = gst_buffer_new_and_alloc (max_size + FF_MIN_BUFFER_SIZE);
  audio_out = GST_BUFFER_DATA (outbuf);

  GST_LOG_OBJECT (maruenc, "encoding buffer of max size %d", max_size);
  if (maruenc->buffer_size != max_size) {
    maruenc->buffer_size = max_size;
  }

  res = interface->encode_audio (maruenc->context, audio_out, max_size,
                            audio_in, in_size, timestamp, maruenc->dev);

  if (res < 0) {
    GST_ERROR_OBJECT (maruenc, "Failed to encode buffer: %d", res);
    gst_buffer_unref (outbuf);
    return GST_FLOW_OK;
  }
  GST_LOG_OBJECT (maruenc, "got output size %d", res);

  GST_BUFFER_SIZE (outbuf) = res;
  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) = duration;
  if (discont) {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
  }
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (maruenc->srcpad));

  GST_LOG_OBJECT (maruenc, "pushing size %d, timestamp %",
      GST_TIME_FORMAT, res, GST_TIME_ARGS (timestamp));

  ret = gst_pad_push (maruenc->srcpad, outbuf);

  return ret;
}

static GstFlowReturn
gst_maruenc_chain_audio (GstPad *pad, GstBuffer *buffer)
{
  GstMaruEnc *maruenc;
  // GstMaruEncClass *oclass;
  GstClockTime timestamp, duration;
  guint in_size, frame_size;
  gint osize;
  GstFlowReturn ret;
  gint out_size = 0;
  gboolean discont;
  guint8 *in_data;
  CodecContext *ctx;

  maruenc = (GstMaruEnc *) (GST_OBJECT_PARENT (pad));
  // oclass = (GstMaruEncClass *) G_OBJECT_GET_CLASS (maruenc);

  ctx = maruenc->context;

  in_size = GST_BUFFER_SIZE (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);
  discont = GST_BUFFER_IS_DISCONT (buffer);

  GST_DEBUG_OBJECT (maruenc,
    "Received time %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT
    ", size %d", GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration), in_size);

  frame_size = ctx->audio.frame_size;
  osize = ctx->audio.bits_per_sample_fmt;

  if (frame_size > 1) {
    guint avail, frame_bytes;

    if (discont) {
      GST_LOG_OBJECT (maruenc, "DISCONT, clear adapter");
      gst_adapter_clear (maruenc->adapter);
      maruenc->discont = TRUE;
    }

    if (gst_adapter_available (maruenc->adapter) == 0) {
      GST_LOG_OBJECT (maruenc, "taking buffer timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));
      maruenc->adapter_ts = timestamp;
      maruenc->adapter_consumed = 0;
    } else {
      GstClockTime upstream_time;
      GstClockTime consumed_time;
      guint64 bytes;

      consumed_time =
        gst_util_uint64_scale (maruenc->adapter_consumed, GST_SECOND,
            ctx->audio.sample_rate);
      timestamp = maruenc->adapter_ts + consumed_time;
      GST_LOG_OBJECT (maruenc, "taking adapter timestamp %" GST_TIME_FORMAT
        " and adding consumed time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (maruenc->adapter_ts), GST_TIME_ARGS (consumed_time));

      upstream_time = gst_adapter_prev_timestamp (maruenc->adapter, &bytes);
      if (GST_CLOCK_TIME_IS_VALID (upstream_time)) {
        GstClockTimeDiff diff;

        upstream_time +=
          gst_util_uint64_scale (bytes, GST_SECOND,
            ctx->audio.sample_rate * osize * ctx->audio.channels);
        diff = upstream_time - timestamp;

        if (diff > GST_SECOND / 10 || diff < -GST_SECOND / 10) {
          GST_DEBUG_OBJECT (maruenc, "adapter timestamp drifting, "
            "taking upstream timestamp %" GST_TIME_FORMAT,
            GST_TIME_ARGS (upstream_time));
          timestamp = upstream_time;

          maruenc->adapter_consumed = bytes / (osize * ctx->audio.channels);
          maruenc->adapter_ts =
            upstream_time - gst_util_uint64_scale (maruenc->adapter_consumed,
                GST_SECOND, ctx->audio.sample_rate);
          maruenc->discont = TRUE;
        }
      }
    }

    GST_LOG_OBJECT (maruenc, "pushing buffer in adapter");
    gst_adapter_push (maruenc->adapter, buffer);

    frame_bytes = frame_size * osize * ctx->audio.channels;
    avail = gst_adapter_available (maruenc->adapter);

    GST_LOG_OBJECT (maruenc, "frame_bytes %u, avail %u", frame_bytes, avail);

    while (avail >= frame_bytes) {
      GST_LOG_OBJECT (maruenc, "taking %u bytes from the adapter", frame_bytes);

      in_data = (guint8 *) gst_adapter_peek (maruenc->adapter, frame_bytes);
      maruenc->adapter_consumed += frame_size;

      duration =
        gst_util_uint64_scale (maruenc->adapter_consumed, GST_SECOND,
          ctx->audio.sample_rate);
      duration -= (timestamp - maruenc->adapter_ts);

      out_size = frame_bytes * 4;

      ret =
        gst_maruenc_encode_audio (maruenc, in_data, frame_bytes, out_size,
          timestamp, duration, maruenc->discont);

      gst_adapter_flush (maruenc->adapter, frame_bytes);
      if (ret != GST_FLOW_OK) {
        GST_DEBUG_OBJECT (maruenc, "Failed to push buffer %d (%s)", ret,
          gst_flow_get_name (ret));
      }

      timestamp += duration;

      maruenc->discont = FALSE;
      avail = gst_adapter_available (maruenc->adapter);
    }
    GST_LOG_OBJECT (maruenc, "%u bytes left in the adapter", avail);
  } else {
#if 0
    int coded_bps = av_get_bits_per_sample (oclass->codec->name);

    GST_LOG_OBJECT (maruenc, "coded bps %d, osize %d", coded_bps, osize);

    out_size = in_size / osize;
    if (coded_bps) {
      out_size = (out_size * coded_bps) / 8;
    }
#endif
    in_data = (guint8 *) GST_BUFFER_DATA (buffer);
    ret = gst_maruenc_encode_audio (maruenc, in_data, in_size, out_size,
      timestamp, duration, discont);
    gst_buffer_unref (buffer);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (maruenc, "Failed to push buffer %d (%s)", ret,
        gst_flow_get_name (ret));
    }
  }

  return GST_FLOW_OK;
}

static void
gst_maruenc_flush_buffers (GstMaruEnc *maruenc, gboolean send)
{
#if 0
  GstBuffer *outbuf, *inbuf;
  gint ret_size = 0;
#endif

  GST_DEBUG_OBJECT (maruenc, "flushing buffers with sending %d", send);

  if (!maruenc->opened) {
    while (!g_queue_is_empty (maruenc->delay)) {
      gst_buffer_unref (g_queue_pop_head (maruenc->delay));
    }
  }

#if 0
  while (!g_queue_is_empty (maruenc->delay)) {
    maruenc_setup_working_buf (maruenc);

    ret_size = codec_encode_video (maruenc->context,
      maruenc->working_buf, maruenc->working_buf_size, NULL, NULL, 0,
      maruenc->dev);

    if (ret_size < 0) {
      GstMaruEncClass *oclass =
        (GstMaruEncClass *) (G_OBJECT_GET_CLASS (maruenc));
      GST_WARNING_OBJECT (maruenc,
        "maru_%senc: failed to flush buffer", oclass->codec->name);
      break;
    }

    if (maruenc->file && maruenc->context->stats_out) {
      if (fprintf (maruenc->file, "%s", maruenc->context->stats_out) < 0) {
        GST_ELEMENT_ERROR (emeulenc, RESOURCE, WRITE,
          (("Could not write to file \"%s\"."), maruenc->filename),
          GST_ERROR_SYSTEM);
      }
    }

    inbuf = g_queue_pop_head (maruenc->delay);

    outbuf = gst_buffer_new_and_alloc (ret_size);
    memcpy (GST_BUFFER_DATA (outbuf), maruenc->working_buf, ret_size);
    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);

    if (!maruenc->context->coded_frame->key_frame) {
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    }
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (maruenc->srcpad));

    gst_buffer_unref (inbuf);

    if (send) {
      gst_pad_push (maruenc->srcpad, outbuf);
    } else {
      gst_buffer_unref (outbuf);
    }
  }

  while (!g_queue_is_empty (maruenc->delay)) {
    gst_buffer_unref (g_queue_pop_head (maruenc->delay));
  }
#endif
}

static gboolean
gst_maruenc_event_video (GstPad *pad, GstEvent *event)
{
  GstMaruEnc *maruenc;
  maruenc = (GstMaruEnc *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_EOS:
    gst_maruenc_flush_buffers (maruenc, TRUE);
    break;
  case GST_EVENT_CUSTOM_DOWNSTREAM:
  {
    const GstStructure *s;
    s = gst_event_get_structure (event);

    if (gst_structure_has_name (s, "GstForceKeyUnit")) {
#if 0
      maruenc->picture->pict_type = FF_I_TYPE;
#endif
    }
  }
    break;
  default:
    break;
  }

  return gst_pad_push_event (maruenc->srcpad, event);
}

static gboolean
gst_maruenc_event_src (GstPad *pad, GstEvent *event)
{
  GstMaruEnc *maruenc = (GstMaruEnc *) (GST_PAD_PARENT (pad));
  gboolean forward = TRUE;

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_CUSTOM_UPSTREAM:
  {
    const GstStructure *s;
    s = gst_event_get_structure (event);

    if (gst_structure_has_name (s, "GstForceKeyUnit")) {
#if 0
      GST_OBJECT_LOCK (maruenc);
      maruenc->force_keyframe = TRUE;
      GST_OBJECT_UNLOCK (maruenc);
#endif
      forward = FALSE;
      gst_event_unref (event);
    }
  }
    break;
  default:
    break;
  }

  if (forward) {
    return gst_pad_push_event (maruenc->sinkpad, event);
  }

  return TRUE;
}

GstStateChangeReturn
gst_maruenc_change_state (GstElement *element, GstStateChange transition)
{
  GstMaruEnc *maruenc = (GstMaruEnc*)element;
  GstStateChangeReturn ret;

  switch (transition) {
  default:
    break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    gst_maruenc_flush_buffers (maruenc, FALSE);
    if (maruenc->opened) {
      GST_DEBUG_OBJECT (maruenc, "change_state: PAUSED_TO_READY, close context");
      gst_maru_avcodec_close (maruenc->context, maruenc->dev);
      maruenc->opened = FALSE;
    }
    gst_adapter_clear (maruenc->adapter);

#if 0
    if (maruenc->flie) {
      fclose (maruenc->file);
      maruenc->file = NULL;
    }
#endif

    if (maruenc->working_buf) {
      g_free (maruenc->working_buf);
      maruenc->working_buf = NULL;
    }
    break;
  default:
    break;
  }

  return ret;
}

gboolean
gst_maruenc_register (GstPlugin *plugin, GList *element)
{
  GTypeInfo typeinfo = {
      sizeof (GstMaruEncClass),
      (GBaseInitFunc) gst_maruenc_base_init,
      NULL,
      (GClassInitFunc) gst_maruenc_class_init,
      NULL,
      NULL,
      sizeof (GstMaruEnc),
      0,
      (GInstanceInitFunc) gst_maruenc_init,
  };

  GType type;
  gchar *type_name;
  gint rank = GST_RANK_PRIMARY;
  GList *elem = element;
  CodecElement *codec = NULL;

  if (!elem) {
    return FALSE;
  }

  /* register element */
  do {
    codec = (CodecElement *)(elem->data);
    if (!codec) {
      return FALSE;
    }

    if (codec->codec_type != CODEC_TYPE_ENCODE) {
      continue;
    }

    type_name = g_strdup_printf ("maru_%senc", codec->name);
    type = g_type_from_name (type_name);
    if (!type) {
      type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
      g_type_set_qdata (type, GST_MARUENC_PARAMS_QDATA, (gpointer) codec);
    }

    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_free (type_name);
      return FALSE;
    }
    g_free (type_name);
  } while ((elem = elem->next));

  return TRUE;
}
