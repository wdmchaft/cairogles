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

#include "cairo-composite-rectangles-private.h"
#include "cairo-default-context-private.h"
#include "cairo-recording-surface-private.h"
#include "cairo-error-private.h"
#include "cairo-gl-private.h"
#include "cairo-gl-tristrip-indices-private.h"

#include <sys/time.h>
#include "cairo-surface-clipper-private.h"
#include <math.h>

static long _get_tick(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec * 1000000 + now.tv_usec;
}

static cairo_int_status_t
_cairo_gl_surface_fill_rectangles (void			   *abstract_dst,
				   cairo_operator_t	    op,
				   const cairo_color_t     *color,
				   cairo_rectangle_int_t   *rects,
				   int			    num_rects);

static cairo_int_status_t
_cairo_gl_surface_mask(void *abstract_surface,
	cairo_operator_t op,
	const cairo_pattern_t *source,
	const cairo_pattern_t *mask,
	cairo_clip_t *clip);

static cairo_status_t
_cairo_gl_surface_mark_dirty_rectangle(cairo_surface_t *abstract_surface,
	int x, int y, int width, int height);

static cairo_int_status_t
_cairo_gl_surface_composite (cairo_operator_t		  op,
			     const cairo_pattern_t	 *src,
			     const cairo_pattern_t	 *mask,
			     void			 *abstract_dst,
			     int			  src_x,
			     int			  src_y,
			     int			  mask_x,
			     int			  mask_y,
			     int			  dst_x,
			     int			  dst_y,
			     unsigned int		  width,
			     unsigned int		  height,
			     cairo_region_t		 *clip_region);

static cairo_status_t
_cairo_gl_surface_flush (void *abstract_surface);

static void
_cairo_gl_surface_remove_from_cache(cairo_surface_t *abstract_surface);

static int
_cairo_gl_surface_max_size(cairo_gl_surface_t *surface)
{
	cairo_gl_context_t *ctx = (cairo_gl_context_t *)surface->base.device;
	return ctx->max_texture_size;
}

static void
_cairo_gl_disable_stencil_test(cairo_gl_context_t *ctx)
{
    if(ctx->stencil_test_reset)
    {
        glDisable(GL_STENCIL_TEST);
        ctx->stencil_test_reset = FALSE;
        ctx->stencil_test_enabled = FALSE;
    }
    else
    {
        if(ctx->stencil_test_enabled)
        {
            glDisable(GL_STENCIL_TEST);
            ctx->stencil_test_enabled = FALSE;
        }
    }
}

static void 
_cairo_gl_disable_scissor_test(cairo_gl_context_t *ctx)
{
    if(ctx->scissor_test_reset == TRUE)
    {
        glDisable (GL_SCISSOR_TEST);
        ctx->scissor_test_reset = FALSE;
        ctx->scissor_test_enabled = FALSE;
    }
    else if(ctx->scissor_test_enabled)
    {
        glDisable (GL_SCISSOR_TEST);
        ctx->scissor_test_enabled = FALSE;
    }
}

static void
_cairo_gl_enable_stencil_test(cairo_gl_context_t *ctx)
{
    if(ctx->stencil_test_reset)
    {
        glEnable(GL_STENCIL_TEST);
        ctx->stencil_test_reset = FALSE;
        ctx->stencil_test_enabled = TRUE;
    }
    else
    {
        if(ctx->stencil_test_enabled == FALSE)
        {
            glEnable(GL_STENCIL_TEST);
            ctx->stencil_test_enabled = TRUE;
        }
    }
}

static void
_cairo_gl_enable_scissor_test (cairo_gl_context_t *ctx, 
			       cairo_gl_surface_t *surface, 
			       cairo_rectangle_int_t rect)
{
    if(ctx->scissor_test_reset == TRUE)
    {
        glEnable (GL_SCISSOR_TEST);
        ctx->scissor_test_reset = FALSE;
        ctx->scissor_test_enabled = TRUE;
    }
    else if(ctx->scissor_test_enabled == FALSE)
    {
        glEnable (GL_SCISSOR_TEST);
        ctx->scissor_test_enabled = TRUE;
    }
    if(_cairo_gl_surface_is_texture(surface))
    {
        if(ctx->scissor_box.x != rect.x ||
           ctx->scissor_box.y != rect.y ||
           ctx->scissor_box.width != rect.width ||
           ctx->scissor_box.height != rect.height)
        {
            glScissor(rect.x, rect.y,
                      rect.width, rect.height);
            ctx->scissor_box.x = rect.x;
            ctx->scissor_box.y = rect.y;
            ctx->scissor_box.width = rect.width;
            ctx->scissor_box.height = rect.height;
        }
    }
    else
    {
        if(ctx->scissor_box.x != rect.x ||
           ctx->scissor_box.y != surface->height - rect.y - rect.height ||
           ctx->scissor_box.width != rect.width ||
           ctx->scissor_box.height != rect.height)
        {
            glScissor(rect.x, 
                      surface->height - rect.y - rect.height,
                      rect.width, rect.height);
            ctx->scissor_box.x = rect.x;
            ctx->scissor_box.y = surface->height - rect.y - rect.height;
            ctx->scissor_box.width = rect.width;
            ctx->scissor_box.height = rect.height;
        }
    }
}

static cairo_bool_t
_cairo_gl_clip_contains_rectangle (cairo_clip_t *clip,
                             cairo_rectangle_int_t *rect)
{
    if(clip->path == NULL && clip->num_boxes <= 0)
        return TRUE;
    else if(clip->path == NULL && clip->num_boxes == 1) {
    if(rect->x >= clip->extents.x &&
       rect->y >= clip->extents.y &&
       rect->x + rect->width <= clip->extents.x + clip->extents.width &&
       rect->y + rect->height <= clip->extents.y + clip->extents.height)
        return TRUE;
    }
    return _cairo_clip_contains_rectangle (clip, rect);
}    

// true means inner is within outer
static cairo_bool_t 
_cairo_gl_compare_region (int inner_x, int inner_y, 
                        int inner_width, int inner_height,  
                        int outer_x, int outer_y,
                        int outer_width, int outer_height)
{
    int x1, y1, x2, y2;

    if(inner_x < outer_x || inner_y < outer_y)
        return FALSE;
    x1 = inner_x + inner_width;
    y1 = inner_y + inner_height;
    x2 = outer_x + outer_width;
    y2 = outer_y + outer_height;
    if(x1 > x2 || y1 > y2)
        return FALSE;
    return TRUE;
}

// true means we need clip
static cairo_bool_t 
_cairo_gl_extents_within_clip (cairo_composite_rectangles_t extents,
                               cairo_bool_t bounded,
                               cairo_clip_t *clip)
{
    int x1, y1, w1, h1;
    int x2, y2, w2, h2;
    int i;

    if(clip == NULL || (clip->path == NULL && clip->num_boxes == 0))
        return FALSE;
    else if(clip->path != NULL)
        return TRUE;
    
    if(bounded == FALSE) {
        x1 = extents.unbounded.x;
        y1 = extents.unbounded.y;
        w1 = extents.unbounded.width;
        w1 = extents.unbounded.height;
    }
    else {
        x1 = extents.bounded.x;
        y1 = extents.bounded.y;
        w1 = extents.bounded.width;
        h1 = extents.bounded.height;
    }

    for(i = 0; i < clip->num_boxes; i++)
    {
        x2 = clip->boxes[i].p1.x;
        y2 = clip->boxes[i].p1.y;
        w2 = clip->boxes[i].p2.x - x2;
        h2 = clip->boxes[i].p2.y - y2;

        if (!_cairo_gl_compare_region (x1, y1, w1, h1, 
                                      x2, y2, w2, h2))
            return TRUE;
    }
    return FALSE;
}        

static cairo_bool_t
_cairo_gl_support_standard_npot(cairo_gl_surface_t *surface)
{
	cairo_gl_context_t *ctx = (cairo_gl_context_t *)surface->base.device;
	return ctx->standard_npot;
}

cairo_status_t
_cairo_gl_fill (cairo_gl_tristrip_indices_t *indices)
{
	cairo_gl_composite_t *setup = indices->setup;
	cairo_gl_context_t *ctx = setup->ctx;

	char *src_colors = NULL;
	double *src_v = NULL;

	int index = 0;
	int stride = 4 * sizeof(GLfloat);
	cairo_status_t status;

	int number_of_vertices = 0;
	GLfloat *vertices = NULL;

	int number_of_indices = 0;
	unsigned short *gl_indices = NULL;

	GLfloat *mask_texture_coords = NULL;
	if (_cairo_array_num_elements (&indices->mask_texture_coords) > 0)
		mask_texture_coords = _cairo_array_index (&indices->mask_texture_coords, 0);

	number_of_vertices = _cairo_array_num_elements (&indices->vertices) / 2;
	vertices = _cairo_array_index (&indices->vertices, 0);

	number_of_indices = _cairo_array_num_elements (&indices->indices);
	gl_indices = _cairo_array_index (&indices->indices, 0);

	if(setup->src.type == CAIRO_GL_OPERAND_TEXTURE)
	{
		cairo_matrix_t m, m1;
		GLfloat *st = NULL;
		src_v = (double *)malloc(sizeof(double)*number_of_vertices*2);
		src_colors = (char *)malloc(sizeof(GLfloat)*2*number_of_vertices);
		cairo_matrix_init_scale(&m, 1.0, 1.0);
		cairo_matrix_multiply(&m, &m, &(setup->source->matrix));
		cairo_matrix_init_scale(&m1, 1.0 / setup->src.texture.width,
			1.0 / setup->src.texture.height);
		cairo_matrix_multiply(&m, &m, &m1);
		st = (GLfloat*)src_colors;
		for(index = 0; index < number_of_vertices; index++)
		{
			src_v[index*2] = vertices[index*2];
			src_v[index*2+1] = vertices[index*2+1];
			cairo_matrix_transform_point(&m, &src_v[index*2], &src_v[index*2+1]); 
			st[index*2] = src_v[index*2];
			st[index*2+1] = src_v[index*2+1];
		}
	}
    //printf("\tcopy color %ld\n", _get_tick() - now);

		// we need to fill colors with st values
    //now = _get_tick();
	status = _cairo_gl_composite_begin_constant_color(setup, 
			number_of_vertices, 
			vertices, 
			src_colors,
			mask_texture_coords,
			ctx);

	if (unlikely(status))
	{
        if(src_colors)
		    free (src_colors);
        if(src_colors)
		    free (src_v);
		return status;
	}

	_cairo_gl_composite_fill_constant_color(ctx, number_of_indices, gl_indices);
    if(src_colors)
	    free (src_colors);
    if(src_colors)
	    free (src_v);
	return status;
}

static cairo_status_t
_cairo_gl_add_triangle (void		   *closure,
			const cairo_point_t triangle[3])
{
    cairo_status_t status;
    cairo_gl_tristrip_indices_t *indices = (cairo_gl_tristrip_indices_t *)closure;
    cairo_gl_composite_t *setup = indices->setup;

    /* Flush everything if the mesh is very complicated. */
    if (_cairo_array_num_elements (&indices->indices) > MAX_INDEX && setup != NULL) {
	cairo_status_t status = _cairo_gl_fill(indices);
	_cairo_gl_tristrip_indices_destroy (indices);
	status = _cairo_gl_tristrip_indices_init (indices);
	indices->setup = setup;
    }

    status = _cairo_gl_tristrip_add_vertex (indices, &triangle[0]);
    status = _cairo_gl_tristrip_add_vertex (indices, &triangle[1]);
    status = _cairo_gl_tristrip_add_vertex (indices, &triangle[2]);
    if (unlikely (status))
	return status;
    return _cairo_gl_tristrip_indices_append_vertex_indices (indices, 3);
}

static cairo_status_t
_cairo_gl_add_triangle_fan(void			*closure,
			   const cairo_point_t	*midpt,
			   const cairo_point_t	*points,
			   int			 npoints)
{
    int i;
    cairo_gl_tristrip_indices_t *indices = (cairo_gl_tristrip_indices_t *)closure;
    cairo_gl_composite_t *setup = indices->setup;

    /* Flush everything if the mesh is very complicated. */
    if (_cairo_array_num_elements (&indices->indices) > MAX_INDEX && setup != NULL) {
	cairo_status_t status = _cairo_gl_fill(indices);
	_cairo_gl_tristrip_indices_destroy (indices);
	status = _cairo_gl_tristrip_indices_init (indices);
	indices->setup = setup;
    }

    /* Our strategy here is to not even try to build a triangle fan, but to
       draw each triangle as if it was an unconnected member of a triangle strip. */
    for (i = 1; i < npoints; i++) {
	cairo_status_t status;
	status =_cairo_gl_tristrip_add_vertex (indices, midpt);
	status = _cairo_gl_tristrip_add_vertex (indices, &points[i - 1]);
	status = _cairo_gl_tristrip_add_vertex (indices, &points[i]);
	if (unlikely (status))
	    return status;
	status = _cairo_gl_tristrip_indices_append_vertex_indices (indices, 3);
	if (unlikely (status))
	    return status;
    }

    return CAIRO_STATUS_SUCCESS;
}

static cairo_bool_t
_cairo_gl_size_need_extend(int in_size, int *out_size, float *scale)
{
	float f;
	int d;
	int extend_size = 1;

	cairo_bool_t need_extend = FALSE;
	if(in_size <= 0)
	{ 
		*out_size = 0;
		*scale = 0.0;
		return FALSE;
	}

	if(in_size == 1)
	{
		need_extend = 1;
		*out_size = 2;
		*scale = 2.0;
		return TRUE;
	}
	
	f = logf(in_size) / logf(2.0);
	d = (int)f;
	if(f - d == 0.0)
	{
		*out_size = in_size;
		*scale = 1.0;
		return FALSE;
	}

	if(f - d <= 0.5)
		extend_size <<= d;
	else
		extend_size <<= (d+1);
	*out_size = extend_size;
	*scale = (float)extend_size / (float)in_size;
	return TRUE;
}


static cairo_gl_surface_t *
_cairo_gl_surface_generate_npot_surface(cairo_gl_surface_t *src)
{
	int max_size;
	cairo_t *cr;
	int out_size_x, out_size_y;
	cairo_bool_t need_extend_x, need_extend_y;
	float extend_scale_x, extend_scale_y;
	cairo_gl_surface_t *extend_src = NULL;

	max_size = _cairo_gl_surface_max_size(src);

	need_extend_x = _cairo_gl_size_need_extend(src->width, 
											   &out_size_x, 
											   &extend_scale_x);
	need_extend_y = _cairo_gl_size_need_extend(src->height, 
											   &out_size_y, 
											   &extend_scale_y);
	if(need_extend_x == FALSE && need_extend_y == FALSE)
		return src;
	
	extend_src = 
		(cairo_gl_surface_t *)cairo_surface_create_similar(&src->base,
						cairo_surface_get_content(&src->base),
						out_size_x,
						out_size_y);
									
	cr = cairo_create(&extend_src->base);
	cairo_scale(cr, extend_scale_x, extend_scale_y);
	cairo_set_source_surface(cr, &src->base, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);
	extend_src->orig_width = src->orig_width;
	extend_src->orig_height = src->orig_height;
	extend_src->scale_width = src->scale_width * extend_scale_x;
	extend_src->scale_height = src->scale_height * extend_scale_y;
	return extend_src;
}

static cairo_status_t
_cairo_gl_clip (cairo_clip_t		*clip,
		cairo_gl_composite_t	*setup, 
		cairo_gl_context_t	*ctx,
		cairo_gl_surface_t	*surface)
{
    cairo_status_t status = CAIRO_STATUS_SUCCESS;

    if (clip->path == NULL && clip->num_boxes == 0)
	return CAIRO_STATUS_SUCCESS;
    
    /* if stencil buffer not changed and clip equal */
    
    /*if(surface->clip == clip && 
       surface->stencil_buffer_changed == FALSE &&
        _cairo_gl_surface_is_texture (surface)) {
    glDepthMask (GL_TRUE);
    glEnable (GL_STENCIL_TEST);
    glColorMask (1, 1, 1, 1);
    //glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
    //glStencilFunc (GL_EQUAL, 1, 1);
    return CAIRO_STATUS_SUCCESS;
    }*/

    if(surface->clip == clip && 
       surface->stencil_buffer_changed == FALSE &&
       _cairo_gl_surface_is_texture (surface)) {
    glDepthMask (GL_TRUE);
    _cairo_gl_enable_stencil_test(ctx);
            
    glColorMask (1, 1, 1, 1);
    return CAIRO_STATUS_SUCCESS;
    }

    /* Operations on_triangle strip indices may end up flushing the surface
       triangle strip cache and doing the fill. In case that happens we prepare
       to update the stencil buffer now. */
    glDepthMask (GL_TRUE);
    //printf("\tdepth mask enable %ld usec\n", _get_tick() - now);
    //now = _get_tick();
    _cairo_gl_enable_stencil_test(ctx);
    glClear (GL_STENCIL_BUFFER_BIT);
    glStencilOp (GL_REPLACE,  GL_REPLACE, GL_REPLACE);
    //printf("\tstencil op %ld usec\n", _get_tick() - now);
    //now = _get_tick();
    glStencilFunc (GL_ALWAYS, 1, 0xffffffff);
    //printf("\tstencil func %ld usec\n", _get_tick() - now);
    //now = _get_tick();

    glColorMask (0, 0, 0, 0);

    //if (surface->clip != clip) { /* The cached clip is out of date. */
	cairo_traps_t traps;
	cairo_polygon_t polygon;
	cairo_antialias_t antialias;
	cairo_fill_rule_t fill_rule;

	surface->clip = clip;
	_cairo_gl_tristrip_indices_destroy (surface->clip_indices);

	status = _cairo_gl_tristrip_indices_init (surface->clip_indices);
	if (unlikely (status))
	    goto FAIL;;

	surface->clip_indices->setup = setup;
	status = _cairo_clip_get_polygon (clip, &polygon, &fill_rule, &antialias);
	if (unlikely (status))
	    goto FAIL;

	_cairo_traps_init (&traps);
	status = _cairo_bentley_ottmann_tessellate_polygon (&traps,
							    &polygon,
							    fill_rule);
	_cairo_polygon_fini (&polygon);
	if (unlikely (status))
	    goto FAIL;

	status = _cairo_gl_tristrip_indices_add_traps (surface->clip_indices, &traps);
	_cairo_traps_fini (&traps);
	if (unlikely (status))
	    goto FAIL;
    //}

    surface->clip_indices->setup = setup;
    if (unlikely ((status = _cairo_gl_fill (surface->clip_indices))))
	goto FAIL;
    glColorMask (1, 1, 1, 1);
    glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
    glStencilFunc (GL_EQUAL, 1, 1);
    surface->stencil_buffer_changed = FALSE;
    return CAIRO_STATUS_SUCCESS;

FAIL:
    _cairo_gl_disable_stencil_test(ctx);
    glColorMask (1, 1, 1, 1);
    surface->stencil_buffer_changed = TRUE;
    return status;
}

static cairo_bool_t _cairo_surface_is_gl (cairo_surface_t *surface)
{
    return surface->backend == &_cairo_gl_surface_backend;
}

static cairo_bool_t
_cairo_gl_get_image_format_and_type_gles2 (pixman_format_code_t pixman_format,
					   GLenum *internal_format, GLenum *format,
					   GLenum *type, cairo_bool_t *has_alpha,
					   cairo_bool_t *needs_swap)
{
    cairo_bool_t is_big_endian = !_cairo_is_little_endian ();

    *has_alpha = TRUE;

    switch ((int) pixman_format) {
    case PIXMAN_a8r8g8b8:
	*internal_format = GL_BGRA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_BYTE;
	*needs_swap = is_big_endian;
	return TRUE;

    case PIXMAN_x8r8g8b8:
	*internal_format = GL_BGRA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_BYTE;
	*has_alpha = FALSE;
	*needs_swap = is_big_endian;
	return TRUE;

    case PIXMAN_a8b8g8r8:
	*internal_format = GL_RGBA;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_BYTE;
	*needs_swap = is_big_endian;
	return TRUE;

    case PIXMAN_x8b8g8r8:
	*internal_format = GL_RGBA;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_BYTE;
	*has_alpha = FALSE;
	*needs_swap = is_big_endian;
	return TRUE;

    case PIXMAN_b8g8r8a8:
	*internal_format = GL_BGRA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_BYTE;
	*needs_swap = !is_big_endian;
	return TRUE;

    case PIXMAN_b8g8r8x8:
	*internal_format = GL_BGRA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_BYTE;
	*has_alpha = FALSE;
	*needs_swap = !is_big_endian;
	return TRUE;

    case PIXMAN_r8g8b8:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_BYTE;
	*needs_swap = !is_big_endian;
	return TRUE;

    case PIXMAN_b8g8r8:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_BYTE;
	*needs_swap = is_big_endian;
	return TRUE;

    case PIXMAN_r5g6b5:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_SHORT_5_6_5;
	*needs_swap = FALSE;
	return TRUE;

    case PIXMAN_b5g6r5:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_SHORT_5_6_5;
	*needs_swap = TRUE;
	return TRUE;

    case PIXMAN_a1b5g5r5:
	*internal_format = GL_RGBA;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_SHORT_5_5_5_1;
	*needs_swap = TRUE;
	return TRUE;

    case PIXMAN_x1b5g5r5:
	*internal_format = GL_RGBA;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_SHORT_5_5_5_1;
	*has_alpha = FALSE;
	*needs_swap = TRUE;
	return TRUE;

    case PIXMAN_a8:
	*internal_format = GL_ALPHA;
	*format = GL_ALPHA;
	*type = GL_UNSIGNED_BYTE;
	*needs_swap = FALSE;
	return TRUE;

    default:
	return FALSE;
    }
}

static cairo_bool_t
_cairo_gl_get_image_format_and_type_gl (pixman_format_code_t pixman_format,
					GLenum *internal_format, GLenum *format,
					GLenum *type, cairo_bool_t *has_alpha,
					cairo_bool_t *needs_swap)
{
    *has_alpha = TRUE;
    *needs_swap = FALSE;

    switch (pixman_format) {
    case PIXMAN_a8r8g8b8:
	*internal_format = GL_RGBA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_INT_8_8_8_8_REV;
	return TRUE;
    case PIXMAN_x8r8g8b8:
	*internal_format = GL_RGB;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_INT_8_8_8_8_REV;
	*has_alpha = FALSE;
	return TRUE;
    case PIXMAN_a8b8g8r8:
	*internal_format = GL_RGBA;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_INT_8_8_8_8_REV;
	return TRUE;
    case PIXMAN_x8b8g8r8:
	*internal_format = GL_RGB;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_INT_8_8_8_8_REV;
	*has_alpha = FALSE;
	return TRUE;
    case PIXMAN_b8g8r8a8:
	*internal_format = GL_RGBA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_INT_8_8_8_8;
	return TRUE;
    case PIXMAN_b8g8r8x8:
	*internal_format = GL_RGB;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_INT_8_8_8_8;
	*has_alpha = FALSE;
	return TRUE;
    case PIXMAN_r8g8b8:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_BYTE;
	return TRUE;
    case PIXMAN_b8g8r8:
	*internal_format = GL_RGB;
	*format = GL_BGR;
	*type = GL_UNSIGNED_BYTE;
	return TRUE;
    case PIXMAN_r5g6b5:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_SHORT_5_6_5;
	return TRUE;
    case PIXMAN_b5g6r5:
	*internal_format = GL_RGB;
	*format = GL_RGB;
	*type = GL_UNSIGNED_SHORT_5_6_5_REV;
	return TRUE;
    case PIXMAN_a1r5g5b5:
	*internal_format = GL_RGBA;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
	return TRUE;
    case PIXMAN_x1r5g5b5:
	*internal_format = GL_RGB;
	*format = GL_BGRA;
	*type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
	*has_alpha = FALSE;
	return TRUE;
    case PIXMAN_a1b5g5r5:
	*internal_format = GL_RGBA;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
	return TRUE;
    case PIXMAN_x1b5g5r5:
	*internal_format = GL_RGB;
	*format = GL_RGBA;
	*type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
	*has_alpha = FALSE;
	return TRUE;
    case PIXMAN_a8:
	*internal_format = GL_ALPHA;
	*format = GL_ALPHA;
	*type = GL_UNSIGNED_BYTE;
	return TRUE;

    case PIXMAN_a2b10g10r10:
    case PIXMAN_x2b10g10r10:
    case PIXMAN_a4r4g4b4:
    case PIXMAN_x4r4g4b4:
    case PIXMAN_a4b4g4r4:
    case PIXMAN_x4b4g4r4:
    case PIXMAN_r3g3b2:
    case PIXMAN_b2g3r3:
    case PIXMAN_a2r2g2b2:
    case PIXMAN_a2b2g2r2:
    case PIXMAN_c8:
    case PIXMAN_x4a4:
    /* case PIXMAN_x4c4: */
    case PIXMAN_x4g4:
    case PIXMAN_a4:
    case PIXMAN_r1g2b1:
    case PIXMAN_b1g2r1:
    case PIXMAN_a1r1g1b1:
    case PIXMAN_a1b1g1r1:
    case PIXMAN_c4:
    case PIXMAN_g4:
    case PIXMAN_a1:
    case PIXMAN_g1:
    case PIXMAN_yuy2:
    case PIXMAN_yv12:
    case PIXMAN_x2r10g10b10:
    case PIXMAN_a2r10g10b10:
    case PIXMAN_r8g8b8a8:
    case PIXMAN_r8g8b8x8:
    case PIXMAN_x14r6g6b6:
    default:
	return FALSE;
    }
}

/*
 * Extracts pixel data from an image surface.
 */
static cairo_status_t
_cairo_gl_surface_extract_image_data (cairo_image_surface_t *image,
				      int x, int y,
				      int width, int height,
				      void **output)
{
    int cpp = PIXMAN_FORMAT_BPP (image->pixman_format) / 8;
    char *data = _cairo_malloc_ab (width * height, cpp);
    char *dst = data;
    unsigned char *src = image->data + y * image->stride + x * cpp;
    int i;

    if (unlikely (data == NULL))
	return CAIRO_STATUS_NO_MEMORY;

    for (i = 0; i < height; i++) {
	memcpy (dst, src, width * cpp);
	src += image->stride;
	dst += width * cpp;
    }

    *output = data;

    return CAIRO_STATUS_SUCCESS;
}

cairo_bool_t
_cairo_gl_get_image_format_and_type (cairo_gl_flavor_t flavor,
				     pixman_format_code_t pixman_format,
				     GLenum *internal_format, GLenum *format,
				     GLenum *type, cairo_bool_t *has_alpha,
				     cairo_bool_t *needs_swap)
{
    if (flavor == CAIRO_GL_FLAVOR_DESKTOP)
	return _cairo_gl_get_image_format_and_type_gl (pixman_format,
						       internal_format, format,
						       type, has_alpha,
						       needs_swap);
    else
	return _cairo_gl_get_image_format_and_type_gles2 (pixman_format,
							  internal_format, format,
							  type, has_alpha,
							  needs_swap);

}

cairo_bool_t
_cairo_gl_operator_is_supported (cairo_operator_t op)
{
    return op < CAIRO_OPERATOR_SATURATE;
}

void
_cairo_gl_surface_init (cairo_device_t *device,
			cairo_gl_surface_t *surface,
			cairo_content_t content,
			int width, int height)
{
    cairo_gl_context_t *ctx;
    cairo_status_t status;
    _cairo_surface_init (&surface->base,
			 &_cairo_gl_surface_backend,
			 device,
			 content);

    surface->needs_update = FALSE;
	surface->tex_img = 0;
	surface->external_tex = FALSE;
	surface->width = surface->orig_width = width;
	surface->height = surface->orig_height = height;
	surface->scale_width = 1.0;
	surface->scale_height = 1.0;
	surface->fb = 0;
	surface->rb = 0;
	surface->ms_rb = 0;
	surface->ms_stencil_rb = 0;
	surface->ms_fb = 0;
	surface->require_aa = FALSE;
    surface->multisample_resolved = TRUE;
    status = _cairo_gl_context_acquire (device, &ctx);
    if (unlikely (status))
	return;

    status = _cairo_gl_context_release (ctx, status);
	surface->data_surface = NULL;
	surface->needs_new_data_surface = FALSE;

	surface->mask_surface = NULL;
	surface->parent_surface = NULL;
	surface->bound_fbo = FALSE;
    surface->tex_format = GL_RGBA; // default
    surface->clip = NULL;
    surface->clip_indices = 
        (cairo_gl_tristrip_indices_t *)malloc(sizeof(cairo_gl_tristrip_indices_t));
    status = _cairo_gl_tristrip_indices_init(surface->clip_indices);
    surface->stencil_buffer_changed = TRUE;
}

static cairo_surface_t *
_cairo_gl_surface_create_scratch_for_texture (cairo_gl_context_t   *ctx,
					      cairo_content_t	    content,
					      GLuint		    tex,
					      int		    width,
					      int		    height)
{
    cairo_gl_surface_t *surface;

    assert (width <= ctx->max_texture_size && height <= ctx->max_texture_size);

    surface = calloc (1, sizeof (cairo_gl_surface_t));
    if (unlikely (surface == NULL))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));

    _cairo_gl_surface_init (&ctx->base, surface, content, width, height);
    surface->tex = tex;

    /* Create the texture used to store the surface's data. */
    _cairo_gl_context_activate (ctx, CAIRO_GL_TEX_TEMP);
    glBindTexture (ctx->tex_target, surface->tex);
    glTexParameteri (ctx->tex_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri (ctx->tex_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    return &surface->base;
}

static cairo_surface_t *
_cairo_gl_surface_create_scratch (cairo_gl_context_t   *ctx,
				  cairo_content_t	content,
				  int			width,
				  int			height) 
{
    cairo_gl_surface_t *surface;
    GLenum format;
    GLuint tex;
    cairo_status_t status;

    glGenTextures (1, &tex);
    surface = (cairo_gl_surface_t *)
	_cairo_gl_surface_create_scratch_for_texture (ctx, content,
						      tex, width, height);
    if (unlikely (surface->base.status))
	return &surface->base;

    surface->owns_tex = TRUE;

    /* adjust the texture size after setting our real extents */
    if (width < 1)
	width = 1;
    if (height < 1)
	height = 1;

    switch (content) {
    default:
	ASSERT_NOT_REACHED;
    case CAIRO_CONTENT_COLOR_ALPHA:
	format = GL_RGBA;
	break;
    case CAIRO_CONTENT_ALPHA:
	/* We want to be trying GL_ALPHA framebuffer objects here. */
	//format = GL_RGBA;
	format = GL_ALPHA;
	break;
    case CAIRO_CONTENT_COLOR:
	/* GL_RGB is almost what we want here -- sampling 1 alpha when
	 * texturing, using 1 as destination alpha factor in blending,
	 * etc.  However, when filtering with GL_CLAMP_TO_BORDER, the
	 * alpha channel of the border color will also be clamped to
	 * 1, when we actually want the border color we explicitly
	 * specified.  So, we have to store RGBA, and fill the alpha
	 * channel with 1 when blending.
	 */
	format = GL_RGBA;
	break;
    }

    glTexImage2D (ctx->tex_target, 0, format, width, height, 0,
		  format, GL_UNSIGNED_BYTE, NULL);
    //glGenerateMipmap(GL_TEXTURE_2D);
	surface->data_surface = NULL;
	surface->needs_new_data_surface = FALSE;

	surface->mask_surface = NULL;
	surface->parent_surface = NULL;
	surface->bound_fbo = FALSE;
    surface->tex_format = format;
    
	surface->internal_format = format;
    surface->tex_format = format;
    return &surface->base;
}

cairo_status_t
_cairo_gl_surface_clear (cairo_gl_surface_t  *surface,
                         const cairo_color_t *color)
{
    cairo_gl_context_t *ctx;
    cairo_status_t status;
    double r, g, b, a;

    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely (status))
	return status;

    if(surface->multisample_resolved == FALSE)
        surface->require_aa = TRUE;
    else
        surface->require_aa = FALSE;
    _cairo_gl_context_set_destination (ctx, surface);

    if (surface->base.content & CAIRO_CONTENT_COLOR) {
        r = color->red   * color->alpha;
        g = color->green * color->alpha;
        b = color->blue  * color->alpha;
    } else {
        r = g = b = 0;
    }
    if (surface->base.content & CAIRO_CONTENT_ALPHA) {
        a = color->alpha;
    } else {
        a = 1.0;
    }
    _cairo_gl_disable_scissor_test(ctx);
    if(ctx->clear_red != r ||
       ctx->clear_green != g ||
       ctx->clear_blue != b ||
       ctx->clear_alpha != a)
    {
        glClearColor (r, g, b, a);
        ctx->clear_red = r;
        ctx->clear_green = g;
        ctx->clear_blue = b;
        ctx->clear_alpha = a;
    }
    glClear (GL_COLOR_BUFFER_BIT);
	surface->needs_new_data_surface = TRUE;
    return _cairo_gl_context_release (ctx, status);
}
cairo_surface_t *
_cairo_gl_surface_create (cairo_device_t		*abstract_device,
			 cairo_content_t	 content,
			 int			 width,
			 int			 height)
{
    cairo_gl_context_t *ctx;
    cairo_gl_surface_t *surface;
    cairo_status_t status;

    if (! CAIRO_CONTENT_VALID (content))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_INVALID_CONTENT));

    if (abstract_device == NULL) {
	return cairo_image_surface_create (_cairo_format_from_content (content),
					   width, height);
    }

    if (abstract_device->status)
	return _cairo_surface_create_in_error (abstract_device->status);

    if (abstract_device->backend->type != CAIRO_DEVICE_TYPE_GL)
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH));

    status = _cairo_gl_context_acquire (abstract_device, &ctx);
    if (unlikely (status))
	return _cairo_surface_create_in_error (status);

    surface = (cairo_gl_surface_t *)
	_cairo_gl_surface_create_scratch (ctx, content, width, height);
    if (unlikely (surface->base.status)) {
	status = _cairo_gl_context_release (ctx, surface->base.status);
	cairo_surface_destroy (&surface->base);
	return _cairo_surface_create_in_error (status);
    }

    /* Cairo surfaces start out initialized to transparent (black) */
	//if(content != CAIRO_CONTENT_ALPHA)
    	status = _cairo_gl_surface_clear (surface, CAIRO_COLOR_TRANSPARENT);
    if(unlikely(status))
    {
        cairo_status_t state = _cairo_gl_context_release(ctx, status);
        cairo_surface_destroy(&surface->base);
        return _cairo_surface_create_in_error(status);
    }
    status = _cairo_gl_context_release (ctx, status);
    if (unlikely (status)) {
	cairo_surface_destroy (&surface->base);
	return _cairo_surface_create_in_error (status);
    }

    return &surface->base;
}


cairo_surface_t *
cairo_gl_surface_create (cairo_device_t		*abstract_device,
			 cairo_content_t	 content,
			 int			 width,
			 int			 height)
{
    cairo_gl_context_t *ctx;
    cairo_gl_surface_t *surface;
    cairo_status_t status;

    if (! CAIRO_CONTENT_VALID (content))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_INVALID_CONTENT));

    if (abstract_device == NULL) {
	return cairo_image_surface_create (_cairo_format_from_content (content),
					   width, height);
    }

    if (abstract_device->status)
	return _cairo_surface_create_in_error (abstract_device->status);

    if (abstract_device->backend->type != CAIRO_DEVICE_TYPE_GL)
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH));

    status = _cairo_gl_context_acquire (abstract_device, &ctx);
    if (unlikely (status))
	return _cairo_surface_create_in_error (status);

    surface = (cairo_gl_surface_t *)
	_cairo_gl_surface_create_scratch (ctx, content, width, height);
    if (unlikely (surface->base.status)) {
	status = _cairo_gl_context_release (ctx, surface->base.status);
	cairo_surface_destroy (&surface->base);
	return _cairo_surface_create_in_error (status);
    }

    /* Cairo surfaces start out initialized to transparent (black) */
	if(content != CAIRO_CONTENT_ALPHA)
    	status = _cairo_gl_surface_clear (surface, CAIRO_COLOR_TRANSPARENT);
    if(unlikely(status))
    {
        cairo_status_t state = _cairo_gl_context_release(ctx, status);
        cairo_surface_destroy(&surface->base);
        return _cairo_surface_create_in_error(status);
    }
    status = _cairo_gl_context_release (ctx, status);
    if (unlikely (status)) {
	cairo_surface_destroy (&surface->base);
	return _cairo_surface_create_in_error (status);
    }

    return &surface->base;
}
slim_hidden_def (cairo_gl_surface_create);


/**
 * cairo_gl_surface_create_for_texture:
 * @content: type of content in the surface
 * @tex: name of texture to use for storage of surface pixels
 * @width: width of the surface, in pixels
 * @height: height of the surface, in pixels
 *
 * Creates a GL surface for the specified texture with the specified
 * content and dimensions.  The texture must be kept around until the
 * #cairo_surface_t is destroyed or cairo_surface_finish() is called
 * on the surface.  The initial contents of @tex will be used as the
 * initial image contents; you must explicitly clear the buffer,
 * using, for example, cairo_rectangle() and cairo_fill() if you want
 * it cleared.  The format of @tex should be compatible with @content,
 * in the sense that it must have the color components required by
 * @content.
 *
 * Return value: a pointer to the newly created surface. The caller
 * owns the surface and should call cairo_surface_destroy() when done
 * with it.
 *
 * This function always returns a valid pointer, but it will return a
 * pointer to a "nil" surface if an error such as out of memory
 * occurs. You can use cairo_surface_status() to check for this.
 **/
cairo_surface_t *
cairo_gl_surface_create_for_texture (cairo_device_t	*abstract_device,
				     cairo_content_t	 content,
				     unsigned int	 tex,
				     int		 width,
				     int		 height)
{
    cairo_gl_context_t *ctx;
    cairo_gl_surface_t *surface;
    cairo_status_t status;

    if (! CAIRO_CONTENT_VALID (content))
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_INVALID_CONTENT));

    if (abstract_device == NULL)
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NULL_POINTER));

    if (abstract_device->status)
	return _cairo_surface_create_in_error (abstract_device->status);

    if (abstract_device->backend->type != CAIRO_DEVICE_TYPE_GL)
	return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH));

    status = _cairo_gl_context_acquire (abstract_device, &ctx);
    if (unlikely (status))
	return _cairo_surface_create_in_error (status);
    
    surface = (cairo_gl_surface_t *)
	_cairo_gl_surface_create_scratch_for_texture (ctx, content,
						      tex, width, height);


	surface->external_tex = TRUE;
	surface->owns_tex = FALSE;

	surface->data_surface = NULL;
	surface->needs_new_data_surface = FALSE;

	surface->mask_surface = NULL;
	surface->parent_surface = NULL;
	surface->bound_fbo = FALSE;

    // internal format default to GL_RGBA
    surface->internal_format = GL_RGBA;
    surface->tex_format = GL_RGBA;
    /*status = _cairo_gl_ensure_framebuffer (ctx, surface);
    if(status != CAIRO_STATUS_SUCCESS)
        status = _cairo_surface_set_error(&surface->base, status);
    */
    status = _cairo_gl_context_release (ctx, status);
    return &surface->base;
}
slim_hidden_def (cairo_gl_surface_create_for_texture);

cairo_surface_t *
cairo_gl_surface_create_for_texture_with_internal_format(cairo_device_t *abstract_device,
	cairo_content_t content,
	unsigned int tex,
	int internal_format,
	int width, int height)
{
	cairo_status_t status;
	cairo_gl_surface_t *surface = (cairo_gl_surface_t *)
		cairo_gl_surface_create_for_texture(abstract_device, content,tex, 
			width, height);

	surface->internal_format = internal_format;
    surface->tex_format = internal_format;
    /*status = _cairo_gl_ensure_framebuffer (ctx, surface);
    if(status != CAIRO_STATUS_SUCCESS)
        status = _cairo_surface_set_error(&surface->base, status);
    */
	return &surface->base;
}
slim_hidden_def (cairo_gl_surface_create_for_texture_with_internal_format);

void
cairo_gl_surface_resolve(cairo_surface_t *surface)
{
    cairo_gl_surface_t *gl_surface = NULL;
    cairo_status_t status;
    cairo_gl_context_t *ctx;

    if (surface == NULL || surface->type != CAIRO_SURFACE_TYPE_GL)
        return;
    
    gl_surface = (cairo_gl_surface_t *)surface;
    if(gl_surface->multisample_resolved == TRUE)
        return;
    status = _cairo_gl_context_acquire (gl_surface->base.device, &ctx);
    if (unlikely (status))
        return;
    // blit multisample renderbuffer to texture
    _cairo_gl_context_blit_destination(ctx, gl_surface);
    status = _cairo_gl_context_release (ctx, status);
    if (status)
        status = _cairo_surface_set_error (surface, status);        
} 
slim_hidden_def (cairo_gl_surface_resolve);

void
cairo_gl_surface_set_size (cairo_surface_t *abstract_surface,
			   int              width,
			   int              height)
{
    cairo_gl_surface_t *surface = (cairo_gl_surface_t *) abstract_surface;

    if (unlikely (abstract_surface->status))
	return;
    if (unlikely (abstract_surface->finished)) {
        return;
    }

    if (! _cairo_surface_is_gl (abstract_surface) ||
        _cairo_gl_surface_is_texture (surface)) {
	return;
    }

    if (surface->width != width || surface->height != height) {
	surface->needs_update = TRUE;
	surface->width = surface->orig_width = width;
	surface->height = surface->orig_height = height;
	surface->scale_width = 1.0;
	surface->scale_height = 1.0;
    }
}

int
cairo_gl_surface_get_width (cairo_surface_t *abstract_surface)
{
    cairo_gl_surface_t *surface = (cairo_gl_surface_t *) abstract_surface;

    if (! _cairo_surface_is_gl (abstract_surface))
	return 0;

    return surface->orig_width;

}


int
cairo_gl_surface_get_height (cairo_surface_t *abstract_surface)
{
    cairo_gl_surface_t *surface = (cairo_gl_surface_t *) abstract_surface;

    if (! _cairo_surface_is_gl (abstract_surface))
	return 0;

    return surface->orig_height;
}

void
cairo_gl_surface_swapbuffers (cairo_surface_t *abstract_surface)
{
    cairo_gl_surface_t *surface = (cairo_gl_surface_t *) abstract_surface;
    cairo_status_t status;

    if (unlikely (abstract_surface->status))
	return;

    if (unlikely (abstract_surface->finished)) {
	status = _cairo_surface_set_error (abstract_surface,
		                           _cairo_error (CAIRO_STATUS_SURFACE_FINISHED));
        return;
    }
    if (! _cairo_surface_is_gl (abstract_surface)) {
	status = _cairo_surface_set_error (abstract_surface,
		                           CAIRO_STATUS_SURFACE_TYPE_MISMATCH);
	return;
    }

    if (! _cairo_gl_surface_is_texture (surface)) 
	{
		cairo_gl_context_t *ctx;
        status = _cairo_gl_context_acquire (surface->base.device, &ctx);
        if (unlikely (status))
            return;
        surface->require_aa = FALSE;
        _cairo_gl_context_set_destination(ctx, surface);

        cairo_surface_flush (abstract_surface);

	ctx->swap_buffers (ctx, surface);
  
        status = _cairo_gl_context_release (ctx, status);
        if (status)
            status = _cairo_surface_set_error (abstract_surface, status);         
    }
}

static cairo_surface_t *
_cairo_gl_surface_create_similar (void		 *abstract_surface,
				  cairo_content_t  content,
				  int		  width,
				  int		  height)
{
    cairo_surface_t *surface = abstract_surface;
    cairo_gl_context_t *ctx;
    cairo_status_t status;
	cairo_surface_t *orig_surface = abstract_surface;
	cairo_gl_surface_t *new_surface;

	int max_size;
	cairo_gl_surface_t *gl_surface = (cairo_gl_surface_t *)surface;


    if (width < 1 || height < 1)
        return cairo_image_surface_create (_cairo_format_from_content (content),
                                           width, height);
	max_size = _cairo_gl_surface_max_size(gl_surface);
	if(width > max_size || height > max_size)
		return _cairo_surface_create_in_error(CAIRO_STATUS_INVALID_SIZE);

    status = _cairo_gl_context_acquire (surface->device, &ctx);
    if (unlikely (status))
	return _cairo_surface_create_in_error (status);

    surface = _cairo_gl_surface_create_scratch (ctx, content, width, height);
	if(orig_surface->type == CAIRO_SURFACE_TYPE_GL)
	{
		if(gl_surface->owns_tex == TRUE && 
			surface->type == CAIRO_SURFACE_TYPE_GL)
		{
			new_surface = (cairo_gl_surface_t *)surface;
			new_surface->external_tex = gl_surface->external_tex;
		}
	}
  	status = _cairo_gl_surface_clear (surface, CAIRO_COLOR_TRANSPARENT);
    if(status != CAIRO_STATUS_SUCCESS)
    {
        status = _cairo_gl_context_release(ctx, status);
        cairo_surface_destroy(surface);
        return _cairo_surface_create_in_error(status);
    }

    status = _cairo_gl_context_release (ctx, status);
    if (unlikely (status)) {
        cairo_surface_destroy (surface);
        return _cairo_surface_create_in_error (status);
    }

    return surface;
}

// Henry Song
static cairo_gl_surface_t *
_cairo_gl_generate_clone(cairo_gl_surface_t *surface, cairo_surface_t *src, int extend)
{
	cairo_status_t status;
	int new_width, new_height;
	cairo_gl_surface_t *clone = NULL;
	cairo_surface_t *snapshot = NULL;
	cairo_image_surface_t *img_src = NULL;
    void *extra = NULL;
	int max_size = _cairo_gl_surface_max_size(surface);
	cairo_bool_t standard_npot = _cairo_gl_support_standard_npot(surface);

	if(cairo_surface_get_type(src) == CAIRO_SURFACE_TYPE_GL)
	{
		//cairo_gl_surface_t *s = (cairo_gl_surface_t *)src;
		if(extend == 0 || standard_npot)
			return (cairo_gl_surface_t *)cairo_surface_reference(src);
		else
		{
			cairo_surface_t *snapshot = _cairo_surface_has_snapshot((cairo_surface_t *)src, &_cairo_gl_surface_backend);
			if(snapshot != NULL)
			{
				return (cairo_gl_surface_t *)cairo_surface_reference(snapshot);
			}
			else // snapshot == NULL
			{
				// we need to generate a snapshot
				clone = _cairo_gl_surface_generate_npot_surface((cairo_gl_surface_t *)src);
				if(clone == NULL)
					return clone;

				if(&clone->base != src)
					_cairo_surface_attach_snapshot(src, &clone->base, _cairo_gl_surface_remove_from_cache);
				return (cairo_gl_surface_t *)cairo_surface_reference(clone);
			}
		}
	}

	// src is not cairo_gl_surface
	// we need to generate snapshot
	snapshot = _cairo_surface_has_snapshot((cairo_surface_t *)src, &_cairo_gl_surface_backend);
	if(snapshot != NULL)
	{
		// we have snapshot
		return _cairo_gl_generate_clone(surface, snapshot, extend);
	}

	if(_cairo_surface_is_recording(src))
	{
		cairo_rectangle_int_t recording_extents;
		cairo_color_t clear_color;
		cairo_bool_t bounded  = _cairo_surface_get_extents(src, &recording_extents);
		if(bounded == FALSE)
			clone = (cairo_gl_surface_t *)
				_cairo_gl_surface_create_similar(&surface->base, 
				src->content,
				surface->width, surface->height);
		else
		{
			new_width = recording_extents.width;
			new_height = recording_extents.height;
			if(new_height > max_size || new_width > max_size)
				return NULL;
			clone = (cairo_gl_surface_t *)
				_cairo_gl_surface_create_similar(&surface->base, 
				src->content,
				new_width, new_height);
		}
		if(unlikely(clone->base.status))
		{
			cairo_surface_destroy(&clone->base);
			return NULL;
		}
		clear_color.red = 0;
		clear_color.green = 0;
		clear_color.blue = 0;
		clear_color.alpha = 0;
		status = _cairo_gl_surface_clear (clone, &clear_color);
		status = _cairo_recording_surface_replay(src, &clone->base);
		if(unlikely(status))
		{
			cairo_surface_destroy(&clone->base);
			return NULL;
		}
	}
	else
	{
		status = _cairo_surface_acquire_source_image(src, &img_src, &extra);
		if(unlikely(status))
			return NULL;
		
		new_width = (max_size > img_src->width) ? img_src->width : max_size;
		new_height = (max_size > img_src->height) ? img_src->height : max_size;
		clone = (cairo_gl_surface_t *)
			_cairo_gl_surface_create_similar(&surface->base, 
				((cairo_surface_t *)img_src)->content,
				new_width, new_height);

		if(clone == NULL || cairo_surface_get_type(&clone->base) == CAIRO_SURFACE_TYPE_IMAGE)
		{
			if(cairo_surface_get_type(src) != CAIRO_SURFACE_TYPE_IMAGE)
                _cairo_surface_release_source_image(src, img_src, extra);
			if(clone != NULL)
			{
				cairo_surface_destroy(&clone->base);
			}
			
			return NULL;
		}
		status = _cairo_gl_surface_draw_image(clone, img_src, 0, 0,
			img_src->width, img_src->height, 0, 0, FALSE);
        if(cairo_surface_get_type(src) != CAIRO_SURFACE_TYPE_IMAGE)
            _cairo_surface_release_source_image(src, img_src, extra);
		
        if(unlikely (status))
		{
			cairo_surface_destroy(&clone->base);
			return NULL;
		}
	}

	// setup source surface snapshot of cloned gl surface
	_cairo_surface_attach_snapshot(src, &clone->base, _cairo_gl_surface_remove_from_cache);
	// now we have reference counte of 2 on clone
	return _cairo_gl_generate_clone(surface, &clone->base, extend);
}

/* this is a little tricky.  This function is only called in few places
   1. during gl_generate_clone
   2. release_dest_image
   3. clone_similar
   4. uploading text character
   
   This situation of drawing 2 images to a same texture will not happen.
   Therefore, we only need to deal with shrink image once.  
*/
cairo_status_t
_cairo_gl_surface_draw_image (cairo_gl_surface_t *dst,
			      cairo_image_surface_t *src,
			      int src_x, int src_y,
			      int width, int height,
			      int dst_x, int dst_y, cairo_bool_t keep_size)
{
    GLenum internal_format, format, type;
    cairo_bool_t has_alpha, needs_swap;
    cairo_image_surface_t *clone = NULL;
    cairo_gl_context_t *ctx;
    int cpp;
    cairo_status_t status = CAIRO_STATUS_SUCCESS;
	int max_size;
	cairo_image_surface_t *shrink_src = NULL;
	float scale_x; 
	float scale_y;
	cairo_t *cr;

	//int new_width, new_height;
	//int new_dst_x, new_dst_y;

	max_size = _cairo_gl_surface_max_size(dst);
	if(dst_x + width > max_size || dst_y + height > max_size) {
		if(keep_size == TRUE)
			return CAIRO_STATUS_INVALID_SIZE;
		scale_x = 1.0;
		scale_y = 1.0;
		if(dst_x + width > max_size)
			scale_x = (float)max_size / (float)(dst_x + width);
		if(dst_y + height > max_size)
			scale_y = max_size / (float)(dst_y + height);

		width *= scale_x;
		height *= scale_y;
		dst_x *= scale_x;
		dst_y *= scale_y;
		src_x *= scale_x;
		src_y *= scale_y;

		// shrink image
		shrink_src = 
			(cairo_image_surface_t *)cairo_surface_create_similar(
				&src->base, cairo_surface_get_content(&src->base),
				width, height);
		cr = cairo_create(&shrink_src->base);
		cairo_scale(cr, scale_x, scale_y);
		cairo_set_source_surface(cr, &src->base, 0, 0);
		cairo_paint(cr);
		cairo_destroy(cr);
		src = shrink_src;
		dst->orig_width /= scale_x;
		dst->orig_height /= scale_y;
		dst->scale_width = scale_x;
		dst->scale_height = scale_y;
	}

    status = _cairo_gl_context_acquire (dst->base.device, &ctx);
    if (unlikely (status))
	return status;

    if (! _cairo_gl_get_image_format_and_type (ctx->gl_flavor,
					       src->pixman_format,
					       &internal_format,
					       &format,
					       &type,
					       &has_alpha,
					       &needs_swap))
    {
	cairo_bool_t is_supported;

	clone = _cairo_image_surface_coerce (src);
	if (unlikely (status = clone->base.status))
	    goto FAIL;

	is_supported =
	    _cairo_gl_get_image_format_and_type (ctx->gl_flavor,
						 clone->pixman_format,
		                                 &internal_format,
						 &format,
						 &type,
						 &has_alpha,
						 &needs_swap);
	assert (is_supported);
	assert (!needs_swap);
	src = clone;
    }

    cpp = PIXMAN_FORMAT_BPP (src->pixman_format) / 8;

    status = _cairo_gl_surface_flush (&dst->base);
    if (unlikely (status))
	goto FAIL;

    if (ctx->gl_flavor == CAIRO_GL_FLAVOR_DESKTOP)
	glPixelStorei (GL_UNPACK_ROW_LENGTH, src->stride / cpp);
    if (_cairo_gl_surface_is_texture (dst)) {
	void *data_start = src->data + src_y * src->stride + src_x * cpp;
	void *data_start_gles2 = NULL;

	/*
	 * Due to GL_UNPACK_ROW_LENGTH missing in GLES2 we have to extract the
	 * image data ourselves in some cases. In particular, we must extract
	 * the pixels if:
	 * a. we don't want full-length lines or
	 * b. the row stride cannot be handled by GL itself using a 4 byte alignment
	 *    constraint
	 */
    
    // for GLES, we need to check internal format match tex format
    if (ctx->gl_flavor == CAIRO_GL_FLAVOR_ES) {
        if(format != dst->tex_format && 
           dst->owns_tex == TRUE &&
           dst->tex != 0) {
            glDeleteTextures(1, &dst->tex);
            if (dst->depth)
                ctx->dispatch.DeleteFramebuffers (1, &dst->depth);
            if (dst->fb)
	        {
                ctx->dispatch.DeleteFramebuffers (1, &dst->fb);
		        dst->fb = 0;
	        }
	        if(dst->rb)
	        {
	        	ctx->dispatch.DeleteRenderbuffers(1, &dst->rb);
		        dst->rb = 0;
	        }
            glGenTextures(1, &dst->tex);
            _cairo_gl_context_activate (ctx, CAIRO_GL_TEX_TEMP);
            glBindTexture (ctx->tex_target, dst->tex);
            glTexParameteri (ctx->tex_target, GL_TEXTURE_MIN_FILTER, 
                             GL_NEAREST);
            glTexParameteri (ctx->tex_target, GL_TEXTURE_MAG_FILTER, 
                             GL_NEAREST);
            glTexImage2D (ctx->tex_target, 0, format, dst->width, 
                          dst->height, 0,
		                  format, GL_UNSIGNED_BYTE, NULL);
            //glGenerateMipmap(GL_TEXTURE_2D);
            dst->tex_format = format;
        }
    }
	if (ctx->gl_flavor == CAIRO_GL_FLAVOR_ES &&
	    (src->width * cpp < src->stride - 3 ||
	     width != src->width))
	{
	    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
	    status = _cairo_gl_surface_extract_image_data (src, src_x, src_y,
							   width, height,
							   &data_start_gles2);
	    if (unlikely (status))
		goto FAIL;
	}
	else
	{
	    glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
	}
    _cairo_gl_context_activate (ctx, CAIRO_GL_TEX_TEMP);
	glBindTexture (ctx->tex_target, dst->tex);
	glTexParameteri (ctx->tex_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri (ctx->tex_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexSubImage2D (ctx->tex_target, 0,
			 dst_x, dst_y, width, height,
			 format, type,
			 data_start_gles2 != NULL ? data_start_gles2 :
						    data_start);
    //glGenerateMipmap(GL_TEXTURE_2D);
	if (data_start_gles2)
	    free (data_start_gles2);

	/* If we just treated some rgb-only data as rgba, then we have to
	 * go back and fix up the alpha channel where we filled in this
	 * texture data.
	 */
	dst->needs_new_data_surface = TRUE;
	if (!has_alpha) {
	    cairo_rectangle_int_t rect;

	    rect.x = dst_x;
	    rect.y = dst_y;
	    rect.width = width;
	    rect.height = height;

            _cairo_gl_composite_flush (ctx);
	    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
	    _cairo_gl_surface_fill_rectangles (dst,
					       CAIRO_OPERATOR_SOURCE,
					       CAIRO_COLOR_BLACK,
					       &rect, 1);
            _cairo_gl_composite_flush (ctx);
	    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}
    } else {
		goto FAIL;
	}

FAIL:
    if (ctx->gl_flavor == CAIRO_GL_FLAVOR_DESKTOP)
	glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);

    status = _cairo_gl_context_release (ctx, status);

    if (clone)
        cairo_surface_destroy (&clone->base);
	if (shrink_src)
		cairo_surface_destroy (&shrink_src->base);

    return status;
}

static cairo_status_t
_cairo_gl_surface_get_image (cairo_gl_surface_t      *surface,
			     cairo_rectangle_int_t   *interest,
			     cairo_image_surface_t  **image_out,
			     cairo_rectangle_int_t   *rect_out)
{
    cairo_image_surface_t *image;
    cairo_gl_context_t *ctx;
    GLenum format, type;
    pixman_format_code_t pixman_format;
    unsigned int cpp;
    cairo_status_t status;
    cairo_image_surface_t *argb32_image;
	int width, height;
	int x, y;
	cairo_image_surface_t *enlarge_image = NULL;
	cairo_t *cr;

    /* Want to use a switch statement here but the compiler gets whiny. */
    if (surface->base.content == CAIRO_CONTENT_COLOR_ALPHA) {
	format = GL_BGRA;
	pixman_format = PIXMAN_a8r8g8b8;
	type = GL_UNSIGNED_INT_8_8_8_8_REV;
	cpp = 4;
    } else if (surface->base.content == CAIRO_CONTENT_COLOR) {
	format = GL_BGRA;
	pixman_format = PIXMAN_x8r8g8b8;
	type = GL_UNSIGNED_INT_8_8_8_8_REV;
	cpp = 4;
    } else if (surface->base.content == CAIRO_CONTENT_ALPHA) {
	format = GL_ALPHA;
	pixman_format = PIXMAN_a8;
	type = GL_UNSIGNED_BYTE;
	cpp = 1;
    } else {
	ASSERT_NOT_REACHED;
	return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely (status))
        return status;

    /*
     * GLES2 supports only RGBA, UNSIGNED_BYTE so use that.
     * We are also using this format for ALPHA as GLES2 does not
     * support GL_PACK_ROW_LENGTH anyway, and this makes sure that the
     * pixman image that is created has row_stride = row_width * bpp.
     */
    if (ctx->gl_flavor == CAIRO_GL_FLAVOR_ES) {
	format = GL_RGBA;
	if (!_cairo_is_little_endian ()) {
	    ASSERT_NOT_REACHED;
	    /* TODO: Add r8g8b8a8 support to pixman and enable this
	       if (surface->base.content == CAIRO_CONTENT_COLOR)
	       pixman_format = PIXMAN_r8g8b8x8;
	       else
	       pixman_format = PIXMAN_r8g8b8a8;
	    */
	} else {
	    if (surface->base.content == CAIRO_CONTENT_COLOR) {
		pixman_format = PIXMAN_x8b8g8r8;
		//pixman_format = PIXMAN_x8r8g8b8;
		cpp = 4;
	    } else if(surface->base.content == CAIRO_CONTENT_COLOR_ALPHA) {
		pixman_format = PIXMAN_a8b8g8r8;
		//pixman_format = PIXMAN_a8r8g8b8;
		cpp = 4;
	    } else {
		format = GL_ALPHA;
		pixman_format = PIXMAN_a8;
		cpp = 1;
	    }
	}
	type = GL_UNSIGNED_BYTE;
    }

	width = interest->width;
	height = interest->height;
	x = interest->x;
	y = interest->y;

    image = (cairo_image_surface_t*)
	_cairo_image_surface_create_with_pixman_format (NULL,
							pixman_format,
							interest->width,
							interest->height,
							-1);
    if (unlikely (image->base.status))
	return _cairo_gl_context_release (ctx, image->base.status);

    /* This is inefficient, as we'd rather just read the thing without making
     * it the destination.  But then, this is the fallback path, so let's not
     * fall back instead.
     */
    _cairo_gl_composite_flush (ctx);
   
    // force blit to texture
    if(surface->multisample_resolved == FALSE)
        _cairo_gl_context_blit_destination(ctx, surface);
    surface->require_aa = FALSE;
    _cairo_gl_context_set_destination (ctx, surface);

    glPixelStorei (GL_PACK_ALIGNMENT, 4);
    if (ctx->gl_flavor == CAIRO_GL_FLAVOR_DESKTOP)
	glPixelStorei (GL_PACK_ROW_LENGTH, image->stride / cpp);
    if (! _cairo_gl_surface_is_texture (surface) &&
	ctx->has_mesa_pack_invert)
	glPixelStorei (GL_PACK_INVERT_MESA, 1);
    glReadPixels (interest->x, interest->y,
		  interest->width, interest->height,
		  format, type, image->data);
    if (! _cairo_gl_surface_is_texture (surface) &&
	ctx->has_mesa_pack_invert)
	glPixelStorei (GL_PACK_INVERT_MESA, 0);

    status = _cairo_gl_context_release (ctx, status);
    if (unlikely (status)) {
	cairo_surface_destroy (&image->base);
	return status;
    }

	// we need to check whether we need to enlarge image
    if(surface->scale_width != 1.0 || surface->scale_height != 1.0)
	{
		enlarge_image = (cairo_image_surface_t *)
			cairo_surface_create_similar(&image->base,
				cairo_surface_get_content(&image->base),
				surface->orig_width, surface->orig_height);
		cr = cairo_create(&enlarge_image->base);
		cairo_scale(cr, 1.0 / surface->scale_width, 1.0 / surface->scale_height);
    
		cairo_set_source_surface(cr, &image->base, 0, 0);
		cairo_paint(cr);
		cairo_destroy(cr);
		cairo_surface_destroy(&image->base);
		image = enlarge_image;
	}
	argb32_image = _cairo_image_surface_coerce_to_format(image, CAIRO_FORMAT_ARGB32);
	cairo_surface_destroy(&image->base);
	image = argb32_image;
	image->base.is_clear = 0;

	image->base.is_clear = 0;

    *image_out = image;
    if (rect_out != NULL)
	*rect_out = *interest;

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_gl_surface_finish (void *abstract_surface)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_status_t status;
    cairo_gl_context_t *ctx = NULL;

    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely (status))
        return status;

    if (ctx->operands[CAIRO_GL_TEX_SOURCE].type == CAIRO_GL_OPERAND_TEXTURE &&
        ctx->operands[CAIRO_GL_TEX_SOURCE].texture.surface == surface)
        _cairo_gl_context_destroy_operand (ctx, CAIRO_GL_TEX_SOURCE);
    if (ctx->operands[CAIRO_GL_TEX_MASK].type == CAIRO_GL_OPERAND_TEXTURE &&
        ctx->operands[CAIRO_GL_TEX_MASK].texture.surface == surface)
        _cairo_gl_context_destroy_operand (ctx, CAIRO_GL_TEX_MASK);
    if (ctx->current_target == surface)
	ctx->current_target = NULL;

    if (surface->depth)
        ctx->dispatch.DeleteFramebuffers (1, &surface->depth);
    if (surface->fb)
	{
        ctx->dispatch.DeleteFramebuffers (1, &surface->fb);
		surface->fb = 0;
	}
	if(surface->rb)
	{
		ctx->dispatch.DeleteRenderbuffers(1, &surface->rb);
		surface->rb = 0;
	}
	if(surface->ms_rb)
	{
		ctx->dispatch.DeleteRenderbuffers(1, &surface->ms_rb);
		surface->ms_rb = 0;
	}
	if(surface->ms_stencil_rb)
	{
		ctx->dispatch.DeleteRenderbuffers(1, &surface->ms_stencil_rb);
		surface->ms_stencil_rb = 0;
	}
	if(surface->ms_fb)
	{
		ctx->dispatch.DeleteFramebuffers(1, &surface->ms_fb);
		surface->ms_fb = 0;
	}
    if (surface->owns_tex)
	{
		glDeleteTextures (1, &surface->tex);
		surface->tex = 0;
	}
	if(surface->tex_img != 0)
		glDeleteTextures(1, (GLuint*)&surface->tex_img);

	surface->external_tex = FALSE;

	if(surface->data_surface != NULL)
		cairo_surface_destroy(surface->data_surface);
	if(surface->mask_surface != NULL)
		cairo_surface_destroy(&(surface->mask_surface->base));
	surface->parent_surface = NULL;
		
	surface->needs_new_data_surface = FALSE;
    _cairo_gl_tristrip_indices_destroy(surface->clip_indices);
    free(surface->clip_indices);
    surface->clip = NULL;
    surface->require_aa = FALSE;
    surface->multisample_resolved = TRUE;

    return _cairo_gl_context_release (ctx, status);
}

static cairo_status_t
_cairo_gl_surface_acquire_source_image (void		       *abstract_surface,
					cairo_image_surface_t **image_out,
					void		      **image_extra)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_rectangle_int_t extents;

    *image_extra = NULL;

    extents.x = extents.y = 0;
    extents.width = surface->orig_width;
    extents.height = surface->orig_height;
    return _cairo_gl_surface_get_image (surface, &extents, image_out, NULL);
}

static void
_cairo_gl_surface_release_source_image (void		      *abstract_surface,
					cairo_image_surface_t *image,
					void		      *image_extra)
{
    cairo_surface_destroy (&image->base);
}

static cairo_status_t
_cairo_gl_surface_acquire_dest_image (void		      *abstract_surface,
				      cairo_rectangle_int_t   *interest_rect,
				      cairo_image_surface_t  **image_out,
				      cairo_rectangle_int_t   *image_rect_out,
				      void		     **image_extra)
{
    cairo_gl_surface_t *surface = abstract_surface;

    *image_extra = NULL;
    return _cairo_gl_surface_get_image (surface, interest_rect, image_out,
					image_rect_out);
}

static void
_cairo_gl_surface_release_dest_image (void		      *abstract_surface,
				      cairo_rectangle_int_t   *interest_rect,
				      cairo_image_surface_t   *image,
				      cairo_rectangle_int_t   *image_rect,
				      void		      *image_extra)
{
    cairo_status_t status;

	status = _cairo_gl_surface_draw_image(abstract_surface, image,
					   0, 0,
					   image->width, image->height,
					   image_rect->x, image_rect->y, TRUE);
    /* as we created the image, its format should be directly applicable */
    //assert (status == CAIRO_STATUS_SUCCESS);

    cairo_surface_destroy (&image->base);
}

static cairo_status_t
_cairo_gl_surface_clone_similar (void		     *abstract_surface,
				 cairo_surface_t     *src,
				 int                  src_x,
				 int                  src_y,
				 int                  width,
				 int                  height,
				 int                 *clone_offset_x,
				 int                 *clone_offset_y,
				 cairo_surface_t    **clone_out)
{
    cairo_gl_surface_t *surface = abstract_surface;

    /* XXX: Use GLCopyTexImage2D to clone non-texture-surfaces */
    if (src->device == surface->base.device &&
        _cairo_gl_surface_is_texture ((cairo_gl_surface_t *) src)) {
	*clone_offset_x = 0;
	*clone_offset_y = 0;
	*clone_out = cairo_surface_reference (src);

	return CAIRO_STATUS_SUCCESS;
    } else if (_cairo_surface_is_image (src)) {
	cairo_image_surface_t *image_src = (cairo_image_surface_t *)src;
	cairo_gl_surface_t *clone;
	cairo_status_t status;

	clone = (cairo_gl_surface_t *)
	    _cairo_gl_surface_create_similar (&surface->base,
		                              src->content,
					      width, height);
	if (clone == NULL || cairo_surface_get_type((cairo_surface_t*)clone) == CAIRO_SURFACE_TYPE_IMAGE)
	    return UNSUPPORTED ("create_similar failed");
	if (clone->base.status)
	    return clone->base.status;

	status = _cairo_gl_surface_draw_image (clone, image_src,
					       src_x, src_y,
					       width, height,
					       0, 0, TRUE);
	if (status) {
	    cairo_surface_destroy (&clone->base);
	    return status;
	}

	*clone_out = &clone->base;
	*clone_offset_x = src_x;
	*clone_offset_y = src_y;

	return CAIRO_STATUS_SUCCESS;
    }

    return UNSUPPORTED ("unknown src surface type in clone_similar");
}

static cairo_int_status_t
_cairo_gl_surface_composite (cairo_operator_t		  op,
			     const cairo_pattern_t	 *src,
			     const cairo_pattern_t	 *mask,
			     void			 *abstract_dst,
			     int			  src_x,
			     int			  src_y,
			     int			  mask_x,
			     int			  mask_y,
			     int			  dst_x,
			     int			  dst_y,
			     unsigned int		  width,
			     unsigned int		  height,
			     cairo_region_t		 *clip_region)
{
	// src->matrix contains inversed matrix for vertex translation
    cairo_gl_surface_t *dst = abstract_dst;
    cairo_gl_context_t *ctx;
    cairo_status_t status;
    cairo_gl_composite_t setup;
    cairo_rectangle_int_t rect = { dst_x, dst_y, width, height };
    int dx, dy;

    if (op == CAIRO_OPERATOR_SOURCE &&
        mask == NULL &&
        src->type == CAIRO_PATTERN_TYPE_SURFACE &&
        _cairo_surface_is_image (((cairo_surface_pattern_t *) src)->surface) &&
        _cairo_matrix_is_integer_translation (&src->matrix, &dx, &dy)) {
        cairo_image_surface_t *image = (cairo_image_surface_t *)
            ((cairo_surface_pattern_t *) src)->surface;
        dx += src_x;
        dy += src_y;
        if (dx >= 0 &&
            dy >= 0 &&
            dx + width <= (unsigned int) image->width &&
            dy + height <= (unsigned int) image->height) {
            status = _cairo_gl_surface_draw_image (dst, image,
                                                   dx, dy,
                                                   width, height,
                                                   dst_x, dst_y, TRUE);
            if (status != CAIRO_STATUS_SUCCESS)
                return status;
        }
    }

    status = _cairo_gl_composite_init (&setup, op, dst,
                                       mask && mask->has_component_alpha,
                                       &rect);
    if (unlikely (status))
        goto CLEANUP;
	

    status = _cairo_gl_composite_set_source (&setup, src,
                                             src_x, src_y,
                                             dst_x, dst_y,
                                             width, height,
											 0, 0, 0);
    if (unlikely (status))
        goto CLEANUP;

    status = _cairo_gl_composite_set_mask (&setup, mask,
                                           mask_x, mask_y,
                                           dst_x, dst_y,
                                           width, height,
										   0, 0, 0);
    if (unlikely (status))
        goto CLEANUP;

    status = _cairo_gl_composite_begin (&setup, &ctx);
    if (unlikely (status))
	goto CLEANUP;

    if (clip_region != NULL) {
        int i, num_rectangles;

        num_rectangles = cairo_region_num_rectangles (clip_region);

	for (i = 0; i < num_rectangles; i++) {
	    cairo_rectangle_int_t rect;

	    cairo_region_get_rectangle (clip_region, i, &rect);
            _cairo_gl_composite_emit_rect (ctx,
                                           rect.x,              rect.y,
                                           rect.x + rect.width, rect.y + rect.height,
                                           0);
	}
    } else {
        _cairo_gl_composite_emit_rect (ctx,
                                       dst_x,         dst_y,
                                       dst_x + width, dst_y + height,
                                       0);
    }

    status = _cairo_gl_context_release (ctx, status);

  CLEANUP:
    _cairo_gl_composite_fini (&setup);

    return status;
}

static cairo_int_status_t
_cairo_gl_surface_fill_rectangles (void			   *abstract_dst,
				   cairo_operator_t	    op,
				   const cairo_color_t     *color,
				   cairo_rectangle_int_t   *rects,
				   int			    num_rects)
{
    cairo_gl_surface_t *dst = abstract_dst;
    cairo_solid_pattern_t solid;
    cairo_gl_context_t *ctx;
    cairo_status_t status;
    cairo_gl_composite_t setup;
    int i;
	cairo_point_t points[4];
	cairo_gl_tristrip_indices_t indices;
	double x, y;

	status = _cairo_gl_context_acquire(dst->base.device, &ctx);
	if(unlikely(status))
		goto CLEANUP;

    status = _cairo_gl_composite_init (&setup, op, dst,
                                       FALSE,
                                       /* XXX */ NULL);
    if (unlikely (status))
        goto CLEANUP;
	setup.ctx = ctx;
    
    // force blit to destination
    if(dst->multisample_resolved == FALSE)
        _cairo_gl_context_blit_destination(ctx, dst);
    dst->require_aa = FALSE;
	_cairo_gl_context_set_destination(ctx, dst);

	setup.src.type = CAIRO_GL_OPERAND_CONSTANT;

    _cairo_pattern_init_solid (&solid, color);
	setup.source = &(solid.base);
    status = _cairo_gl_composite_set_source (&setup, &solid.base,
                                             0, 0,
                                             0, 0,
                                             0, 0, 
											 0, 0, 0);
    if (unlikely (status))
        goto CLEANUP;

    status = _cairo_gl_composite_set_mask (&setup, NULL,
                                           0, 0,
                                           0, 0,
                                           0, 0, 
										   0, 0, 0);
    if (unlikely (status))
        goto CLEANUP;

	status = _cairo_gl_tristrip_indices_init (&indices);
	indices.setup = &setup;

    for (i = 0; i < num_rects; i++) 
	{
		x = rects[i].x;
		y = rects[i].y;
		points[0].x = _cairo_fixed_from_double(x);
		points[0].y = _cairo_fixed_from_double(y);
		points[1].x = _cairo_fixed_from_double(x);
		points[1].y = _cairo_fixed_from_double(y + rects[i].height);
		points[2].x = _cairo_fixed_from_double(x + rects[i].width);
		points[2].y = _cairo_fixed_from_double(y + rects[i].height);
		points[3].x = _cairo_fixed_from_double(x + rects[i].width);
		points[3].y = _cairo_fixed_from_double(y);
		status = _cairo_gl_tristrip_indices_add_quad (&indices, points);
		if(unlikely(status))
		{
			_cairo_gl_tristrip_indices_destroy (&indices);
			goto CLEANUP;
		}
    }
	status = _cairo_gl_fill(&indices);
	_cairo_gl_tristrip_indices_destroy (&indices);

	dst->needs_new_data_surface = TRUE;
  CLEANUP:
    status = _cairo_gl_context_release (ctx, status);
    _cairo_gl_composite_fini (&setup);

    return status;
}

typedef struct _cairo_gl_surface_span_renderer {
    cairo_span_renderer_t base;

    cairo_gl_composite_t setup;

    int xmin, xmax;
    int ymin, ymax;

    cairo_gl_context_t *ctx;
} cairo_gl_surface_span_renderer_t;

static cairo_bool_t
_cairo_gl_surface_get_extents (void		     *abstract_surface,
			       cairo_rectangle_int_t *rectangle)
{
    cairo_gl_surface_t *surface = abstract_surface;

    rectangle->x = 0;
    rectangle->y = 0;
    rectangle->width  = surface->orig_width;
    rectangle->height = surface->orig_height;

    return TRUE;
}

static void
_cairo_gl_surface_get_font_options (void                  *abstract_surface,
				    cairo_font_options_t  *options)
{
    _cairo_font_options_init_default (options);

    cairo_font_options_set_hint_metrics (options, CAIRO_HINT_METRICS_ON);
    _cairo_font_options_set_round_glyph_positions (options, CAIRO_ROUND_GLYPH_POS_ON);
}

static cairo_status_t
_cairo_gl_surface_flush (void *abstract_surface)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_status_t status;
    cairo_gl_context_t *ctx;

    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely (status))
        return status;

    if ((ctx->operands[CAIRO_GL_TEX_SOURCE].type == CAIRO_GL_OPERAND_TEXTURE &&
         ctx->operands[CAIRO_GL_TEX_SOURCE].texture.surface == surface) ||
        (ctx->operands[CAIRO_GL_TEX_MASK].type == CAIRO_GL_OPERAND_TEXTURE &&
         ctx->operands[CAIRO_GL_TEX_MASK].texture.surface == surface) ||
        (ctx->current_target == surface))
      _cairo_gl_composite_flush (ctx);

    return _cairo_gl_context_release (ctx, status);
}

static cairo_int_status_t
_cairo_gl_surface_paint (void *abstract_surface,
			 cairo_operator_t	 op,
			 const cairo_pattern_t *source,
			 cairo_clip_t	    *clip)
{
	// clip path is already scaled
	//cairo_rectangle_int_t source_extents, *clip_extents = NULL;
	//cairo_gl_composite_t *setup;
	//cairo_gl_context_t *ctx;
	//char *colors;
	//int stride;
	//GLfloat stencil_color[] ={ 0, 0, 0, 1};
	//cairo_image_surface_t *image_surface;
	//void *image_extra;

	//cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;
    
    /* simplify the common case of clearing the surface */
    if (clip == NULL) {
        if (op == CAIRO_OPERATOR_CLEAR)
		{
			return _cairo_gl_surface_clear(abstract_surface, CAIRO_COLOR_TRANSPARENT);
		}
       else if (source->type == CAIRO_PATTERN_TYPE_SOLID &&
                (op == CAIRO_OPERATOR_SOURCE ||
                 (op == CAIRO_OPERATOR_OVER && _cairo_pattern_is_opaque_solid (source)))) 
		{
           	return _cairo_gl_surface_clear (abstract_surface,
                                            &((cairo_solid_pattern_t *) source)->color);
        }
    }
	return _cairo_gl_surface_mask(abstract_surface,
	op,
	source,
	NULL,
	clip);
}

static void
map_vertices_to_surface_space (GLfloat			*coords,
			       int			 coord_count,
			       cairo_gl_surface_t	*surface,
			       const cairo_matrix_t	*matrix,
			       GLfloat			*surface_coords)
{
    int i;
    cairo_matrix_t scale_matrix = *matrix;
    cairo_matrix_t m;
    cairo_matrix_init_scale(&m, 
                            1.0 / surface->orig_width, 
                            1.0 / surface->orig_height);
    cairo_matrix_multiply(&scale_matrix, &scale_matrix, &m);
    for (i = 0; i < coord_count; i++) {
	double x = coords[i * 2];
	double y = coords[(i * 2) + 1];
	cairo_matrix_transform_point(&scale_matrix, &x, &y);
	surface_coords[i * 2] = x;
	surface_coords[(i * 2) + 1] = y;
    }
}

static cairo_int_status_t
_cairo_gl_surface_mask (void *abstract_surface,
	cairo_operator_t op,
	const cairo_pattern_t *source,
	const cairo_pattern_t *mask,
	cairo_clip_t *clip)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_composite_rectangles_t extents; 
    cairo_status_t status;
    cairo_gl_composite_t *setup = NULL;
    cairo_gl_context_t *ctx = NULL;
    cairo_gl_surface_t *clone = NULL;
    cairo_gl_surface_t *mask_clone = NULL;
    GLfloat vertices[] = {0, 0, 0, 0, 0, 0, 0, 0};
	GLfloat texture_coordinates[8];
	GLfloat mask_texture_coords[8];
    cairo_clip_t *clip_pt = clip;
    cairo_bool_t done_clip = FALSE;
    cairo_rectangle_int_t surface_rect;
    
    if (mask == NULL)
		status = _cairo_composite_rectangles_init_for_paint(&extents,
			surface->width,
			surface->height,
			op, source, clip);
	else {
		if (op == CAIRO_OPERATOR_IN ||
		   op == CAIRO_OPERATOR_OUT ||
		   op == CAIRO_OPERATOR_DEST_IN ||
		   op == CAIRO_OPERATOR_DEST_ATOP)
			status = _cairo_composite_rectangles_init_for_paint(&extents,
				surface->width,
				surface->height,
				op, source, clip);
		else
			status = _cairo_composite_rectangles_init_for_mask(&extents,
				surface->width,
				surface->height,
				op, source, mask,
				clip);
	}

	
	if (unlikely(status))
		return status;
   
    if(source->matrix.xy == 0)
        surface->require_aa = FALSE;
    surface_rect.x = extents.bounded.x;
    surface_rect.y = extents.bounded.y;
    surface_rect.width = extents.bounded.width;
    surface_rect.height = extents.bounded.height;
    _cairo_composite_rectangles_fini(&extents);
    
    /*if(clip_pt != NULL && clip_pt->path == NULL && clip_pt->num_boxes == 1)
    {
        glEnable(GL_SCISSOR_TEST);
        if(_cairo_gl_surface_is_texture(surface))
        glScissor(clip_pt->extents.x, clip_pt->extents.y,
              clip_pt->extents.width, clip_pt->extents.height);
        else
        glScissor(clip_pt->extents.x, 
                surface->height - clip_pt->extents.y - clip_pt->extents.height,
              clip_pt->extents.width, clip_pt->extents.height);
        clip_pt = NULL;
    }
    else
    {
        //glEnable(GL_SCISSOR_TEST);
        //glScissor(surface_rect.x, surface_rect.y,
        //      surface_rect.width, surface_rect.height);
        glDisable(GL_SCISSOR_TEST);
    }*/
        
    if(clip_pt != NULL && _cairo_gl_clip_contains_rectangle(clip_pt, &surface_rect))
    {
        clip_pt = NULL;
    }

	//printf("get rectangle extents %ld usec\n", _get_tick() - now);
	// upload image
	// check has snapsot
	if (source->type == CAIRO_PATTERN_TYPE_SURFACE) {
		cairo_surface_t *src = ((cairo_surface_pattern_t *)source)->surface;
		cairo_bool_t extend = source->extend == CAIRO_EXTEND_REPEAT ||
				      source->extend == CAIRO_EXTEND_REFLECT;
		clone = _cairo_gl_generate_clone(surface, src, extend);
		if (clone == NULL) {
			return UNSUPPORTED("create_clone failed");
        }
        // for source to blit to texture
	}

	setup = (cairo_gl_composite_t *)malloc(sizeof(cairo_gl_composite_t));
	status = _cairo_gl_composite_init(setup, op, surface, FALSE, &extents.bounded);
	if (unlikely (status))
		goto FINISH;

	if (mask != NULL && mask->type == CAIRO_PATTERN_TYPE_SURFACE) {
		cairo_surface_t *msk = ((cairo_surface_pattern_t *)mask)->surface;
		cairo_bool_t extend = source->extend == CAIRO_EXTEND_REPEAT ||
				      source->extend == CAIRO_EXTEND_REFLECT;
		mask_clone = _cairo_gl_generate_clone(surface, msk, extend);
		if (mask_clone == NULL) {
			status = UNSUPPORTED("generate_clone for mask failed");
			goto FINISH;
		}
	}
    //printf("generate mask clone %ld usec\n", _get_tick() - now);
	setup->source = (cairo_pattern_t*)source;

	// set up source
	if (clone == NULL) {
		status = _cairo_gl_composite_set_source(setup, source,
                            surface_rect.x,
                            surface_rect.y,
                            surface_rect.x,
                            surface_rect.y,
                            surface_rect.width,
                            surface_rect.height,
							0, /* texture */
							0, /* width */
							0); /* height */
	} else {
		status = _cairo_gl_composite_set_source(setup, source,
                            surface_rect.x,
                            surface_rect.y,
                            surface_rect.x,
                            surface_rect.y,
                            surface_rect.width,
                            surface_rect.height,
							clone->tex,
							(int) clone->orig_width,
							(int) clone->orig_height);
	}
	if (unlikely(status))
		goto FINISH;

	status = _cairo_gl_context_acquire (surface->base.device, &ctx);
	if (unlikely(status))
		goto FINISH;
    
    if(clip_pt != NULL && clip_pt->path != NULL && clip_pt->num_boxes == 1)
    {
        _cairo_gl_enable_scissor_test (ctx, surface, clip_pt->extents);
        /*if(_cairo_gl_surface_is_texture(surface))
        glScissor(clip_pt->extents.x, clip_pt->extents.y,
              clip_pt->extents.width, clip_pt->extents.height);
        else
        glScissor(clip_pt->extents.x, 
                surface->height - clip_pt->extents.y - clip_pt->extents.height,
              clip_pt->extents.width, clip_pt->extents.height);
	*/
        clip_pt = NULL;
    }

    if(clip_pt == NULL)
        _cairo_gl_disable_scissor_test (ctx);

	setup->ctx = ctx;
    
    //now = _get_tick();
    // for blit for clone and mask_clone    
    if(clone != NULL && clone->multisample_resolved == FALSE)
        _cairo_gl_context_blit_destination(ctx, clone);
    if(mask_clone != NULL && mask_clone->multisample_resolved == FALSE)
        _cairo_gl_context_blit_destination(ctx, mask_clone);
    //printf("blit clone and mask clone %ld \n", _get_tick() - now);
    //now = _get_tick();
    //if(!_cairo_gl_extents_within_clip (extents, extents.is_bounded, clip_pt))
    //    clip_pt = NULL;

    if(clip_pt != NULL && clip_pt->path != NULL) {
        if(clip_pt->path->antialias != CAIRO_ANTIALIAS_NONE)
            surface->require_aa = TRUE;
        else
            surface->require_aa = FALSE;
    }

	//surface->require_aa = FALSE;
	// we set require_aa to false if multisample is resolved
	/*if(surface->multisample_resolved == TRUE)
		surface->require_aa = FALSE;
	else
		surface->require_aa = TRUE;
    */
	_cairo_gl_context_set_destination(ctx, surface);
/*
    if (clip != NULL && clip->path != NULL) {
	status = _cairo_gl_clip(clip, setup, ctx, surface);
	if (unlikely(status))
		goto FINISH;
    done_clip = TRUE;
    }
*/
	if (mask_clone != NULL) {
		status = _cairo_gl_composite_set_mask(setup, mask, 
                            surface_rect.x,
                            surface_rect.y,
                            surface_rect.x,
                            surface_rect.y,
                            surface_rect.width,
                            surface_rect.height,
						      mask_clone->tex,
						      (int) mask_clone->orig_width,
						      (int) mask_clone->orig_height);
	} else {
		status = _cairo_gl_composite_set_mask(setup, mask, 
                            surface_rect.x,
                            surface_rect.y,
                            surface_rect.x,
                            surface_rect.y,
                            surface_rect.width,
                            surface_rect.height,
						      0, /* texture */
						      0, /* width */
						      0); /* height */
	}
	if (unlikely (status))
		goto FINISH;

	if (source->type == CAIRO_PATTERN_TYPE_SURFACE)
		setup->src.type = CAIRO_GL_OPERAND_TEXTURE;
	else if (source->type == CAIRO_PATTERN_TYPE_SOLID)
		setup->src.type = CAIRO_GL_OPERAND_CONSTANT;
	else if (source->type == CAIRO_PATTERN_TYPE_LINEAR) {
		if (source->extend == CAIRO_EXTEND_NONE)
			setup->src.type = CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_NONE;
		else if (source->extend == CAIRO_EXTEND_PAD)
			setup->src.type = CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_PAD;
		else if (source->extend == CAIRO_EXTEND_REPEAT)
			setup->src.type = CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_REPEAT;
		else
			setup->src.type = CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_REFLECT;

	} else if (source->type == CAIRO_PATTERN_TYPE_RADIAL) {
		// FIXME: What do we do here?
	} else {
		status = CAIRO_INT_STATUS_UNSUPPORTED;
		goto FINISH;
	}

    if(clip_pt != NULL && (clip_pt->path != NULL || clip_pt->num_boxes > 0))
    {
        cairo_gl_tristrip_indices_t indices;
        cairo_traps_t traps;
	    cairo_polygon_t polygon;
	    cairo_antialias_t antialias;
	    cairo_fill_rule_t fill_rule;

        status = _cairo_gl_tristrip_indices_init (&indices);
	    _cairo_traps_init (&traps);
	    indices.setup = setup;
	    if (unlikely (status))
        {   
	        _cairo_gl_tristrip_indices_destroy (&indices);
	        _cairo_traps_fini (&traps);
	        goto FINISH;
        }
        if(clip_pt->path == NULL && clip_pt->num_boxes == 1) 
        {
            if(mask_clone == NULL)
                status = _cairo_gl_tristrip_indices_add_boxes(&indices, clip_pt->num_boxes, clip_pt->boxes);
            else
                status = _cairo_gl_tristrip_indices_add_boxes_with_mask(&indices, clip_pt->num_boxes, clip_pt->boxes, &mask->matrix, mask_clone);
        }
        else 
        {
	        status = _cairo_clip_get_polygon (clip_pt, &polygon, &fill_rule, &antialias);
	        if (unlikely (status))
            {
	            _cairo_gl_tristrip_indices_destroy (&indices);
                _cairo_polygon_fini(&polygon);
	            _cairo_traps_fini (&traps);
	            goto FINISH;
            }
	        status = _cairo_bentley_ottmann_tessellate_polygon (&traps,
							    &polygon,
							    fill_rule);
	        _cairo_polygon_fini (&polygon);
	        if (unlikely (status))
            {
	            _cairo_gl_tristrip_indices_destroy (&indices);
	            _cairo_traps_fini (&traps);
	            goto FINISH;
            }
            if(mask_clone == NULL)
            {
	            status = _cairo_gl_tristrip_indices_add_traps (&indices, &traps);
            }
            else
            {
	            status = _cairo_gl_tristrip_indices_add_traps_with_mask (&indices, &traps, &mask->matrix, mask_clone);
            }
        }
	    if (unlikely (status))
        {
	        _cairo_gl_tristrip_indices_destroy (&indices);
	        _cairo_traps_fini (&traps);
	        goto FINISH;
        }
	    cairo_status_t status = _cairo_gl_fill(&indices);
	    _cairo_gl_tristrip_indices_destroy (&indices);
	    _cairo_traps_fini (&traps);
        
    }
    else 
    {
	// we have the image uploaded, we need to setup vertices
	vertices[0] = surface_rect.x;
	vertices[1] = surface_rect.y;
	vertices[2] = surface_rect.x + surface_rect.width;
	vertices[3] = surface_rect.y;
	vertices[4] = surface_rect.x + surface_rect.width;
	vertices[5] = surface_rect.y + surface_rect.height;
	vertices[6] = surface_rect.x;
	vertices[7] = surface_rect.y + surface_rect.height;

	if (source->type == CAIRO_PATTERN_TYPE_SURFACE) {
		map_vertices_to_surface_space (vertices, 4, clone, &source->matrix, texture_coordinates);
		if (mask != NULL && mask_clone != NULL) {
			map_vertices_to_surface_space (vertices, 4, mask_clone,
						       &mask->matrix,
						       mask_texture_coords);
			status = _cairo_gl_composite_begin_constant_color(setup, 
				4, 
				vertices, 
				texture_coordinates,
				mask_texture_coords,
				ctx);
		} else {
			status = _cairo_gl_composite_begin_constant_color(setup, 
				4, 
				vertices, 
				texture_coordinates,
				NULL,
				ctx);
		}

	} else if (source->type == CAIRO_PATTERN_TYPE_SOLID) {
		if (mask != NULL && mask_clone != NULL) {
			map_vertices_to_surface_space (vertices, 4, mask_clone,
						       &mask->matrix,
						       mask_texture_coords);
			status = _cairo_gl_composite_begin_constant_color(setup, 
				4, 
				vertices, 
				NULL,
				mask_texture_coords,
				ctx);
		} else {
			status = _cairo_gl_composite_begin_constant_color(setup, 
				4, 
				vertices, 
				NULL,
				NULL,
				ctx);
		}
	} else if (source->type == CAIRO_PATTERN_TYPE_LINEAR ||
		source->type == CAIRO_PATTERN_TYPE_RADIAL) {
		if (mask != NULL && mask_clone != NULL) {
			map_vertices_to_surface_space (vertices, 4, mask_clone,
						       &mask->matrix,
						       mask_texture_coords);
			status = _cairo_gl_composite_begin_constant_color(setup, 
				4, 
				vertices, 
				NULL,
				mask_texture_coords,
				ctx);
		} else {
			status = _cairo_gl_composite_begin_constant_color(setup, 
				4, 
				vertices, 
				NULL,
				NULL,
				ctx);
		}
	}
    if (unlikely(status))
	goto FINISH;


    _cairo_gl_composite_fill_constant_color(ctx, 4, NULL);
    }
    surface->needs_new_data_surface = TRUE;

FINISH:
    _cairo_gl_composite_fini(setup);

    surface->require_aa = FALSE;
    free(setup);
    if (clone != NULL)
	cairo_surface_destroy(&clone->base);
    if (mask_clone != NULL)
	cairo_surface_destroy(&mask_clone->base);
/*    if(ctx)
    {
        _cairo_gl_disable_stencil_test(ctx);
        _cairo_gl_disable_scissor_test(ctx);
    }
*/
    //else
    //    glDisable(GL_SCISSOR_TEST);
    if (ctx)
    status = _cairo_gl_context_release(ctx, status);
    
    //glDepthMask(GL_FALSE);
    return status;
}

static cairo_int_status_t
_cairo_gl_surface_prepare_mask_surface (cairo_gl_surface_t *surface)
{
    cairo_surface_t *mask_surface;

    if (surface->mask_surface != NULL &&
	surface->mask_surface->width == surface->width &&
	surface->mask_surface->height == surface->height) {
	return _cairo_gl_surface_clear (surface->mask_surface, CAIRO_COLOR_TRANSPARENT);
    }

    if (surface->mask_surface != NULL) {
	cairo_surface_destroy (&surface->mask_surface->base);
	surface->mask_surface = NULL;
    }

    mask_surface = cairo_surface_create_similar (&surface->base,
						 CAIRO_CONTENT_COLOR_ALPHA,
						 surface->width,
						 surface->height);
    if (mask_surface == NULL)
        return CAIRO_INT_STATUS_UNSUPPORTED;
    if (cairo_surface_get_type (mask_surface) != CAIRO_SURFACE_TYPE_GL ||
        unlikely (mask_surface->status)) {
        cairo_surface_destroy (mask_surface);
        return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    surface->mask_surface = (cairo_gl_surface_t *) mask_surface;
    surface->mask_surface->parent_surface = surface;
    return _cairo_gl_surface_clear (surface->mask_surface, CAIRO_COLOR_TRANSPARENT);
}

static cairo_int_status_t
_cairo_gl_surface_paint_back_mask_surface (cairo_gl_surface_t	*surface,
					   cairo_operator_t	op,
					   cairo_clip_t		*clip)
{
    cairo_status_t status = CAIRO_STATUS_SUCCESS;
    cairo_surface_pattern_t mask_pattern;

    surface->mask_surface->base.is_clear = FALSE;
    _cairo_pattern_init_for_surface (&mask_pattern,
				     (cairo_surface_t*) surface->mask_surface);
    mask_pattern.base.has_component_alpha = FALSE;

    status = _cairo_surface_paint (&surface->base, op, &(mask_pattern.base), clip);

    _cairo_pattern_fini (&mask_pattern.base);
    return status;
}

static cairo_status_t
_cairo_path_fixed_stroke_to_shaper_add_quad (void		 *closure,
					     const cairo_point_t quad[4])
{
    return _cairo_gl_tristrip_indices_add_quad (closure, quad);
}

static cairo_int_status_t
_cairo_gl_surface_stroke (void			        *abstract_surface,
                          cairo_operator_t		 op,
                          const cairo_pattern_t	        *source,
                          cairo_path_fixed_t		*path,
                          const cairo_stroke_style_t	*style,
                          const cairo_matrix_t	        *ctm,
                          const cairo_matrix_t	        *ctm_inverse,
                          double			 tolerance,
                          cairo_antialias_t		 antialias,
                          cairo_clip_t		        *clip)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_composite_rectangles_t extents;
    //cairo_box_t boxes_stack[32], *clip_boxes = boxes_stack;
    //int num_boxes = ARRAY_LENGTH (boxes_stack);
    //cairo_clip_t local_clip;
    //cairo_bool_t have_clip = FALSE;
    //cairo_polygon_t polygon;
    cairo_status_t status;
    cairo_gl_tristrip_indices_t indices;
    cairo_traps_t traps;
    // Henry Song
    cairo_gl_composite_t *setup = NULL;
    cairo_gl_context_t *ctx = NULL;
    cairo_gl_surface_t *clone = NULL;
    //cairo_surface_t *snapshot = NULL;
    int v;
    
    int extend = 0;
    cairo_clip_t *clip_pt = clip;
    cairo_bool_t has_alpha = TRUE;
    cairo_rectangle_int_t surface_rect;
    cairo_bool_t is_rectilinear = FALSE;
    long now, whole_now;
	//cairo_rectangle_int_t *clip_extent, stroke_extent;
    
    //now = _get_tick();
    //whole_now = now;
    //printf("&&&&&&&&&&&&&&&&&&& start stroke &&&&&&&&&&&&&&&&&&&&\n");
    status = _cairo_composite_rectangles_init_for_stroke (&extents,
							  surface->width,
							  surface->height,
							  op, source,
							  path, style, ctm,
							  clip);
    if (unlikely (status))
		return status;
    //printf("\tinit stroke %ld\n", _get_tick() - now);
    
    if (extents.is_bounded == 0) {
        _cairo_composite_rectangles_fini(&extents);
	if (unlikely ((status = _cairo_gl_surface_prepare_mask_surface (surface)))) {
	    return status;
    }

	status = _cairo_gl_surface_stroke (surface->mask_surface,
					   CAIRO_OPERATOR_OVER,
					   source,
					   path,
					   style,
					   ctm,
					   ctm_inverse,
					   tolerance,
					   antialias,
					   NULL);
	if (unlikely (status))
	    return status;
	return _cairo_gl_surface_paint_back_mask_surface (surface, op, clip);
    }
    
    surface_rect.x = extents.bounded.x;
    surface_rect.y = extents.bounded.y;
    surface_rect.width = extents.bounded.width;
    surface_rect.height = extents.bounded.height;
    _cairo_composite_rectangles_fini(&extents);
	
    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
	if(unlikely(status))
    {
        //glDisable(GL_SCISSOR_TEST);
		return status;
    }
    
    if(clip_pt != NULL && clip_pt->path == NULL && clip_pt->num_boxes == 1)
    {
        _cairo_gl_enable_scissor_test(ctx, surface, clip_pt->extents);
	/*
        if(_cairo_gl_surface_is_texture(surface))
            glScissor(clip_pt->extents.x, clip_pt->extents.y,
              clip_pt->extents.width, clip_pt->extents.height);
        else
            glScissor(clip_pt->extents.x, 
                surface->height - clip_pt->extents.y - clip_pt->extents.height,
              clip_pt->extents.width, clip_pt->extents.height);
        */
        clip_pt = NULL;
    }
    else
    {
        //glEnable(GL_SCISSOR_TEST);
        //glScissor(surface_rect.x, surface_rect.y,
        //      surface_rect.width, surface_rect.height);
        _cairo_gl_disable_scissor_test(ctx);
    }
    //if(clip_pt != NULL && _cairo_gl_clip_contains_rectangle(clip_pt, &surface_rect))
    //if(clip_pt != NULL && _cairo_clip_contains_rectangle(clip_pt, &surface_rect))
    //    clip_pt = NULL;
    /*if(clip_pt != NULL && clip_pt->path == NULL) 
    {
        if(_cairo_gl_clip_contains_rectangle(clip_pt, &surface_rect))
            clip_pt = NULL;
    }*/
    
    if(_cairo_path_fixed_stroke_is_rectilinear(path))
        is_rectilinear = TRUE;
    
    if(is_rectilinear)
        has_alpha = FALSE;
   
    // for stroke, it always bounded 
    //now = _get_tick();
    //if(!_cairo_gl_extents_within_clip (extents, TRUE, clip_pt))
    //    clip_pt = NULL;
    //printf("\tcheck extents within clip %ld\n", _get_tick() - now);
    
    //now = _get_tick();
	//printf("\taquire context %ld\n", _get_tick() - now);
    // upload image
    //now = _get_tick();
	if(source->type == CAIRO_PATTERN_TYPE_SURFACE)
	{
		cairo_surface_t *src = ((cairo_surface_pattern_t *)source)->surface;
		cairo_bool_t extend = source->extend == CAIRO_EXTEND_REPEAT ||
				      source->extend == CAIRO_EXTEND_REFLECT;
		clone = _cairo_gl_generate_clone(surface, src, extend);
		if(clone == NULL)
		{
            _cairo_gl_disable_stencil_test(ctx);
            //_cairo_gl_disable_scissor_test(ctx);
			status = _cairo_gl_context_release(ctx, status);
            //glDisable(GL_SCISSOR_TEST);
			return UNSUPPORTED("create_clone failed");
		}
	}
    else if(source->type == CAIRO_PATTERN_TYPE_SOLID)
        has_alpha = ((cairo_solid_pattern_t *)source)->color.alpha == 1.0 ? FALSE : TRUE;
	
    //now = _get_tick();
	setup = (cairo_gl_composite_t *)malloc(sizeof(cairo_gl_composite_t));
	
	status = _cairo_gl_composite_init(setup, op, surface, FALSE,
		&extents.bounded);
	if(unlikely (status))
	{
		_cairo_gl_composite_fini(setup);
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		free(setup);
        _cairo_gl_disable_stencil_test(ctx);
        //_cairo_gl_disable_scissor_test(ctx);
		status = _cairo_gl_context_release(ctx, status);
        //glDisable(GL_SCISSOR_TEST);
		return status;
	}
    //printf("\tsetup init %ld\n", _get_tick() - now);

	setup->source = (cairo_pattern_t*)source;
	if(clone == NULL)
		status = _cairo_gl_composite_set_source(setup,
            source, surface_rect.x, surface_rect.y,
            surface_rect.x, surface_rect.y,
            surface_rect.width, surface_rect.height,
			0, 0, 0);
	else
	{
            float temp_width = clone->orig_width;
            float temp_height = clone->orig_height;
		status = _cairo_gl_composite_set_source(setup,
            source, surface_rect.x, surface_rect.y,
            surface_rect.x, surface_rect.y,
            surface_rect.width, surface_rect.height,
			clone->tex, (int)temp_width, (int)temp_height); 
	}

	if(unlikely(status))
	{
		_cairo_gl_composite_fini(setup);
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		free(setup);
        _cairo_gl_disable_stencil_test(ctx);
        //_cairo_gl_disable_scissor_test(ctx);
		status = _cairo_gl_context_release(ctx, status);
        //glDisable(GL_SCISSOR_TEST);
		return status;
	}


	setup->ctx = ctx;
    
    // force clone to blit back to texture
    //now = _get_tick();
    if(clone != NULL && clone->multisample_resolved == FALSE)
        _cairo_gl_context_blit_destination(ctx, clone);
    //printf("\tblit clone %ld\n", _get_tick() - now);
    if(antialias != CAIRO_ANTIALIAS_NONE || 
      (clip_pt != NULL && clip_pt->path != NULL && 
       clip_pt->path->antialias != CAIRO_ANTIALIAS_NONE))
        surface->require_aa = TRUE;
    else
        surface->require_aa = FALSE;
    //now = _get_tick();
	_cairo_gl_context_set_destination(ctx, surface);
    //printf("\tset destination %ld\n", _get_tick() - now);
    //now = _get_tick(); 
    if (clip_pt != NULL) {
	status = _cairo_gl_clip(clip_pt, setup, ctx, surface);
    //printf("\tgl_clip %ld usec\n", _get_tick() - now);
	if (unlikely(status)) {
	    if (clone != NULL)
		cairo_surface_destroy(&clone->base);

	    _cairo_gl_composite_fini(setup);
	    free(setup);
        _cairo_gl_disable_stencil_test(ctx);
        //_cairo_gl_disable_stencil_test(ctx);
        //glDisable(GL_SCISSOR_TEST);
	    //glDepthMask(GL_FALSE);
            surface->require_aa = FALSE;
	    status = _cairo_gl_context_release(ctx, status);
	    return status;
	}
    } else {
	/* Enable the stencil buffer, even if we have no clip so that
	   we can use it below to prevent overlapping shapes. We initialize
	   it all to one here which represents infinite clip. */
    if(has_alpha) {
	glDepthMask (GL_TRUE);
    _cairo_gl_enable_stencil_test(ctx);
	glClearStencil(1);
	glClear (GL_STENCIL_BUFFER_BIT);
	glStencilFunc (GL_EQUAL, 1, 1);
    }
    }

    /* This prevents shapes from _cairo_path_fixed_stroke_to_shaper from overlapping. */
    if(has_alpha) {
    glStencilOp (GL_ZERO, GL_ZERO, GL_ZERO);
    surface->stencil_buffer_changed = TRUE;
    }
	
    status = _cairo_gl_tristrip_indices_init (&indices);
	indices.setup = setup;
	
// setup indices
    if(is_rectilinear) {
	    _cairo_traps_init(&traps);

        status = _cairo_path_fixed_stroke_rectilinear_to_traps(path,
                                                        style,
                                                        ctm,
                                                        antialias,
                                                        &traps);
        if (unlikely (status))
        {
            _cairo_traps_fini(&traps);
            goto CLEANUP;
        }
        status = _cairo_gl_tristrip_indices_add_traps (&indices, &traps);
        if (unlikely (status))
        {
            _cairo_traps_fini(&traps);
            goto CLEANUP;
        }
        _cairo_traps_fini(&traps);
    }
    else {
	    status = _cairo_path_fixed_stroke_to_shaper (path,
						    style,
						    ctm,
						    ctm_inverse,
						    tolerance,
						    _cairo_gl_add_triangle,
						    _cairo_gl_add_triangle_fan,
						    _cairo_path_fixed_stroke_to_shaper_add_quad,
						    &indices);
    //printf("\ttessellate %ld\n", _get_tick() - now);
	//glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &v);
	// fill it, we fix t later
    //now = _get_tick();
    }
	status = _cairo_gl_fill(&indices);
	//printf("\tgl_fill %ld\n", _get_tick() - now);
CLEANUP:
    if(clone != NULL)
		cairo_surface_destroy(&clone->base);
	_cairo_gl_tristrip_indices_destroy (&indices);
	_cairo_gl_composite_fini(setup);
	free(setup);
    _cairo_gl_disable_stencil_test(ctx);
    //_cairo_gl_disable_scissor_test(ctx);
    //now = _get_tick();
          
    //glDisable(GL_SCISSOR_TEST);
	//glDisable(GL_DEPTH_TEST);
	//glDepthMask(GL_FALSE);
    //printf("\tdisable GL %ld\n", _get_tick() - now);
    surface->require_aa = FALSE;
    //now = _get_tick();
	status = _cairo_gl_context_release(ctx, status);
    //printf("\t release context %ld\n", _get_tick() - now);
	surface->needs_new_data_surface = TRUE;
    //printf("&&&&&&&&&&&&& finish stroke %ld &&&&&&&&&&&&&&&&&&\n\n", _get_tick() - whole_now);
    return status;
}

static cairo_int_status_t
_cairo_gl_surface_fill (void			*abstract_surface,
                        cairo_operator_t	 op,
                        const cairo_pattern_t	*source,
                        cairo_path_fixed_t	*path,
                        cairo_fill_rule_t	 fill_rule,
                        double			 tolerance,
                        cairo_antialias_t	 antialias,
                        cairo_clip_t		*clip)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_gl_composite_t *setup = NULL;

    cairo_composite_rectangles_t extents;
    cairo_status_t status;
    cairo_gl_surface_t *clone = NULL;
    cairo_gl_context_t *ctx = NULL;
    cairo_traps_t traps;
    cairo_gl_tristrip_indices_t indices;
    
    cairo_clip_t *clip_pt = clip;  
    cairo_rectangle_int_t surface_rect;

    // When clip and path do not intersect,, return without actually drawing.
    status = _cairo_composite_rectangles_init_for_fill (&extents,
							surface->width,
							surface->height,
							op, source, path,
							clip);
    if (unlikely (status))
	return status;

    if (extents.is_bounded == 0) {
        _cairo_composite_rectangles_fini(&extents);
	if (unlikely ((status = _cairo_gl_surface_prepare_mask_surface (surface))))
    {
	    return status;
    }
	status = _cairo_gl_surface_fill (surface->mask_surface,
					 CAIRO_OPERATOR_OVER,
					 source,
					 path,
					 fill_rule,
					 tolerance,
					 antialias,
					 NULL);
	if (unlikely (status))
	    return status;
	return _cairo_gl_surface_paint_back_mask_surface (surface, op, clip);
    }
        
    surface_rect.x = extents.bounded.x;
    surface_rect.y = extents.bounded.y;
    surface_rect.width = extents.bounded.width;
    surface_rect.height = extents.bounded.height;
    _cairo_composite_rectangles_fini(&extents);
    
    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely(status)) {
	goto CLEANUP;
    }
    if(clip_pt != NULL && clip_pt->path == NULL && clip_pt->num_boxes == 1)
    {
        _cairo_gl_enable_scissor_test(ctx, surface, clip_pt->extents);
	/*
        if(_cairo_gl_surface_is_texture(surface))
        glScissor(clip_pt->extents.x, clip_pt->extents.y,
                  clip_pt->extents.width, clip_pt->extents.height);
        else
        glScissor(clip_pt->extents.x, 
            surface->height - clip_pt->extents.y - clip_pt->extents.height,
              clip_pt->extents.width, clip_pt->extents.height);
	*/
        clip_pt = NULL;
    }
    else
    {
        //glEnable(GL_SCISSOR_TEST);
        //glScissor(surface_rect.x, surface_rect.y,
        //      surface_rect.width, surface_rect.height);
        _cairo_gl_disable_scissor_test(ctx);
    }
    //if(clip_pt != NULL && _cairo_gl_clip_contains_rectangle(clip_pt, &surface_rect))
    //if(clip_pt != NULL && _cairo_clip_contains_rectangle(clip_pt, &surface_rect))
    //    clip_pt = NULL;
    // for fill, it is always bounded
    //if(!_cairo_gl_extents_within_clip (extents, TRUE, clip_pt))
    //    clip_pt = NULL;
	// upload image
	if(source->type == CAIRO_PATTERN_TYPE_SURFACE)
	{
		cairo_surface_t *src = ((cairo_surface_pattern_t *)source)->surface;
		cairo_bool_t extend = source->extend == CAIRO_EXTEND_REPEAT ||
				      source->extend == CAIRO_EXTEND_REFLECT;
		clone = _cairo_gl_generate_clone(surface, src, extend);

		if(clone == NULL)
		{
            _cairo_gl_disable_stencil_test(ctx);
            //_cairo_gl_disable_scissor_test(ctx);
            status = _cairo_gl_context_release(ctx, status);
			return UNSUPPORTED("create_clone failed");
		}
	}


    setup = (cairo_gl_composite_t *)malloc(sizeof(cairo_gl_composite_t));
    status = _cairo_gl_composite_init(setup, op, surface, FALSE, &extents.bounded);
    if(unlikely (status))
	goto CLEANUP_AND_RELEASE_DEVICE;

	setup->source = source;
	if(clone == NULL)
		status = _cairo_gl_composite_set_source(setup,
            source, surface_rect.x, surface_rect.y,
            surface_rect.x, surface_rect.y,
            surface_rect.width, surface_rect.height,
			0, 0, 0);
	else
	{
            float temp_width = clone->orig_width;
            float temp_height = clone->orig_height;
		status = _cairo_gl_composite_set_source(setup,
            source, surface_rect.x, surface_rect.y,
            surface_rect.x, surface_rect.y,
            surface_rect.width, surface_rect.height,
			clone->tex, (int)temp_width, (int)temp_height); 
	}
    if (unlikely(status))
	goto CLEANUP_AND_RELEASE_DEVICE;

	// let's acquire context, set surface
	setup->ctx = ctx;
    // force clone to blit back to texture
    if(clone != NULL && clone->multisample_resolved == FALSE)
        _cairo_gl_context_blit_destination(ctx, clone);

    if(antialias != CAIRO_ANTIALIAS_NONE || 
      (clip_pt != NULL && clip_pt->path != NULL && 
       clip_pt->path->antialias != CAIRO_ANTIALIAS_NONE))
        surface->require_aa = TRUE;
    else
        surface->require_aa = FALSE;
    //surface->require_aa = FALSE;
	_cairo_gl_context_set_destination(ctx, surface);
	// remember, we have set the current context, we need to release it
	// when done

    if (clip_pt != NULL) {
	status = _cairo_gl_clip(clip_pt, setup, ctx, surface);
	if (unlikely(status))
	    goto CLEANUP_STENCIL_AND_DEPTH_TESTING;
    }

	if(source->type == CAIRO_PATTERN_TYPE_SURFACE)
		setup->src.type = CAIRO_GL_OPERAND_TEXTURE;
	else if(source->type == CAIRO_PATTERN_TYPE_SOLID)
		setup->src.type = CAIRO_GL_OPERAND_CONSTANT;
	else if(source->type == CAIRO_PATTERN_TYPE_LINEAR)
	{
		if(source->extend == CAIRO_EXTEND_NONE)
			setup->src.type = CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_NONE;
		else if(source->extend == CAIRO_EXTEND_PAD)
			setup->src.type = CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_PAD;
		else if(source->extend == CAIRO_EXTEND_REPEAT)
			setup->src.type = CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_REPEAT;
		else
			setup->src.type = CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_REFLECT;
	}
	else if(source->type == CAIRO_PATTERN_TYPE_RADIAL)
	{}
		//setup->src.type = CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_NONE_CIRCLE_IN_CIRCLE;
	else
	{
		status = CAIRO_INT_STATUS_UNSUPPORTED;
		goto CLEANUP_STENCIL_AND_DEPTH_TESTING;
	}

	// setup indices
	_cairo_traps_init(&traps);
	status = _cairo_gl_tristrip_indices_init (&indices);
	indices.setup = setup;

    if(_cairo_path_fixed_fill_is_rectilinear(path))
        status = _cairo_path_fixed_fill_rectilinear_to_traps(path,
                                                        fill_rule,
                                                        antialias,
                                                        &traps);
    else
        status = _cairo_path_fixed_fill_to_traps(path, fill_rule, tolerance, &traps);
    if (unlikely (status))
	goto CLEANUP_TRAPS_AND_GL;

    status = _cairo_gl_tristrip_indices_add_traps (&indices, &traps);
    if (unlikely (status))
	goto CLEANUP_TRAPS_AND_GL;

    status = _cairo_gl_fill(&indices);
    surface->needs_new_data_surface = TRUE;
    surface->require_aa = FALSE;

CLEANUP_TRAPS_AND_GL:
    _cairo_traps_fini(&traps);
    _cairo_gl_tristrip_indices_destroy (&indices);
    //glDisable(GL_SCISSOR_TEST);

CLEANUP_STENCIL_AND_DEPTH_TESTING:
    _cairo_gl_disable_stencil_test(ctx);
    //_cairo_gl_disable_scissor_test(ctx);
    //glDepthMask(GL_FALSE);

CLEANUP_AND_RELEASE_DEVICE:
    if (setup != NULL) {
	_cairo_gl_composite_fini (setup);
	free (setup);
    }
    status = _cairo_gl_context_release(ctx, status);

CLEANUP:
    if (clone != NULL)
	cairo_surface_destroy (&clone->base);
    surface->require_aa = FALSE;
    return status;
}

const cairo_surface_backend_t _cairo_gl_surface_backend = {
    CAIRO_SURFACE_TYPE_GL,
    _cairo_default_context_create,
    _cairo_gl_surface_create_similar,
    _cairo_gl_surface_finish,

    _cairo_gl_surface_acquire_source_image,
    _cairo_gl_surface_release_source_image,
    _cairo_gl_surface_acquire_dest_image,
    _cairo_gl_surface_release_dest_image,

    _cairo_gl_surface_clone_similar,
    NULL, /*_cairo_gl_surface_composite,*/
    NULL, /*_cairo_gl_surface_fill_rectangles,*/
    NULL, /*_cairo_gl_surface_composite_trapezoids,*/
    NULL, /*_cairo_gl_surface_create_span_renderer,*/
    NULL, /*_cairo_gl_surface_check_span_renderer,*/

    NULL, /* copy_page */
    NULL, /* show_page */
    _cairo_gl_surface_get_extents,
    NULL, /* old_show_glyphs */
    _cairo_gl_surface_get_font_options,
    _cairo_gl_surface_flush,
    _cairo_gl_surface_mark_dirty_rectangle, /* mark_dirty_rectangle */
    _cairo_gl_surface_scaled_font_fini,
    _cairo_gl_surface_scaled_glyph_fini,
    _cairo_gl_surface_paint,
    _cairo_gl_surface_mask, /* mask */
    _cairo_gl_surface_stroke,
    _cairo_gl_surface_fill,
    _cairo_gl_surface_show_glyphs, /* show_glyphs */
    NULL  /* snapshot */
};

static void
_cairo_gl_surface_remove_from_cache(cairo_surface_t *abstract_surface)
{
	cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;
	cairo_surface_destroy(&surface->base);
}

void cairo_gl_reset_device(cairo_device_t *device)
{
	cairo_gl_context_t *ctx = NULL;
	if(device == NULL)
		return;
	ctx = (cairo_gl_context_t *)device;
    ctx->bound_fb = 0;
    ctx->current_program = -1;
    ctx->active_texture = -9999;
    ctx->src_color_factor = -9999;
    ctx->dst_color_factor = -9999;
    ctx->src_alpha_factor = -9999;
    ctx->dst_alpha_factor = -9999;
    
    ctx->scissor_box.x = 0;
    ctx->scissor_box.y = 0;
    ctx->scissor_box.width = 0;
    ctx->scissor_box.height = 0;

    ctx->draw_buffer = GL_NONE;

    ctx->stencil_test_enabled = FALSE;
    ctx->scissor_test_enabled = FALSE;
    ctx->blend_enabled = FALSE;
    ctx->multisample_enabled = FALSE;
    
    ctx->clear_red = -1;
    ctx->clear_green = -1; 
    ctx->clear_blue = -1;
    ctx->clear_alpha = -1;

    ctx->stencil_test_reset = TRUE;
    ctx->scissor_test_reset = TRUE;
    ctx->program_reset = TRUE;
    ctx->source_texture_attrib_reset = TRUE;
    ctx->mask_texture_attrib_reset = TRUE;
    ctx->vertex_attrib_reset = TRUE;

    ctx->current_target = NULL;

	ctx->reset(ctx);
}


cairo_status_t
_cairo_gl_surface_mark_dirty_rectangle(cairo_surface_t *abstract_surface,
	int x, int y, int width, int height)
{
    cairo_gl_context_t *ctx;
    cairo_status_t status;
	cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;
	if(surface->tex == 0)
		return CAIRO_INT_STATUS_UNSUPPORTED;
	if(surface->data_surface == NULL)
		return CAIRO_STATUS_SUCCESS;

	// we need to upload the portion to 
	status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely (status))
		return status;
    _cairo_gl_composite_flush (ctx);

    // force surface to blit to texture
    if(surface->multisample_resolved == FALSE)
        _cairo_gl_context_blit_destination(ctx, surface);
    surface->require_aa = FALSE;
    _cairo_gl_context_set_destination (ctx, surface);
	status = _cairo_gl_surface_draw_image(surface, (cairo_image_surface_t *)surface->data_surface,
		x, y, width, height, x, y, FALSE);
	if(_cairo_surface_has_snapshot(&surface->base, &_cairo_gl_surface_backend))
		_cairo_surface_detach_snapshot(&surface->base);
    status = _cairo_gl_context_release (ctx, status);
	surface->needs_new_data_surface = FALSE;
	return CAIRO_STATUS_SUCCESS;

}

unsigned char *
cairo_gl_surface_get_data(cairo_surface_t *abstract_surface)
{
	void *image_extra = NULL;
	cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;

	if(!_cairo_surface_is_gl(abstract_surface))
		return NULL;
	if(surface->tex == 0)
		return NULL;
	
	if(surface->needs_new_data_surface == FALSE)
	{
		if(surface->data_surface != NULL)
		{
			return cairo_image_surface_get_data(surface->data_surface);
		}
	}
	if(surface->data_surface != NULL)
	{
		cairo_surface_destroy(surface->data_surface);
		surface->data_surface = NULL;
	}
	surface->needs_new_data_surface = FALSE;
	// create an image surface
	_cairo_gl_surface_acquire_source_image (abstract_surface,
					(cairo_image_surface_t **)&(surface->data_surface),
					&image_extra);
	return cairo_image_surface_get_data(surface->data_surface);
}

int 
cairo_gl_surface_get_stride(cairo_surface_t *abstract_surface)
{
	void *image_extra = NULL;
	cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;

	if(!_cairo_surface_is_gl(abstract_surface))
		return 0;
	if(surface->needs_new_data_surface == FALSE)
	{
		if(surface->data_surface != NULL)
		{
			return cairo_image_surface_get_stride(surface->data_surface);
		}
	}
	if(surface->data_surface != NULL)
	{
		cairo_surface_destroy(surface->data_surface);
		surface->data_surface = NULL;
	}
	surface->needs_new_data_surface = FALSE;
	// create an image surface
	_cairo_gl_surface_acquire_source_image (abstract_surface,
					(cairo_image_surface_t **)&(surface->data_surface),
					&image_extra);
	return cairo_image_surface_get_stride(surface->data_surface);
}

cairo_format_t 
cairo_gl_surface_get_format(cairo_surface_t *abstract_surface)
{
	void *image_extra = NULL;
    cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;

	if(!_cairo_surface_is_gl(abstract_surface))
		return 0;
	if(surface->needs_new_data_surface == FALSE)
	{
		if(surface->data_surface != NULL)
		{
			return cairo_image_surface_get_format(surface->data_surface);
		}
	}
	if(surface->data_surface != NULL)
	{
		cairo_surface_destroy(surface->data_surface);
		surface->data_surface = NULL;
	}
	surface->needs_new_data_surface = FALSE;
	// create an image surface
    _cairo_gl_surface_acquire_source_image (abstract_surface,
					(cairo_image_surface_t **)&(surface->data_surface),
					&image_extra);
	return cairo_image_surface_get_format(surface->data_surface);
}

cairo_surface_t *
cairo_gl_surface_get_image_surface(cairo_surface_t *abstract_surface)
{
	void *image_extra = NULL;
	cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;

	if(!_cairo_surface_is_gl(abstract_surface))
		return NULL;
	
	if(surface->needs_new_data_surface == FALSE)
	{
		if(surface->data_surface != NULL)
		{
			return cairo_surface_reference(surface->data_surface);
		}
	}
	if(surface->data_surface != NULL)
	{
		cairo_surface_destroy(surface->data_surface);
		surface->data_surface = NULL;
	}
	surface->needs_new_data_surface = FALSE;
	// create an image surface
	_cairo_gl_surface_acquire_source_image (abstract_surface,
					(cairo_image_surface_t **)&(surface->data_surface),
					&image_extra);
	
	return cairo_surface_reference(surface->data_surface);
}

unsigned int
cairo_gl_surface_get_texture(cairo_surface_t *abstract_surface)
{
	cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;
	if(!_cairo_surface_is_gl(abstract_surface))
		return 0;
	return surface->tex;
}

cairo_status_t
cairo_gl_surface_make_texture_external(cairo_surface_t *abstract_surface)
{
	cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;
	if(!_cairo_surface_is_gl(abstract_surface))
		return CAIRO_STATUS_SURFACE_TYPE_MISMATCH;
	
	if(surface->tex == 0)
		return CAIRO_INT_STATUS_UNSUPPORTED;
	surface->external_tex = TRUE;
	surface->owns_tex = FALSE;
	return CAIRO_STATUS_SUCCESS;
}

