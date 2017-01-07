/* Copyright 2017 Ben Saylor <brsaylor@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:element-rubberband
 *
 * Provides audio time-stretching and pitch-shifting using Rubber Band Library
 * (http://www.breakfastquay.com/rubberband/).
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 ---gst-debug=rubberband:5 filesrc location=test.ogg ! decodebin ! rubberband ! autoaudiosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <rubberband/rubberband-c.h>

#include "gstrubberband.h"

#define INITIAL_MAX_PROCESS_SIZE 4096

GST_DEBUG_CATEGORY_STATIC (gst_rubber_band_debug);
#define GST_CAT_DEFAULT gst_rubber_band_debug

/* Filter signals and args */
enum
{
    /* FILL ME */
    LAST_SIGNAL
};

enum
{
    PROP_0,
    PROP_SILENT
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (
            "audio/x-raw, "
            "format = (string) " GST_AUDIO_NE (F32) ", "
            "channels = (int) [ 1, MAX ], "
            "rate = (int) [ 8000, MAX ]"
            )
        );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (
            "audio/x-raw, "
            "format = (string) " GST_AUDIO_NE (F32) ", "
            "channels = (int) [ 1, MAX ], "
            "rate = (int) [ 8000, MAX ]"
            )
        );

#define gst_rubber_band_parent_class parent_class
G_DEFINE_TYPE (GstRubberBand, gst_rubber_band, GST_TYPE_ELEMENT);

static void gst_rubber_band_set_property (GObject * object, guint prop_id,
        const GValue * value, GParamSpec * pspec);
static void gst_rubber_band_get_property (GObject * object, guint prop_id,
        GValue * value, GParamSpec * pspec);

static gboolean gst_rubber_band_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_rubber_band_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);

static gboolean gst_rubber_band_activate(GstPad* pad, GstObject* parent);
static gboolean gst_rubber_band_activate_mode(
        GstPad* pad, GstObject* parent, GstPadMode mode, gboolean active);
static void gst_rubber_band_loop(GstRubberBand * filter);


/* GObject vmethod implementations */

/* initialize the rubberband's class */
static void gst_rubber_band_class_init (GstRubberBandClass * klass)
{
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;

    gobject_class = (GObjectClass *) klass;
    gstelement_class = (GstElementClass *) klass;

    gobject_class->set_property = gst_rubber_band_set_property;
    gobject_class->get_property = gst_rubber_band_get_property;

    g_object_class_install_property (gobject_class, PROP_SILENT,
            g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
                FALSE, G_PARAM_READWRITE));

    gst_element_class_set_static_metadata (gstelement_class,
            "Rubber Band time stretcher",
            "Filter/Audio",
            "Provides time stretching and pitch shifting using Rubber Band Library",
            "Ben Saylor <brsaylor@gmail.com>");

    gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&src_factory));
    gst_element_class_add_pad_template (gstelement_class,
            gst_static_pad_template_get (&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void gst_rubber_band_init(GstRubberBand* filter)
{
    filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
    gst_pad_set_event_function (filter->sinkpad,
            GST_DEBUG_FUNCPTR(gst_rubber_band_sink_event));
    gst_pad_set_chain_function (filter->sinkpad,
            GST_DEBUG_FUNCPTR(gst_rubber_band_chain));
    GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
    gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

    filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
    GST_PAD_SET_PROXY_CAPS (filter->srcpad);
    gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

    gst_pad_set_activate_function (filter->sinkpad, gst_rubber_band_activate);
    gst_pad_set_activatemode_function (filter->sinkpad,
            gst_rubber_band_activate_mode);

    filter->rb_state = NULL;
    
    // Initialize Rubber Band input and output buffers

    filter->rb_inbuf.data = NULL;
    filter->rb_inbuf.channel_data = NULL;
    filter->rb_inbuf.channels = 0;
    filter->rb_inbuf.frames_buffered = 0;
    filter->rb_inbuf.frame_capacity= 0;

    filter->rb_outbuf.data = NULL;
    filter->rb_outbuf.channel_data = NULL;
    filter->rb_inbuf.channels = 0;
    filter->rb_outbuf.frames_buffered = 0;
    filter->rb_outbuf.frame_capacity = 0;

    filter->rate = 1.0;
    //filter->channels = 0;

    // FIXME: call rubberband_delete(filter->rubberband_state) somewhere

    filter->silent = FALSE;
}

static void gst_rubber_band_set_property (GObject * object, guint prop_id,
        const GValue * value, GParamSpec * pspec)
{
    GstRubberBand *filter = GST_RUBBERBAND (object);

    switch (prop_id) {
        case PROP_SILENT:
            filter->silent = g_value_get_boolean (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void gst_rubber_band_get_property (GObject * object, guint prop_id,
        GValue * value, GParamSpec * pspec)
{
    GstRubberBand *filter = GST_RUBBERBAND (object);

    switch (prop_id) {
        case PROP_SILENT:
            g_value_set_boolean (value, filter->silent);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean gst_rubber_band_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
    GstRubberBand *filter;
    gboolean ret;

    filter = GST_RUBBERBAND (parent);

    GST_LOG_OBJECT (filter, "Received %s event: %" GST_PTR_FORMAT,
            GST_EVENT_TYPE_NAME (event), event);

    switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_CAPS:
            {
                GstCaps * caps;

                gst_event_parse_caps (event, &caps);

                // Get GstAudioInfo from the caps
                if (!gst_audio_info_from_caps(&filter->info, caps))
                    return FALSE;

                if (filter->info.layout == GST_AUDIO_LAYOUT_INTERLEAVED) {
                    GST_DEBUG_OBJECT(filter, "layout is interleaved");
                } else if (filter->info.layout == GST_AUDIO_LAYOUT_NON_INTERLEAVED) {
                    GST_DEBUG_OBJECT(filter, "layout is non-interleaved");
                    // FIXME: Handle this
                }

                GST_DEBUG_OBJECT(filter, "Creating RubberBandState with rate = %d, channels = %d",
                        filter->info.rate, filter->info.channels);
                if (filter->rb_state != NULL) {
                    rubberband_delete(filter->rb_state);
                }
                filter->rb_state = rubberband_new(
                        filter->info.rate, filter->info.channels,
                        RubberBandOptionProcessRealTime | RubberBandOptionThreadingNever,
                        1.0, 1.0); 
                rubberband_set_time_ratio(filter->rb_state, 2.5);

                /* and forward */
                ret = gst_pad_event_default (pad, parent, event);
                break;
            }
        default:
            ret = gst_pad_event_default (pad, parent, event);
            break;
    }
    return ret;
}

static void gst_rubber_band_buffer_allocate(GstRubberBandBuffer *buf, int frames, int channels) {
    if (buf->data != NULL) {
        free(buf->data);
    }
    if (buf->channel_data != NULL) {
        free(buf->channel_data);
    }
    buf->data = (float *) malloc(frames * channels * sizeof(float));
    buf->channel_data = (float **) malloc(channels * sizeof(float*));
    buf->channels = channels;
    buf->frame_capacity = frames;
    buf->frames_buffered = 0;

    for (int c = 0; c < channels; c++) {
        buf->channel_data[c] = buf->data + c * buf->frame_capacity;
    }
}

// Deinterleave input_frames into buf
static void gst_rubber_band_buffer_append(GstRubberBandBuffer *buf, float *new_frames, int frame_count) {
    if (frame_count > buf->frame_capacity - buf->frames_buffered) {
        GST_DEBUG("*** Warning: tried to append too many frames");
    }

    int f1;  // Current frame in new_frames
    int f2;  // Current frame in buf
    for (f1 = 0, f2 = buf->frames_buffered;
            f1 < frame_count && f2 < buf->frame_capacity;
            f1++, f2++) {
        for (int c = 0; c < buf->channels; c++) {
            buf->channel_data[c][f2] = new_frames[f1 * buf->channels + c];
        }
    }
    buf->frames_buffered = f2;
}

// Interleave buf into output
static void gst_rubber_band_buffer_fetch(GstRubberBandBuffer *buf, float *output, int frame_count) {
    for (int f = 0; f < frame_count; f++) {
        for (int c = 0; c < buf->channels; c++) {
            output[f * buf->channels + c] = buf->channel_data[c][f];
        }
    }
}

/* chain function
 * this function does the actual processing
 */

static int chain_call_count = 0;

static GstFlowReturn gst_rubber_band_chain(GstPad * pad, GstObject * parent, GstBuffer * inbuf)
{

    chain_call_count++;

    GstRubberBand *filter;
    filter = GST_RUBBERBAND (parent); 

    GstMapInfo inbuf_info, outbuf_info;

    gst_buffer_map(inbuf, &inbuf_info, GST_MAP_READ);
    gint channels = GST_AUDIO_INFO_CHANNELS(&filter->info);
    int input_frame_count = inbuf_info.size / channels / sizeof(float);

    // Allocate RB input buffer and set Rubber Band's max process size
    int required_rb_inbuf_size;
    if (input_frame_count < INITIAL_MAX_PROCESS_SIZE) {
        required_rb_inbuf_size = INITIAL_MAX_PROCESS_SIZE;
    } else {
        required_rb_inbuf_size = input_frame_count;
    }
    if (filter->rb_inbuf.frame_capacity < required_rb_inbuf_size) {
        GST_DEBUG_OBJECT(filter, "(re)-allocating RB input buffer (%d frames)", required_rb_inbuf_size);
        gst_rubber_band_buffer_allocate(&(filter->rb_inbuf), required_rb_inbuf_size, channels);
        rubberband_set_max_process_size(filter->rb_state, required_rb_inbuf_size);
    }

    // Fill RB input buffer
    gst_rubber_band_buffer_append(&(filter->rb_inbuf), (float *) inbuf_info.data, input_frame_count);

    // Release the input buffer
    gst_buffer_unmap(inbuf, &inbuf_info);
    gst_buffer_unref(inbuf);

    // Process the input
    rubberband_process(
            filter->rb_state,
            (const float **) filter->rb_inbuf.channel_data,
            filter->rb_inbuf.frames_buffered,
            FALSE);  // FIXME: Set to TRUE if this is the last buffer
    
    // Clear the RB input buffer
    filter->rb_inbuf.frames_buffered = 0;

    // If no output frames are available yet, return without pushing
    if (rubberband_available(filter->rb_state) == 0) {
        return GST_FLOW_OK;
    }

    // Otherwise, push available output frames
    int output_frame_count;
    int retrieve_loop_count = 0;
    GstFlowReturn flow_return = GST_FLOW_OK;
    while ((output_frame_count = rubberband_available(filter->rb_state)) > 0) {
        retrieve_loop_count++;
        if (retrieve_loop_count > 1) {
            GST_DEBUG_OBJECT(filter, "retrieve_loop_count = %d", retrieve_loop_count);
        }

        // Reallocate RB output buffer if it's not big enough
        if (filter->rb_outbuf.frame_capacity < output_frame_count) {
            GST_DEBUG_OBJECT(filter, "(re)-allocating output buffer (%d frames)", output_frame_count);
            gst_rubber_band_buffer_allocate(&(filter->rb_outbuf), output_frame_count, channels);
        }

        // Retrieve output frames from Rubber Band
        int frames_retrieved = rubberband_retrieve(
                filter->rb_state,
                filter->rb_outbuf.channel_data,
                output_frame_count);
        //GST_DEBUG_OBJECT(filter, "frames_retrieved = %d", frames_retrieved);

        // Make an output buffer and copy the RB output frames into it
        // FIXME: Possible to avoid allocating a new buffer each time?
        GstBuffer *outbuf = gst_buffer_new_allocate(NULL,
                output_frame_count * channels * sizeof(float),
                NULL);
        gst_buffer_map(outbuf, &outbuf_info, GST_MAP_WRITE);
        gst_rubber_band_buffer_fetch(&(filter->rb_outbuf),
                (float *) outbuf_info.data,
                output_frame_count);
        gst_buffer_unmap(outbuf, &outbuf_info);

        // Push the output buffer
        flow_return = gst_pad_push(filter->srcpad, outbuf);
        if (flow_return != GST_FLOW_OK) {
            GST_DEBUG_OBJECT(filter, "****** Error pushing");
            break;
        }
    }

    return flow_return;
}

// Copied from https://gstreamer.freedesktop.org/documentation/plugin-development/advanced/scheduling.html
static gboolean gst_rubber_band_activate (GstPad * pad, GstObject * parent)
{
    GstQuery *query;
    gboolean pull_mode;

    // FIXME: Try to get pull mode working
    goto activate_push;

    /* first check what upstream scheduling is supported */
    query = gst_query_new_scheduling ();

    if (!gst_pad_peer_query (pad, query)) {
        gst_query_unref (query);
        goto activate_push;
    }

    /* see if pull-mode is supported */
    pull_mode = gst_query_has_scheduling_mode_with_flags (query,
            GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE);
    gst_query_unref (query);

    if (!pull_mode)
        goto activate_push;

    /* now we can activate in pull-mode. GStreamer will also
     * activate the upstream peer in pull-mode */
    return gst_pad_activate_mode (pad, GST_PAD_MODE_PULL, TRUE);

activate_push:
    {
        /* something not right, we fallback to push-mode */
        GST_DEBUG_OBJECT(parent, "*** falling back to push mode");
        return gst_pad_activate_mode (pad, GST_PAD_MODE_PUSH, TRUE);
    }
}

// Copied from https://gstreamer.freedesktop.org/documentation/plugin-development/advanced/scheduling.html
static gboolean gst_rubber_band_activate_mode (GstPad    * pad,
        GstObject * parent,
        GstPadMode  mode,
        gboolean    active)
{
    gboolean res;
    GstRubberBand *filter = GST_RUBBERBAND (parent);

    switch (mode) {
        case GST_PAD_MODE_PUSH:
            res = TRUE;
            break;
        case GST_PAD_MODE_PULL:
            if (active) {
                filter->offset = 0;
                res = gst_pad_start_task (pad,
                        (GstTaskFunction) gst_rubber_band_loop, filter, NULL);
            } else {
                res = gst_pad_stop_task (pad);
            }
            break;
        default:
            /* unknown scheduling mode */
            res = FALSE;
            break;
    }
    return res;
}

static void gst_rubber_band_loop (GstRubberBand * filter)
{
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean rubberband_init (GstPlugin * rubberband)
{
    /* debug category for fltering log messages */
    GST_DEBUG_CATEGORY_INIT (gst_rubber_band_debug, "rubberband",
            0, "Rubber Band");

    return gst_element_register(rubberband, "rubberband", GST_RANK_NONE,
            GST_TYPE_RUBBERBAND);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "gst-rubberband"
#endif

/* gstreamer looks for this structure to register rubberbands */
GST_PLUGIN_DEFINE (
        GST_VERSION_MAJOR,
        GST_VERSION_MINOR,
        rubberband,
        "Rubber Band",
        rubberband_init,
        VERSION,
        "LGPL",
        "GStreamer",
        "http://gstreamer.net/"
        )
