/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2015 Martin Petrov Vachovski <<user@hostname.org>>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/**
 * SECTION:element-dwtfilter
 *
 * FIXME:Describe dwtfilter here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! dwtfilter ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include <string.h>
#include <gsl/gsl_wavelet.h>
#include <gsl/gsl_wavelet2d.h>

#include "gstdwtfilter.h"

GST_DEBUG_CATEGORY_STATIC (gst_dwt_filter_debug);
#define GST_CAT_DEFAULT gst_dwt_filter_debug

/* Filter signals and args */
enum
{
	/* FILL ME */
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_SILENT,
	PROP_WAVELET
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS ("video/x-raw,format=GRAY8")
);

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS ("video/x-raw,format=GRAY8")
);

#define gst_dwt_filter_parent_class parent_class
G_DEFINE_TYPE (GstDwtFilter, gst_dwt_filter, GST_TYPE_ELEMENT);

static void gst_dwt_filter_set_property (GObject * object, guint prop_id,
		const GValue * value, GParamSpec * pspec);
static void gst_dwt_filter_get_property (GObject * object, guint prop_id,
		GValue * value, GParamSpec * pspec);

static gboolean gst_dwt_filter_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static gboolean gst_dwt_filter_src_event (GstPad * pad, GstObject * parent, GstEvent * event);

static GstFlowReturn gst_dwt_filter_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);
static gboolean gst_dwt_filter_query (GstPad *pad, GstObject *parent, GstQuery *query);

static void guint8_to_gdouble(guint8* src, gdouble *dst, gsize sz);
static void gdouble_to_guint8(gdouble* src, guint8 *dst, gsize sz);

/* GObject vmethod implementations */

/* initialize the dwtfilter's class */
static void
gst_dwt_filter_class_init (GstDwtFilterClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	gobject_class->set_property = gst_dwt_filter_set_property;
	gobject_class->get_property = gst_dwt_filter_get_property;

	g_object_class_install_property (gobject_class, PROP_SILENT,
			g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
					FALSE, G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, PROP_WAVELET,
			g_param_spec_string("wavelet", "Wavelet", "Family and order of the wavelet",
					"h2", G_PARAM_READWRITE));

	gst_element_class_set_details_simple(gstelement_class,
			"DwtFilter",
			"DWT Element",
			"The element performs a GSL-based DW transform to the input square image."
			"The element can act as low-pass or high-pass filter by removing the slower "
			"or faster spacial modes from the image. The image is then transformed back.",
			"Martin Petrov Vachovski <<user@hostname.org>>");

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
static void
gst_dwt_filter_init (GstDwtFilter * filter)
{
	filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
	gst_pad_set_event_function (filter->sinkpad,
			GST_DEBUG_FUNCPTR(gst_dwt_filter_sink_event));
	gst_pad_set_chain_function (filter->sinkpad,
			GST_DEBUG_FUNCPTR(gst_dwt_filter_chain));
	GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
	gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

	filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
	GST_PAD_SET_PROXY_CAPS (filter->srcpad);
	gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

	gst_pad_set_event_function (filter->srcpad,
		GST_DEBUG_FUNCPTR(gst_dwt_filter_src_event));

	filter->silent = FALSE;

	filter->wavelet_prop.type = GST_DWTFILTER_HAAR;
	filter->wavelet_prop.order = 2;

	filter->w = gsl_wavelet_alloc (gsl_wavelet_daubechies_centered, 4);

	gst_pad_set_query_function (filter->srcpad, gst_dwt_filter_query);

}

static void
gst_dwt_filter_set_property (GObject * object, guint prop_id,
		const GValue * value, GParamSpec * pspec)
{
	GstDwtFilter *filter = GST_DWTFILTER (object);

	switch (prop_id) {
	case PROP_SILENT:
		filter->silent = g_value_get_boolean (value);
		break;
	case PROP_WAVELET:
//		filter->wavelet_prop g_value_get_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gst_dwt_filter_get_property (GObject * object, guint prop_id,
		GValue * value, GParamSpec * pspec)
{
	GstDwtFilter *filter = GST_DWTFILTER (object);

	switch (prop_id) {
	case PROP_SILENT:
		g_value_set_boolean (value, filter->silent);
		break;
	case PROP_WAVELET:
		g_value_set_string (value, "h2");
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_dwt_filter_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
	gboolean ret;
	GstDwtFilter *filter;
	GstStructure *structure;
	gchar *format;
	int i;

	filter = GST_DWTFILTER (parent);

	switch (GST_EVENT_TYPE (event)) {
	case GST_EVENT_CAPS:
	{
		GstCaps *caps;

		gst_event_parse_caps (event, &caps);
		/* do something with the caps */

		for (i = 0; i < gst_caps_get_size (caps); i++)
		{
			structure = gst_caps_get_structure (caps, i);
			if(structure)
			{
				gst_structure_get_int(structure, "width", &filter->width);
				gst_structure_get_int(structure, "height", &filter->height);
				format = gst_structure_get_string(structure, "format");
				g_print("width = %d height = %d format = %s\n", filter->width, filter->height, format);
				//pAccum = malloc(4 * width * height * sizeof(long int));
				//memset(pAccum, 0, 4 * width * height * sizeof(long int));
				filter->pDWTBuffer = (double*) malloc(filter->width * filter->height * sizeof(double));
				memset(filter->pDWTBuffer, 0, filter->width * filter->height * sizeof(double));

				filter->work = gsl_wavelet_workspace_alloc (filter->width);
			}
			else
			{
				//				g_print("structure = 0\n");
			}
		}

		/* and forward */
		ret = gst_pad_event_default (pad, parent, event);
		break;
	}
	case GST_EVENT_QOS:
		g_print("GST_EVENT_QOS\n");

		GstQOSType type;
		gdouble proportion;
		GstClockTimeDiff diff;
		GstClockTime timestamp;

		gst_event_parse_qos (event, &type, &proportion, &diff, &timestamp);

		ret = gst_pad_event_default (pad, parent, event);
		break;
	default:
		ret = gst_pad_event_default (pad, parent, event);
		break;
	}
	return ret;
}

/* this function handles src events */
static gboolean
gst_dwt_filter_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
	gboolean ret;
	GstDwtFilter *filter;

	filter = GST_DWTFILTER (parent);

	switch (GST_EVENT_TYPE (event))
	{
	case GST_EVENT_QOS:
//		g_print("GST_EVENT_QOS\n");

//		GstQOSType type;
//		gdouble proportion;
//		GstClockTimeDiff diff;
//		GstClockTime timestamp;
//
//		gst_event_parse_qos (event, &type, &proportion, &diff, &timestamp);
//		g_print("type=%d (GST_QOS_TYPE_UNDERFLOW=%d) proportion=%lf timestamp=%ld diff=%ld\n",
//			type, GST_QOS_TYPE_UNDERFLOW, proportion, timestamp, diff);


		ret = gst_pad_event_default (pad, parent, event);
		break;
	default:
		ret = gst_pad_event_default (pad, parent, event);
		break;
	}
	return ret;
}


/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_dwt_filter_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
	GstDwtFilter *filter;
	GstMapInfo info;
	int i, j;
	struct timespec t1, t2, diff;

	filter = GST_DWTFILTER (parent);

	gst_buffer_map (buf, &info, GST_MAP_WRITE);
	guint8_to_gdouble(info.data, filter->pDWTBuffer, filter->height * filter->width);

	clock_gettime(CLOCK_REALTIME, &t1);
//	for(i = 0; i < filter->height; i++)
//	{
//		double *data = filter->pDWTBuffer + filter->height * i;
////		g_print ("Calling gsl_wavelet_transform_forward()...\n");
//		res = gsl_wavelet_transform_forward(filter->w, data, 1, filter->width, filter->work);
//
//		memset(data, 0, filter->width * sizeof(gdouble) / 2);
//
//		res = gsl_wavelet_transform_inverse(filter->w, data, 1, filter->width, filter->work);
//	}

	gsl_wavelet2d_transform_forward(filter->w,
									filter->pDWTBuffer,
									filter->width,
									filter->width,
									filter->height,
									filter->work);

	for(j = 0; j < filter->height / 2; j++)
	{
		memset(filter->pDWTBuffer + j * filter->width, 0, sizeof(gdouble) * (filter->width / 2));
	}

	gsl_wavelet2d_transform_inverse(filter->w,
									filter->pDWTBuffer,
									filter->width,
									filter->width,
									filter->height,
									filter->work);

	clock_gettime(CLOCK_REALTIME, &t2);

	gdouble_to_guint8(filter->pDWTBuffer, info.data, filter->height * filter->width);
	gst_buffer_unmap (buf, &info);

	if(t2.tv_nsec >= t1.tv_nsec)
	{
		diff.tv_sec = t2.tv_sec - t1.tv_sec;
		diff.tv_nsec = t2.tv_nsec - t1.tv_nsec;
	}
	else
	{
		diff.tv_sec = t2.tv_sec - t1.tv_sec - 1;
		diff.tv_nsec = 1000000000 + t2.tv_nsec - t1.tv_nsec;
	}


//	g_print ("diff=%ld.%09ld\n", diff.tv_sec, diff.tv_nsec);

	return gst_pad_push (filter->srcpad, buf);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
dwtfilter_init (GstPlugin * dwtfilter)
{
	/* debug category for fltering log messages
	 *
	 * exchange the string 'Template dwtfilter' with your description
	 */
	GST_DEBUG_CATEGORY_INIT (gst_dwt_filter_debug, "dwtfilter",
			0, "Template dwtfilter");

	return gst_element_register (dwtfilter, "dwtfilter", GST_RANK_NONE,
			GST_TYPE_DWTFILTER);
}

static gboolean
gst_dwt_filter_query (GstPad    *pad,
		GstObject *parent,
		GstQuery  *query)
{
	gboolean ret;
	GstDwtFilter *filter = GST_DWTFILTER (parent);

	switch (GST_QUERY_TYPE (query)) {
//	case GST_QUERY_POSITION:
//		/* we should report the current position */
//		[...]
//		 break;
//	case GST_QUERY_DURATION:
//		/* we should report the duration here */
//		[...]
//		 break;
//	case GST_QUERY_CAPS:
//		/* we should report the supported caps here */
//		[...]
//		 break;
	case GST_QUERY_LATENCY:
		g_print("GST_QUERY_LATENCY arrived\n");
//		gst_query_set_latency (query, TRUE, 10000000000, 10000000000);
//		ret = gst_pad_query_default (pad, parent, query);
		break;

	default:
		/* just call the default handler */
		ret = gst_pad_query_default (pad, parent, query);
		break;
	}
	return ret;
}

static gboolean my_bus_callback (GstBus *bus, GstMessage *message, gpointer data)
{
	g_print ("Got %s message\n", GST_MESSAGE_TYPE_NAME (message));

	switch (GST_MESSAGE_TYPE (message)) {
	case GST_MESSAGE_QOS:
		g_print("GST_MESSAGE_QOS\n");
		break;
	default:
		g_print("GST_MESSAGE_*\n");
		break;
	}

	/* we want to be notified again the next time there is a message
	 * on the bus, so returning TRUE (FALSE means we want to stop watching
	 * for messages on the bus and our callback should not be called again)
	 */
	return TRUE;
}

static void guint8_to_gdouble(guint8* src, gdouble *dst, gsize sz)
{
	int i;

	for(i = 0; i < sz; i++)
	{
		dst[i] = src[i];
	}
}

static void gdouble_to_guint8(gdouble* src, guint8 *dst, gsize sz)
{
	int i;

	for(i = 0; i < sz; i++)
	{
		dst[i] = src[i];
	}
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstdwtfilter"
#endif

/* gstreamer looks for this structure to register dwtfilters
 *
 * exchange the string 'Template dwtfilter' with your dwtfilter description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dwtfilter,
    "Template dwtfilter",
    dwtfilter_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
