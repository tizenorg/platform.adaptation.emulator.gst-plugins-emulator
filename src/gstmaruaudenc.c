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

#define GST_MARUENC_PARAMS_QDATA g_quark_from_static_string("maruaudenc-params")

enum
{
  PROP_0,
  PROP_BIT_RATE
};

typedef struct _GstMaruAudEnc
{
  GstAudioEncoder parent;

  // cache
  gint bitrate;
  gint rtp_payload_size;
  gint compliance;

  GstAudioChannelPosition layout[64];
  gboolean needs_reorder;

  CodecContext *context;
  CodecDevice *dev;
  gboolean opened;

} GstMaruAudEnc;

typedef struct _GstMaruAudEncClass
{
  GstAudioEncoderClass parent_class;

  CodecElement *codec;
  GstPadTemplate *sinktempl;
  GstPadTemplate *srctempl;
} GstMaruAudEncClass;

static GstElementClass *parent_class = NULL;

static void gst_maruaudenc_base_init (GstMaruAudEncClass *klass);
static void gst_maruaudenc_class_init (GstMaruAudEncClass *klass);
static void gst_maruaudenc_init (GstMaruAudEnc *maruaudenc);
static void gst_maruaudenc_finalize (GObject *object);

static GstCaps *gst_maruaudenc_getcaps (GstAudioEncoder *encoder, GstCaps *filter);
static gboolean gst_maruaudenc_set_format (GstAudioEncoder *encoder, GstAudioInfo *info);
static GstFlowReturn gst_maruaudenc_handle_frame (GstAudioEncoder *encoder, GstBuffer *inbuf);
static gboolean gst_maruaudenc_start (GstAudioEncoder *encoder);
static gboolean gst_maruaudenc_stop (GstAudioEncoder *encoder);
static void gst_maruaudenc_flush (GstAudioEncoder *encoder);

static void gst_maruaudenc_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_maruaudenc_get_property(GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);

#define DEFAULT_AUDIO_BITRATE   128000

#define MARU_DEFAULT_COMPLIANCE 0

/*
 * Implementation
 */
static void
gst_maruaudenc_base_init (GstMaruAudEncClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *sinktempl = NULL, *srctempl = NULL;
  GstCaps *sinkcaps = NULL, *srccaps = NULL;
  gchar *longname, *description;

  CodecElement *codec = NULL;

  codec =
    (CodecElement *)g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
        GST_MARUENC_PARAMS_QDATA);
  g_assert (codec != NULL);

  longname = g_strdup_printf ("Maru %s Encoder", codec->longname);
  description = g_strdup_printf ("Maru %s Encoder", codec->name);

  gst_element_class_set_metadata (element_class,
      longname,
      "Codec/Encoder/Audio",
      description,
      "SooYoung Ha <yoosah.ha@samsung.com>");

  g_free (longname);
  g_free (description);

  if (!(srccaps = gst_maru_codecname_to_caps (codec->name, NULL, TRUE))) {
    GST_DEBUG ("Couldn't get source caps for encoder '%s'", codec->name);
    srccaps = gst_caps_new_empty_simple ("unknown/unknown");
  }

  sinkcaps = gst_maru_codectype_to_audio_caps (NULL, codec->name, TRUE, codec);
  if (!sinkcaps) {
    GST_DEBUG ("Couldn't get sink caps for encoder '%s'", codec->name);
    sinkcaps = gst_caps_new_empty_simple ("unknown/unknown");
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
}


static void
gst_maruaudenc_class_init (GstMaruAudEncClass *klass)
{
  GObjectClass *gobject_class;
  GstAudioEncoderClass *gstaudioencoder_class;

  gobject_class = (GObjectClass *) klass;
  gstaudioencoder_class = (GstAudioEncoderClass *) klass;

  gobject_class->set_property = gst_maruaudenc_set_property;
  gobject_class->get_property = gst_maruaudenc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BIT_RATE,
      g_param_spec_int ("bitrate", "Bit Rate",
          "Target Audio Bitrate", 0, G_MAXINT, DEFAULT_AUDIO_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_maruaudenc_finalize;

  gstaudioencoder_class->start = GST_DEBUG_FUNCPTR (gst_maruaudenc_start);
  gstaudioencoder_class->stop = GST_DEBUG_FUNCPTR (gst_maruaudenc_stop);
  gstaudioencoder_class->getcaps = GST_DEBUG_FUNCPTR (gst_maruaudenc_getcaps);
  gstaudioencoder_class->flush = GST_DEBUG_FUNCPTR (gst_maruaudenc_flush);
  gstaudioencoder_class->set_format = GST_DEBUG_FUNCPTR (gst_maruaudenc_set_format);
  gstaudioencoder_class->handle_frame = GST_DEBUG_FUNCPTR (gst_maruaudenc_handle_frame);
}

static void
gst_maruaudenc_init (GstMaruAudEnc *maruaudenc)
{
  // instead of AVCodecContext
  maruaudenc->context = g_malloc0 (sizeof(CodecContext));
  maruaudenc->context->audio.sample_fmt = SAMPLE_FMT_NONE;

  maruaudenc->opened = FALSE;

  maruaudenc->dev = g_malloc0 (sizeof(CodecDevice));

  maruaudenc->compliance = MARU_DEFAULT_COMPLIANCE;

  gst_audio_encoder_set_drainable (GST_AUDIO_ENCODER (maruaudenc), TRUE);
}

static void
gst_maruaudenc_finalize (GObject *object)
{
  // Deinit Decoder
  GstMaruAudEnc *maruaudenc = (GstMaruAudEnc *) object;

  if (maruaudenc->opened) {
    gst_maru_avcodec_close (maruaudenc->context, maruaudenc->dev);
    maruaudenc->opened = FALSE;
  }

  g_free (maruaudenc->context);
  maruaudenc->context = NULL;

  g_free (maruaudenc->dev);
  maruaudenc->dev = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_maruaudenc_start (GstAudioEncoder * encoder)
{
/*
  GstMaruAudEnc *maruaudenc = (GstMaruAudEnc *) encoder;
  GstMaruAudEncClass *oclass =
      (GstMaruAudEncClass *) G_OBJECT_GET_CLASS (maruaudenc);

  gst_maru_avcodec_close (maruaudenc->context, maruaudenc->dev);
*/
  return TRUE;
}

static gboolean
gst_maruaudenc_stop (GstAudioEncoder * encoder)
{
  GstMaruAudEnc *maruaudenc = (GstMaruAudEnc *) encoder;

  /* close old session */
  gst_maru_avcodec_close (maruaudenc->context, maruaudenc->dev);
  maruaudenc->opened = FALSE;

  return TRUE;
}

static void
gst_maruaudenc_flush (GstAudioEncoder * encoder)
{
  GstMaruAudEnc *maruaudenc = (GstMaruAudEnc *) encoder;

  if (maruaudenc->opened) {
    interface->flush_buffers (maruaudenc->context, maruaudenc->dev);
  }
}

static GstCaps *
gst_maruaudenc_getcaps (GstAudioEncoder * encoder, GstCaps * filter)
{
  GstMaruAudEnc *maruenc = (GstMaruAudEnc *) encoder;
  GstCaps *caps = NULL;

  GST_DEBUG_OBJECT (maruenc, "getting caps");

  /* audio needs no special care */
  caps = gst_audio_encoder_proxy_getcaps (encoder, NULL, filter);

  GST_DEBUG_OBJECT (maruenc, "audio caps, return %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_maruaudenc_set_format (GstAudioEncoder *encoder, GstAudioInfo *info)
{
  GstMaruAudEnc *maruaudenc = (GstMaruAudEnc *) encoder;
  GstCaps *other_caps;
  GstCaps *allowed_caps;
  GstCaps *icaps;
  gsize frame_size;

  GstMaruAudEncClass *oclass =
    (GstMaruAudEncClass *) (G_OBJECT_GET_CLASS (maruaudenc));

  if (maruaudenc->opened) {
    gst_maru_avcodec_close (maruaudenc->context, maruaudenc->dev);
    maruaudenc->opened = FALSE;
  }

  if (maruaudenc->bitrate > 0) {
    GST_DEBUG_OBJECT (maruaudenc, "Setting context to bitrate %d",
      maruaudenc->bitrate);
    maruaudenc->context->bit_rate = maruaudenc->bitrate;
  } else {
    GST_INFO_OBJECT (maruaudenc, "Using context default bitrate %d",
      maruaudenc->context->bit_rate);
  }

  // TODO: need to verify this code
  /*
  if (maruaudenc->rtp_payload_size) {
    maruaudenc->context->rtp_payload_size = maruaudenc->rtp_payload_size;
  }
  */

  // TODO: set these values in qemu layer.
  /*
  maruaudenc->context->rc_strategy = 2;
  maruaudenc->context->b_frame_strategy = 0;
  maruaudenc->context->coder_type = 0;
  maruaudenc->context->context_model = 0;
  */
  if (!maruaudenc->context) {
    GST_ERROR("ctx NULL");
  }
  if (!maruaudenc->context->codec) {
    GST_ERROR("codec NULL");
  }
  gst_maru_audioinfo_to_context (info, maruaudenc->context);

  // open codec
  if (gst_maru_avcodec_open (maruaudenc->context,
      oclass->codec, maruaudenc->dev) < 0) {
    gst_maru_avcodec_close (maruaudenc->context, maruaudenc->dev);
    GST_DEBUG_OBJECT (maruaudenc, "maru_%senc: Failed to open codec",
        oclass->codec->name);

    return FALSE;
  }

  GST_DEBUG_OBJECT (maruaudenc, "picking an output format.");
  allowed_caps = gst_pad_get_allowed_caps (GST_AUDIO_ENCODER_SRC_PAD (encoder));
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (maruaudenc, "but no peer, using template caps");
    allowed_caps =
      gst_caps_copy (gst_pad_get_pad_template_caps (GST_AUDIO_ENCODER_SRC_PAD (encoder)));
  }

  GST_DEBUG_OBJECT (maruaudenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);
  gst_maru_caps_with_codecname (oclass->codec->name,
    oclass->codec->media_type, allowed_caps, maruaudenc->context);

  other_caps =
  gst_maru_codecname_to_caps (oclass->codec->name, maruaudenc->context, TRUE);
  if (!other_caps) {
    gst_caps_unref (allowed_caps);
    gst_maru_avcodec_close (maruaudenc->context, maruaudenc->dev);
    GST_DEBUG ("Unsupported codec - no caps found");
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

if (!gst_audio_encoder_set_output_format (GST_AUDIO_ENCODER (maruaudenc),
          icaps)) {
    gst_maru_avcodec_close (maruaudenc->context, maruaudenc->dev);
    gst_caps_unref (icaps);

    return FALSE;
  }
  gst_caps_unref (icaps);

  frame_size = maruaudenc->context->audio.frame_size;
  if (frame_size > 1) {
    gst_audio_encoder_set_frame_samples_min (GST_AUDIO_ENCODER (maruaudenc),
        frame_size);
    gst_audio_encoder_set_frame_samples_max (GST_AUDIO_ENCODER (maruaudenc),
        frame_size);
    gst_audio_encoder_set_frame_max (GST_AUDIO_ENCODER (maruaudenc), 1);
  } else {
    gst_audio_encoder_set_frame_samples_min (GST_AUDIO_ENCODER (maruaudenc),
        0);
    gst_audio_encoder_set_frame_samples_max (GST_AUDIO_ENCODER (maruaudenc),
        0);
    gst_audio_encoder_set_frame_max (GST_AUDIO_ENCODER (maruaudenc), 0);
  }

  /* success! */
  maruaudenc->opened = TRUE;

  return TRUE;
}

static GstFlowReturn
gst_maruaudenc_encode_audio (GstMaruAudEnc *maruaudenc, guint8 *audio_in,
  guint in_size, gint *have_data)
{
  GstAudioEncoder *enc;
  gint res;
  GstFlowReturn ret;
  guint8 * audio_out;

  enc = GST_AUDIO_ENCODER (maruaudenc);

  GST_LOG_OBJECT (maruaudenc, "encoding buffer %p size %u", audio_in, in_size);

  audio_out = g_malloc0 (FF_MAX_AUDIO_FRAME_SIZE);
  res = interface->encode_audio (maruaudenc->context, audio_out, 0,
        audio_in, in_size, 0, maruaudenc->dev);

  if (res < 0) {
    GST_ERROR_OBJECT (enc, "Failed to encode buffer: %d", res);
    return GST_FLOW_OK;
  }

  GST_LOG_OBJECT (maruaudenc, "got output size %d", res);

  if (*have_data) {
    GstBuffer *outbuf;

    GST_LOG_OBJECT (maruaudenc, "pushing size %d", res);

    outbuf =
        gst_buffer_new_wrapped_full (0, audio_out, res, 0, res,
        audio_out, g_free);

    ret = gst_audio_encoder_finish_frame (enc, outbuf, maruaudenc->context->audio.frame_size);
  } else {
    GST_LOG_OBJECT (maruaudenc, "no output produced");
    ret = GST_FLOW_OK;
  }

  return ret;
}

static void
gst_maruaudenc_drain (GstMaruAudEnc *maruaudenc)
{
  gint have_data, try = 0;

  GST_LOG_OBJECT (maruaudenc,
    "codec has delay capabilities, calling until libav has drained everything");

  do {
    GstFlowReturn ret;

    ret = gst_maruaudenc_encode_audio (maruaudenc, NULL, 0, &have_data);
    if (ret != GST_FLOW_OK || have_data == 0)
      break;
  } while (try++ < 10);

}

static GstFlowReturn
gst_maruaudenc_handle_frame (GstAudioEncoder *encoder, GstBuffer *inbuf)
{
  GstMaruAudEnc *maruaudenc;

  GstFlowReturn ret;
  guint8 *in_data;
  guint size;
  gint have_data;
  GstMapInfo map;

  maruaudenc = (GstMaruAudEnc *) encoder;

  if (G_UNLIKELY (!maruaudenc->opened)) {
    GST_ELEMENT_ERROR (maruaudenc, CORE, NEGOTIATION, (NULL),
      ("not configured to input format before data start"));
    gst_buffer_unref (inbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (!inbuf) {
    gst_maruaudenc_drain (maruaudenc);
    return GST_FLOW_OK;
  }

  inbuf = gst_buffer_ref (inbuf);

  GST_DEBUG_OBJECT (maruaudenc,
    "Received time %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT
    ", size %" G_GSIZE_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)),
    GST_TIME_ARGS (GST_BUFFER_DURATION (inbuf)), gst_buffer_get_size (inbuf));

  if (maruaudenc->needs_reorder) {
    GstAudioInfo *info = gst_audio_encoder_get_audio_info (encoder);

    inbuf = gst_buffer_make_writable (inbuf);
    gst_audio_buffer_reorder_channels (inbuf, info->finfo->format,
        info->channels, info->position, maruaudenc->layout);
  }

  gst_buffer_map (inbuf, &map, GST_MAP_READ);

  in_data = map.data;
  size = map.size;
  ret = gst_maruaudenc_encode_audio (maruaudenc, in_data, size, &have_data);
  gst_buffer_unmap (inbuf, &map);
  gst_buffer_unref (inbuf);

  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (maruaudenc, "Failed to push buffer %d (%s)", ret,
      gst_flow_get_name (ret));
    return ret;
  }

  return GST_FLOW_OK;
}


static void
gst_maruaudenc_set_property (GObject *object,
  guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstMaruAudEnc *maruaudenc;

  maruaudenc = (GstMaruAudEnc *) (object);

  if (maruaudenc->opened) {
    GST_WARNING_OBJECT (maruaudenc,
      "Can't change properties one decoder is setup !");
    return;
  }

  switch (prop_id) {
    case PROP_BIT_RATE:
      maruaudenc->bitrate = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_maruaudenc_get_property (GObject *object,
  guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMaruAudEnc *maruaudenc;

  maruaudenc = (GstMaruAudEnc *) (object);

  switch (prop_id) {
    case PROP_BIT_RATE:
      g_value_set_int (value, maruaudenc->bitrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_maruaudenc_register (GstPlugin *plugin, GList *element)
{
  GTypeInfo typeinfo = {
      sizeof (GstMaruAudEncClass),
      (GBaseInitFunc) gst_maruaudenc_base_init,
      NULL,
      (GClassInitFunc) gst_maruaudenc_class_init,
      NULL,
      NULL,
      sizeof (GstMaruAudEnc),
      0,
      (GInstanceInitFunc) gst_maruaudenc_init,
  };

  GType type;
  gchar *type_name;
  gint rank = GST_RANK_PRIMARY * 2;
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

    if ((codec->media_type != AVMEDIA_TYPE_AUDIO) && (codec->codec_type != CODEC_TYPE_ENCODE)) {
      continue;
    }

    type_name = g_strdup_printf ("maru_%senc", codec->name);
    type = g_type_from_name (type_name);
    if (!type) {
      type = g_type_register_static (GST_TYPE_AUDIO_ENCODER, type_name, &typeinfo, 0);
      g_type_set_qdata (type, GST_MARUENC_PARAMS_QDATA, (gpointer) codec);

      {
        static const GInterfaceInfo preset_info = {
          NULL,
          NULL,
          NULL
        };
        g_type_add_interface_static (type, GST_TYPE_PRESET, &preset_info);
      }
    }

    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);
  } while ((elem = elem->next));

  GST_LOG ("Finished registering encoders");

  return TRUE;
}
