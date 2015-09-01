/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2015 martin <<user@hostname.org>>
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
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
	PROP_SILENT
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS ("ANY")
);

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS ("ANY")
);

#define gst_dwt_filter_parent_class parent_class
G_DEFINE_TYPE (GstDwtFilter, gst_dwt_filter, GST_TYPE_ELEMENT);

static void gst_dwt_filter_set_property (GObject * object, guint prop_id,
		const GValue * value, GParamSpec * pspec);
static void gst_dwt_filter_get_property (GObject * object, guint prop_id,
		GValue * value, GParamSpec * pspec);

static gboolean gst_dwt_filter_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_dwt_filter_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);
static gboolean gst_dwt_filter_query (GstPad *pad, GstObject *parent, GstQuery  *query);



static void guint8_to_gdouble(guint8* src, gdouble *dst, gsize sz);
static void gdouble_to_guint8(gdouble* src, guint8 *dst, gsize sz);

int width, height;
double *pDWTBuffer;

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

	gst_element_class_set_details_simple(gstelement_class,
			"DwtFilter",
			"DWT edge detection",
			"DWT edge detection",
			"martin <<user@hostname.org>>");

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

	filter->silent = FALSE;

	filter->w = gsl_wavelet_alloc (gsl_wavelet_haar, 2);

	gst_pad_set_query_function (filter->srcpad, gst_dwt_filter_query);
	gst_pad_set_query_function (filter->sinkpad, gst_dwt_filter_query);

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
		GstCaps * caps;

		gst_event_parse_caps (event, &caps);
		/* do something with the caps */

		for (i = 0; i < gst_caps_get_size (caps); i++)
		{
			structure = gst_caps_get_structure (caps, i);
			if(structure)
			{
				gst_structure_get_int(structure, "width", &width);
				gst_structure_get_int(structure, "height", &height);
				format = gst_structure_get_string(structure, "format");
				g_print("width = %d height = %d format = %s\n", width, height, format);
				//pAccum = malloc(4 * width * height * sizeof(long int));
				//memset(pAccum, 0, 4 * width * height * sizeof(long int));
				pDWTBuffer = (double*) malloc(width * height * sizeof(double));
				memset(pDWTBuffer, 0, width * height * sizeof(double));
				
				filter->work = gsl_wavelet_workspace_alloc (width * height * sizeof(double));
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
	int res, i;
	static nFrameNumber = 0;

	filter = GST_DWTFILTER (parent);

//	if (filter->silent == FALSE)
//		g_print ("I'm plugged, therefore I'm in.\n");

	gst_buffer_map (buf, &info, GST_MAP_WRITE);
	guint8_to_gdouble(info.data, pDWTBuffer, height * width);

	for(i = 0; i < height; i++)
	{
		res = gsl_wavelet_transform_forward(filter->w, pDWTBuffer + (i * width),
			1, width, filter->work);
	}

//	res = gsl_wavelet2d_transform_forward(filter->w, pDWTBuffer,
//		width, width, height, filter->work);
	
//	res = gsl_wavelet2d_transform_inverse(filter->w, pDWTBuffer,
//		width, width, height, filter->work);

	gdouble_to_guint8(pDWTBuffer, info.data, height * width);
	gst_buffer_unmap (buf, &info);

	g_print("Buffer number: %d time %ld: \n", nFrameNumber++, time(NULL));

	/* just push out the incoming buffer without touching it */
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

//gst_pad_set_query_function (filter->srcpad,
//    gst_my_filter_src_query);

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
		ret = gst_pad_query_default (pad, parent, query);
		break;

	default:
		/* just call the default handler */
		ret = gst_pad_query_default (pad, parent, query);
		break;
	}
	return ret;
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

