/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Eric Anholt
 * Copyright © 2009 Chris Wilson
 * Copyright © 2005,2010 Red Hat, Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it either under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (the "LGPL") or, at your option, under the terms of the Mozilla
 * Public License Version 1.1 (the "MPL"). If you do not alter this
 * notice, a recipient may use your version of this file under either
 * the MPL or the LGPL.
 *
 * You should have received a copy of the LGPL along with this library
 * in the file COPYING-LGPL-2.1; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA
 * You should have received a copy of the MPL along with this library
 * in the file COPYING-MPL-1.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
 * OF ANY KIND, either express or implied. See the LGPL or the MPL for
 * the specific language governing rights and limitations.
 *
 * The Original Code is the cairo graphics library.
 *
 * The Initial Developer of the Original Code is Red Hat, Inc.
 *
 * Contributor(s):
 *	Benjamin Otte <otte@gnome.org>
 *	Carl Worth <cworth@cworth.org>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 *	Eric Anholt <eric@anholt.net>
 */

#include "cairoint.h"

#include "cairo-error-private.h"
#include "cairo-gl-gradient-private.h"
#include "cairo-gl-private.h"

// Henry Song
// we want to get start and end stops location, number of stops - offsets
// colors for seach section of rbga, number of offsets
cairo_status_t
_cairo_gl_gradient_digest_linear_gradient(const cairo_gradient_pattern_t *pattern, float surface_height, float *stops, float *colors, float *offsets, float *total_dist, int *nstops, float *delta)
{
	double a, b, c, d;
	int i;
        cairo_matrix_t matrix;
        cairo_linear_pattern_t *linear = NULL;
	if(pattern->n_stops > 8)
		return CAIRO_INT_STATUS_UNSUPPORTED;

	// TODO: we take care of CAIRO_EXTEND_NONE later
	// get matrix
	memcpy(&matrix, &(pattern->base.matrix), sizeof(double)*6);
	cairo_matrix_invert(&matrix);
	// get transformed points
	linear = (cairo_linear_pattern_t *)pattern;

	a = linear->pd1.x;
	b = linear->pd1.y;
	c = linear->pd2.x;
	d = linear->pd2.y;
	cairo_matrix_transform_point(&matrix, &a, &b);
	cairo_matrix_transform_point(&matrix, &c, &d);

	stops[0] = a;
	//stops[1] = surface_height - b;
	stops[1] = b;
	stops[2] = c;
	//stops[3] = surface_height - d;
	stops[3] = d;
	
	for(i = 0; i < pattern->n_stops; i++)
	{
		colors[i*4] = pattern->stops[i].color.red;
		colors[i*4+1] = pattern->stops[i].color.green;
		colors[i*4+2] = pattern->stops[i].color.blue;
		colors[i*4+3] = pattern->stops[i].color.alpha;
		offsets[i] = pattern->stops[i].offset;
		if(offsets[i] > 1.0)
			offsets[i] = 1.0;
		else if(offsets[i] < 0.0)
			offsets[i] = 0.0;
		if(i > 0)
		{
			if(offsets[i] < offsets[i-1])
				offsets[i] = offsets[i-1];
		}
	}
	*nstops = pattern->n_stops;
	delta[0] = stops[2] - stops[0];
	delta[1] = stops[3] - stops[1];
	*total_dist = delta[0] * delta[0] + delta[1] * delta[1];
	return CAIRO_STATUS_SUCCESS;
}
// Henry Song
cairo_status_t
_cairo_gl_gradient_digest_radial_gradient(const cairo_gradient_pattern_t *pattern, float surface_height, float *scales, float *colors, float *offsets, int *nstops, float *circle_1, float *circle_2)
{
	double a, b, c, d;
	double x, y, dx1, dy1;
  	cairo_matrix_t matrix;
        cairo_radial_pattern_t *radial = NULL;
	int i;

	if(pattern->n_stops > 8)
		return CAIRO_INT_STATUS_UNSUPPORTED;

	// TODO: we take care of CAIRO_EXTEND_NONE later
	// get matrix
	memcpy(&matrix, &(pattern->base.matrix), sizeof(double)*6);
	cairo_matrix_invert(&matrix);
	// get transformed points
	radial = (cairo_radial_pattern_t *)pattern;

	a = radial->cd1.center.x;
	b = radial->cd1.center.y;
	c = radial->cd2.center.x;
	d = radial->cd2.center.y;
	cairo_matrix_transform_point(&matrix, &a, &b);
	cairo_matrix_transform_point(&matrix, &c, &d);
	// we have to transform radius
	dx1 = 100.0; 
	dy1 = 100.0;
	cairo_matrix_transform_distance(&matrix, &dx1, &dy1);
	scales[0] = 100.0 / dx1; 
	scales[1] = 100.0 / dy1;

	x = radial->cd1.radius;
	y = 0;
	cairo_matrix_transform_distance(&matrix, &x, &y);
	
	circle_1[0] = a;
	//circle_1[1] = surface_height - b;
	circle_1[1] = b;
	//circle_1[2] = radial->cd1.radius;
	circle_1[2] = x;
	
	x = radial->cd2.radius;
	y = 0;
	cairo_matrix_transform_distance(&matrix, &x, &y);
	circle_2[0] = c;
	//circle_2[1] = surface_height - d;
	circle_2[1] = d;
	//circle_2[2] = radial->cd2.radius;
	circle_2[2] = x;

	for(i = 0; i < pattern->n_stops; i++)
	{
		colors[i*4] = pattern->stops[i].color.red;
		colors[i*4+1] = pattern->stops[i].color.green;
		colors[i*4+2] = pattern->stops[i].color.blue;
		colors[i*4+3] = pattern->stops[i].color.alpha;
		offsets[i] = pattern->stops[i].offset;
		if(offsets[i] > 1.0)
			offsets[i] = 1.0;
		else if(offsets[i] < 0.0)
			offsets[i] = 0.0;
		if(i > 0)
		{
			if(offsets[i] < offsets[i-1])
				offsets[i] = offsets[i-1];
		}
	}
	*nstops = pattern->n_stops;
	return CAIRO_STATUS_SUCCESS;
}

static int
_cairo_gl_gradient_sample_width (unsigned int                 n_stops,
                                 const cairo_gradient_stop_t *stops)
{
    unsigned int n;
    int width;

    width = 8;
    for (n = 1; n < n_stops; n++) {
	double dx = stops[n].offset - stops[n-1].offset;
	double delta, max;
	int ramp;

	if (dx == 0)
	    continue;

	max = stops[n].color.red - stops[n-1].color.red;

	delta = stops[n].color.green - stops[n-1].color.green;
	if (delta > max)
	    max = delta;

	delta = stops[n].color.blue - stops[n-1].color.blue;
	if (delta > max)
	    max = delta;

	delta = stops[n].color.alpha - stops[n-1].color.alpha;
	if (delta > max)
	    max = delta;

	ramp = 128 * max / dx;
	if (ramp > width)
	    width = ramp;
    }

    width = (width + 7) & -8;
    return MIN (width, 1024);
}

static cairo_status_t
_cairo_gl_gradient_render (const cairo_gl_context_t    *ctx,
                           unsigned int                 n_stops,
                           const cairo_gradient_stop_t *stops,
                           void                        *bytes,
                           int                          width)
{
    pixman_image_t *gradient, *image;
    pixman_gradient_stop_t pixman_stops_stack[32];
    pixman_gradient_stop_t *pixman_stops;
    pixman_point_fixed_t p1, p2;
    unsigned int i;
    pixman_format_code_t gradient_pixman_format;

    /*
     * Ensure that the order of the gradient's components in memory is BGRA.
     * This is done so that the gradient's pixel data is always suitable for
     * texture upload using format=GL_BGRA and type=GL_UNSIGNED_BYTE.
     */
    if (!_cairo_is_little_endian ())
	gradient_pixman_format = PIXMAN_b8g8r8a8;
    else
	gradient_pixman_format = PIXMAN_a8r8g8b8;

    pixman_stops = pixman_stops_stack;
    if (unlikely (n_stops > ARRAY_LENGTH (pixman_stops_stack))) {
	pixman_stops = _cairo_malloc_ab (n_stops,
					 sizeof (pixman_gradient_stop_t));
	if (unlikely (pixman_stops == NULL))
	    return _cairo_error (CAIRO_STATUS_NO_MEMORY);
    }

    for (i = 0; i < n_stops; i++) {
	pixman_stops[i].x = _cairo_fixed_16_16_from_double (stops[i].offset);
	pixman_stops[i].color.red   = stops[i].color.red_short;
	pixman_stops[i].color.green = stops[i].color.green_short;
	pixman_stops[i].color.blue  = stops[i].color.blue_short;
	pixman_stops[i].color.alpha = stops[i].color.alpha_short;
    }

    p1.x = 0;
    p1.y = 0;
    p2.x = width << 16;
    p2.y = 0;

    gradient = pixman_image_create_linear_gradient (&p1, &p2,
						    pixman_stops,
						    n_stops);
    if (pixman_stops != pixman_stops_stack)
	free (pixman_stops);

    if (unlikely (gradient == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    pixman_image_set_filter (gradient, PIXMAN_FILTER_BILINEAR, NULL, 0);
    pixman_image_set_repeat (gradient, PIXMAN_REPEAT_PAD);

    image = pixman_image_create_bits (gradient_pixman_format, width, 1,
				      bytes, sizeof(uint32_t)*width);
    if (unlikely (image == NULL)) {
	pixman_image_unref (gradient);
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);
    }

    pixman_image_composite32 (PIXMAN_OP_SRC,
                              gradient, NULL, image,
                              0, 0,
                              0, 0,
                              0, 0,
                              width, 1);

    pixman_image_unref (gradient);
    pixman_image_unref (image);
    return CAIRO_STATUS_SUCCESS;
}

static unsigned long
_cairo_gl_gradient_hash (unsigned int                  n_stops,
                         const cairo_gradient_stop_t  *stops)
{
    return _cairo_hash_bytes (n_stops,
                              stops,
                              sizeof (cairo_gradient_stop_t) * n_stops);
}

static cairo_gl_gradient_t *
_cairo_gl_gradient_lookup (cairo_gl_context_t           *ctx,
                           unsigned long                 hash,
                           unsigned int                  n_stops,
                           const cairo_gradient_stop_t  *stops)
{
    cairo_gl_gradient_t lookup;

    lookup.cache_entry.hash = hash,
    lookup.n_stops = n_stops;
    lookup.stops = stops;

    return _cairo_cache_lookup (&ctx->gradients, &lookup.cache_entry);
}

cairo_bool_t
_cairo_gl_gradient_equal (const void *key_a, const void *key_b)
{
    const cairo_gl_gradient_t *a = key_a;
    const cairo_gl_gradient_t *b = key_b;

    if (a->n_stops != b->n_stops)
        return FALSE;

    return memcmp (a->stops, b->stops, a->n_stops * sizeof (cairo_gradient_stop_t)) == 0;
}

cairo_int_status_t
_cairo_gl_gradient_create (cairo_gl_context_t           *ctx,
                           unsigned int                  n_stops,
                           const cairo_gradient_stop_t  *stops,
                           cairo_gl_gradient_t         **gradient_out)
{
    unsigned long hash;
    cairo_gl_gradient_t *gradient;
    cairo_status_t status;
    int tex_width;
    void *data;
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;

    if ((unsigned int) ctx->max_texture_size / 2 <= n_stops)
        return CAIRO_INT_STATUS_UNSUPPORTED;

    hash = _cairo_gl_gradient_hash (n_stops, stops);
    
    gradient = _cairo_gl_gradient_lookup (ctx, hash, n_stops, stops);
    if (gradient) {
        *gradient_out = _cairo_gl_gradient_reference (gradient);
        return CAIRO_STATUS_SUCCESS;
    }

    gradient = malloc (sizeof (cairo_gl_gradient_t) + sizeof (cairo_gradient_stop_t) * (n_stops - 1));
    if (gradient == NULL)
        return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    tex_width = _cairo_gl_gradient_sample_width (n_stops, stops);

    CAIRO_REFERENCE_COUNT_INIT (&gradient->ref_count, 1);
    gradient->cache_entry.hash = hash;
    gradient->cache_entry.size = tex_width;
    gradient->device = &ctx->base;
    gradient->n_stops = n_stops;
    gradient->stops = gradient->stops_embedded;
    memcpy (gradient->stops_embedded, stops, n_stops * sizeof (cairo_gradient_stop_t));

    glGenTextures (1, &gradient->tex);
    _cairo_gl_context_activate (ctx, CAIRO_GL_TEX_TEMP);
    glBindTexture (ctx->tex_target, gradient->tex);

    /* GL_PIXEL_UNPACK_BUFFER is only available in Desktop GL */
    if (ctx->gl_flavor == CAIRO_GL_FLAVOR_DESKTOP) {
	dispatch->BindBuffer (GL_PIXEL_UNPACK_BUFFER, ctx->texture_load_pbo);
	dispatch->BufferData (GL_PIXEL_UNPACK_BUFFER,
			      tex_width * sizeof (uint32_t), 0, GL_STREAM_DRAW);
	data = dispatch->MapBuffer (GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);

	status = _cairo_gl_gradient_render (ctx, n_stops, stops, data, tex_width);

	dispatch->UnmapBuffer (GL_PIXEL_UNPACK_BUFFER);

	if (unlikely (status)) {
	    dispatch->BindBuffer (GL_PIXEL_UNPACK_BUFFER, 0);
	    free (gradient);
	    return status;
	}

	glTexImage2D (ctx->tex_target, 0, GL_RGBA8, tex_width, 1, 0,
		      GL_BGRA, GL_UNSIGNED_BYTE, 0);

	dispatch->BindBuffer (GL_PIXEL_UNPACK_BUFFER, 0);
    }
    else {
	data = _cairo_malloc_ab (tex_width, sizeof (uint32_t));

	status = _cairo_gl_gradient_render (ctx, n_stops, stops, data, tex_width);

	glTexImage2D (ctx->tex_target, 0, GL_BGRA, tex_width, 1, 0,
		      GL_BGRA, GL_UNSIGNED_BYTE, data);

	free (data);
    }

    /* we ignore errors here and just return an uncached gradient */
    if (likely (! _cairo_cache_insert (&ctx->gradients, &gradient->cache_entry)))
        _cairo_gl_gradient_reference (gradient);

    *gradient_out = gradient;
    return CAIRO_STATUS_SUCCESS;
}

cairo_gl_gradient_t *
_cairo_gl_gradient_reference (cairo_gl_gradient_t *gradient)
{
    assert (CAIRO_REFERENCE_COUNT_HAS_REFERENCE (&gradient->ref_count));

    _cairo_reference_count_inc (&gradient->ref_count);

    return gradient;
}

void
_cairo_gl_gradient_destroy (cairo_gl_gradient_t *gradient)
{
    cairo_gl_context_t *ctx;
    cairo_status_t ignore;

    assert (CAIRO_REFERENCE_COUNT_HAS_REFERENCE (&gradient->ref_count));

    if (! _cairo_reference_count_dec_and_test (&gradient->ref_count))
	return;

    if (_cairo_gl_context_acquire (gradient->device, &ctx) == CAIRO_STATUS_SUCCESS) {
        glDeleteTextures (1, &gradient->tex);
        ignore = _cairo_gl_context_release (ctx, CAIRO_STATUS_SUCCESS);
    }

    free (gradient);
}
