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

typedef struct _GstMaruVidDecClass
{
  GstVideoDecoderClass parent_class;

  CodecElement *codec;
} GstMaruVidDecClass;

typedef struct _GstMaruDecClass
{
  GstElementClass parent_class;

  CodecElement *codec;
  GstPadTemplate *sinktempl;
  GstPadTemplate *srctempl;
} GstMaruDecClass;


static GstElementClass *parent_class = NULL;

static void gst_maruviddec_base_init (GstMaruVidDecClass *klass);
static void gst_maruviddec_class_init (GstMaruVidDecClass *klass);
static void gst_maruviddec_init (GstMaruVidDec *marudec);
static void gst_maruviddec_finalize (GObject *object);

static gboolean gst_marudec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state);
static GstFlowReturn gst_maruviddec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame);
static gboolean gst_marudec_negotiate (GstMaruVidDec *dec, gboolean force);
static gint gst_maruviddec_frame (GstMaruVidDec *marudec, guint8 *data, guint size, gint *got_data,
                  const GstTSInfo *dec_info, gint64 in_offset, GstVideoCodecFrame * frame, GstFlowReturn *ret);

static gboolean gst_marudec_open (GstMaruVidDec *marudec);
static gboolean gst_marudec_close (GstMaruVidDec *marudec);

GstFlowReturn alloc_and_copy (GstMaruVidDec *marudec, guint64 offset, guint size,
                  GstCaps *caps, GstBuffer **buf);

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
  GST_DEBUG (" >> ENTER ");
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
  GST_DEBUG (" >> ENTER ");
  if (!profile_init) {
    profile_init = 1;
    profile_decode_timer = g_timer_new();
  }
}

static void reset_codec_profile(void)
{
  GST_DEBUG (" >> ENTER ");
  g_mutex_lock (&profile_mutex);
  decoded_frame_cnt = -1;
  g_mutex_lock (&profile_mutex);
}

static void begin_video_decode_profile(void)
{
  GST_DEBUG (" >> ENTER ");
  g_timer_start(profile_decode_timer);
}

static void end_video_decode_profile(void)
{
  GST_DEBUG (" >> ENTER ");
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
gst_ts_info_store (GstMaruVidDec *dec, GstClockTime timestamp,
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

static const GstTSInfo *
gst_ts_info_get (GstMaruVidDec *dec, gint idx)
{
  GST_DEBUG (" >> ENTER ");
  if (G_UNLIKELY (idx < 0 || idx > MAX_TS_MASK)){
  GST_DEBUG (" >> LEAVE 0");
    return GST_TS_INFO_NONE;
  }

  GST_DEBUG (" >> LEAVE ");
  return &dec->ts_info[idx];
}

static void
gst_marudec_reset_ts (GstMaruVidDec *marudec)
{
  GST_DEBUG (" >> ENTER ");
  marudec->next_out = GST_CLOCK_TIME_NONE;
}

static void
gst_marudec_do_qos (GstMaruVidDec *marudec, GstVideoCodecFrame * frame,
    GstClockTime timestamp, gboolean *mode_switch)
{
  GST_DEBUG (" >> ENTER ");
  GstClockTimeDiff diff;

  *mode_switch = FALSE;

  if (G_UNLIKELY (frame == NULL)) {
    marudec->processed++;
    GST_DEBUG (" >> LEAVE ");
    return ;
  }

  diff = gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (marudec), frame);
  /* if we don't have timing info, then we don't do QoS */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (diff))) {
    marudec->processed++;
    GST_DEBUG (" >> LEAVE ");
    return ;
  }

  if (diff > 0) {
    marudec->processed++;
    GST_DEBUG (" >> LEAVE ");
  } else if (diff <= 0) {
    GST_DEBUG (" >> LEAVE ");
  }
}

static void
gst_marudec_drain (GstMaruVidDec *marudec)
{
  GST_DEBUG (" >> ENTER ");
  GST_DEBUG_OBJECT (marudec, "drain frame");

  {
    gint have_data, len, try = 0;

    do {
      GstFlowReturn ret;

      len =
        gst_maruviddec_frame (marudec, NULL, 0, &have_data, &ts_info_none, 0, NULL, &ret);

      if (len < 0 || have_data == 0) {
        break;
      }
    } while (try++ < 10);
  }
}

/*
 * Implementation
 */
static void
gst_maruviddec_base_init (GstMaruVidDecClass *klass)
{
  GST_DEBUG (" >> ENTER ");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *sinktempl, *srctempl;
  GstCaps *sinkcaps, *srccaps;
  CodecElement *codec;
  gchar *longname, *description;

  codec =
      (CodecElement *)g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
                                      GST_MARUDEC_PARAMS_QDATA);
  g_assert (codec != NULL);

  longname = g_strdup_printf ("%s Decoder long", codec->longname);
  description = g_strdup_printf("%s Decoder desc", codec->name);

  gst_element_class_set_metadata (element_class,
            longname,
            "Codec/Decoder/Video sims",
            description,
            "Sooyoung Ha <yoosah.ha@samsung.com>");

  g_free (longname);
  g_free (description);

  /* get the caps */
  sinkcaps = gst_maru_codecname_to_caps (codec->name, NULL, FALSE);
  if (!sinkcaps) {
    sinkcaps = gst_caps_new_empty_simple ("unknown/unknown");
  }

  srccaps = gst_maru_codectype_to_video_caps (NULL, codec->name, FALSE, codec);

  if (!srccaps) {
    srccaps = gst_caps_from_string ("video/x-raw");
  }

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
                GST_PAD_ALWAYS, sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
                GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->codec = codec;
}

static void
gst_maruviddec_class_init (GstMaruVidDecClass *klass)
{
  GST_DEBUG (" >> ENTER ");
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoDecoderClass *viddec_class = GST_VIDEO_DECODER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_maruviddec_finalize;

  /* use these function when defines new properties.
  gobject_class->set_property = gst_marudec_set_property
  gobject_class->get_property = gst_marudec_get_property
  */

  viddec_class->set_format = gst_marudec_set_format;
  viddec_class->handle_frame = gst_maruviddec_handle_frame;
}

static void
gst_maruviddec_init (GstMaruVidDec *marudec)
{
  GST_DEBUG (" >> ENTER ");
  marudec->context = g_malloc0 (sizeof(CodecContext));
  marudec->context->video.pix_fmt = PIX_FMT_NONE;
  marudec->context->audio.sample_fmt = SAMPLE_FMT_NONE;

  marudec->opened = FALSE;
}

static void
gst_maruviddec_finalize (GObject *object)
{
  GST_DEBUG (" >> ENTER ");
  GstMaruVidDec *marudec = (GstMaruVidDec *) object;

  GST_DEBUG_OBJECT (marudec, "finalize object and release context");
  g_free (marudec->context);
  marudec->context = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_marudec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GST_DEBUG (" >> ENTER ");
  GstMaruVidDec *marudec;
  GstMaruVidDecClass *oclass;
  gboolean ret = FALSE;

  marudec = (GstMaruVidDec *) decoder;
  oclass = (GstMaruVidDecClass *) (G_OBJECT_GET_CLASS (marudec));
  if (!marudec) {
      GST_ERROR ("invalid marudec");
    return FALSE;
  }
  if (marudec->last_caps != NULL &&
      gst_caps_is_equal (marudec->last_caps, state->caps)) {
    return TRUE;
  }

  GST_DEBUG_OBJECT (marudec, "setcaps called.");

  GST_OBJECT_LOCK (marudec);

  /* stupid check for VC1 */
  if (!strcmp(oclass->codec->name, "wmv3") ||
      !strcmp(oclass->codec->name, "vc1")) {
    gst_maru_caps_to_codecname (state->caps, oclass->codec->name, NULL);
  }

  /* close old session */
  if (marudec->opened) {
    GST_OBJECT_UNLOCK (marudec);
    gst_marudec_drain (marudec);
    GST_OBJECT_LOCK (marudec);
    if (!gst_marudec_close (marudec)) {
      GST_OBJECT_UNLOCK (marudec);
      return FALSE;
    }
  }

  gst_caps_replace (&marudec->last_caps, state->caps);

  GST_LOG_OBJECT (marudec, "size %dx%d", marudec->context->video.width,
      marudec->context->video.height);

  gst_maru_caps_with_codecname (oclass->codec->name, oclass->codec->media_type,
                                state->caps, marudec->context);

  GST_LOG_OBJECT (marudec, "size after %dx%d", marudec->context->video.width,
      marudec->context->video.height);

  if (!marudec->context->video.fps_d || !marudec->context->video.fps_n) {
    GST_DEBUG_OBJECT (marudec, "forcing 25/1 framerate");
    marudec->context->video.fps_n = 1;
    marudec->context->video.fps_d = 25;
  }

  GstQuery *query;
  gboolean is_live;

  query = gst_query_new_latency ();
  is_live = FALSE;
  /* Check if upstream is live. If it isn't we can enable frame based
   * threading, which is adding latency */
  if (gst_pad_peer_query (GST_VIDEO_DECODER_SINK_PAD (marudec), query)) {
    gst_query_parse_latency (query, &is_live, NULL, NULL);
  }
  gst_query_unref (query);

  if (!gst_marudec_open (marudec)) {
    GST_DEBUG_OBJECT (marudec, "Failed to open");

    ret = FALSE;
  } else {
    ret = TRUE;
  }
  if (marudec->input_state) {
    gst_video_codec_state_unref (marudec->input_state);
  }
  marudec->input_state = gst_video_codec_state_ref (state);

  if (marudec->input_state->info.fps_n) {
    GstVideoInfo *info = &marudec->input_state->info;
    gst_util_uint64_scale_ceil (GST_SECOND, info->fps_d, info->fps_n);
  }
  GST_OBJECT_UNLOCK (marudec);

  return ret;
}

static gboolean
gst_marudec_open (GstMaruVidDec *marudec)
{
  GST_DEBUG (" >> ENTER ");
  GstMaruVidDecClass *oclass;

  oclass = (GstMaruVidDecClass *) (G_OBJECT_GET_CLASS (marudec));

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

  gst_marudec_reset_ts (marudec);

  // initialize profile resource
  INIT_CODEC_PROFILE(marudec->dev->fd);

  return TRUE;
}

static gboolean
gst_marudec_close (GstMaruVidDec *marudec)
{
  GST_DEBUG (" >> ENTER ");
  if (!marudec->opened) {
    GST_DEBUG_OBJECT (marudec, "not opened yet");
    return FALSE;
  }

  if (marudec->context) {
    g_free(marudec->context->codecdata);
    marudec->context->codecdata = NULL;
  }

  if (!marudec->dev) {
    return FALSE;
  }

  gst_maru_avcodec_close (marudec->context, marudec->dev);
  marudec->opened = FALSE;

  if (marudec->dev) {
    g_free(marudec->dev);
    marudec->dev = NULL;
  }

  // reset profile resource
  RESET_CODEC_PROFILE();
  return TRUE;
}

static gboolean
update_video_context (GstMaruVidDec * marudec, CodecContext * context,
    gboolean force)
{
  GST_DEBUG (" >> ENTER ");
  if (!force && marudec->ctx_width == context->video.width
      && marudec->ctx_height == context->video.height
      && marudec->ctx_ticks == context->video.ticks_per_frame
      && marudec->ctx_time_n == context->video.fps_n
      && marudec->ctx_time_d == context->video.fps_d
      && marudec->ctx_pix_fmt == context->video.pix_fmt
      && marudec->ctx_par_n == context->video.par_n
      && marudec->ctx_par_d == context->video.par_d) {
    return FALSE;
  }
  marudec->ctx_width = context->video.width;
  marudec->ctx_height = context->video.height;
  marudec->ctx_ticks = context->video.ticks_per_frame;
  marudec->ctx_time_n = context->video.fps_n;
  marudec->ctx_time_d = context->video.fps_d;
  marudec->ctx_pix_fmt = context->video.pix_fmt;
  marudec->ctx_par_n = context->video.par_n;
  marudec->ctx_par_d = context->video.par_d;

  return TRUE;
}

static void
gst_maruviddec_update_par (GstMaruVidDec * marudec,
    GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  GST_DEBUG (" >> ENTER");
  gboolean demuxer_par_set = FALSE;
  gboolean decoder_par_set = FALSE;
  gint demuxer_num = 1, demuxer_denom = 1;
  gint decoder_num = 1, decoder_denom = 1;

  if (in_info->par_n && in_info->par_d) {
    demuxer_num = in_info->par_n;
    demuxer_denom = in_info->par_d;
    demuxer_par_set = TRUE;
    GST_DEBUG_OBJECT (marudec, "Demuxer PAR: %d:%d", demuxer_num,
        demuxer_denom);
  }

  if (marudec->ctx_par_n && marudec->ctx_par_d) {
    decoder_num = marudec->ctx_par_n;
    decoder_denom = marudec->ctx_par_d;
    decoder_par_set = TRUE;
    GST_DEBUG_OBJECT (marudec, "Decoder PAR: %d:%d", decoder_num,
        decoder_denom);
  }

  if (!demuxer_par_set && !decoder_par_set)
    goto no_par;

  if (demuxer_par_set && !decoder_par_set)
    goto use_demuxer_par;

  if (decoder_par_set && !demuxer_par_set)
    goto use_decoder_par;

  /* Both the demuxer and the decoder provide a PAR. If one of
   * the two PARs is 1:1 and the other one is not, use the one
   * that is not 1:1. */
  if (demuxer_num == demuxer_denom && decoder_num != decoder_denom)
    goto use_decoder_par;

  if (decoder_num == decoder_denom && demuxer_num != demuxer_denom)
    goto use_demuxer_par;

  /* Both PARs are non-1:1, so use the PAR provided by the demuxer */
  goto use_demuxer_par;

use_decoder_par:
  {
    GST_DEBUG_OBJECT (marudec,
        "Setting decoder provided pixel-aspect-ratio of %u:%u", decoder_num,
        decoder_denom);
    out_info->par_n = decoder_num;
    out_info->par_d = decoder_denom;
    return;
  }
use_demuxer_par:
  {
    GST_DEBUG_OBJECT (marudec,
        "Setting demuxer provided pixel-aspect-ratio of %u:%u", demuxer_num,
        demuxer_denom);
    out_info->par_n = demuxer_num;
    out_info->par_d = demuxer_denom;
    return;
  }
no_par:
  {
    GST_DEBUG_OBJECT (marudec,
        "Neither demuxer nor codec provide a pixel-aspect-ratio");
    out_info->par_n = 1;
    out_info->par_d = 1;
    return;
  }
}


static gboolean
gst_marudec_negotiate (GstMaruVidDec *marudec, gboolean force)
{
  GST_DEBUG (" >> ENTER ");
  CodecContext *context = marudec->context;
  GstVideoFormat fmt;
  GstVideoInfo *in_info, *out_info;
  GstVideoCodecState *output_state;
  gint fps_n, fps_d;

  if (!update_video_context (marudec, context, force))
    return TRUE;

  fmt = gst_maru_pixfmt_to_videoformat (marudec->ctx_pix_fmt);
  if (G_UNLIKELY (fmt == GST_VIDEO_FORMAT_UNKNOWN))
    goto unknown_format;

  output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (marudec), fmt,
      marudec->ctx_width, marudec->ctx_height, marudec->input_state);
  if (marudec->output_state)
    gst_video_codec_state_unref (marudec->output_state);
  marudec->output_state = output_state;

  in_info = &marudec->input_state->info;
  out_info = &marudec->output_state->info;
  out_info->interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;

  /* try to find a good framerate */
  if ((in_info->fps_d && in_info->fps_n) ||
      GST_VIDEO_INFO_FLAG_IS_SET (in_info, GST_VIDEO_FLAG_VARIABLE_FPS)) {
    /* take framerate from input when it was specified (#313970) */
    fps_n = in_info->fps_n;
    fps_d = in_info->fps_d;
  } else {
    fps_n = marudec->ctx_time_d / marudec->ctx_ticks;
    fps_d = marudec->ctx_time_n;

    if (!fps_d) {
      GST_LOG_OBJECT (marudec, "invalid framerate: %d/0, -> %d/1", fps_n,
          fps_n);
      fps_d = 1;
    }
    if (gst_util_fraction_compare (fps_n, fps_d, 1000, 1) > 0) {
      GST_LOG_OBJECT (marudec, "excessive framerate: %d/%d, -> 0/1", fps_n,
          fps_d);
      fps_n = 0;
      fps_d = 1;
    }
  }
  GST_LOG_OBJECT (marudec, "setting framerate: %d/%d", fps_n, fps_d);
  out_info->fps_n = fps_n;
  out_info->fps_d = fps_d;

  /* calculate and update par now */
  gst_maruviddec_update_par (marudec, in_info, out_info);

  if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (marudec)))
    goto negotiate_failed;

  return TRUE;

  /* ERRORS */
unknown_format:
  {
    GST_ERROR_OBJECT (marudec,
        "decoder requires a video format unsupported by GStreamer");
    return FALSE;
  }
negotiate_failed:
  {
    /* Reset so we try again next time even if force==FALSE */
    marudec->ctx_width = 0;
    marudec->ctx_height = 0;
    marudec->ctx_ticks = 0;
    marudec->ctx_time_n = 0;
    marudec->ctx_time_d = 0;
    marudec->ctx_pix_fmt = 0;
    marudec->ctx_par_n = 0;
    marudec->ctx_par_d = 0;

    GST_ERROR_OBJECT (marudec, "negotiation failed");
    return FALSE;
  }
}

static GstFlowReturn
get_output_buffer (GstMaruVidDec *marudec, GstVideoCodecFrame * frame)
{
  GST_DEBUG (" >> ENTER ");
  gint pict_size;
  GstFlowReturn ret = GST_FLOW_OK;

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

  ret = gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER (marudec), frame);

  alloc_and_copy(marudec, 0, pict_size, NULL, &(frame->output_buffer));

  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_ERROR ("alloc output buffer failed");
  }

  return ret;
}

static gint
gst_maruviddec_video_frame (GstMaruVidDec *marudec, guint8 *data, guint size,
    const GstTSInfo *dec_info, gint64 in_offset,
    GstVideoCodecFrame * frame, GstFlowReturn *ret)
{
  GST_DEBUG (" >> ENTER ");
  gint len = -1;
  gboolean mode_switch;
  GstClockTime out_timestamp, out_duration, out_pts;
  gint64 out_offset;
  const GstTSInfo *out_info;
  int have_data;

  /* run QoS code, we don't stop decoding the frame when we are late because
   * else we might skip a reference frame */
  gst_marudec_do_qos (marudec, frame, dec_info->timestamp, &mode_switch);

  GST_DEBUG_OBJECT (marudec, "decode video: input buffer size %d", size);

  // begin video decode profile
  BEGIN_VIDEO_DECODE_PROFILE();

  len = interface->decode_video (marudec, data, size,
        dec_info->idx, in_offset, NULL, &have_data);
  if (len < 0 || !have_data) {
    GST_ERROR ("decode video failed, len = %d", len);
    return len;
  }

  // end video decode profile
  END_VIDEO_DECODE_PROFILE();

  *ret = get_output_buffer (marudec, frame);
  if (G_UNLIKELY (*ret != GST_FLOW_OK)) {
    GST_DEBUG_OBJECT (marudec, "no output buffer");
    len = -1;
    GST_DEBUG_OBJECT (marudec, "return flow %d, out %p, len %d",
      *ret, (void *) (frame->output_buffer), len);
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
  GST_BUFFER_TIMESTAMP (frame->output_buffer) = out_timestamp;

  /* Offset */
  if (out_offset != GST_BUFFER_OFFSET_NONE) {
    GST_LOG_OBJECT (marudec, "Using offset returned by ffmpeg");
  } else if (out_timestamp != GST_CLOCK_TIME_NONE) {
/* TODO: check this is needed.
    GstFormat out_fmt = GST_FORMAT_DEFAULT;
    GST_LOG_OBJECT (marudec, "Using offset converted from timestamp");

    gst_pad_query_peer_convert (marudec->sinkpad,
      GST_FORMAT_TIME, out_timestamp, &out_fmt, &out_offset);
*/
  } else if (dec_info->offset != GST_BUFFER_OFFSET_NONE) {
    GST_LOG_OBJECT (marudec, "using in_offset %" G_GINT64_FORMAT,
      dec_info->offset);
    out_offset = dec_info->offset;
  } else {
    GST_LOG_OBJECT (marudec, "no valid offset found");
    out_offset = GST_BUFFER_OFFSET_NONE;
  }
  GST_BUFFER_OFFSET (frame->output_buffer) = out_offset;

  /* Duration */
  if (GST_CLOCK_TIME_IS_VALID (out_duration)) {
    GST_LOG_OBJECT (marudec, "Using duration returned by ffmpeg");
  } else if (GST_CLOCK_TIME_IS_VALID (dec_info->duration)) {
    GST_LOG_OBJECT (marudec, "Using in_duration");
    out_duration = dec_info->duration;
  } else {
    if (marudec->ctx_time_n != -1 &&
        (marudec->ctx_time_n != 1000 &&
        marudec->ctx_time_d != 1)) {
      GST_LOG_OBJECT (marudec, "using input framerate for duration");
      out_duration = gst_util_uint64_scale_int (GST_SECOND,
        marudec->ctx_time_d, marudec->ctx_time_n);
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

  GST_DEBUG_OBJECT (marudec, "return flow %d, out %p, len %d",
    *ret, (void *) (frame->output_buffer), len);

 *ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (marudec), frame);

  return len;
}

static gint
gst_maruviddec_frame (GstMaruVidDec *marudec, guint8 *data, guint size,
    gint *got_data, const GstTSInfo *dec_info, gint64 in_offset,
    GstVideoCodecFrame * frame, GstFlowReturn *ret)
{
  GST_DEBUG (" >> ENTER ");
  GstMaruVidDecClass *oclass;
  gint have_data = 0, len = 0;

  if (G_UNLIKELY (marudec->context->codec == NULL)) {
    GST_ERROR_OBJECT (marudec, "no codec context");
    return -1;
  }
  GST_LOG_OBJECT (marudec, "data:%p, size:%d", data, size);

  *ret = GST_FLOW_OK;
  oclass = (GstMaruVidDecClass *) (G_OBJECT_GET_CLASS (marudec));

  switch (oclass->codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    len = gst_maruviddec_video_frame (marudec, data, size,
        dec_info, in_offset, frame, ret);
    break;
  default:
    GST_ERROR_OBJECT (marudec, "Asked to decode non-audio/video frame!");
    g_assert_not_reached ();
    break;
  }

  if (frame && frame->output_buffer) {
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

  return len;
}

static GstFlowReturn
gst_maruviddec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GST_DEBUG (" >> ENTER ");
  GstMaruVidDec *marudec = (GstMaruVidDec *) decoder;
  gint have_data;
  GstMapInfo mapinfo;
  GstFlowReturn ret = GST_FLOW_OK;

  guint8 *in_buf;
  gint in_size;
  GstClockTime in_timestamp;
  GstClockTime in_duration;
  gint64 in_offset;
  const GstTSInfo *in_info;
  const GstTSInfo *dec_info;

  if (!gst_buffer_map (frame->input_buffer, &mapinfo, GST_MAP_READ)) {
    GST_ERROR_OBJECT (marudec, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }

  in_timestamp = GST_BUFFER_TIMESTAMP (frame->input_buffer);
  in_duration = GST_BUFFER_DURATION (frame->input_buffer);
  in_offset = GST_BUFFER_OFFSET (frame->input_buffer);

  in_info = gst_ts_info_store (marudec, in_timestamp, in_duration, in_offset);
  GST_LOG_OBJECT (marudec,
    "Received new data of size %zu, offset: %" G_GUINT64_FORMAT ", ts:%"
    GST_TIME_FORMAT ", dur: %" GST_TIME_FORMAT ", info %d",
    mapinfo.size, GST_BUFFER_OFFSET (frame->input_buffer),
    GST_TIME_ARGS (in_timestamp), GST_TIME_ARGS (in_duration), in_info->idx);

  in_size = mapinfo.size;
  in_buf = (guint8 *) mapinfo.data;

  dec_info = in_info;

  gst_buffer_unmap (frame->input_buffer, &mapinfo);

  gst_maruviddec_frame (marudec, in_buf, in_size, &have_data, dec_info, in_offset, frame, &ret);

  return ret;
}

gboolean
gst_maruviddec_register (GstPlugin *plugin, GList *element)
{
  GTypeInfo typeinfo = {
      sizeof (GstMaruVidDecClass),
      (GBaseInitFunc) gst_maruviddec_base_init,
      NULL,
      (GClassInitFunc) gst_maruviddec_class_init,
      NULL,
      NULL,
      sizeof (GstMaruVidDec),
      0,
      (GInstanceInitFunc) gst_maruviddec_init,
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

    if (codec->codec_type != CODEC_TYPE_DECODE || codec->media_type != AVMEDIA_TYPE_VIDEO) {
      continue;
    }

    type_name = g_strdup_printf ("maru_%sdec", codec->name);
    type = g_type_from_name (type_name);
    if (!type) {
      type = g_type_register_static (GST_TYPE_VIDEO_DECODER, type_name, &typeinfo, 0);
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
