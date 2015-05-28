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

typedef struct _GstMaruVidEnc
{

  GstVideoEncoder parent;

  GstVideoCodecState *input_state;

  CodecContext *context;
  CodecDevice *dev;
  gboolean opened;
  gboolean discont;

  /* cache */
  gulong bitrate;
  gint gop_size;
  gulong buffer_size;

  guint8 *working_buf;
  gulong working_buf_size;

  GQueue *delay;

} GstMaruVidEnc;

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

typedef struct _GstMaruVidEncClass
{
  GstVideoEncoderClass parent_class;

  CodecElement *codec;
  GstPadTemplate *sinktempl;
  GstPadTemplate *srctempl;
} GstMaruVidEncClass;

typedef struct _GstMaruEncClass
{
  GstElementClass parent_class;

  CodecElement *codec;
  GstPadTemplate *sinktempl;
  GstPadTemplate *srctempl;
  GstCaps *sinkcaps;
} GstMaruEncClass;

static GstElementClass *parent_class = NULL;

static void gst_maruvidenc_base_init (GstMaruVidEncClass *klass);
static void gst_maruvidenc_class_init (GstMaruVidEncClass *klass);
static void gst_maruvidenc_init (GstMaruVidEnc *maruenc);
static void gst_maruvidenc_finalize (GObject *object);

static gboolean gst_maruvidenc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static gboolean gst_maruvidenc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

static GstCaps *gst_maruvidenc_getcaps (GstVideoEncoder * encoder, GstCaps * filter);
static GstFlowReturn gst_maruvidenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);

static void gst_maruvidenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_maruvidenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

#define DEFAULT_VIDEO_BITRATE   300000
#define DEFAULT_VIDEO_GOP_SIZE  15

#define DEFAULT_WIDTH 352
#define DEFAULT_HEIGHT 288

/*
 * Implementation
 */
static void
gst_maruvidenc_base_init (GstMaruVidEncClass *klass)
{
  GST_DEBUG (" >> ENTER");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  CodecElement *codec;
  GstPadTemplate *sinktempl = NULL, *srctempl = NULL;
  GstCaps *sinkcaps = NULL, *srccaps = NULL;
  gchar *longname, *description;

  codec =
    (CodecElement *)g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
        GST_MARUENC_PARAMS_QDATA);
  g_assert (codec != NULL);

  /* construct the element details struct */
  longname = g_strdup_printf ("%s Encoder", codec->longname);
  description = g_strdup_printf ("%s Encoder", codec->name);

  gst_element_class_set_metadata (element_class,
      longname,
      "Codec/Encoder/Video",
      description,
      "Sooyoung Ha <yoosah.ha@samsung.com>");
  g_free (longname);
  g_free (description);

  if (!(srccaps = gst_maru_codecname_to_caps (codec->name, NULL, TRUE))) {
    GST_DEBUG ("Couldn't get source caps for encoder '%s'", codec->name);
    srccaps = gst_caps_new_empty_simple ("unknown/unknown");
  }

  sinkcaps = gst_maru_codectype_to_video_caps (NULL, codec->name, FALSE, codec);

  if (!sinkcaps) {
      GST_DEBUG ("Couldn't get sink caps for encoder '%s'", codec->name);
      sinkcaps = gst_caps_new_empty_simple ("unknown/unknown");
  }

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
          GST_PAD_ALWAYS, sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
          GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->codec = codec;
  klass->sinktempl = sinktempl;
  klass->srctempl = srctempl;
}

static void
gst_maruvidenc_class_init (GstMaruVidEncClass *klass)
{
  GST_DEBUG (" >> ENTER");
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstVideoEncoderClass *venc_class = (GstVideoEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_maruvidenc_set_property;
  gobject_class->get_property = gst_maruvidenc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
      g_param_spec_ulong ("bitrate", "Bit Rate",
      "Target VIDEO Bitrate", 0, G_MAXULONG, DEFAULT_VIDEO_BITRATE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  venc_class->handle_frame = gst_maruvidenc_handle_frame;
  venc_class->getcaps = gst_maruvidenc_getcaps;
  venc_class->set_format = gst_maruvidenc_set_format;
  venc_class->propose_allocation = gst_maruvidenc_propose_allocation;

  gobject_class->finalize = gst_maruvidenc_finalize;
}

static void
gst_maruvidenc_init (GstMaruVidEnc *maruenc)
{
  GST_DEBUG (" >> ENTER");
  // instead of AVCodecContext
  maruenc->context = g_malloc0 (sizeof(CodecContext));
  maruenc->context->video.pix_fmt = PIX_FMT_NONE;
  maruenc->context->audio.sample_fmt = SAMPLE_FMT_NONE;

  maruenc->opened = FALSE;

  maruenc->dev = g_malloc0 (sizeof(CodecDevice));

  maruenc->bitrate = DEFAULT_VIDEO_BITRATE;
  maruenc->buffer_size = 512 * 1024;
  maruenc->gop_size = DEFAULT_VIDEO_GOP_SIZE;
}

static void
gst_maruvidenc_finalize (GObject *object)
{
  GST_DEBUG (" >> ENTER");
  // Deinit Decoder
  GstMaruVidEnc *maruenc = (GstMaruVidEnc *) object;

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

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_maruvidenc_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  GST_DEBUG (" >> ENTER");
  GstMaruVidEnc *maruenc = (GstMaruVidEnc *) encoder;
  GstCaps *caps = NULL;

  GST_DEBUG_OBJECT (maruenc, "getting caps");

  caps = gst_video_encoder_proxy_getcaps (encoder, NULL, filter);
  GST_DEBUG_OBJECT (maruenc, "return caps %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_maruvidenc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GST_DEBUG (" >> ENTER");
  GstCaps *other_caps;
  GstCaps *allowed_caps;
  GstCaps *icaps;
  GstVideoCodecState *output_format;
  enum PixelFormat pix_fmt;
  GstMaruVidEnc *maruenc = (GstMaruVidEnc *) encoder;
  GstMaruVidEncClass *oclass =
      (GstMaruVidEncClass *) G_OBJECT_GET_CLASS (maruenc);

  /* close old session */
  if (maruenc->opened) {
    gst_maru_avcodec_close (maruenc->context, maruenc->dev);
    maruenc->opened = FALSE;
  }

  /* user defined properties */
  maruenc->context->bit_rate = maruenc->bitrate;
  GST_DEBUG_OBJECT (maruenc, "Setting avcontext to bitrate %lu, gop_size %d",
      maruenc->bitrate, maruenc->gop_size);

  GST_DEBUG_OBJECT (maruenc, "Extracting common video information");
  /* fetch pix_fmt, fps, par, width, height... */
  gst_maru_videoinfo_to_context (&state->info, maruenc->context);

  if (!strcmp(oclass->codec->name ,"mpeg4")
      && (maruenc->context->video.fps_d > 65535)) {
    /* MPEG4 Standards do not support time_base denominator greater than
     * (1<<16) - 1 . We therefore scale them down.
     * Agreed, it will not be the exact framerate... but the difference
     * shouldn't be that noticeable */
      maruenc->context->video.fps_n =
        (gint) gst_util_uint64_scale_int (maruenc->context->video.fps_n,
            65535, maruenc->context->video.fps_d);
      maruenc->context->video.fps_d = 65535;
      GST_DEBUG_OBJECT (maruenc, "MPEG4 : scaled down framerate to %d / %d",
          maruenc->context->video.fps_d, maruenc->context->video.fps_n);
  }

  pix_fmt = maruenc->context->video.pix_fmt;

  /* open codec */
  if (gst_maru_avcodec_open (maruenc->context,
      oclass->codec, maruenc->dev) < 0) {
    GST_DEBUG_OBJECT (maruenc, "maru_%senc: Failed to open codec",
        oclass->codec->name);
    return FALSE;
  }

  /* is the colourspace correct? */
  if (pix_fmt != maruenc->context->video.pix_fmt) {
    gst_maru_avcodec_close (maruenc->context, maruenc->dev);
    GST_DEBUG_OBJECT (maruenc,
      "maru_%senc: AV wants different colorspace (%d given, %d wanted)",
      oclass->codec->name, pix_fmt, maruenc->context->video.pix_fmt);
    return FALSE;
  }

  /* we may have failed mapping caps to a pixfmt,
   * and quite some codecs do not make up their own mind about that
   * in any case, _NONE can never work out later on */
  if (oclass->codec->media_type == AVMEDIA_TYPE_VIDEO
    && pix_fmt == PIX_FMT_NONE) {
    GST_DEBUG_OBJECT (maruenc, "maru_%senc: Failed to determine input format",
      oclass->codec->name);
    return FALSE;
  }

  /* some codecs support more than one format, first auto-choose one */
  GST_DEBUG_OBJECT (maruenc, "picking an output format ...");
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (maruenc, "... but no peer, using template caps");
    /* we need to copy because get_allowed_caps returns a ref, and
     * get_pad_template_caps doesn't */
    allowed_caps =
        gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  }
  GST_DEBUG_OBJECT (maruenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);
  gst_maru_caps_with_codecname (oclass->codec->name,
    oclass->codec->media_type, allowed_caps, maruenc->context);

  /* try to set this caps on the other side */
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
  icaps = gst_caps_truncate (icaps);

  /* Store input state and set output state */
  if (maruenc->input_state)
    gst_video_codec_state_unref (maruenc->input_state);
  maruenc->input_state = gst_video_codec_state_ref (state);

  output_format = gst_video_encoder_set_output_state (encoder, icaps, state);
  gst_video_codec_state_unref (output_format);

  /* success! */
  maruenc->opened = TRUE;

  return TRUE;
}

static gboolean
gst_maruvidenc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query)
{
  GST_DEBUG (" >> ENTER");
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

static void
gst_maruenc_setup_working_buf (GstMaruVidEnc *maruenc)
{
  GST_DEBUG (" >> ENTER");
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

static GstFlowReturn
gst_maruvidenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GST_DEBUG (" >> ENTER");
  GstMaruVidEnc *maruenc = (GstMaruVidEnc *) encoder;
  GstBuffer *outbuf;
  gint ret_size = 0;
  int coded_frame = 0, is_keyframe = 0;
  GstMapInfo mapinfo;

  gst_buffer_map (frame->input_buffer, &mapinfo, GST_MAP_READ);

  gst_maruenc_setup_working_buf (maruenc);

  ret_size =
    interface->encode_video (maruenc->context, maruenc->working_buf,
                maruenc->working_buf_size, mapinfo.data,
                mapinfo.size, GST_BUFFER_TIMESTAMP (frame->input_buffer),
                &coded_frame, &is_keyframe, maruenc->dev);
  gst_buffer_unmap (frame->input_buffer, &mapinfo);

  if (ret_size < 0) {
    GstMaruVidEncClass *oclass =
      (GstMaruVidEncClass *) (G_OBJECT_GET_CLASS (maruenc));
    GST_ERROR_OBJECT (maruenc,
        "maru_%senc: failed to encode buffer", oclass->codec->name);
    return GST_FLOW_OK;
  }

  /* Encoder needs more data */
  if (!ret_size) {
    return GST_FLOW_OK;
  }

  gst_video_codec_frame_unref (frame);

  /* Get oldest frame */
  frame = gst_video_encoder_get_oldest_frame (encoder);

  /* Allocate output buffer */
  if (gst_video_encoder_allocate_output_frame (encoder, frame,
          ret_size) != GST_FLOW_OK) {
    gst_video_codec_frame_unref (frame);
    GstMaruVidEncClass *oclass =
      (GstMaruVidEncClass *) (G_OBJECT_GET_CLASS (maruenc));
    GST_ERROR_OBJECT (maruenc,
        "maru_%senc: failed to alloc buffer", oclass->codec->name);
    return GST_FLOW_ERROR;
  }

  outbuf = frame->output_buffer;
  gst_buffer_fill (outbuf, 0, maruenc->working_buf, ret_size);

  /* buggy codec may not set coded_frame */
  if (coded_frame) {
    if (is_keyframe)
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  } else
    GST_WARNING_OBJECT (maruenc, "codec did not provide keyframe info");

  return gst_video_encoder_finish_frame (encoder, frame);
}


static void
gst_maruvidenc_set_property (GObject *object,
  guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GST_DEBUG (" >> ENTER");
  GstMaruVidEnc *maruenc;

  maruenc = (GstMaruVidEnc *) (object);

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
gst_maruvidenc_get_property (GObject *object,
  guint prop_id, GValue *value, GParamSpec *pspec)
{
  GST_DEBUG (" >> ENTER");
  GstMaruVidEnc *maruenc;

  maruenc = (GstMaruVidEnc *) (object);

  switch (prop_id) {
    case ARG_BIT_RATE:
      g_value_set_ulong (value, maruenc->bitrate);
      break;
    default:
      break;
  }
}

gboolean
gst_maruvidenc_register (GstPlugin *plugin, GList *element)
{
  GTypeInfo typeinfo = {
      sizeof (GstMaruVidEncClass),
      (GBaseInitFunc) gst_maruvidenc_base_init,
      NULL,
      (GClassInitFunc) gst_maruvidenc_class_init,
      NULL,
      NULL,
      sizeof (GstMaruVidEnc),
      0,
      (GInstanceInitFunc) gst_maruvidenc_init,
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

    if (codec->codec_type != CODEC_TYPE_ENCODE || codec->media_type != AVMEDIA_TYPE_VIDEO) {
      continue;
    }

    type_name = g_strdup_printf ("maru_%senc", codec->name);
    type = g_type_from_name (type_name);
    if (!type) {
      type = g_type_register_static (GST_TYPE_VIDEO_ENCODER, type_name, &typeinfo, 0);
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
