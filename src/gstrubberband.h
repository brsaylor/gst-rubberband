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

#ifndef __GST_RUBBERBAND_H__
#define __GST_RUBBERBAND_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <rubberband/rubberband-c.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_RUBBERBAND \
    (gst_rubber_band_get_type())
#define GST_RUBBERBAND(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RUBBERBAND,GstRubberBand))
#define GST_RUBBERBAND_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RUBBERBAND,GstRubberBandClass))
#define GST_IS_RUBBERBAND(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RUBBERBAND))
#define GST_IS_RUBBERBAND_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RUBBERBAND))

typedef struct _GstRubberBand      GstRubberBand;
typedef struct _GstRubberBandClass GstRubberBandClass;

typedef struct _GstRubberBandBuffer {
    float *data;           // deinterleaved sample frames
    float **channel_data;  // pointers to individual channels in *data
    int channels;          // number of channels
    int frames_buffered;   // number of sample frames stored in buffer
    int frame_capacity;    // buffer capacity in sample frames
} GstRubberBandBuffer;

struct _GstRubberBand
{
    GstElement element;

    GstPad *sinkpad, *srcpad;

    RubberBandState rb_state;

    GstRubberBandBuffer rb_inbuf, rb_outbuf;

    // Rubber Band stretcher properties
    double time_ratio;
    double pitch_scale;
    
    gint offset;
    GstAudioInfo info;

    gboolean silent;
};

struct _GstRubberBandClass 
{
    GstElementClass parent_class;
};

GType gst_rubber_band_get_type (void);

G_END_DECLS

#endif /* __GST_RUBBERBAND_H__ */
