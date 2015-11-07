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

/* Modifications by Samsung Electronics Co., Ltd.
 * 1. Provide a hardware buffer in order to avoid additional memcpy operations.
 */



#include "gstmarudevice.h"
#include "gstmaruutils.h"
#include "gstmaruinterface.h"

#define GST_MARUDEC_PARAMS_QDATA g_quark_from_static_string("marudec-params")

typedef struct _GstMaruAudDecClass
{
  GstAudioDecoderClass parent_class;

  CodecElement *codec;
  GstPadTemplate *sinktempl;
  GstPadTemplate *srctempl;
} GstMaruAudDecClass;

static GstElementClass *parent_class = NULL;

static void gst_maruauddec_base_init (GstMaruAudDecClass *klass);
static void gst_maruauddec_class_init (GstMaruAudDecClass *klass);
static void gst_maruauddec_init (GstMaruAudDec *maruauddec);
static void gst_maruauddec_finalize (GObject *object);

static gboolean gst_maruauddec_start (GstAudioDecoder *decoder);
static gboolean gst_maruauddec_stop (GstAudioDecoder *decoder);
static void gst_maruauddec_flush (GstAudioDecoder *decoder, gboolean hard);
static gboolean gst_maruauddec_set_format (GstAudioDecoder *decoder,
    GstCaps *caps);
static GstFlowReturn gst_maruauddec_handle_frame (GstAudioDecoder *decoder,
    GstBuffer *inbuf);

static gboolean gst_maruauddec_negotiate (GstMaruAudDec *maruauddec,
    gboolean force);

static void
gst_maruauddec_base_init (GstMaruAudDecClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCaps *sinkcaps = NULL, *srccaps = NULL;
  GstPadTemplate *sinktempl, *srctempl;
  gchar *longname, *description;

  CodecElement *codec = NULL;

  codec =
      (CodecElement *)g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
                                      GST_MARUDEC_PARAMS_QDATA);\
  g_assert (codec != NULL);

  longname = g_strdup_printf ("Maru %s Decoder", codec->longname);
  description = g_strdup_printf("Maru %s Decoder", codec->name);

  gst_element_class_set_details_simple (element_class,
            longname,
            "Codec/Decoder/Audio",
            description,
            "SooYoung Ha <yoosah.ha@samsung.com>");

  g_free (longname);
  g_free (description);

  sinkcaps = gst_maru_codecname_to_caps (codec->name, NULL, FALSE);
  if (!sinkcaps) {
    GST_DEBUG ("Couldn't get sink caps for decoder %s", codec->name);
    sinkcaps = gst_caps_from_string ("unknown/unknown");
  }

  srccaps = gst_maru_codectype_to_audio_caps (NULL, codec->name, FALSE, codec);
  if (!srccaps) {
    GST_DEBUG ("Couldn't get src caps for decoder %s", codec->name);
    srccaps = gst_caps_from_string ("audio/x-raw");
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
gst_maruauddec_class_init (GstMaruAudDecClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAudioDecoderClass *gstauddecoder_class = GST_AUDIO_DECODER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_maruauddec_finalize;

  gstauddecoder_class->start = GST_DEBUG_FUNCPTR (gst_maruauddec_start);
  gstauddecoder_class->stop = GST_DEBUG_FUNCPTR (gst_maruauddec_stop);
  gstauddecoder_class->set_format = GST_DEBUG_FUNCPTR (gst_maruauddec_set_format);
  gstauddecoder_class->handle_frame = GST_DEBUG_FUNCPTR (gst_maruauddec_handle_frame);
  gstauddecoder_class->flush = GST_DEBUG_FUNCPTR (gst_maruauddec_flush);
}

static void
gst_maruauddec_init (GstMaruAudDec *maruauddec)
{
  maruauddec->context = g_malloc0 (sizeof(CodecContext));
  maruauddec->context->audio.sample_fmt = SAMPLE_FMT_NONE;
  maruauddec->opened = FALSE;

  // TODO: check why
  gst_audio_decoder_set_drainable (GST_AUDIO_DECODER (maruauddec), TRUE);
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (maruauddec), TRUE);
}

static void
gst_maruauddec_finalize (GObject *object)
{
  GstMaruAudDec *maruauddec = (GstMaruAudDec *) object;

  GST_DEBUG_OBJECT (maruauddec, "finalize object and release context");

  // TODO: check why
  if (maruauddec->opened) {
    gst_maru_avcodec_close (maruauddec->context, maruauddec->dev);
    maruauddec->opened = FALSE;
  }

  g_free (maruauddec->context);
  maruauddec->context = NULL;

  g_free (maruauddec->dev);
  maruauddec->dev = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_maruauddec_close (GstMaruAudDec *maruauddec, gboolean reset)
{
  GST_LOG_OBJECT (maruauddec, "closing maru codec");

  gst_caps_replace (&maruauddec->last_caps, NULL);
  gst_buffer_replace (&maruauddec->outbuf, NULL);

  gst_maru_avcodec_close (maruauddec->context, maruauddec->dev);
  maruauddec->opened = FALSE;

  if (maruauddec->context) {
    g_free(maruauddec->context->codecdata);
    maruauddec->context->codecdata = NULL;
  }

  return TRUE;
}

gboolean gst_maruauddec_start(GstAudioDecoder *decoder)
{
  GstMaruAudDec *maruauddec = (GstMaruAudDec *) decoder;

  GST_OBJECT_LOCK (maruauddec);
  gst_maru_avcodec_close (maruauddec->context, maruauddec->dev);

  GST_OBJECT_UNLOCK (maruauddec);

  return TRUE;
}

gboolean gst_maruauddec_stop(GstAudioDecoder *decoder)
{
  GstMaruAudDec *maruauddec = (GstMaruAudDec *) decoder;

  GST_OBJECT_LOCK (maruauddec);
  gst_maruauddec_close (maruauddec, FALSE);
  GST_OBJECT_UNLOCK (maruauddec);

  // initialize 'GstAudioInfo' with default values
  gst_audio_info_init (&maruauddec->info);

  // TODO: check why
  gst_caps_replace (&maruauddec->last_caps, NULL);

  return TRUE;
}

static gboolean
gst_maruauddec_open (GstMaruAudDec *maruauddec)
{
  GstMaruAudDecClass *oclass;

  oclass = (GstMaruAudDecClass *) (G_OBJECT_GET_CLASS (maruauddec));

  maruauddec->dev = g_try_malloc0 (sizeof(CodecDevice));
  if (!maruauddec->dev) {
    GST_ERROR_OBJECT (maruauddec, "failed to allocate memory for CodecDevice");
    return FALSE;
  }

  if (gst_maru_avcodec_open (maruauddec->context, oclass->codec, maruauddec->dev) < 0) {
    gst_maruauddec_close(maruauddec, TRUE);
    GST_DEBUG_OBJECT (maruauddec,
      "maru_%sdec: Failed to open codec", oclass->codec->name);
    return FALSE;
  }

  maruauddec->opened = TRUE;
  GST_LOG_OBJECT (maruauddec, "Opened codec %s", oclass->codec->name);

  gst_audio_info_init (&maruauddec->info);

  maruauddec->audio.sample_rate = 0;
  maruauddec->audio.channels = 0;
  maruauddec->audio.depth = 0;

  return TRUE;
}

static gint
gst_maruauddec_audio_frame (GstMaruAudDec *marudec,
    CodecElement *codec, guint8 *data, guint size, GstTSInfo *dec_info,
    GstBuffer **outbuf, GstFlowReturn *ret)
{
  GST_DEBUG (" >> ENTER");

  gint len = -1;
  gint have_data = FF_MAX_AUDIO_FRAME_SIZE;
  GstClockTime out_timestamp, out_duration;
  gint64 out_offset;
  GstMapInfo outmapinfo;

  void* buf = g_malloc0(FF_MAX_AUDIO_FRAME_SIZE);

  GST_DEBUG_OBJECT (marudec, "decode audio, input buffer size %d", size);

  len = interface->decode_audio (marudec->context, (int16_t *) buf, &have_data,
      data, size, marudec->dev);

  if (len >= 0 && have_data > 0) {
    GST_DEBUG_OBJECT (marudec, "Creating output buffer");
    if (!gst_maruauddec_negotiate (marudec, FALSE)) {
       GST_DEBUG ("negotiation failed.");
      return len;
    } else {
       GST_DEBUG ("negotiation passed.");
    }

    *outbuf = gst_audio_decoder_allocate_output_buffer (GST_AUDIO_DECODER (marudec), len);
    gst_buffer_map (*outbuf, &outmapinfo, GST_MAP_READWRITE);
    memcpy(outmapinfo.data, buf, len);
    g_free(buf);

    gst_buffer_unmap (*outbuf, &outmapinfo);
    out_timestamp = dec_info->timestamp;

    /* calculate based on number of samples */
    out_duration = gst_util_uint64_scale (have_data, GST_SECOND,
        marudec->info.finfo->depth * marudec->info.channels *
        marudec->context->audio.sample_rate);

    out_offset = dec_info->offset;

    GST_DEBUG_OBJECT (marudec,
        "Buffer created. Size: %d, timestamp: %" GST_TIME_FORMAT
        ", duration: %" GST_TIME_FORMAT, have_data,
        GST_TIME_ARGS (out_timestamp), GST_TIME_ARGS (out_duration));

    GST_BUFFER_TIMESTAMP (*outbuf) = out_timestamp;
    GST_BUFFER_DURATION (*outbuf) = out_duration;
    GST_BUFFER_OFFSET (*outbuf) = out_offset;
  }

  if (len == -1 && !strcmp(codec->name, "aac")) {
    GST_ELEMENT_ERROR (marudec, STREAM, DECODE, (NULL),
        ("Decoding of AAC stream failed."));
    *ret = GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (marudec, "return flow %d, out %p, len %d",
    *ret, *outbuf, len);

  return len;
}

static gint
gst_maruauddec_frame (GstMaruAudDec *marudec,
    guint8 *data, guint size, GstTSInfo *dec_info, gint *got_data, GstFlowReturn *ret)
{
  GST_DEBUG (" >> ENTER ");

  GstMaruAudDecClass *oclass;
  GstBuffer *outbuf = marudec->outbuf;
  gint have_data = 0, len = 0;

  if (G_UNLIKELY (marudec->context->codec == NULL)) {
    GST_ERROR_OBJECT (marudec, "no codec context");
    return -1;
  }
  GST_LOG_OBJECT (marudec, "data:%p, size:%d", data, size);

  *ret = GST_FLOW_OK;
  oclass = (GstMaruAudDecClass *) (G_OBJECT_GET_CLASS (marudec));

  switch (oclass->codec->media_type) {
  case AVMEDIA_TYPE_AUDIO:
    len = gst_maruauddec_audio_frame (marudec, oclass->codec, data, size,
        dec_info, &marudec->outbuf, ret);
    if (marudec->outbuf == NULL ) {
      GST_DEBUG_OBJECT (marudec, "no buffer but keeping timestamp");
    }
    break;
  default:
    GST_ERROR_OBJECT (marudec, "Asked to decode non-audio/video frame!");
    g_assert_not_reached ();
    break;
  }

  if (marudec->outbuf) {
    have_data = 1;
  }

  if (len < 0 || have_data < 0) {
    GST_WARNING_OBJECT (marudec,
      "maru_%sdec: decoding error (len: %d, have_data: %d)",
      oclass->codec->name, len, have_data);
    *got_data = 0;
    return len;
  } else if (len == 0 && have_data == 0) {
    *got_data = 0;
    return len;
  } else {
    *got_data = 1;
  }
  if (marudec->outbuf) {
    GST_DEBUG_OBJECT (marudec, "Decoded data, now storing buffer %p", outbuf);
  } else {
    GST_DEBUG_OBJECT (marudec, "We didn't get a decoded buffer");
  }
  return len;
}

static void
gst_maruauddec_drain (GstMaruAudDec *maruauddec)
{
  GST_DEBUG_OBJECT (maruauddec, "drain frame");

  gint have_data, len;

  do {
    GstFlowReturn ret;

    len =
      gst_maruauddec_frame (maruauddec, NULL, 0, NULL, &have_data, &ret);

  } while (len >= 0 && have_data == 1);

  if (maruauddec->outbuf) {
    gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (maruauddec),
        maruauddec->outbuf, 1);
    maruauddec->outbuf = NULL;
  }
}

gboolean gst_maruauddec_set_format(GstAudioDecoder *decoder, GstCaps *caps)
{
  GstMaruAudDec *maruauddec = NULL;
  GstMaruAudDecClass *oclass;
  gboolean ret = TRUE;

  maruauddec = (GstMaruAudDec *) decoder;
  if (!maruauddec) {
    GST_DEBUG ("maruauddec is NULL");
    return FALSE;
  }

  oclass = (GstMaruAudDecClass *) (G_OBJECT_GET_CLASS (maruauddec));

  GST_DEBUG_OBJECT (maruauddec, "set_format called.");

  GST_OBJECT_LOCK (maruauddec);

  // TODO: check why
  if (maruauddec->last_caps && gst_caps_is_equal (maruauddec->last_caps, caps)) {
    GST_DEBUG_OBJECT (maruauddec, "same caps");
    GST_OBJECT_UNLOCK (maruauddec);
    return TRUE;
  }

  gst_caps_replace (&maruauddec->last_caps, caps);

  if (maruauddec->opened) {
    GST_OBJECT_UNLOCK (maruauddec);
    gst_maruauddec_drain (maruauddec);
    GST_OBJECT_LOCK (maruauddec);

    if (!gst_maruauddec_close (maruauddec, TRUE)) {
      GST_OBJECT_UNLOCK (maruauddec);
      return FALSE;
    }
  }

  gst_maru_caps_with_codecname (oclass->codec->name, oclass->codec->media_type,
                                caps, maruauddec->context);

  if (!gst_maruauddec_open (maruauddec)) {
    GST_DEBUG_OBJECT (maruauddec, "Failed to open");
    GST_OBJECT_UNLOCK (maruauddec);
    return FALSE;
  }

  GST_OBJECT_UNLOCK (maruauddec);

  return ret;
}

static GstTSInfo *
gst_ts_info_store (GstMaruAudDec *dec, GstClockTime timestamp,
    GstClockTime duration, gint64 offset)
{
  GST_DEBUG (" >> ENTER ");
  gint idx = dec->ts_idx;
  dec->ts_info[idx].idx = idx;
  dec->ts_info[idx].timestamp = timestamp;
  dec->ts_info[idx].duration = duration;
  dec->ts_info[idx].offset = offset;
  dec->ts_idx = (idx + 1) & MAX_TS_MASK;

  return &dec->ts_info[idx];
}

GstFlowReturn gst_maruauddec_handle_frame(GstAudioDecoder *decoder, GstBuffer *inbuf)
{
  GST_DEBUG (" >> ENTER ");

  GstMaruAudDec *marudec = (GstMaruAudDec *) decoder;
  gint have_data;
  GstMapInfo mapinfo;
  GstFlowReturn ret = GST_FLOW_OK;

  guint8 *in_buf;
  gint in_size;
  GstClockTime in_timestamp;
  GstClockTime in_duration;
  gint64 in_offset;
  GstTSInfo *in_info;
  GstTSInfo *dec_info;

  if (inbuf == NULL) {
    gst_maruauddec_drain (marudec);
    return GST_FLOW_OK;
  }
  inbuf = gst_buffer_ref (inbuf);

  if (!gst_buffer_map (inbuf, &mapinfo, GST_MAP_READ)) {
    GST_ERROR_OBJECT (marudec, "Failed to map buffer");
    gst_buffer_unref (inbuf);
    return GST_FLOW_ERROR;
  }

  in_timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  in_duration = GST_BUFFER_DURATION (inbuf);
  in_offset = GST_BUFFER_OFFSET (inbuf);

  in_info = gst_ts_info_store (marudec, in_timestamp, in_duration, in_offset);
  GST_LOG_OBJECT (marudec,
    "Received new data of size %zu, offset: %" G_GUINT64_FORMAT ", ts:%"
    GST_TIME_FORMAT ", dur: %" GST_TIME_FORMAT ", info %d",
    mapinfo.size, GST_BUFFER_OFFSET (inbuf),
    GST_TIME_ARGS (in_timestamp), GST_TIME_ARGS (in_duration), in_info->idx);

  in_size = mapinfo.size;
  in_buf = (guint8 *) mapinfo.data;

  dec_info = in_info;

  gst_maruauddec_frame (marudec, in_buf, in_size, dec_info, &have_data, &ret);

  gst_buffer_unmap (inbuf, &mapinfo);
  gst_buffer_unref (inbuf);

  if (marudec->outbuf) {
    ret = gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (marudec),
          marudec->outbuf, 1);
  } else {
    GST_DEBUG ("There is NO valid marudec->output");
  }
  marudec->outbuf = NULL;

  return ret;
}

void gst_maruauddec_flush(GstAudioDecoder *decoder, gboolean hard)
{
  GstMaruAudDec *maruauddec = (GstMaruAudDec *) decoder;

  GST_DEBUG_OBJECT (maruauddec, "flush decoded buffers");
  interface->flush_buffers (maruauddec->context, maruauddec->dev);
}

static gboolean
gst_maruauddec_negotiate (GstMaruAudDec *maruauddec, gboolean force)
{
  GstMaruAudDecClass *oclass;

  gint depth;
  GstAudioFormat format;
  GstAudioChannelPosition pos[64] = { 0, };

  oclass = (GstMaruAudDecClass *) (G_OBJECT_GET_CLASS (maruauddec));

  depth = gst_maru_smpfmt_depth (maruauddec->context->audio.sample_fmt);
  format = gst_maru_smpfmt_to_audioformat (maruauddec->context->audio.sample_fmt);
  if (format == GST_AUDIO_FORMAT_UNKNOWN) {
    GST_ELEMENT_ERROR (maruauddec, CORE, NEGOTIATION,
      ("Could not find GStreamer caps mapping for codec '%s'.",
      oclass->codec->name), (NULL));
    return FALSE;
  }

  if (!force && maruauddec->info.rate ==
      maruauddec->context->audio.sample_rate &&
      maruauddec->info.channels == maruauddec->context->audio.channels &&
      maruauddec->info.finfo->depth == depth) {
      return TRUE;
  }

  GST_DEBUG_OBJECT (maruauddec,
      "Renegotiating audio from %dHz@%dchannels (%d) to %dHz@%dchannels (%d)",
      maruauddec->info.rate, maruauddec->info.channels,
      maruauddec->info.finfo->depth,
      maruauddec->context->audio.sample_rate, maruauddec->context->audio.channels, depth);

  gst_maru_channel_layout_to_gst (maruauddec->context->audio.channel_layout,
      maruauddec->context->audio.channels, pos);
  memcpy (maruauddec->layout, pos,
      sizeof (GstAudioChannelPosition) * maruauddec->context->audio.channels);

  /* Get GStreamer channel layout */
  gst_audio_channel_positions_to_valid_order (pos, maruauddec->context->audio.channels);

  // TODO: purpose of needs_reorder ?
  maruauddec->needs_reorder =
      memcmp (pos, maruauddec->layout,
      sizeof (pos[0]) * maruauddec->context->audio.channels) != 0;

  gst_audio_info_set_format (&maruauddec->info, format,
      maruauddec->context->audio.sample_rate, maruauddec->context->audio.channels, pos);

  if (!gst_audio_decoder_set_output_format (GST_AUDIO_DECODER (maruauddec),
    &maruauddec->info)) {
    GST_ELEMENT_ERROR (maruauddec, CORE, NEGOTIATION, (NULL),
        ("Could not set caps for maru decoder (%s), not fixed?",
            oclass->codec->name));
    memset (&maruauddec->info, 0, sizeof (maruauddec->info));
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_maruauddec_register (GstPlugin *plugin, GList *element)
{
  GTypeInfo typeinfo = {
      sizeof (GstMaruAudDecClass),
      (GBaseInitFunc) gst_maruauddec_base_init,
      NULL,
      (GClassInitFunc) gst_maruauddec_class_init,
      NULL,
      NULL,
      sizeof (GstMaruAudDec),
      0,
      (GInstanceInitFunc) gst_maruauddec_init,
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

    if ((codec->media_type != AVMEDIA_TYPE_AUDIO) || (codec->codec_type != CODEC_TYPE_DECODE)) {
      continue;
    }

    type_name = g_strdup_printf ("maru_%sdec", codec->name);
    type = g_type_from_name (type_name);
    if (!type) {
      type = g_type_register_static (GST_TYPE_AUDIO_DECODER, type_name, &typeinfo, 0);
      g_type_set_qdata (type, GST_MARUDEC_PARAMS_QDATA, (gpointer) codec);
    }

    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);
  } while ((elem = elem->next));

  GST_LOG ("Finished Registering decoders");

  return TRUE;
}
