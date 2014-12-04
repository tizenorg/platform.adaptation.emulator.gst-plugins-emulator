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

/* indicate dts, pts, offset in the stream */
#define GST_TS_INFO_NONE &ts_info_none
static const GstTSInfo ts_info_none = { -1, -1, -1, -1 };

typedef struct _GstMaruDecClass
{
  GstElementClass parent_class;

  CodecElement *codec;
  GstPadTemplate *sinktempl;
  GstPadTemplate *srctempl;
} GstMaruDecClass;


static GstElementClass *parent_class = NULL;

static void gst_marudec_base_init (GstMaruDecClass *klass);
static void gst_marudec_class_init (GstMaruDecClass *klass);
static void gst_marudec_init (GstMaruDec *marudec);
static void gst_marudec_finalize (GObject *object);

static gboolean gst_marudec_setcaps (GstPad *pad, GstCaps *caps);

// sinkpad
static gboolean gst_marudec_sink_event (GstPad *pad, GstEvent *event);
static GstFlowReturn gst_marudec_chain (GstPad *pad, GstBuffer *buffer);

// srcpad
static gboolean gst_marudec_src_event (GstPad *pad, GstEvent *event);
static GstStateChangeReturn gst_marudec_change_state (GstElement *element,
                                                GstStateChange transition);

static gboolean gst_marudec_negotiate (GstMaruDec *dec, gboolean force);

static gint gst_marudec_frame (GstMaruDec *marudec, guint8 *data,
                              guint size, gint *got_data,
                              const GstTSInfo *dec_info, gint64 in_offset, GstFlowReturn *ret);

static gboolean gst_marudec_open (GstMaruDec *marudec);
static void gst_marudec_close (GstMaruDec *marudec);

// for profile
static GTimer* profile_decode_timer = NULL;
static gdouble elapsed_decode_time = 0;
static int decoded_frame_cnt = 0;
static int last_frame_cnt = 0;
static GMutex profile_mutex;
static int profile_init = 0;

static gboolean
maru_profile_cb (gpointer user_data)
{
  int decoding_fps = 0;
  gdouble decoding_time = 0;

  g_mutex_lock (&profile_mutex);
  if (decoded_frame_cnt < 0) {
    decoded_frame_cnt = 0;
    last_frame_cnt = 0;
    elapsed_decode_time = 0;
    g_mutex_unlock (&profile_mutex);
    return FALSE;
  }

  decoding_fps = decoded_frame_cnt - last_frame_cnt;
  last_frame_cnt = decoded_frame_cnt;

  decoding_time = elapsed_decode_time;
  elapsed_decode_time = 0;
  g_mutex_unlock (&profile_mutex);

  GST_DEBUG ("decoding fps=%d, latency=%f\n", decoding_fps, decoding_time/decoding_fps);
  return TRUE;
}

static void init_codec_profile(void)
{
  if (!profile_init) {
    profile_init = 1;
    profile_decode_timer = g_timer_new();
  }
}

static void reset_codec_profile(void)
{
  g_mutex_lock (&profile_mutex);
  decoded_frame_cnt = -1;
  g_mutex_lock (&profile_mutex);
}

static void begin_video_decode_profile(void)
{
  g_timer_start(profile_decode_timer);
}

static void end_video_decode_profile(void)
{
  g_timer_stop(profile_decode_timer);

  g_mutex_lock (&profile_mutex);
  if (decoded_frame_cnt == 0) {
    g_timeout_add_seconds(1, maru_profile_cb, NULL);
  }

  elapsed_decode_time += g_timer_elapsed(profile_decode_timer, NULL);
  decoded_frame_cnt++;
  g_mutex_unlock (&profile_mutex);
}

#define INIT_CODEC_PROFILE(fd)              \
  if (interface->get_profile_status(fd)) {  \
    init_codec_profile();                   \
  }
#define RESET_CODEC_PROFILE(s)  \
  if (profile_init) {           \
    reset_codec_profile();      \
  }
#define BEGIN_VIDEO_DECODE_PROFILE()  \
  if (profile_init) {                 \
    begin_video_decode_profile();     \
  }
#define END_VIDEO_DECODE_PROFILE()  \
  if (profile_init) {               \
    end_video_decode_profile();     \
  }


static const GstTSInfo *
gst_ts_info_store (GstMaruDec *dec, GstClockTime timestamp,
    GstClockTime duration, gint64 offset)
{
  gint idx = dec->ts_idx;
  dec->ts_info[idx].idx = idx;
  dec->ts_info[idx].timestamp = timestamp;
  dec->ts_info[idx].duration = duration;
  dec->ts_info[idx].offset = offset;
  dec->ts_idx = (idx + 1) & MAX_TS_MASK;

  return &dec->ts_info[idx];
}

static const GstTSInfo *
gst_ts_info_get (GstMaruDec *dec, gint idx)
{
  if (G_UNLIKELY (idx < 0 || idx > MAX_TS_MASK))
    return GST_TS_INFO_NONE;

  return &dec->ts_info[idx];
}

static void
gst_marudec_reset_ts (GstMaruDec *marudec)
{
  marudec->next_out = GST_CLOCK_TIME_NONE;
}

static void
gst_marudec_update_qos (GstMaruDec *marudec, gdouble proportion,
  GstClockTime timestamp)
{
  GST_LOG_OBJECT (marudec, "update QOS: %f, %" GST_TIME_FORMAT,
      proportion, GST_TIME_ARGS (timestamp));

  GST_OBJECT_LOCK (marudec);
  marudec->proportion = proportion;
  marudec->earliest_time = timestamp;
  GST_OBJECT_UNLOCK (marudec);
}

static void
gst_marudec_reset_qos (GstMaruDec *marudec)
{
  gst_marudec_update_qos (marudec, 0.5, GST_CLOCK_TIME_NONE);
  marudec->processed = 0;
  marudec->dropped = 0;
}

static gboolean
gst_marudec_do_qos (GstMaruDec *marudec, GstClockTime timestamp,
  gboolean *mode_switch)
{
  GstClockTimeDiff diff;
  gdouble proportion;
  GstClockTime qostime, earliest_time;
  gboolean res = TRUE;

  *mode_switch = FALSE;

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp))) {
    marudec->processed++;
    return TRUE;
  }

  proportion = marudec->proportion;
  earliest_time = marudec->earliest_time;

  qostime = gst_segment_to_running_time (&marudec->segment, GST_FORMAT_TIME,
    timestamp);

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (qostime))) {
    marudec->processed++;
    return TRUE;
  }

  diff = GST_CLOCK_DIFF (qostime, earliest_time);

  if (proportion < 0.4 && diff < 0 ){
    marudec->processed++;
    return TRUE;
  } else {
    if (diff >= 0) {
      if (0) {
        res = FALSE;
      }

      GstClockTime stream_time, jitter;
      GstMessage *qos_msg;

      marudec->dropped++;
      stream_time =
          gst_segment_to_stream_time (&marudec->segment, GST_FORMAT_TIME,
                  timestamp);
      jitter = GST_CLOCK_DIFF (qostime, earliest_time);
      qos_msg =
          gst_message_new_qos (GST_OBJECT_CAST (marudec), FALSE, qostime,
                  stream_time, timestamp, GST_CLOCK_TIME_NONE);
      gst_message_set_qos_values (qos_msg, jitter, proportion, 1000000);
      gst_message_set_qos_stats (qos_msg, GST_FORMAT_BUFFERS,
              marudec->processed, marudec->dropped);
      gst_element_post_message (GST_ELEMENT_CAST (marudec), qos_msg);

      return res;
    }
  }

  marudec->processed++;
  return TRUE;
}

static void
clear_queued (GstMaruDec *marudec)
{
  g_list_foreach (marudec->queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (marudec->queued);
  marudec->queued = NULL;
}

static GstFlowReturn
flush_queued (GstMaruDec *marudec)
{
  GstFlowReturn res = GST_FLOW_OK;

  GST_DEBUG_OBJECT (marudec, "flush queued");

  while (marudec->queued) {
    GstBuffer *buf = GST_BUFFER_CAST (marudec->queued->data);

    GST_LOG_OBJECT (marudec, "pushing buffer %p, offset %"
      G_GUINT64_FORMAT ", timestamp %"
      GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT, buf,
      GST_BUFFER_OFFSET (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    res = gst_pad_push (marudec->srcpad, buf);

    marudec->queued =
      g_list_delete_link (marudec->queued, marudec->queued);
  }

  return res;
}

static void
gst_marudec_drain (GstMaruDec *marudec)
{
#if 0
  GstMaruDecClass *oclass;
  oclass = (GstMaruDecClass *) (G_OBJECT_GET_CLASS (marudec));
#endif

  GST_DEBUG_OBJECT (marudec, "drain frame");
  {
    gint have_data, len, try = 0;

    do {
      GstFlowReturn ret;

      len =
        gst_marudec_frame (marudec, NULL, 0, &have_data, &ts_info_none, 0, &ret);

      if (len < 0 || have_data == 0) {
        break;
      }
    } while (try++ < 10);
  }

  if (marudec->segment.rate < 0.0) {
    GST_DEBUG_OBJECT (marudec, "reverse playback");
    flush_queued (marudec);
  }
}

/*
 * Implementation
 */
static void
gst_marudec_base_init (GstMaruDecClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCaps *sinkcaps = NULL, *srccaps = NULL;
  GstPadTemplate *sinktempl, *srctempl;
  CodecElement *codec;
  gchar *longname, *classification, *description;

  codec =
      (CodecElement *)g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
                                      GST_MARUDEC_PARAMS_QDATA);

  longname = g_strdup_printf ("%s Decoder", codec->longname);
  classification = g_strdup_printf ("Codec/Decoder/%s",
                    (codec->media_type == AVMEDIA_TYPE_VIDEO) ?
                    "Video" : "Audio");
  description = g_strdup_printf("%s Decoder", codec->name);

  gst_element_class_set_details_simple (element_class,
            longname,
            classification,
            description,
            "Kitae Kim <kt920.kim@samsung.com>");

  g_free (longname);
  g_free (classification);
  g_free (description);

  sinkcaps = gst_maru_codecname_to_caps (codec->name, NULL, FALSE);
  if (!sinkcaps) {
    sinkcaps = gst_caps_from_string ("unknown/unknown");
  }

  switch (codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    srccaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv");
    break;
  case AVMEDIA_TYPE_AUDIO:
    srccaps = gst_maru_codectype_to_audio_caps (NULL, codec->name, FALSE, codec);
    break;
  default:
    GST_LOG("unknown media type");
    break;
  }

  if (!srccaps) {
    srccaps = gst_caps_from_string ("unknown/unknown");
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
gst_marudec_class_init (GstMaruDecClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

#if 0
  gobject_class->set_property = gst_marudec_set_property
  gobject_class->get_property = gst_marudec_get_property
#endif

  gobject_class->finalize = gst_marudec_finalize;
  gstelement_class->change_state = gst_marudec_change_state;
}

static void
gst_marudec_init (GstMaruDec *marudec)
{
  GstMaruDecClass *oclass;

  oclass = (GstMaruDecClass*) (G_OBJECT_GET_CLASS(marudec));

  marudec->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_setcaps_function (marudec->sinkpad,
    GST_DEBUG_FUNCPTR(gst_marudec_setcaps));
  gst_pad_set_event_function (marudec->sinkpad,
    GST_DEBUG_FUNCPTR(gst_marudec_sink_event));
  gst_pad_set_chain_function (marudec->sinkpad,
    GST_DEBUG_FUNCPTR(gst_marudec_chain));

  marudec->srcpad = gst_pad_new_from_template (oclass->srctempl, "src") ;
  gst_pad_use_fixed_caps (marudec->srcpad);
  gst_pad_set_event_function (marudec->srcpad,
    GST_DEBUG_FUNCPTR(gst_marudec_src_event));

  gst_element_add_pad (GST_ELEMENT(marudec), marudec->sinkpad);
  gst_element_add_pad (GST_ELEMENT(marudec), marudec->srcpad);

  marudec->context = g_malloc0 (sizeof(CodecContext));
  marudec->context->video.pix_fmt = PIX_FMT_NONE;
  marudec->context->audio.sample_fmt = SAMPLE_FMT_NONE;

  marudec->opened = FALSE;
  marudec->format.video.par_n = -1;
  marudec->format.video.fps_n = -1;
  marudec->format.video.old_fps_n = -1;

  marudec->queued = NULL;
  gst_segment_init (&marudec->segment, GST_FORMAT_TIME);
}

static void
gst_marudec_finalize (GObject *object)
{
  GstMaruDec *marudec = (GstMaruDec *) object;

  GST_DEBUG_OBJECT (marudec, "finalize object and release context");
  g_free (marudec->context);
  marudec->context = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_marudec_src_event (GstPad *pad, GstEvent *event)
{
  GstMaruDec *marudec;
  gboolean res;

  marudec = (GstMaruDec *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
    /* Quality Of Service (QOS) event contains a report
      about the current real-time performance of the stream.*/
  case GST_EVENT_QOS:
  {
    gdouble proportion;
    GstClockTimeDiff diff;
    GstClockTime timestamp;

    gst_event_parse_qos (event, &proportion, &diff, &timestamp);

    /* update our QoS values */
    gst_marudec_update_qos (marudec, proportion, timestamp + diff);
    break;
  }
  default:
    break;
  }

  /* forward upstream */
  res = gst_pad_push_event (marudec->sinkpad, event);

  gst_object_unref (marudec);

  return res;
}

static gboolean
gst_marudec_sink_event (GstPad *pad, GstEvent *event)
{
  GstMaruDec *marudec;
  gboolean ret = FALSE;

  marudec = (GstMaruDec *) gst_pad_get_parent (pad);

  GST_DEBUG_OBJECT (marudec, "Handling %s event",
    GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_EOS:
    gst_marudec_drain (marudec);
    break;
  case GST_EVENT_FLUSH_STOP:
  {
    if (marudec->opened) {
      GST_DEBUG_OBJECT (marudec, "flush decoded buffers");
      interface->flush_buffers (marudec->context, marudec->dev);
    }

    gst_marudec_reset_ts (marudec);
    gst_marudec_reset_qos (marudec);
    gst_segment_init (&marudec->segment, GST_FORMAT_TIME);
    clear_queued (marudec);
  }
    break;
  case GST_EVENT_NEWSEGMENT:
  {
    gboolean update;
    GstFormat format;
    gint64 start, stop, time;
    gdouble rate, arate;

    gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
        &start, &stop, &time);

    switch (format) {
    case GST_FORMAT_TIME:
      break;
    case GST_FORMAT_BYTES:
    {
      gint bit_rate;
      bit_rate = marudec->context->bit_rate;

      if (!bit_rate) {
        GST_WARNING_OBJECT (marudec, "no bitrate to convert BYTES to TIME");
        gst_event_unref (event);
        gst_object_unref (marudec);
        return ret;
      }

      GST_DEBUG_OBJECT (marudec, "bitrate: %d", bit_rate);

      if (start != -1) {
        start = gst_util_uint64_scale_int (start, GST_SECOND, bit_rate);
      }
      if (stop != -1) {
        stop = gst_util_uint64_scale_int (stop, GST_SECOND, bit_rate);
      }
      if (time != -1) {
        time = gst_util_uint64_scale_int (time, GST_SECOND, bit_rate);
      }

      gst_event_unref (event);

      format = GST_FORMAT_TIME;

      stop = -1;
      event = gst_event_new_new_segment (update, rate, format,
          start, stop, time);
      break;
    }
    default:
      GST_WARNING_OBJECT (marudec, "unknown format received in NEWSEGMENT");
      gst_event_unref (event);
      gst_object_unref (marudec);
      return ret;
    }

    if (marudec->opened) {
      gst_marudec_drain (marudec);
    }

    GST_DEBUG_OBJECT (marudec,
      "NEWSEGMENT in time start %" GST_TIME_FORMAT " -- stop %"
      GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

    gst_segment_set_newsegment_full (&marudec->segment, update,
        rate, arate, format, start, stop, time);
    break;
  }
  default:
    break;
  }

  ret = gst_pad_push_event (marudec->srcpad, event);

  gst_object_unref (marudec);

  return ret;
}



static gboolean
gst_marudec_setcaps (GstPad *pad, GstCaps *caps)
{
  GstMaruDec *marudec;
  GstMaruDecClass *oclass;
  GstStructure *structure;
  const GValue *par;
  const GValue *fps;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (pad, "setcaps called.");

  marudec = (GstMaruDec *) (gst_pad_get_parent (pad));
  oclass = (GstMaruDecClass *) (G_OBJECT_GET_CLASS (marudec));

  GST_OBJECT_LOCK (marudec);

  if (marudec->opened) {
    GST_OBJECT_UNLOCK (marudec);
    gst_marudec_drain (marudec);
    GST_OBJECT_LOCK (marudec);
  }
  gst_marudec_close (marudec);

  GST_LOG_OBJECT (marudec, "size %dx%d", marudec->context->video.width,
      marudec->context->video.height);

  if (!strcmp(oclass->codec->name, "wmv3") ||
      !strcmp(oclass->codec->name, "vc1")) {
    gst_maru_caps_to_codecname (caps, oclass->codec->name, NULL);
  }

  gst_maru_caps_with_codecname (oclass->codec->name, oclass->codec->media_type,
                                caps, marudec->context);

  GST_LOG_OBJECT (marudec, "size after %dx%d", marudec->context->video.width,
      marudec->context->video.height);

  if (!marudec->context->video.fps_d || !marudec->context->video.fps_n) {
    GST_DEBUG_OBJECT (marudec, "forcing 25/1 framerate");
    marudec->context->video.fps_n = 1;
    marudec->context->video.fps_d = 25;
  }

  structure = gst_caps_get_structure (caps, 0);

  par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par) {
    GST_DEBUG_OBJECT (marudec, "sink caps have pixel-aspect-ratio of %d:%d",
        gst_value_get_fraction_numerator (par),
        gst_value_get_fraction_denominator (par));
  }

  fps = gst_structure_get_value (structure, "framerate");
  if (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps)) {
    marudec->format.video.fps_n = gst_value_get_fraction_numerator (fps);
    marudec->format.video.fps_d = gst_value_get_fraction_denominator (fps);
    GST_DEBUG_OBJECT (marudec, "Using framerate %d/%d from incoming",
        marudec->format.video.fps_n, marudec->format.video.fps_d);
  } else {
    marudec->format.video.fps_n = -1;
    GST_DEBUG_OBJECT (marudec, "Using framerate from codec");
  }

#if 0
  if (strcmp (oclass->codec->name, "aac") == 0) {
    const gchar *format = gst_structure_get_string (structure, "stream-format");
    if (format == NULL || strcmp ("format", "raw") == 0) {
      marudec->turnoff_parser = TRUE;
    }
  }
#endif

  if (!gst_marudec_open (marudec)) {
    GST_DEBUG_OBJECT (marudec, "Failed to open");
    GST_OBJECT_UNLOCK (marudec);
    gst_object_unref (marudec);

    return FALSE;
  }

  gst_structure_get_int (structure, "width",
    &marudec->format.video.clip_width);
  gst_structure_get_int (structure, "height",
    &marudec->format.video.clip_height);

  GST_DEBUG_OBJECT (pad, "clipping to %dx%d",
    marudec->format.video.clip_width, marudec->format.video.clip_height);

  GST_OBJECT_UNLOCK (marudec);
  gst_object_unref (marudec);

  return ret;
}

static gboolean
gst_marudec_open (GstMaruDec *marudec)
{
  GstMaruDecClass *oclass;

  oclass = (GstMaruDecClass *) (G_OBJECT_GET_CLASS (marudec));

  marudec->dev = g_try_malloc0 (sizeof(CodecDevice));
  if (!marudec->dev) {
    GST_ERROR_OBJECT (marudec, "failed to allocate memory for CodecDevice");
    return FALSE;
  }

  if (gst_maru_avcodec_open (marudec->context, oclass->codec, marudec->dev) < 0) {
    g_free(marudec->dev);
    marudec->dev = NULL;
    GST_ERROR_OBJECT (marudec,
      "maru_%sdec: Failed to open codec", oclass->codec->name);
    return FALSE;
  }

  marudec->opened = TRUE;
  GST_LOG_OBJECT (marudec, "Opened codec %s", oclass->codec->name);

  switch (oclass->codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    marudec->format.video.width = 0;
    marudec->format.video.height = 0;
    marudec->format.video.clip_width = -1;
    marudec->format.video.clip_height = -1;
    marudec->format.video.pix_fmt = PIX_FMT_NB;
    marudec->format.video.interlaced = FALSE;
    break;
  case AVMEDIA_TYPE_AUDIO:
    marudec->format.audio.samplerate = 0;
    marudec->format.audio.channels = 0;
    marudec->format.audio.depth = 0;
    break;
  default:
    break;
  }

  gst_marudec_reset_ts (marudec);

  marudec->proportion = 0.0;
  marudec->earliest_time = -1;

  // initialize profile resource
  INIT_CODEC_PROFILE(marudec->dev->fd);

  return TRUE;
}

static void
gst_marudec_close (GstMaruDec *marudec)
{
  if (!marudec->opened) {
    GST_DEBUG_OBJECT (marudec, "not opened yet");
    return;
  }

  if (marudec->context) {
    g_free(marudec->context->codecdata);
    marudec->context->codecdata = NULL;
  }

  if (!marudec->dev) {
    return;
  }

  gst_maru_avcodec_close (marudec->context, marudec->dev);
  marudec->opened = FALSE;

  if (marudec->dev) {
    g_free(marudec->dev);
    marudec->dev = NULL;
  }

  // reset profile resource
  RESET_CODEC_PROFILE();
}


static gboolean
gst_marudec_negotiate (GstMaruDec *marudec, gboolean force)
{
  GstMaruDecClass *oclass;
  GstCaps *caps;

  oclass = (GstMaruDecClass *) (G_OBJECT_GET_CLASS (marudec));

  switch (oclass->codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    if (!force && marudec->format.video.width == marudec->context->video.width
      && marudec->format.video.height == marudec->context->video.height
      && marudec->format.video.fps_n == marudec->format.video.old_fps_n
      && marudec->format.video.fps_d == marudec->format.video.old_fps_d
      && marudec->format.video.pix_fmt == marudec->context->video.pix_fmt
      && marudec->format.video.par_n == marudec->context->video.par_n
      && marudec->format.video.par_d == marudec->context->video.par_d) {
      return TRUE;
    }
    marudec->format.video.width = marudec->context->video.width;
    marudec->format.video.height = marudec->context->video.height;
    marudec->format.video.old_fps_n = marudec->format.video.fps_n;
    marudec->format.video.old_fps_d = marudec->format.video.fps_d;
    marudec->format.video.pix_fmt = marudec->context->video.pix_fmt;
    marudec->format.video.par_n = marudec->context->video.par_n;
    marudec->format.video.par_d = marudec->context->video.par_d;
    break;
  case AVMEDIA_TYPE_AUDIO:
  {
    gint depth = gst_maru_smpfmt_depth (marudec->context->audio.sample_fmt);
    if (!force && marudec->format.audio.samplerate ==
      marudec->context->audio.sample_rate &&
      marudec->format.audio.channels == marudec->context->audio.channels &&
      marudec->format.audio.depth == depth) {
      return TRUE;
    }
    marudec->format.audio.samplerate = marudec->context->audio.sample_rate;
    marudec->format.audio.channels = marudec->context->audio.channels;
    marudec->format.audio.depth = depth;
  }
    break;
  default:
    break;
  }

  caps =
    gst_maru_codectype_to_caps (oclass->codec->media_type, marudec->context,
      oclass->codec->name, FALSE);

  if (caps == NULL) {
    GST_ELEMENT_ERROR (marudec, CORE, NEGOTIATION,
      ("Could not find GStreamer caps mapping for codec '%s'.",
      oclass->codec->name), (NULL));
    return FALSE;
  }

  switch (oclass->codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
  {
    gint width, height;
    gboolean interlaced;

    width = marudec->format.video.clip_width;
    height = marudec->format.video.clip_height;
    interlaced = marudec->format.video.interlaced;

    if (width != -1 && height != -1) {
      if (width < marudec->context->video.width) {
        gst_caps_set_simple (caps, "width", G_TYPE_INT, width, NULL);
      }
      if (height < marudec->context->video.height) {
          gst_caps_set_simple (caps, "height", G_TYPE_INT, height, NULL);
      }
      gst_caps_set_simple (caps, "interlaced", G_TYPE_BOOLEAN, interlaced,
        NULL);

      if (marudec->format.video.fps_n != -1) {
          gst_caps_set_simple (caps, "framerate",
            GST_TYPE_FRACTION, marudec->format.video.fps_n,
            marudec->format.video.fps_d, NULL);
      }
    }
  }
    break;
  case AVMEDIA_TYPE_AUDIO:
    break;
  default:
    break;
  }

  if (!gst_pad_set_caps (marudec->srcpad, caps)) {
    GST_ELEMENT_ERROR (marudec, CORE, NEGOTIATION, (NULL),
      ("Could not set caps for decoder (%s), not fixed?",
      oclass->codec->name));
    gst_caps_unref (caps);
    return FALSE;
  }

  gst_caps_unref (caps);

  return TRUE;
}

GstBuffer *
new_aligned_buffer (gint size, GstCaps *caps)
{
  GstBuffer *buf;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = g_malloc0 (size);
  GST_BUFFER_SIZE (buf) = size;
  GST_BUFFER_FREE_FUNC (buf) = g_free;

  if (caps) {
    gst_buffer_set_caps (buf, caps);
  }

  return buf;
}

static GstFlowReturn
get_output_buffer (GstMaruDec *marudec, GstBuffer **outbuf)
{
  gint pict_size;
  GstFlowReturn ret;

  ret = GST_FLOW_OK;

  *outbuf = NULL;

  if (G_UNLIKELY (!gst_marudec_negotiate (marudec, FALSE))) {
    GST_DEBUG_OBJECT (marudec, "negotiate failed");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  pict_size = gst_maru_avpicture_size (marudec->context->video.pix_fmt,
    marudec->context->video.width, marudec->context->video.height);
  if (pict_size < 0) {
    GST_DEBUG_OBJECT (marudec, "size of a picture is negative. "
      "pixel format: %d, width: %d, height: %d",
      marudec->context->video.pix_fmt, marudec->context->video.width,
      marudec->context->video.height);
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (marudec, "outbuf size of decoded video %d", pict_size);

  gst_pad_set_element_private(GST_PAD_PEER(marudec->srcpad), (gpointer)marudec);

  /* GstPadBufferAllocFunction is mostly overridden by elements that can
   * provide a hardware buffer in order to avoid additional memcpy operations.
   */
  gst_pad_set_bufferalloc_function(
    GST_PAD_PEER(marudec->srcpad),
    (GstPadBufferAllocFunction) interface->buffer_alloc_and_copy);

  ret = gst_pad_alloc_buffer_and_set_caps (marudec->srcpad,
    GST_BUFFER_OFFSET_NONE, pict_size,
    GST_PAD_CAPS (marudec->srcpad), outbuf);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_DEBUG_OBJECT (marudec, "pad_alloc failed %d (%s)", ret,
      gst_flow_get_name (ret));
    return ret;
  }

  return ret;
}

static gboolean
clip_video_buffer (GstMaruDec *dec, GstBuffer *buf,
    GstClockTime in_ts, GstClockTime in_dur)
{
  gboolean res = TRUE;

  return res;
}

static gboolean
clip_audio_buffer (GstMaruDec *dec, GstBuffer *buf,
    GstClockTime in_ts, GstClockTime in_dur)
{
  GstClockTime stop;
  gint64 diff, cstart, cstop;
  gboolean res = TRUE;

  if (G_UNLIKELY (dec->segment.format != GST_FORMAT_TIME)) {
    GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
    return res;
  }

  // in_ts: in_timestamp. check a start time.
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (in_ts))) {
    GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
    return res;
  }

  stop =
    GST_CLOCK_TIME_IS_VALID (in_dur) ? (in_ts + in_dur) : GST_CLOCK_TIME_NONE;

  res = gst_segment_clip (&dec->segment, GST_FORMAT_TIME, in_ts,
                          stop, &cstart, &cstop);
  if (G_UNLIKELY (!res)) {
    GST_LOG_OBJECT (dec, "out of segment");
    GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
    return res;
  }

  if (G_UNLIKELY ((diff = cstart - in_ts) > 0)) {
    diff =
      gst_util_uint64_scale_int (diff, dec->format.audio.samplerate, GST_SECOND) *
        (dec->format.audio.depth * dec->format.audio.channels);

    GST_DEBUG_OBJECT (dec, "clipping start to %" GST_TIME_FORMAT " %"
        G_GINT64_FORMAT " bytes", GST_TIME_ARGS (cstart), diff);

    GST_BUFFER_SIZE (buf) -= diff;
    GST_BUFFER_DATA (buf) += diff;

  }

  if (G_UNLIKELY ((diff = stop - cstop) > 0)) {
    diff =
      gst_util_uint64_scale_int (diff, dec->format.audio.samplerate, GST_SECOND) *
        (dec->format.audio.depth * dec->format.audio.channels);

    GST_DEBUG_OBJECT (dec, "clipping stop to %" GST_TIME_FORMAT " %"
        G_GINT64_FORMAT " bytes", GST_TIME_ARGS (cstop), diff);

    GST_BUFFER_SIZE (buf) -= diff;
  }

  GST_BUFFER_TIMESTAMP (buf) = cstart;
  GST_BUFFER_DURATION (buf) = cstop - cstart;

  GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
  return res;
}

static gint
gst_marudec_video_frame (GstMaruDec *marudec, guint8 *data, guint size,
    const GstTSInfo *dec_info, gint64 in_offset, GstBuffer **outbuf,
    GstFlowReturn *ret)
{
  gint len = -1;
  gboolean mode_switch;
  gboolean decode;
  GstClockTime out_timestamp, out_duration, out_pts;
  gint64 out_offset;
  const GstTSInfo *out_info;
  int have_data;

  decode = gst_marudec_do_qos (marudec, dec_info->timestamp, &mode_switch);

  GST_DEBUG_OBJECT (marudec, "decode video: input buffer size %d", size);

  // begin video decode profile
  BEGIN_VIDEO_DECODE_PROFILE();

  len = interface->decode_video (marudec, data, size,
        dec_info->idx, in_offset, outbuf, &have_data);
  if (len < 0 || !have_data) {
    return len;
  }

  // end video decode profile
  END_VIDEO_DECODE_PROFILE();

  *ret = get_output_buffer (marudec, outbuf);
  if (G_UNLIKELY (*ret != GST_FLOW_OK)) {
    GST_DEBUG_OBJECT (marudec, "no output buffer");
    len = -1;
    GST_DEBUG_OBJECT (marudec, "return flow %d, out %p, len %d",
      *ret, *outbuf, len);
    return len;
  }

  out_info = gst_ts_info_get (marudec, dec_info->idx);
  out_pts = out_info->timestamp;
  out_duration = out_info->duration;
  out_offset = out_info->offset;

  /* Timestamps */
  out_timestamp = -1;
  if (out_pts != -1) {
    out_timestamp = (GstClockTime) out_pts;
    GST_LOG_OBJECT (marudec, "using timestamp %" GST_TIME_FORMAT
      " returned by ffmpeg", GST_TIME_ARGS (out_timestamp));
  }

  if (!GST_CLOCK_TIME_IS_VALID (out_timestamp) && marudec->next_out != -1) {
    out_timestamp = marudec->next_out;
    GST_LOG_OBJECT (marudec, "using next timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (out_timestamp));
  }

  if (!GST_CLOCK_TIME_IS_VALID (out_timestamp)) {
    out_timestamp = dec_info->timestamp;
    GST_LOG_OBJECT (marudec, "using in timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (out_timestamp));
  }
  GST_BUFFER_TIMESTAMP (*outbuf) = out_timestamp;

  /* Offset */
  if (out_offset != GST_BUFFER_OFFSET_NONE) {
    GST_LOG_OBJECT (marudec, "Using offset returned by ffmpeg");
  } else if (out_timestamp != GST_CLOCK_TIME_NONE) {
    GstFormat out_fmt = GST_FORMAT_DEFAULT;
    GST_LOG_OBJECT (marudec, "Using offset converted from timestamp");

    gst_pad_query_peer_convert (marudec->sinkpad,
      GST_FORMAT_TIME, out_timestamp, &out_fmt, &out_offset);
  } else if (dec_info->offset != GST_BUFFER_OFFSET_NONE) {
    GST_LOG_OBJECT (marudec, "using in_offset %" G_GINT64_FORMAT,
      dec_info->offset);
    out_offset = dec_info->offset;
  } else {
    GST_LOG_OBJECT (marudec, "no valid offset found");
    out_offset = GST_BUFFER_OFFSET_NONE;
  }
  GST_BUFFER_OFFSET (*outbuf) = out_offset;

  /* Duration */
  if (GST_CLOCK_TIME_IS_VALID (out_duration)) {
    GST_LOG_OBJECT (marudec, "Using duration returned by ffmpeg");
  } else if (GST_CLOCK_TIME_IS_VALID (dec_info->duration)) {
    GST_LOG_OBJECT (marudec, "Using in_duration");
    out_duration = dec_info->duration;
  } else {
    if (marudec->format.video.fps_n != -1 &&
        (marudec->format.video.fps_n != 1000 &&
        marudec->format.video.fps_d != 1)) {
      GST_LOG_OBJECT (marudec, "using input framerate for duration");
      out_duration = gst_util_uint64_scale_int (GST_SECOND,
        marudec->format.video.fps_d, marudec->format.video.fps_n);
    } else {
      if (marudec->context->video.fps_n != 0 &&
          (marudec->context->video.fps_d > 0 &&
            marudec->context->video.fps_d < 1000)) {
        GST_LOG_OBJECT (marudec, "using decoder's framerate for duration");
        out_duration = gst_util_uint64_scale_int (GST_SECOND,
          marudec->context->video.fps_n * 1,
          marudec->context->video.fps_d);
      } else {
        GST_LOG_OBJECT (marudec, "no valid duration found");
      }
    }
  }

  if (G_UNLIKELY (!clip_video_buffer (marudec, *outbuf, out_timestamp,
      out_duration))) {
    GST_DEBUG_OBJECT (marudec, "buffer clipped");
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
    GST_DEBUG_OBJECT (marudec, "return flow %d, out %p, len %d",
      *ret, *outbuf, len);
    return len;
  }

  GST_DEBUG_OBJECT (marudec, "return flow %d, out %p, len %d",
    *ret, *outbuf, len);
  return len;
}

static gint
gst_marudec_audio_frame (GstMaruDec *marudec, CodecElement *codec,
                          guint8 *data, guint size,
                          const GstTSInfo *dec_info, GstBuffer **outbuf,
                          GstFlowReturn *ret)
{
  gint len = -1;
  gint have_data = FF_MAX_AUDIO_FRAME_SIZE;
  GstClockTime out_timestamp, out_duration;
  gint64 out_offset;

  *outbuf =
      new_aligned_buffer (FF_MAX_AUDIO_FRAME_SIZE,
          GST_PAD_CAPS (marudec->srcpad));

  GST_DEBUG_OBJECT (marudec, "decode audio, input buffer size %d", size);

  len = interface->decode_audio (marudec->context,
      (int16_t *) GST_BUFFER_DATA (*outbuf), &have_data,
      data, size, marudec->dev);

  GST_DEBUG_OBJECT (marudec,
    "Decode audio: len=%d, have_data=%d", len, have_data);

  if (len >= 0 && have_data > 0) {
    GST_DEBUG_OBJECT (marudec, "Creating output buffer");
    if (!gst_marudec_negotiate (marudec, FALSE)) {
      gst_buffer_unref (*outbuf);
      *outbuf = NULL;
      len = -1;
      GST_DEBUG_OBJECT (marudec, "return flow %d, out %p, len %d",
        *ret, *outbuf, len);
      return len;
    }

    GST_BUFFER_SIZE (*outbuf) = len;

    if (GST_CLOCK_TIME_IS_VALID (dec_info->timestamp)) {
      out_timestamp = dec_info->timestamp;
    } else {
      out_timestamp = marudec->next_out;
    }

    /* calculate based on number of samples */
    out_duration = gst_util_uint64_scale (have_data, GST_SECOND,
        marudec->format.audio.depth * marudec->format.audio.channels *
        marudec->format.audio.samplerate);

    out_offset = dec_info->offset;

    GST_DEBUG_OBJECT (marudec,
        "Buffer created. Size: %d, timestamp: %" GST_TIME_FORMAT
        ", duration: %" GST_TIME_FORMAT, have_data,
        GST_TIME_ARGS (out_timestamp), GST_TIME_ARGS (out_duration));

    GST_BUFFER_TIMESTAMP (*outbuf) = out_timestamp;
    GST_BUFFER_DURATION (*outbuf) = out_duration;
    GST_BUFFER_OFFSET (*outbuf) = out_offset;
    gst_buffer_set_caps (*outbuf, GST_PAD_CAPS (marudec->srcpad));

    if (GST_CLOCK_TIME_IS_VALID (out_timestamp)) {
      marudec->next_out = out_timestamp + out_duration;
    }

    if (G_UNLIKELY (!clip_audio_buffer (marudec, *outbuf,
        out_timestamp, out_duration))) {
      GST_DEBUG_OBJECT (marudec, "buffer_clipped");
      gst_buffer_unref (*outbuf);
      *outbuf = NULL;
      GST_DEBUG_OBJECT (marudec, "return flow %d, out %p, len %d", *ret, *outbuf, len);
      return len;
    }
  } else {
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
  }

  if (len == -1 && !strcmp(codec->name, "aac")) {
    GST_ELEMENT_ERROR (marudec, STREAM, DECODE, (NULL),
        ("Decoding of AAC stream by FFMPEG failed."));
    *ret = GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (marudec, "return flow %d, out %p, len %d",
    *ret, *outbuf, len);
  return len;
}

static gint
gst_marudec_frame (GstMaruDec *marudec, guint8 *data, guint size,
    gint *got_data, const GstTSInfo *dec_info, gint64 in_offset, GstFlowReturn *ret)
{
  GstMaruDecClass *oclass;
  GstBuffer *outbuf = NULL;
  gint have_data = 0, len = 0;

  if (G_UNLIKELY (marudec->context->codec == NULL)) {
    GST_ERROR_OBJECT (marudec, "no codec context");
    return -1;
  }

  *ret = GST_FLOW_OK;
  oclass = (GstMaruDecClass *) (G_OBJECT_GET_CLASS (marudec));

  switch (oclass->codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    len = gst_marudec_video_frame (marudec, data, size,
        dec_info, in_offset, &outbuf, ret);
    break;
  case AVMEDIA_TYPE_AUDIO:
    len = gst_marudec_audio_frame (marudec, oclass->codec, data, size,
        dec_info, &outbuf, ret);
    if (outbuf == NULL && marudec->discont) {
      GST_DEBUG_OBJECT (marudec, "no buffer but keeping timestamp");
    }
    break;
  default:
    GST_ERROR_OBJECT (marudec, "Asked to decode non-audio/video frame!");
    g_assert_not_reached ();
    break;
  }

  if (outbuf) {
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

  if (outbuf) {
    GST_LOG_OBJECT (marudec,
        "Decoded data, now pushing buffer %p with offset %" G_GINT64_FORMAT
        ", timestamp %" GST_TIME_FORMAT " and duration %" GST_TIME_FORMAT,
        outbuf, GST_BUFFER_OFFSET (outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

    if (marudec->discont) {
      /* GST_BUFFER_FLAG_DISCONT :
       * the buffer marks a data discontinuity in the stream. This typically
       * occurs after a seek or a dropped buffer from a live or network source.
       */
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
      marudec->discont = FALSE;
    }

    if (marudec->segment.rate > 0.0) {
      // push forward
      *ret = gst_pad_push (marudec->srcpad, outbuf);
    } else {
      // push reverse
      GST_DEBUG_OBJECT (marudec, "queued frame");
      marudec->queued = g_list_prepend (marudec->queued, outbuf);
      *ret = GST_FLOW_OK;
    }
  } else {
    GST_DEBUG_OBJECT (marudec, "Didn't get a decoded buffer");
  }

  return len;
}

static GstFlowReturn
gst_marudec_chain (GstPad *pad, GstBuffer *buffer)
{
  GstMaruDec *marudec;
  GstMaruDecClass *oclass;
  guint8 *in_buf;
  gint in_size, have_data;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime in_timestamp;
  GstClockTime in_duration;
  gboolean discont;
  gint64 in_offset;
  const GstTSInfo *in_info;
  const GstTSInfo *dec_info;

  marudec = (GstMaruDec *) (GST_PAD_PARENT (pad));

  if (G_UNLIKELY (!marudec->opened)) {
    // not_negotiated
    oclass = (GstMaruDecClass *) (G_OBJECT_GET_CLASS (marudec));
    GST_ELEMENT_ERROR (marudec, CORE, NEGOTIATION, (NULL),
      ("maru_%sdec: input format was not set before data start",
        oclass->codec->name));
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  discont = GST_BUFFER_IS_DISCONT (buffer);
  if (G_UNLIKELY (discont)) {
    GST_DEBUG_OBJECT (marudec, "received DISCONT");
    gst_marudec_drain (marudec);
    interface->flush_buffers (marudec->context, marudec->dev);
    marudec->discont = TRUE;
    gst_marudec_reset_ts (marudec);
  }

  oclass = (GstMaruDecClass *) (G_OBJECT_GET_CLASS (marudec));

  in_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  in_duration = GST_BUFFER_DURATION (buffer);
  in_offset = GST_BUFFER_OFFSET (buffer);

  in_info = gst_ts_info_store (marudec, in_timestamp, in_duration, in_offset);
  GST_LOG_OBJECT (marudec,
    "Received new data of size %u, offset: %" G_GUINT64_FORMAT ", ts:%"
    GST_TIME_FORMAT ", dur: %" GST_TIME_FORMAT ", info %d",
    GST_BUFFER_SIZE (buffer), GST_BUFFER_OFFSET (buffer),
    GST_TIME_ARGS (in_timestamp), GST_TIME_ARGS (in_duration), in_info->idx);

  in_buf = GST_BUFFER_DATA (buffer);
  in_size = GST_BUFFER_SIZE (buffer);

  dec_info = in_info;

  gst_marudec_frame (marudec, in_buf, in_size, &have_data, dec_info, in_offset, &ret);

  gst_buffer_unref (buffer);

  return ret;
}

static GstStateChangeReturn
gst_marudec_change_state (GstElement *element, GstStateChange transition)
{
  GstMaruDec *marudec = (GstMaruDec *) element;
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    GST_OBJECT_LOCK (marudec);
    gst_marudec_close (marudec);
    GST_OBJECT_UNLOCK (marudec);

    /* clear queue */
    clear_queued (marudec);
    break;
  default:
    break;
  }

  return ret;
}

gboolean
gst_marudec_register (GstPlugin *plugin, GList *element)
{
  GTypeInfo typeinfo = {
      sizeof (GstMaruDecClass),
      (GBaseInitFunc) gst_marudec_base_init,
      NULL,
      (GClassInitFunc) gst_marudec_class_init,
      NULL,
      NULL,
      sizeof (GstMaruDec),
      0,
      (GInstanceInitFunc) gst_marudec_init,
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

    if (codec->codec_type != CODEC_TYPE_DECODE) {
      continue;
    }

    type_name = g_strdup_printf ("maru_%sdec", codec->name);
    type = g_type_from_name (type_name);
    if (!type) {
      type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
      g_type_set_qdata (type, GST_MARUDEC_PARAMS_QDATA, (gpointer) codec);
    }

    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_free (type_name);
      return FALSE;
    }
    g_free (type_name);
  } while ((elem = elem->next));

  return TRUE;
}
