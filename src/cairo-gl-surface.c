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

#include <sys/time.h>
#include "cairo-surface-clipper-private.h"
#include <math.h>

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

cairo_status_t
_cairo_gl_create_indices(_cairo_gl_index_t *index)
{
	index->vertices = (float *)malloc(sizeof(float) * INDEX_CAPACITY * 2);
	index->mask_vertices = (float *)malloc(sizeof(float) * INDEX_CAPACITY * 2);
	index->indices = (int *)malloc(sizeof(int) * INDEX_CAPACITY);
	//index->indices = (int *)malloc(sizeof(int) * INDEX_CAPACITY);
	index->capacity = INDEX_CAPACITY;
	index->num_indices = 0;
	index->num_vertices = 0;
	index->setup = NULL;
	index->has_mask_vertices = FALSE;
	return CAIRO_STATUS_SUCCESS;
}

cairo_status_t
_cairo_gl_increase_indices(_cairo_gl_index_t *index)
{
	int new_capacity = index->capacity * 2;
	float *new_vertices = (float *)malloc(sizeof(float) * new_capacity * 2);
	float *new_mask_vertices = (float *)malloc(sizeof(float) * new_capacity * 2);
	int *new_indices = (int *)malloc(sizeof(int) * new_capacity);

	memcpy(new_vertices, index->vertices, sizeof(float) * index->num_vertices * 2);
	memcpy(new_mask_vertices, index->mask_vertices, sizeof(float) * index->num_vertices * 2);
	memcpy(new_indices, index->indices, sizeof(int) * index->num_indices);
	free(index->vertices);
	free(index->indices);
	free(index->mask_vertices);
	index->vertices = new_vertices;
	index->mask_vertices = new_mask_vertices;
	index->indices = new_indices;
	index->capacity = new_capacity;
	return CAIRO_STATUS_SUCCESS;
}

cairo_status_t
_cairo_gl_destroy_indices(_cairo_gl_index_t *index)
{
	free(index->vertices);
	free(index->mask_vertices);
	free(index->indices);
	index->has_mask_vertices = FALSE;
	return CAIRO_STATUS_SUCCESS;
}



cairo_status_t
_cairo_gl_fill(void *closure, int vpoints, GLfloat *vertices, GLfloat *mask_vertices, int npoints, int *indices, cairo_gl_context_t *ctx)
{
	char *src_colors = NULL;
	double *src_v = NULL;
	double *mask_colors = NULL;

	cairo_gl_composite_t *setup = (cairo_gl_composite_t *)closure;
	int index = 0;
	int stride = 4 * sizeof(GLfloat);
	cairo_status_t status;

	if(setup->src.type == CAIRO_GL_OPERAND_CONSTANT)
	{
		src_colors = (char *)malloc(sizeof(GLfloat)*4*vpoints);
		while(index < vpoints)
		{
			memcpy(src_colors+index*stride, &(setup->src.constant.color), stride);
			index++;
		}
	}
	else if(setup->src.type == CAIRO_GL_OPERAND_TEXTURE)
	{
		cairo_matrix_t m, m1;
		GLfloat *st = NULL;
		src_v = (double *)malloc(sizeof(double)*vpoints*2);
		src_colors = (char *)malloc(sizeof(GLfloat)*2*vpoints);
		cairo_matrix_init_scale(&m, 1.0, 1.0);
		cairo_matrix_multiply(&m, &m, &(setup->source->matrix));
		cairo_matrix_init_scale(&m1, 1.0 / setup->src.texture.width,
			1.0 / setup->src.texture.height);
		cairo_matrix_multiply(&m, &m, &m1);
		st = (GLfloat*)src_colors;
		for(index = 0; index < vpoints; index++)
		{
			src_v[index*2] = vertices[index*2];
			src_v[index*2+1] = vertices[index*2+1];
			cairo_matrix_transform_point(&m, &src_v[index*2], &src_v[index*2+1]); 
			st[index*2] = src_v[index*2];
			st[index*2+1] = src_v[index*2+1];
		}
	}

	if(setup->mask.type == CAIRO_GL_OPERAND_CONSTANT)
	{
		mask_colors = malloc(sizeof(GLfloat)*4*vpoints);
		while(index < vpoints)
		{
			memcpy(mask_colors+index*stride, &(setup->mask.constant.color), stride);
			index++;
		}
	}
		// we need to fill colors with st values
	status = _cairo_gl_composite_begin_constant_color(setup, 
			vpoints, 
			vertices, 
			src_colors,
			//mask_vertices,
			//mask_colors,
			NULL,
			mask_vertices,
			ctx);
	if(unlikely(status))
	{
		if(src_colors != NULL)
			free(src_colors);
		if(src_v != NULL)
			free(src_v);
		if(mask_colors != NULL)
			free(mask_colors);
		return status;
	}

	_cairo_gl_composite_fill_constant_color(ctx, npoints, indices);
	// we need to release context
	if(src_colors != NULL)
		free(src_colors);
	if(src_v != NULL)
		free(src_v);
	if(mask_colors != NULL)
		free(mask_colors);
	return status;
}

cairo_status_t _cairo_gl_add_triangle(void *closure,
	const cairo_point_t triangle[3])
{
        int last_index = 0;
        int start_index = 0;
        int i;
        int num_indices, num_vertices;
		cairo_status_t status;

	_cairo_gl_index_t *indices = (_cairo_gl_index_t *)closure;
        cairo_gl_composite_t *setup = indices->setup;
	// first off, we need to flush if max
	if(indices->num_indices > MAX_INDEX)
	{
		if(indices->setup != NULL)
			status = _cairo_gl_fill(indices->setup, indices->num_vertices,
				indices->vertices, NULL, indices->num_indices, 
				indices->indices, indices->setup->ctx);
		// cleanup
		status = _cairo_gl_destroy_indices(indices);
		status = _cairo_gl_create_indices(indices);
		indices->setup = setup;
	}
	// we add a triangle to strip, we add 3 vertices and 5 indices;
	while(indices->num_indices + 5 >= indices->capacity ||
		indices->num_vertices + 3 >= indices->capacity)
	{
		// we need to increase
		status = _cairo_gl_increase_indices(indices);
	}
    num_indices = indices->num_indices;
    num_vertices = indices->num_vertices;

	if(num_indices != 0)
	{
		// we are not the first
		last_index = indices->indices[num_indices-1];
		start_index = last_index + 1;
		indices->indices[num_indices] = last_index;
		indices->indices[num_indices+1] = start_index;
		indices->num_indices += 2;
	}
	num_indices = indices->num_indices;
	for(i = 0; i < 3; i++)
	{
		indices->indices[num_indices+i] = start_index + i;
		indices->vertices[num_vertices*2+i*2] = 
			_cairo_fixed_to_double(triangle[i].x);
		indices->vertices[num_vertices*2+i*2+1] = 
			_cairo_fixed_to_double(triangle[i].y);
	}
	indices->num_indices += 3;
	indices->num_vertices += 3;
	return CAIRO_STATUS_SUCCESS;
}

cairo_status_t _cairo_gl_add_triangle_fan(void *closure,
	const cairo_point_t *midpt,
	const cairo_point_t *points,
	int npoints)
{
        int last_index = 0;
        int start_index = 0;
        int i;
        int num_indices, num_vertices;
	    int mid_index;
		cairo_status_t status;

	_cairo_gl_index_t *indices = (_cairo_gl_index_t *)closure;
        cairo_gl_composite_t *setup = indices->setup;
	//printf("before add triangle fan indices = %d\n", indices->num_indices);
	// first off, we need to flush if max
	if(indices->num_indices > MAX_INDEX)
	{
		if(indices->setup != NULL)
			status = _cairo_gl_fill(indices->setup, indices->num_vertices,
				indices->vertices, NULL, indices->num_indices, 
				indices->indices, indices->setup->ctx);
		// cleanup
		status = _cairo_gl_destroy_indices(indices);
		status = _cairo_gl_create_indices(indices);
		indices->setup = setup;
	}
	// we add a triangle fan to strip, we add npoints+1 vertices and (npoints-2)*2 indices;
	while(indices->num_indices + (npoints - 2) * 2 + npoints + 1 >= indices->capacity ||
		indices->num_vertices + npoints + 1 >= indices->capacity)
	{
		// we need to increase
		status = _cairo_gl_increase_indices(indices);
	}
    num_vertices = indices->num_vertices;
	num_indices = indices->num_indices;
	if(num_indices != 0)
	{
		// we are not the first
		last_index = indices->indices[num_indices-1];
		start_index = last_index + 1;
		indices->indices[num_indices] = last_index;
		indices->indices[num_indices+1] = start_index;
		indices->num_indices += 2;
	}
	num_indices = indices->num_indices;
	// add midpoints
	num_indices = indices->num_indices;
	mid_index = start_index;
	indices->indices[num_indices] = mid_index;
	indices->indices[num_indices+1] = mid_index + 1;
	indices->indices[num_indices+2] = mid_index + 2;
	indices->vertices[num_vertices*2] = 
		_cairo_fixed_to_double(midpt->x);
	indices->vertices[num_vertices*2+1] = 
		_cairo_fixed_to_double(midpt->y);
	indices->vertices[num_vertices*2+2] = 
		_cairo_fixed_to_double(points[0].x);
	indices->vertices[num_vertices*2+3] = 
		_cairo_fixed_to_double(points[0].y);
	indices->vertices[num_vertices*2+4] = 
		_cairo_fixed_to_double(points[1].x);
	indices->vertices[num_vertices*2+5] = 
		_cairo_fixed_to_double(points[1].y);
	indices->num_indices += 3;
	indices->num_vertices += 3;
	num_indices = indices->num_indices;
	num_vertices = indices->num_vertices;

	for(i = 2; i < npoints; i++)
	{
		// add midpoint and last point
		last_index = indices->indices[num_indices - 1];
		indices->indices[num_indices] = last_index;
		indices->indices[num_indices + 1] = mid_index;
		indices->indices[num_indices + 2] = last_index + 1;
		indices->num_indices += 3;
		num_indices = indices->num_indices;

		indices->vertices[num_vertices*2] = 
			_cairo_fixed_to_double(points[i].x);
		indices->vertices[num_vertices*2+1] = 
			_cairo_fixed_to_double(points[i].y);
		indices->num_vertices += 1;
		num_vertices = indices->num_vertices;
	}

	return CAIRO_STATUS_SUCCESS;
}

cairo_status_t _cairo_gl_add_convex_quad_for_clip(void *closure,
	const cairo_point_t quad[4])
{
        int last_index = 0;
        int start_index = 0;
        int i;
        int num_indices, num_vertices;
		cairo_status_t status;

	_cairo_gl_index_t *indices = (_cairo_gl_index_t *)closure;
	cairo_gl_composite_t *setup = indices->setup;

	// first off, we need to flush if max
	if(indices->num_indices > MAX_INDEX)
	{
		if(indices->setup != NULL)
		{
	                _cairo_gl_index_buf_t *buf = NULL;
        	        _cairo_gl_index_buf_t *new_buf = NULL;

			// let's create surface->indices_buf
			buf = (_cairo_gl_index_buf_t *)(indices->setup->dst->indices_buf);
			if(buf != NULL)
			{
				while(buf->next != NULL)
					buf = buf->next;
			}
			new_buf = (_cairo_gl_index_buf_t *)malloc(sizeof(_cairo_gl_index_buf_t));
			new_buf->next = NULL;
			if(buf != NULL)
				buf->next = new_buf;
			else
				indices->setup->dst->indices_buf = new_buf;
			new_buf->indices = (_cairo_gl_index_t *)malloc(sizeof(_cairo_gl_index_t));

			new_buf->indices->vertices = (float *)malloc(sizeof(float)*indices->num_vertices * 2);
			memcpy(new_buf->indices->vertices, indices->vertices, sizeof(float)*2*indices->num_vertices);
			new_buf->indices->indices = (int *)malloc(sizeof(int)*indices->num_indices);
			memcpy(new_buf->indices->indices, indices->indices, sizeof(int)*indices->num_indices);
			new_buf->indices->capacity = indices->capacity;
			new_buf->indices->num_indices = indices->num_indices;
			new_buf->indices->num_vertices = indices->num_vertices;
			new_buf->indices->setup = indices->setup;

			status = _cairo_gl_fill(indices->setup, indices->num_vertices,
				indices->vertices, NULL, indices->num_indices, 
				indices->indices, indices->setup->ctx);
			// cleanup
			status = _cairo_gl_destroy_indices(indices);
			status = _cairo_gl_create_indices(indices);
			indices->setup = setup;
		}
	}
	// we add a triangle to strip, we add 4 vertices and 6 indices;
	while(indices->num_indices + 5 >= indices->capacity ||
		indices->num_vertices + 4 >= indices->capacity)
	{
		// we need to increase
		status = _cairo_gl_increase_indices(indices);
	}
    num_indices = indices->num_indices;
    num_vertices = indices->num_vertices;
	
	if(num_indices != 0)
	{
		// we are not the first
		last_index = indices->indices[num_indices-1];
		start_index = last_index + 1;
		indices->indices[num_indices] = last_index;
		indices->indices[num_indices+1] = start_index;
		indices->num_indices += 2;
	}
	num_indices = indices->num_indices;
	for(i = 0; i < 2; i++)
	{
		indices->indices[num_indices+i] = start_index + i;
		indices->vertices[num_vertices*2+i*2] = 
			_cairo_fixed_to_double(quad[i].x);
		indices->vertices[num_vertices*2+i*2+1] = 
			_cairo_fixed_to_double(quad[i].y);
	}
	indices->num_indices += 2;
	indices->num_vertices += 2;
	num_indices = indices->num_indices;
	num_vertices = indices->num_vertices;
	// we reverse order of point 3 and point 4
	start_index = indices->indices[num_indices-1];
	indices->indices[num_indices] = start_index + 1;
	indices->indices[num_indices+1] = start_index + 2;
	indices->vertices[num_vertices*2] = 
			_cairo_fixed_to_double(quad[3].x);
		indices->vertices[num_vertices*2+1] = 
			_cairo_fixed_to_double(quad[3].y);
	indices->vertices[num_vertices*2+2] = 
			_cairo_fixed_to_double(quad[2].x);
		indices->vertices[num_vertices*2+3] = 
			_cairo_fixed_to_double(quad[2].y);
	indices->num_indices += 2;
	indices->num_vertices += 2;
	return CAIRO_STATUS_SUCCESS;
}

cairo_status_t _cairo_gl_add_convex_quad(void *closure,
	const cairo_point_t quad[4])
{
     int last_index = 0;
     int start_index = 0;
     int i;
     int num_indices,num_vertices;
	 cairo_status_t status;

	 _cairo_gl_index_t *indices = (_cairo_gl_index_t *)closure;
	 cairo_gl_composite_t *setup = indices->setup;

	// first off, we need to flush if max
	if(indices->num_indices > MAX_INDEX)
	{
		if(indices->setup != NULL)
		{
			status = _cairo_gl_fill(indices->setup, indices->num_vertices,
				indices->vertices, NULL, 
				indices->num_indices, 
				indices->indices, indices->setup->ctx);
			// cleanup
			status = _cairo_gl_destroy_indices(indices);
			status = _cairo_gl_create_indices(indices);
			indices->setup = setup;
		}
	}
	// we add a triangle to strip, we add 4 vertices and 6 indices;
	while(indices->num_indices + 5 >= indices->capacity ||
		indices->num_vertices + 4 >= indices->capacity)
	{
		// we need to increase
		status = _cairo_gl_increase_indices(indices);
	}
     num_indices = indices->num_indices;
     num_vertices = indices->num_vertices;

	if(num_indices != 0)
	{
		// we are not the first
		last_index = indices->indices[num_indices-1];
		start_index = last_index + 1;
		indices->indices[num_indices] = last_index;
		indices->indices[num_indices+1] = start_index;
		indices->num_indices += 2;
	}
	num_indices = indices->num_indices;
	indices->has_mask_vertices = FALSE;
	for(i = 0; i < 2; i++)
	{
		indices->indices[num_indices+i] = start_index + i;
		indices->vertices[num_vertices*2+i*2] = 
			_cairo_fixed_to_double(quad[i].x);
		indices->vertices[num_vertices*2+i*2+1] = 
			_cairo_fixed_to_double(quad[i].y);
	}
	indices->num_indices += 2;
	indices->num_vertices += 2;
	num_indices = indices->num_indices;
	num_vertices = indices->num_vertices;
	// we reverse order of point 3 and point 4
	start_index = indices->indices[num_indices-1];
	indices->indices[num_indices] = start_index + 1;
	indices->indices[num_indices+1] = start_index + 2;
	indices->vertices[num_vertices*2] = 
			_cairo_fixed_to_double(quad[3].x);
		indices->vertices[num_vertices*2+1] = 
			_cairo_fixed_to_double(quad[3].y);
	indices->vertices[num_vertices*2+2] = 
			_cairo_fixed_to_double(quad[2].x);
		indices->vertices[num_vertices*2+3] = 
			_cairo_fixed_to_double(quad[2].y);
	indices->num_indices += 2;
	indices->num_vertices += 2;

	return CAIRO_STATUS_SUCCESS;
}

cairo_status_t _cairo_gl_add_convex_quad_with_mask(void *closure,
	const cairo_point_t quad[4], const float *mask_quad)
{
        int last_index = 0;
        int start_index = 0;
        int i;
        int num_indices, num_vertices;
		cairo_status_t status;
	_cairo_gl_index_t *indices = (_cairo_gl_index_t *)closure;
	cairo_gl_composite_t *setup = indices->setup;

	// first off, we need to flush if max
	if(indices->num_indices > MAX_INDEX)
	{
		if(indices->setup != NULL)
		{
			if(indices->has_mask_vertices == TRUE)
				status = _cairo_gl_fill(indices->setup, indices->num_vertices,
					indices->vertices, indices->mask_vertices, 
					indices->num_indices, 
					indices->indices, indices->setup->ctx);
			else
				status = _cairo_gl_fill(indices->setup, indices->num_vertices,
					indices->vertices, NULL, 
					indices->num_indices, 
					indices->indices, indices->setup->ctx);
			// cleanup
			status = _cairo_gl_destroy_indices(indices);
			status = _cairo_gl_create_indices(indices);
			indices->setup = setup;
		}
	}
	// we add a triangle to strip, we add 4 vertices and 6 indices;
	while(indices->num_indices + 5 >= indices->capacity ||
		indices->num_vertices + 4 >= indices->capacity)
	{
		// we need to increase
		status = _cairo_gl_increase_indices(indices);
	}
    num_indices = indices->num_indices;
    num_vertices = indices->num_vertices;

	if(num_indices != 0)
	{
		// we are not the first
		last_index = indices->indices[num_indices-1];
		start_index = last_index + 1;
		indices->indices[num_indices] = last_index;
		indices->indices[num_indices+1] = start_index;
		indices->num_indices += 2;
	}
	num_indices = indices->num_indices;
	if(mask_quad != NULL)
		indices->has_mask_vertices = TRUE;
	else
		indices->has_mask_vertices = FALSE;
	for(i = 0; i < 2; i++)
	{
		indices->indices[num_indices+i] = start_index + i;
		indices->vertices[num_vertices*2+i*2] = 
			_cairo_fixed_to_double(quad[i].x);
		indices->vertices[num_vertices*2+i*2+1] = 
			_cairo_fixed_to_double(quad[i].y);
		if(indices->has_mask_vertices == TRUE)
		{
			indices->mask_vertices[num_vertices*2+i*2] = mask_quad[i*2];
			indices->mask_vertices[num_vertices*2+i*2+1] = mask_quad[i*2+1];
		}
	}
	indices->num_indices += 2;
	indices->num_vertices += 2;
	num_indices = indices->num_indices;
	num_vertices = indices->num_vertices;
	// we reverse order of point 3 and point 4
	start_index = indices->indices[num_indices-1];
	indices->indices[num_indices] = start_index + 1;
	indices->indices[num_indices+1] = start_index + 2;
	indices->vertices[num_vertices*2] = 
			_cairo_fixed_to_double(quad[3].x);
		indices->vertices[num_vertices*2+1] = 
			_cairo_fixed_to_double(quad[3].y);
	indices->vertices[num_vertices*2+2] = 
			_cairo_fixed_to_double(quad[2].x);
		indices->vertices[num_vertices*2+3] = 
			_cairo_fixed_to_double(quad[2].y);
	if(indices->has_mask_vertices)
	{
		indices->mask_vertices[num_vertices*2] = mask_quad[6];
		indices->mask_vertices[num_vertices*2+1] = mask_quad[7];
		indices->mask_vertices[num_vertices*2+2] = mask_quad[4];
		indices->mask_vertices[num_vertices*2+3] = mask_quad[5];
	}
	indices->num_indices += 2;
	indices->num_vertices += 2;

	return CAIRO_STATUS_SUCCESS;
}

cairo_status_t
_cairo_gl_clip(cairo_clip_t *clip, cairo_gl_composite_t *setup, 
	cairo_gl_context_t *ctx,
	cairo_gl_surface_t *surface)
{
	cairo_fill_rule_t fill_rule;
	double tolerance = 0.1;
	cairo_status_t status;
	cairo_clip_path_t *clip_path = NULL;
	_cairo_gl_index_t indices;
	cairo_traps_t traps;
    _cairo_gl_index_buf_t *current, *temp = NULL;
    cairo_point_t points[4];
    int got_traps = 0;
    int remaining_boxes = clip->num_boxes;

	
	// enable depth mask
	glDepthMask(GL_TRUE);
	
	// nothing is changed, we use existing cache
	if(surface->stencil_changed == FALSE)
	{
		_cairo_gl_index_buf_t *exist_buf = (_cairo_gl_index_buf_t *)surface->indices_buf;
		if(exist_buf == NULL || exist_buf->indices->num_indices == 0)
		{
			glDisable(GL_STENCIL_TEST);
			glDisable(GL_DEPTH_TEST);
		
			glColorMask(1, 1, 1, 1);
			return CAIRO_STATUS_SUCCESS;
		}
		glEnable(GL_STENCIL_TEST);
		glClear(GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDisable(GL_DEPTH_TEST);
		glStencilOp(GL_REPLACE,  GL_REPLACE, GL_REPLACE);
		glStencilFunc(GL_ALWAYS, 1, 0xffffffff);
		glColorMask(0, 0, 0, 0);
		while(exist_buf != NULL)
		{
			_cairo_gl_index_t *exist_indices = NULL; 
            exist_indices = exist_buf->indices;
			status = _cairo_gl_fill(setup, exist_indices->num_vertices, 
				exist_indices->vertices, NULL, exist_indices->num_indices, 
				exist_indices->indices, setup->ctx);
			if(unlikely(status))
			{
				glColorMask(1, 1, 1, 1);
				return status;
			}
			exist_buf = exist_buf->next;
		}
		glEnable(GL_DEPTH_TEST);
		glColorMask(1, 1, 1, 1);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		glStencilFunc(GL_EQUAL, 1, 1);
		return CAIRO_STATUS_SUCCESS;
	}

	// clean up clip cache
	if(surface->indices_buf != NULL)
	{	
		current = (_cairo_gl_index_buf_t *)surface->indices_buf;
		while(current != NULL)
		{
			free(current->indices->vertices);
			free(current->indices->indices);
			temp = current;
			current = temp->next;
			free(temp->indices);
			free(temp);
		}
		surface->indices_buf = NULL;
	}
	if(clip->path != NULL)
	{
		fill_rule = clip->path->fill_rule;
		clip_path = clip->path;
	}
	
	glEnable(GL_STENCIL_TEST);
	glClear(GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glStencilOp(GL_REPLACE,  GL_REPLACE, GL_REPLACE);
	glStencilFunc(GL_ALWAYS, 1, 0xffffffff);
	glColorMask(0, 0, 0, 0);

	while(clip_path != NULL || remaining_boxes != 0)
	{
		_cairo_gl_index_buf_t *buf = NULL;
   		_cairo_gl_index_buf_t *new_buf	= NULL;
		cairo_fixed_t x_top_left, x_bottom_left;
		cairo_fixed_t x_top_right, x_bottom_right;
		double x1, x2;
		double y1, y2;
		double dx, dy;
		double top, bottom;
		int box_index;

		status = _cairo_gl_create_indices(&indices);
		indices.setup = setup;
		
		if(remaining_boxes != 0)
		{
			got_traps = 1;
			box_index = clip->num_boxes - remaining_boxes;
			points[0].x = clip->boxes[box_index].p1.x;
			points[0].y = clip->boxes[box_index].p1.y;
			points[1].x = clip->boxes[box_index].p1.x;
			points[1].y = clip->boxes[box_index].p2.y;
			points[2].x = clip->boxes[box_index].p2.x;
			points[2].y = clip->boxes[box_index].p2.y;
			points[3].x = clip->boxes[box_index].p2.x;
			points[3].y = clip->boxes[box_index].p1.y;
			
			status = _cairo_gl_add_convex_quad_for_clip(&indices, points);
			if(unlikely(status))
			{
				glDisable(GL_DEPTH_TEST);
				glDisable(GL_STENCIL_TEST);
		
				glColorMask(1, 1, 1, 1);
				status = _cairo_gl_destroy_indices(&indices);
				return status;
			}
			remaining_boxes -= 1;
		}
		
		if(clip_path != NULL)
		{
			int m;
			_cairo_traps_init(&traps);
			status = _cairo_path_fixed_fill_to_traps(&(clip_path->path), fill_rule, tolerance, &traps);
			if(traps.num_traps != 0)
			{
				got_traps = 1;
			}
			for(m = 0; m < traps.num_traps; m++)
			{
				top = _cairo_fixed_to_double(traps.traps[m].top);
				bottom = _cairo_fixed_to_double(traps.traps[m].bottom);
				x1 = _cairo_fixed_to_double(traps.traps[m].left.p1.x);
				x2 = _cairo_fixed_to_double(traps.traps[m].left.p2.x);
				y1 = _cairo_fixed_to_double(traps.traps[m].left.p1.y);
				y2 = _cairo_fixed_to_double(traps.traps[m].left.p2.y);
				dx = x1 - x2;
				dy = y1 - y2;
				x_top_left = _cairo_fixed_from_double(x1 - dx * (y1 - top) / dy);
				x_bottom_left = _cairo_fixed_from_double(x1  - dx * (y1 - bottom) /dy);
				
				x1 = _cairo_fixed_to_double(traps.traps[m].right.p1.x);
				x2 = _cairo_fixed_to_double(traps.traps[m].right.p2.x);
				y1 = _cairo_fixed_to_double(traps.traps[m].right.p1.y);
				y2 = _cairo_fixed_to_double(traps.traps[m].right.p2.y);
				dx = x1 - x2;
				dy = y1 - y2;
				x_top_right = _cairo_fixed_from_double(x1 - dx * (y1 - top) /dy);
				x_bottom_right = _cairo_fixed_from_double(x1  - dx * (y1 - bottom) /dy);
				points[0].x = x_top_left;
				points[0].y = traps.traps[m].top;
				points[1].x = x_bottom_left;
				points[1].y = traps.traps[m].bottom;
				points[2].x = x_bottom_right;
				points[2].y = traps.traps[m].bottom;
				points[3].x = x_top_right;
				points[3].y = traps.traps[m].top;
				status = _cairo_gl_add_convex_quad_for_clip(&indices, points);
				if(unlikely(status))
				{
					_cairo_traps_fini(&traps);
					glDisable(GL_DEPTH_TEST);
					glDisable(GL_STENCIL_TEST);
		
					glColorMask(1, 1, 1, 1);
					status = _cairo_gl_destroy_indices(&indices);
					return status;
				}
			}
			_cairo_traps_fini(&traps);
			clip_path = clip_path->prev;
		}
		// first off, we need to flush if max
		// let's create surface->indices_buf
		buf = (_cairo_gl_index_buf_t *)(indices.setup->dst->indices_buf);
		if(buf != NULL)
		{
			while(buf->next != NULL)
				buf = buf->next;
		}
		new_buf = (_cairo_gl_index_buf_t *)malloc(sizeof(_cairo_gl_index_buf_t));
		new_buf->next = NULL;
		if(buf != NULL)
			buf->next = new_buf;
		else
			setup->dst->indices_buf = new_buf;

		new_buf->indices = (_cairo_gl_index_t *)malloc(sizeof(_cairo_gl_index_t));

		new_buf->indices->vertices = (float *)malloc(sizeof(float)*indices.num_vertices * 2);
		memcpy(new_buf->indices->vertices, indices.vertices, sizeof(float)*2*indices.num_vertices);
		new_buf->indices->indices = (int *)malloc(sizeof(int)*indices.num_indices);
		memcpy(new_buf->indices->indices, indices.indices, sizeof(int)*indices.num_indices);
		new_buf->indices->capacity = indices.capacity;
		new_buf->indices->num_indices = indices.num_indices;
		new_buf->indices->num_vertices = indices.num_vertices;
		new_buf->indices->setup = indices.setup;
		status = _cairo_gl_fill(setup, indices.num_vertices, 
			indices.vertices, NULL, indices.num_indices, indices.indices, setup->ctx);
		status = _cairo_gl_destroy_indices(&indices);
		if(unlikely(status))
		{
			glDisable(GL_STENCIL_TEST);
			glDisable(GL_DEPTH_TEST);
			glColorMask(1, 1, 1, 1);
			return status;
		}
	}
	if(got_traps == 0)
	{
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_DEPTH_TEST);
		
		glColorMask(1, 1, 1, 1);
		return CAIRO_STATUS_SUCCESS;
	}
	// we done stencil test
	glEnable(GL_DEPTH_TEST);
	glColorMask(1, 1, 1, 1);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glStencilFunc(GL_EQUAL, 1, 1);

	return CAIRO_STATUS_SUCCESS;
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
	surface->clip = NULL;
	surface->needs_stencil = FALSE;
	surface->tex_img = 0;
	surface->indices_buf = NULL;
	surface->stencil_changed = FALSE;
	surface->external_tex = FALSE;
	surface->width = width;
	surface->height = height;
    status = _cairo_gl_context_acquire (device, &ctx);
    if (unlikely (status))
	return;

    status = _cairo_gl_context_release (ctx, status);
	surface->data_surface = NULL;
	surface->needs_new_data_surface = FALSE;

	surface->mask_surface = NULL;
	surface->parent_surface = NULL;
	surface->bound_fbo = FALSE;
}

static cairo_surface_t *
_cairo_gl_surface_create_scratch_for_texture (cairo_gl_context_t   *ctx,
					      cairo_content_t	    content,
					      GLuint		    tex,
					      int		    width,
					      int		    height)
{
    cairo_gl_surface_t *surface;

    assert (width <= ctx->max_framebuffer_size && height <= ctx->max_framebuffer_size);

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
	surface->data_surface = NULL;
	surface->needs_new_data_surface = FALSE;

	surface->mask_surface = NULL;
	surface->parent_surface = NULL;
	surface->bound_fbo = FALSE;
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

    glDisable (GL_SCISSOR_TEST);
    glClearColor (r, g, b, a);
    glClear (GL_COLOR_BUFFER_BIT);
	surface->needs_new_data_surface = TRUE;
    return _cairo_gl_context_release (ctx, status);
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
    status = _cairo_gl_context_release (ctx, status);

	surface->external_tex = TRUE;
	surface->owns_tex = FALSE;

	surface->data_surface = NULL;
	surface->needs_new_data_surface = FALSE;

	surface->mask_surface = NULL;
	surface->parent_surface = NULL;
	surface->bound_fbo = FALSE;
    return &surface->base;
}
slim_hidden_def (cairo_gl_surface_create_for_texture);


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
	surface->width = width;
	surface->height = height;
    }
}

int
cairo_gl_surface_get_width (cairo_surface_t *abstract_surface)
{
    cairo_gl_surface_t *surface = (cairo_gl_surface_t *) abstract_surface;

    if (! _cairo_surface_is_gl (abstract_surface))
	return 0;

    return surface->width;
}


int
cairo_gl_surface_get_height (cairo_surface_t *abstract_surface)
{
    cairo_gl_surface_t *surface = (cairo_gl_surface_t *) abstract_surface;

    if (! _cairo_surface_is_gl (abstract_surface))
	return 0;

    return surface->height;
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

	int orig_width, orig_height;
	float new_width_scale, new_height_scale;
    float scale = 1.0;

    if (width < 1 || height < 1)
        return cairo_image_surface_create (_cairo_format_from_content (content),
                                           width, height);

    status = _cairo_gl_context_acquire (surface->device, &ctx);
    if (unlikely (status))
	return _cairo_surface_create_in_error (status);

	orig_width = width;
	orig_height = height;
    if (width > ctx->max_texture_size ||
	height > ctx->max_texture_size)
    {
		new_width_scale = (float)ctx->max_texture_size / (float)width;
		new_height_scale = (float)ctx->max_texture_size / (float)height;
		if(new_width_scale < new_height_scale)
			scale = new_width_scale;
		else
			scale = new_height_scale;

		width = (int)(scale * orig_width);
		height = (int)(scale * orig_height);
	}

    surface = _cairo_gl_surface_create_scratch (ctx, content, width, height);
	if(orig_surface->type == CAIRO_SURFACE_TYPE_GL)
	{
		cairo_gl_surface_t *g_surface = abstract_surface;
		if(g_surface->owns_tex == TRUE && 
			surface->type == CAIRO_SURFACE_TYPE_GL)
		{
			new_surface = (cairo_gl_surface_t *)surface;
			new_surface->external_tex = g_surface->external_tex;
		}
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
	cairo_gl_surface_t *clone = NULL;
	cairo_surface_t *snapshot = NULL;
	cairo_image_surface_t *img_src = NULL;
    void *extra = NULL;

	if(cairo_surface_get_type(src) == CAIRO_SURFACE_TYPE_GL)
	{
		//cairo_gl_surface_t *s = (cairo_gl_surface_t *)src;
		if(extend == 0)
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
				cairo_gl_surface_t *snap1_gl = NULL;
				cairo_gl_composite_t *setup = NULL;
				cairo_pattern_t *pat = NULL;
				cairo_gl_context_t *ctx = NULL;
                GLfloat vertices[] = {0, 0, 0, 0, 0, 0, 0, 0};
                //GLfloat mask_vertices[] = {0, 0, 0, 0, 0, 0, 0, 0};
                //double v[] = {0, 0, 0, 0, 0, 0, 0, 0};
                GLfloat st[] = { 0, 0, 1, 0, 1, 1, 0, 1};
				cairo_operator_t op = CAIRO_OPERATOR_OVER;

				cairo_gl_surface_t *t = (cairo_gl_surface_t *)src;
				cairo_surface_t *snap1 = cairo_surface_create_similar(src,
                                                                                      src->content, t->width, t->height);
				if(snap1 == NULL)
					return NULL;
				snap1_gl = (cairo_gl_surface_t *)snap1;
				setup = (cairo_gl_composite_t *)malloc(sizeof(cairo_gl_composite_t));
				status = _cairo_gl_composite_init(setup, op, snap1_gl, 
					FALSE, NULL);
				if(unlikely (status))
				{
					if(snap1 != NULL)
						cairo_surface_destroy(&snap1_gl->base);
					_cairo_gl_composite_fini(setup);
					free(setup);
					return NULL;
				}
				pat = cairo_pattern_create_for_surface(src);
				setup->source = pat;

				// set up source
				status = _cairo_gl_composite_set_source(setup, pat, 
					0, 0,
					0, 0,
					t->width, t->height,
					t->tex, t->width, t->height);
				if(unlikely(status))
				{
					if(snap1 != NULL)
						cairo_surface_destroy(&snap1_gl->base);
					cairo_pattern_destroy(pat);
					_cairo_gl_composite_fini(setup);
					free(setup);
					return NULL;
				}

				status = _cairo_gl_context_acquire (surface->base.device, &ctx);
				if(unlikely(status))
				{
					if(snap1 != NULL)
						cairo_surface_destroy(&snap1_gl->base);
					_cairo_gl_composite_fini(setup);
					free(setup);
					cairo_pattern_destroy(pat);
					return NULL;
				}
				setup->ctx = ctx;
				_cairo_gl_context_set_destination(ctx, snap1_gl);
				setup->src.type = CAIRO_GL_OPERAND_TEXTURE;
	
				// we have the image uploaded, we need to setup vertices
				vertices[0] = 0;
				vertices[1] = 0;
				vertices[2] = snap1_gl->width;
				vertices[3] = 0;
				vertices[4] = snap1_gl->width;
				vertices[5] = snap1_gl->height;
				vertices[6] = 0;
				vertices[7] = snap1_gl->height;
				st[0] = 0;
				st[1] = 0;
				st[2] = 1;
				st[3] = 0;
				st[4] = 1;
				st[5] = 1;
				st[6] = 0;
				st[7] = 1;
			
				status = _cairo_gl_composite_begin_constant_color(setup, 
					8, 
					vertices, 
					st,
					NULL,
					NULL,
					ctx);
				if(unlikely(status))
				{
					if(snap1 != NULL)
						cairo_surface_destroy(&snap1_gl->base);
					_cairo_gl_composite_fini(setup);
					free(setup);
					status = _cairo_gl_context_release(ctx, status);
					cairo_pattern_destroy(pat);
					return NULL;
				}

				_cairo_gl_composite_fill_constant_color(ctx, 4, NULL);
				_cairo_gl_composite_fini(setup);
				free(setup);
				glDisable(GL_STENCIL_TEST);
				glDisable(GL_DEPTH_TEST);
				glDepthMask(GL_FALSE);
				status = _cairo_gl_context_release(ctx, status);
				cairo_pattern_destroy(pat);
	
				_cairo_surface_attach_snapshot(src, &snap1_gl->base, _cairo_gl_surface_remove_from_cache);
				return (cairo_gl_surface_t *)cairo_surface_reference(&(snap1_gl->base));
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
			clone = (cairo_gl_surface_t *)
				_cairo_gl_surface_create_similar(&surface->base, 
				src->content,
				recording_extents.width, recording_extents.height);
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
		clone = (cairo_gl_surface_t *)
			_cairo_gl_surface_create_similar(&surface->base, 
				((cairo_surface_t *)img_src)->content,
				img_src->width, img_src->height);

		if(clone == NULL || cairo_surface_get_type(&clone->base) == CAIRO_SURFACE_TYPE_IMAGE)
		{
			if(cairo_surface_get_type(src) != CAIRO_SURFACE_TYPE_IMAGE)
			{
				cairo_surface_destroy(&img_src->base);
				if(extra != NULL)
					free(extra);
			}
			if(clone != NULL)
			{
				cairo_surface_destroy(&clone->base);
			}
			
			return NULL;
		}
		status = _cairo_gl_surface_draw_image(clone, img_src, 0, 0,
			img_src->width, img_src->height, 0, 0);
		if(cairo_surface_get_type(src) != CAIRO_SURFACE_TYPE_IMAGE)
		{
			cairo_surface_destroy(&img_src->base);
			if(extra != NULL)
				free(extra);
		}
		if(unlikely (status))
		{
			cairo_surface_destroy(&clone->base);
			return NULL;
		}
	}

	// setup image surface snapshot of cloned gl surface
	_cairo_surface_attach_snapshot(src, &clone->base, _cairo_gl_surface_remove_from_cache);
	return _cairo_gl_generate_clone(surface, &clone->base, extend);
}

cairo_status_t
_cairo_gl_surface_draw_image (cairo_gl_surface_t *dst,
			      cairo_image_surface_t *src,
			      int src_x, int src_y,
			      int width, int height,
			      int dst_x, int dst_y)
{
    GLenum internal_format, format, type;
    cairo_bool_t has_alpha, needs_swap;
    cairo_image_surface_t *clone = NULL;
    cairo_gl_context_t *ctx;
    int cpp;
    cairo_status_t status = CAIRO_STATUS_SUCCESS;

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


	if (data_start_gles2)
	    free (data_start_gles2);

	/* If we just treated some rgb-only data as rgba, then we have to
	 * go back and fix up the alpha channel where we filled in this
	 * texture data.
	 */
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
        cairo_surface_t *tmp;
        
        tmp = _cairo_gl_surface_create_scratch (ctx,
                                                dst->base.content,
                                                width, height);
        if (unlikely (tmp->status)) {
            cairo_surface_destroy (tmp);
            goto FAIL;
        }
        status = _cairo_gl_surface_draw_image ((cairo_gl_surface_t *) tmp,
                                               src,
                                               src_x, src_y,
                                               width, height,
                                               0, 0);
        if (status == CAIRO_STATUS_SUCCESS) {
            cairo_surface_pattern_t tmp_pattern;

            _cairo_pattern_init_for_surface (&tmp_pattern, tmp);
            _cairo_gl_surface_composite (CAIRO_OPERATOR_SOURCE,
                                         &tmp_pattern.base,
                                         NULL,
                                         dst,
                                         0, 0,
                                         0, 0,
                                         dst_x, dst_y,
                                         width, height,
                                         NULL);
            _cairo_pattern_fini (&tmp_pattern.base);
        }

        cairo_surface_destroy (tmp);
    }

FAIL:
    if (ctx->gl_flavor == CAIRO_GL_FLAVOR_DESKTOP)
	glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);

    status = _cairo_gl_context_release (ctx, status);

    if (clone)
        cairo_surface_destroy (&clone->base);

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
    if (ctx->gl_flavor == CAIRO_GL_FLAVOR_ES) 
	{
		format = GL_RGBA;
		if (!_cairo_is_little_endian ()) 
		{
	    	ASSERT_NOT_REACHED;
	    /* TODO: Add r8g8b8a8 support to pixman and enable this
	       if (surface->base.content == CAIRO_CONTENT_COLOR)
	       pixman_format = PIXMAN_r8g8b8x8;
	       else
	       pixman_format = PIXMAN_r8g8b8a8;
	    */
		}
		else 
		{
	    	if (surface->base.content == CAIRO_CONTENT_COLOR)
			{
				pixman_format = PIXMAN_x8b8g8r8;
				//pixman_format = PIXMAN_x8r8g8b8;
				cpp = 4;
			}
	    	else if(surface->base.content == CAIRO_CONTENT_COLOR_ALPHA)
			{
				pixman_format = PIXMAN_a8b8g8r8;
				//pixman_format = PIXMAN_a8r8g8b8;
				cpp = 4;
			}
			else
			{
				format = GL_ALPHA;
				pixman_format = PIXMAN_a8;
				cpp = 1;
			}
		}
		type = GL_UNSIGNED_BYTE;
    }

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
    _cairo_gl_index_buf_t *current, *temp = NULL;

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
		surface->fb = 0;
	}
    if (surface->owns_tex)
	{
		glDeleteTextures (1, &surface->tex);
		surface->tex = 0;
	}
	if(surface->tex_img != 0)
		glDeleteTextures(1, (GLuint*)&surface->tex_img);

	// release clip
	if(surface->clip != NULL)
	{
		_cairo_clip_destroy(surface->clip);
		surface->clip = NULL;
	}
	if(surface->indices_buf != NULL)
	{	
		current = (_cairo_gl_index_buf_t *)surface->indices_buf;
		while(current != NULL)
		{
			free(current->indices->vertices);
			free(current->indices->indices);
			temp = current;
			current = temp->next;
			free(temp->indices);
			free(temp);
		}
		surface->indices_buf = NULL;
	}
	surface->stencil_changed = FALSE;
	surface->external_tex = FALSE;

	if(surface->data_surface != NULL)
		cairo_surface_destroy(surface->data_surface);
	if(surface->mask_surface != NULL)
		cairo_surface_destroy(&(surface->mask_surface->base));
	surface->parent_surface = NULL;
	surface->bound_fbo = FALSE;
		
	surface->needs_new_data_surface = FALSE;

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
    extents.width = surface->width;
    extents.height = surface->height;
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
					   image_rect->x, image_rect->y);
    /* as we created the image, its format should be directly applicable */
    assert (status == CAIRO_STATUS_SUCCESS);

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
					       0, 0);
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
                                                   dst_x, dst_y);
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

    status = _cairo_gl_composite_init (&setup, op, dst,
                                       FALSE,
                                       /* XXX */ NULL);
    if (unlikely (status))
        goto CLEANUP;

    _cairo_pattern_init_solid (&solid, color);
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

    status = _cairo_gl_composite_begin (&setup, &ctx);
    if (unlikely (status))
        goto CLEANUP;

    for (i = 0; i < num_rects; i++) {
        _cairo_gl_composite_emit_rect (ctx,
                                       rects[i].x,
                                       rects[i].y,
                                       rects[i].x + rects[i].width,
                                       rects[i].y + rects[i].height,
                                       0);
    }

    status = _cairo_gl_context_release (ctx, status);

  CLEANUP:
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
    rectangle->width  = surface->width;
    rectangle->height = surface->height;

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

static cairo_int_status_t
_cairo_gl_surface_mask (void *abstract_surface,
	cairo_operator_t op,
	const cairo_pattern_t *source,
	const cairo_pattern_t *mask,
	cairo_clip_t *clip)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_composite_rectangles_t extents; 
    //cairo_box_t boxes_stack[32] ,*clip_boxes = boxes_stack;
    //cairo_clip_t local_clip;
    //cairo_bool_t have_clip = FALSE;
    //int num_boxes = ARRAY_LENGTH (boxes_stack);
    //cairo_polygon_t polygon;
    cairo_status_t status;
    cairo_gl_composite_t *setup = NULL;
    cairo_gl_context_t *ctx = NULL;
    cairo_gl_surface_t *clone = NULL;
    //cairo_surface_t *snapshot = NULL;
    cairo_solid_pattern_t *solid = NULL;
    cairo_gl_surface_t *mask_clone = NULL;
    //cairo_surface_t *mask_snapshot = NULL;
    //float temp_width, temp_height;
	//_cairo_gl_path_t *current;
	//char *colors;
	//int stride;
	//int index;
    GLfloat vertices[] = {0, 0, 0, 0, 0, 0, 0, 0};
    GLfloat mask_vertices[] = {0, 0, 0, 0, 0, 0, 0, 0};
    double v[] = {0, 0, 0, 0, 0, 0, 0, 0};
    GLfloat st[] = { 0, 0, 1, 0, 1, 1, 0, 1};
    GLfloat mask_st[] = { 0, 0, 1, 0, 1, 1, 0, 1};
    GLfloat colors[] = {0, 0, 0, 0,
                        0, 0, 0, 0,
                        0, 0, 0, 0,
                        0, 0, 0, 0};
    int i = 0;
    cairo_matrix_t m, m1;
    int extend = 0;

	if(mask == NULL)
		status = _cairo_composite_rectangles_init_for_paint(&extents,
			surface->width,
			surface->height,
			op, source, clip);
	else
	{
		if(op == CAIRO_OPERATOR_IN ||
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

	
	if(unlikely(status))
		return status;
	
	// upload image
	if(source->extend == CAIRO_EXTEND_REPEAT ||
		source->extend == CAIRO_EXTEND_REFLECT)
		extend = 1;
	// check has snapsot
	if(source->type == CAIRO_PATTERN_TYPE_SURFACE)
	{
		cairo_surface_t *src = ((cairo_surface_pattern_t *)source)->surface;
		clone = _cairo_gl_generate_clone(surface, src, extend);
		if(clone == NULL)
			return UNSUPPORTED("create_clone failed");
	}
	else if(source->type == CAIRO_PATTERN_TYPE_SOLID)
		solid = (cairo_solid_pattern_t *)source;
	setup = (cairo_gl_composite_t *)malloc(sizeof(cairo_gl_composite_t));
	
	status = _cairo_gl_composite_init(setup, op, surface, FALSE,
		&extents.bounded);
	if(unlikely (status))
	{
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		_cairo_gl_composite_fini(setup);
		free(setup);
		return status;
	}
	extend = 0;
	if(mask != NULL)
	{
		if(mask->extend == CAIRO_EXTEND_REPEAT ||	
			mask->extend == CAIRO_EXTEND_REFLECT)
			extend = 1;
		if(mask->type == CAIRO_PATTERN_TYPE_SURFACE)
		{
			cairo_surface_t *msk = ((cairo_surface_pattern_t *)mask)->surface;
			mask_clone = _cairo_gl_generate_clone(surface, msk, extend);
			if(mask_clone == NULL)
			{
				if(clone != NULL)
					cairo_surface_destroy(&clone->base);
				_cairo_gl_composite_fini(setup);
				free(setup);
				glDisable(GL_STENCIL_TEST);
				glDisable(GL_DEPTH_TEST);
				glDepthMask(GL_FALSE);
				status = _cairo_gl_context_release(ctx, status);
				return UNSUPPORTED("generate_clone for mask failed");
			}
		}
	}

	setup->source = (cairo_pattern_t*)source;

	// set up source
	if(clone == NULL)
		status = _cairo_gl_composite_set_source(setup,
			source, extents.bounded.x, extents.bounded.y,
			extents.bounded.x, extents.bounded.y, 
			extents.bounded.width, extents.bounded.height,
			0, 0, 0);
	else
	{
            float temp_width = clone->width;
            float temp_height = clone->height;

		status = _cairo_gl_composite_set_source(setup,
			source, extents.bounded.x, extents.bounded.y,
			extents.bounded.x, extents.bounded.y, 
			extents.bounded.width, extents.bounded.height,
			clone->tex, (int)temp_width, (int)temp_height);
	}
	if(unlikely(status))
	{
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		_cairo_gl_composite_fini(setup);
		free(setup);
		return status;
	}

	status = _cairo_gl_context_acquire (surface->base.device, &ctx);
	if(unlikely(status))
	{
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		_cairo_gl_composite_fini(setup);
		free(setup);
		return status;
	}
	setup->ctx = ctx;
	_cairo_gl_context_set_destination(ctx, surface);
	if(clip != NULL)
	{
		if(surface->clip != NULL)
		{
			if(!_cairo_clip_equal(clip, surface->clip))
			{
				_cairo_clip_destroy(surface->clip);
				surface->clip = _cairo_clip_copy(clip);
				surface->needs_stencil = TRUE;
				surface->stencil_changed = TRUE;
			}
			else
			{
				surface->needs_stencil = TRUE;
				surface->stencil_changed = FALSE;
			}
		}
		else
		{
			surface->clip = _cairo_clip_copy(clip);
			surface->needs_stencil = TRUE;
			surface->stencil_changed = TRUE;
		}
	}
	else
	{
		if(surface->clip != NULL)
		{
			_cairo_clip_destroy(surface->clip);
			surface->clip = NULL;
			surface->needs_stencil = FALSE;
			surface->stencil_changed = TRUE;
		}
	}
	
	if(surface->needs_stencil == TRUE)
	{
		status = _cairo_gl_clip(clip, setup, ctx, surface);
		if(unlikely(status))
		{
			if(clone != NULL)
				cairo_surface_destroy(&clone->base);
			_cairo_gl_composite_fini(setup);
			free(setup);
			glDisable(GL_STENCIL_TEST);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
			status = _cairo_gl_context_release(ctx, status);
			return status;
		}
	}

	if(mask_clone != NULL)
	{
            float temp_width = mask_clone->width;
            float temp_height = mask_clone->height;

		status = _cairo_gl_composite_set_mask(setup, mask, 
			extents.bounded.x, extents.bounded.y,
			extents.bounded.x, extents.bounded.y, 
			extents.bounded.width, extents.bounded.height,
			mask_clone->tex, (int)temp_width, (int)temp_height); 
	}
	else
		status = _cairo_gl_composite_set_mask(setup, mask, 
			extents.bounded.x, extents.bounded.y,
			extents.bounded.x, extents.bounded.y, 
			extents.bounded.width, extents.bounded.height,
			0, 0, 0);
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
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		if(mask_clone != NULL)
			cairo_surface_destroy(&mask_clone->base);
		_cairo_gl_composite_fini(setup);
		free(setup);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		status = _cairo_gl_context_release(ctx, status);
		return CAIRO_INT_STATUS_UNSUPPORTED;
	}
	
	// we have the image uploaded, we need to setup vertices
	v[0] = extents.bounded.x;
	v[1] = extents.bounded.y;
	v[2] = extents.bounded.x + extents.bounded.width;
	v[3] = extents.bounded.y;
	v[4] = extents.bounded.x + extents.bounded.width;
	v[5] = extents.bounded.y + extents.bounded.height;
	v[6] = extents.bounded.x;
	v[7] = extents.bounded.y + extents.bounded.height;
	for(i = 0; i < 8; i++)
		vertices[i] = v[i];

	if(source->type == CAIRO_PATTERN_TYPE_SURFACE)
	{
		// compute s, t for bounding box
		cairo_matrix_init_scale(&m, 1.0, 1.0);
		cairo_matrix_multiply(&m, &m, &source->matrix);
		cairo_matrix_init_scale(&m1, 1.0 / clone->width,
			1.0 / clone->height);
		cairo_matrix_multiply(&m, &m, &m1);
		
		cairo_matrix_transform_point(&m, &v[0], &v[1]);
		cairo_matrix_transform_point(&m, &v[2], &v[3]);
		cairo_matrix_transform_point(&m, &v[4], &v[5]);
		cairo_matrix_transform_point(&m, &v[6], &v[7]);
		for(i = 0; i < 8; i++)
			st[i] = v[i];

		if(mask != NULL && mask_clone != NULL)
		{
			v[0] = extents.bounded.x;
			v[1] = extents.bounded.y;
			v[2] = extents.bounded.x + extents.bounded.width;
			v[3] = extents.bounded.y;
			v[4] = extents.bounded.x + extents.bounded.width;
			v[5] = extents.bounded.y + extents.bounded.height;
			v[6] = extents.bounded.x;
			v[7] = extents.bounded.y + extents.bounded.height;
			cairo_matrix_init_scale(&m, 1.0, 1.0);
			cairo_matrix_multiply(&m, &m, &mask->matrix);
			cairo_matrix_init_scale(&m1, 1.0 / mask_clone->width, 
				1.0 / mask_clone->height);
			cairo_matrix_multiply(&m, &m, &m1);

			cairo_matrix_transform_point(&m, &v[0], &v[1]);
			cairo_matrix_transform_point(&m, &v[2], &v[3]);
			cairo_matrix_transform_point(&m, &v[4], &v[5]);
			cairo_matrix_transform_point(&m, &v[6], &v[7]);
			for(i = 0; i < 8; i++)
				mask_st[i] = v[i];
			status = _cairo_gl_composite_begin_constant_color(setup, 
				8, 
				vertices, 
				st,
				mask_vertices,
				mask_st,
				ctx);
		}
		else
			status = _cairo_gl_composite_begin_constant_color(setup, 
				8, 
				vertices, 
				st,
				NULL,
				NULL,
				ctx);
	}
	else if(source->type == CAIRO_PATTERN_TYPE_SOLID)
	{
		if(unlikely(status))
		{
			if(clone != NULL)
				cairo_surface_destroy(&clone->base);
			if(mask_clone != NULL)
				cairo_surface_destroy(&mask_clone->base);
			_cairo_gl_composite_fini(setup);
			free(setup);
			glDisable(GL_STENCIL_TEST);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
			//cairo_status_t status1 = _cairo_gl_context_release(ctx, status);
			return status;
		}
		v[0] = extents.bounded.x;
		v[1] = extents.bounded.y;
		v[2] = extents.bounded.x + extents.bounded.width;
		v[3] = extents.bounded.y;
		v[4] = extents.bounded.x + extents.bounded.width;
		v[5] = extents.bounded.y + extents.bounded.height;
		v[6] = extents.bounded.x;
		v[7] = extents.bounded.y + extents.bounded.height;
		if(mask != NULL && mask_clone != NULL)
		{
			cairo_matrix_init_scale(&m, 1.0, 1.0);
			cairo_matrix_multiply(&m, &m, &mask->matrix);
			cairo_matrix_init_scale(&m1, 1.0 / mask_clone->width, 
				1.0 / mask_clone->height);
			cairo_matrix_multiply(&m, &m, &m1);

			cairo_matrix_transform_point(&m, &v[0], &v[1]);
			cairo_matrix_transform_point(&m, &v[2], &v[3]);
			cairo_matrix_transform_point(&m, &v[4], &v[5]);
			cairo_matrix_transform_point(&m, &v[6], &v[7]);
			for(i = 0; i < 8; i++)
				mask_st[i] = v[i];
		}
		
		colors[0] = solid->color.red;
		colors[1] = solid->color.green;
		colors[2] = solid->color.blue;
		colors[3] = solid->color.alpha;
		colors[4] = colors[8] = colors[12] = colors[0];
		colors[5] = colors[9] = colors[13] = colors[1];
		colors[6] = colors[10] = colors[14] = colors[2];
		colors[7] = colors[11] = colors[15] = colors[3];
		if(mask != NULL && mask_clone != NULL)
			status = _cairo_gl_composite_begin_constant_color(setup, 
				4, 
				vertices, 
				colors,
				vertices,
				mask_st,
				ctx);
		else
			status = _cairo_gl_composite_begin_constant_color(setup, 
				4, 
				vertices, 
				colors,
				NULL,
				NULL,
				ctx);
	}
	else if(source->type == CAIRO_PATTERN_TYPE_LINEAR ||
		source->type == CAIRO_PATTERN_TYPE_RADIAL)
	{
		v[0] = extents.bounded.x;
		v[1] = extents.bounded.y;
		v[2] = extents.bounded.x + extents.bounded.width;
		v[3] = extents.bounded.y;
		v[4] = extents.bounded.x + extents.bounded.width;
		v[5] = extents.bounded.y + extents.bounded.height;
		v[6] = extents.bounded.x;
		v[7] = extents.bounded.y + extents.bounded.height;
		if(mask != NULL && mask_clone != NULL)
		{
			cairo_matrix_init_scale(&m, 1.0, 1.0);
			cairo_matrix_multiply(&m, &m, &mask->matrix);
			cairo_matrix_init_scale(&m1, 1.0 / mask_clone->width, 
				1.0 / mask_clone->height);
			cairo_matrix_multiply(&m, &m, &m1);

			cairo_matrix_transform_point(&m, &v[0], &v[1]);
			cairo_matrix_transform_point(&m, &v[2], &v[3]);
			cairo_matrix_transform_point(&m, &v[4], &v[5]);
			cairo_matrix_transform_point(&m, &v[6], &v[7]);
			for(i = 0; i < 8; i++)
				mask_st[i] = v[i];
		}
		if(mask != NULL && mask_clone != NULL)
			status = _cairo_gl_composite_begin_constant_color(setup, 
				4, 
				vertices, 
				colors,
				vertices,
				mask_st,
				ctx);
		else
			status = _cairo_gl_composite_begin_constant_color(setup, 
				4, 
				vertices, 
				colors,
				NULL,
				NULL,
				ctx);
	}

	if(unlikely(status))
	{
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		if(mask_clone != NULL)
			cairo_surface_destroy(&mask_clone->base);
		_cairo_gl_composite_fini(setup);
		free(setup);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		status = _cairo_gl_context_release(ctx, status);
		return status;
	}


	_cairo_gl_composite_fill_constant_color(ctx, 4, NULL);
	// we done drawings
	_cairo_gl_composite_fini(setup);
	if(clone != NULL)
	{
		cairo_surface_destroy(&clone->base);
	}
	if(mask_clone != NULL)
	{
		cairo_surface_destroy(&mask_clone->base);
	}
	free(setup);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	status = _cairo_gl_context_release(ctx, status);
	surface->needs_new_data_surface = TRUE;
	return status;
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
    _cairo_gl_index_t indices;
    // Henry Song
    cairo_gl_composite_t *setup = NULL;
    cairo_gl_context_t *ctx = NULL;
    cairo_color_t color;
    cairo_gl_surface_t *clone = NULL;
    //cairo_surface_t *snapshot = NULL;
    int extend = 0;
    int v;

	//cairo_rectangle_int_t *clip_extent, stroke_extent;

	if(antialias != CAIRO_ANTIALIAS_NONE || clip != NULL)
	{
		//printf("stroke set needs super sampling\n");
		//surface->needs_super_sampling = TRUE;
	}
	
    status = _cairo_composite_rectangles_init_for_stroke (&extents,
							  surface->width,
							  surface->height,
							  op, source,
							  path, style, ctm,
							  clip);
    if (unlikely (status))
		return status;
	
	if(extents.is_bounded == 0)
	{
		cairo_surface_pattern_t mask_pattern;

		// it is unbounded operator
		// get this surface's mask
		if(surface->mask_surface != NULL && 
		   (surface->mask_surface->width != surface->width ||
		    surface->mask_surface->height != surface->height))
		{
			cairo_surface_destroy(&(surface->mask_surface->base));
			surface->mask_surface = NULL;
		}
		if(surface->mask_surface == NULL)
		{
			cairo_surface_t *mask_surface = cairo_surface_create_similar(&surface->base, CAIRO_CONTENT_COLOR_ALPHA, surface->width, surface->height);
			if(mask_surface == NULL || cairo_surface_get_type(mask_surface) != CAIRO_SURFACE_TYPE_GL || unlikely(mask_surface->status))
			{
				cairo_surface_destroy(mask_surface);
				surface->mask_surface = NULL;
			}
			else
			{
				surface->mask_surface = (cairo_gl_surface_t *)mask_surface;
				surface->mask_surface->parent_surface = surface;
				surface->mask_surface->mask_surface = NULL;
			}
		}
		if(surface->mask_surface == NULL)
		{
			return CAIRO_INT_STATUS_UNSUPPORTED;
		}
		
		color.red = 0;
		color.green = 0;
		color.blue = 0;
		color.alpha = 0;
		status = _cairo_gl_surface_clear(surface->mask_surface, &color);
		surface->mask_surface->bound_fbo = TRUE;
		
		status = _cairo_gl_surface_stroke(surface->mask_surface, 
			CAIRO_OPERATOR_OVER, source, path, style, ctm, ctm_inverse,
				tolerance, antialias, NULL);
		if(unlikely(status))
		{
			return status;
		}
		surface->mask_surface->bound_fbo = TRUE;
		surface->mask_surface->base.is_clear = FALSE;
		_cairo_pattern_init_for_surface(&mask_pattern, &surface->mask_surface->base);
		mask_pattern.base.has_component_alpha = FALSE;
		status = _cairo_surface_paint(&surface->base, op, &(mask_pattern.base), clip);
		_cairo_pattern_fini(&mask_pattern.base);
		surface->mask_surface->bound_fbo = FALSE;
		return status;
	}


	status = _cairo_gl_context_acquire (surface->base.device, &ctx);
	if(unlikely(status))
		return status;

	// upload image
	if(source->type == CAIRO_PATTERN_TYPE_SURFACE)
	{
		cairo_surface_t *src = ((cairo_surface_pattern_t *)source)->surface;
		if(source->extend == CAIRO_EXTEND_REPEAT || 
		   source->extend == CAIRO_EXTEND_REFLECT)
		   	extend = 1;
		clone = _cairo_gl_generate_clone(surface, src, extend);
		if(clone == NULL)
		{
			status = _cairo_gl_context_release(ctx, status);
			return UNSUPPORTED("create_clone failed");
		}
	}
	
	setup = (cairo_gl_composite_t *)malloc(sizeof(cairo_gl_composite_t));
	
	status = _cairo_gl_composite_init(setup, op, surface, FALSE,
		&extents.bounded);
	if(unlikely (status))
	{
		_cairo_gl_composite_fini(setup);
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		free(setup);
		status = _cairo_gl_context_release(ctx, status);
		return status;
	}

	setup->source = (cairo_pattern_t*)source;

	if(clone == NULL)
		status = _cairo_gl_composite_set_source(setup,
			source, extents.bounded.x, extents.bounded.y,
			extents.bounded.x, extents.bounded.y, 
			extents.bounded.width, extents.bounded.height,
			0, 0, 0);
	else
	{
            float temp_width = clone->width;
            float temp_height = clone->height;
		status = _cairo_gl_composite_set_source(setup,
			source, extents.bounded.x, extents.bounded.y,
			extents.bounded.x, extents.bounded.y, 
			extents.bounded.width, extents.bounded.height,
			clone->tex, (int)temp_width, (int)temp_height); 
	}

	if(unlikely(status))
	{
		_cairo_gl_composite_fini(setup);
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		free(setup);
		status = _cairo_gl_context_release(ctx, status);
		return status;
	}


	setup->ctx = ctx;
	_cairo_gl_context_set_destination(ctx, surface);

	if(clip != NULL)
	{
		if(surface->clip != NULL)
		{
			if(!_cairo_clip_equal(clip, surface->clip))
			{
				_cairo_clip_destroy(surface->clip);
				surface->clip = _cairo_clip_copy(clip);
				surface->needs_stencil = TRUE;
				surface->stencil_changed = TRUE;
			}
			else
			{
				surface->needs_stencil = TRUE;
				surface->stencil_changed = FALSE;
			}
		}
		else
		{
			surface->clip = _cairo_clip_copy(clip);
			surface->needs_stencil = TRUE;
			surface->stencil_changed = TRUE;
		}
	}
	else
	{
		if(surface->clip != NULL)
		{
			_cairo_clip_destroy(surface->clip);
			surface->clip = NULL;
			surface->needs_stencil = FALSE;
			surface->stencil_changed = TRUE;
		}
	}
	if(surface->needs_stencil == TRUE)
	{
		status = _cairo_gl_clip(clip, setup, ctx, surface);
		if(unlikely(status))
		{
			if(clone != NULL)
				cairo_surface_destroy(&clone->base);
			_cairo_gl_composite_fini(setup);
			free(setup);
			glDisable(GL_STENCIL_TEST);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
			status = _cairo_gl_context_release(ctx, status);
			return status;
		}
	}
	status = _cairo_gl_create_indices(&indices);
	indices.setup = setup;

	status = _cairo_path_fixed_stroke_to_shaper(path,
												style,
												ctm,
												ctm_inverse,
												tolerance,
												_cairo_gl_add_triangle,
												_cairo_gl_add_triangle_fan,
												_cairo_gl_add_convex_quad,
												&indices);


	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &v);
	// fill it, we fix t later
	status = _cairo_gl_fill(setup, indices.num_vertices, 
		indices.vertices, NULL, indices.num_indices, indices.indices,
		setup->ctx);
	
	if(clone != NULL)
		cairo_surface_destroy(&clone->base);
	status = _cairo_gl_destroy_indices(&indices);
	_cairo_gl_composite_fini(setup);
	free(setup);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	status = _cairo_gl_context_release(ctx, status);
	surface->needs_new_data_surface = TRUE;
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
	
    cairo_composite_rectangles_t extents;
    //cairo_box_t boxes_stack[32], *clip_boxes = boxes_stack;
    //cairo_clip_t local_clip;
    //cairo_bool_t have_clip = FALSE;
    //int num_boxes = ARRAY_LENGTH (boxes_stack);
    //cairo_polygon_t polygon;
    cairo_status_t status;
    cairo_color_t color;
    cairo_surface_pattern_t mask_pattern;
    cairo_gl_surface_t *clone = NULL;
    //cairo_surface_t *snapshot = NULL;
    cairo_gl_context_t *ctx = NULL;
    cairo_traps_t traps;
    //cairo_polygon_t polygons
    _cairo_gl_index_t indices;
    cairo_point_t points[4];
    int m;
    cairo_fixed_t x_top_left, x_bottom_left;
    cairo_fixed_t x_top_right, x_bottom_right;
    double x1, x2;
    double y1, y2;
    double dx, dy;
    double top, bottom;
    int extend = 0;

	cairo_gl_composite_t *setup;

	if(antialias != CAIRO_ANTIALIAS_NONE || clip != NULL)
	{
		//printf("fill set needs super sampling\n");
		//surface->needs_super_sampling = TRUE;
	}
		

	// we keep this such that when clip and path do not intersect, 
	// we simply return without actual draw - optimization
	status = _cairo_composite_rectangles_init_for_fill (&extents,
							surface->width,
							surface->height,
							op, source, path,
							clip);
    if (unlikely (status))
	return status;
	if(extents.is_bounded == 0)
	{
		// it is unbounded operator
		// get this surface's mask
		if(surface->mask_surface != NULL && 
		   (surface->mask_surface->width != surface->width ||
		    surface->mask_surface->height != surface->height))
		{
			cairo_surface_destroy(&(surface->mask_surface->base));
			surface->mask_surface = NULL;
		}
		if(surface->mask_surface == NULL)
		{
			cairo_surface_t *mask_surface = cairo_surface_create_similar(&surface->base, CAIRO_CONTENT_COLOR_ALPHA, surface->width, surface->height);
			if(mask_surface == NULL || cairo_surface_get_type(mask_surface) != CAIRO_SURFACE_TYPE_GL || unlikely(mask_surface->status))
			{
				cairo_surface_destroy(mask_surface);
				surface->mask_surface = NULL;
			}
			else
			{
				surface->mask_surface = (cairo_gl_surface_t *)mask_surface;
				surface->mask_surface->parent_surface = surface;
				surface->mask_surface->mask_surface = NULL;
			}
		}
		if(surface->mask_surface == NULL)
		{
			return CAIRO_INT_STATUS_UNSUPPORTED;
		}
		
		color.red = 0;
		color.green = 0;
		color.blue = 0;
		color.alpha = 0;
		status = _cairo_gl_surface_clear(surface->mask_surface, &color);
		surface->mask_surface->bound_fbo = TRUE;
		
		status = _cairo_gl_surface_fill(surface->mask_surface, 
			CAIRO_OPERATOR_OVER, source, path, fill_rule,
				tolerance, antialias, NULL);
		if(unlikely(status))
		{
			return status;
		}
		surface->mask_surface->bound_fbo = TRUE;
		surface->mask_surface->base.is_clear = FALSE;
		_cairo_pattern_init_for_surface(&mask_pattern, (cairo_surface_t*)surface->mask_surface);
		mask_pattern.base.has_component_alpha = FALSE;
		status = _cairo_surface_paint(&surface->base, op, &(mask_pattern.base), clip);
		_cairo_pattern_fini(&mask_pattern.base);
		surface->mask_surface->bound_fbo = FALSE;
		return status;
	}

	// upload image
	if(source->type == CAIRO_PATTERN_TYPE_SURFACE)
	{
		cairo_surface_t *src = ((cairo_surface_pattern_t *)source)->surface;
		if(source->extend == CAIRO_EXTEND_REPEAT || 
		   source->extend == CAIRO_EXTEND_REFLECT)
		   	extend = 1;
		clone = _cairo_gl_generate_clone(surface, src, extend);

		if(clone == NULL)
		{
			status = _cairo_gl_context_release(ctx, status);
			return UNSUPPORTED("create_clone failed");
		}
	}
	
	status = _cairo_gl_context_acquire (surface->base.device, &ctx);
	if(unlikely(status))
	{
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		return status;
	}
	setup = (cairo_gl_composite_t *)malloc(sizeof(cairo_gl_composite_t));
	
	status = _cairo_gl_composite_init(setup, op, surface, FALSE,
		&extents.bounded);
	if(unlikely (status))
	{
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		_cairo_gl_composite_fini(setup);
		free(setup);
		status = _cairo_gl_context_release(ctx, status);
		return status;
	}
	setup->source = source;
	if(clone == NULL)
		status = _cairo_gl_composite_set_source(setup,
			source, extents.bounded.x, extents.bounded.y,
			extents.bounded.x, extents.bounded.y, 
			extents.bounded.width, extents.bounded.height,
			0, 0, 0);
	else
	{
            float temp_width = clone->width;
            float temp_height = clone->height;
		status = _cairo_gl_composite_set_source(setup,
			source, extents.bounded.x, extents.bounded.y,
			extents.bounded.x, extents.bounded.y, 
			extents.bounded.width, extents.bounded.height,
			clone->tex, (int)temp_width, (int)temp_height); 
	}
	if(unlikely(status))
	{
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		_cairo_gl_composite_fini(setup);
		free(setup);
		status = _cairo_gl_context_release(ctx, status);
		return status;
	}

	// let's acquire context, set surface
	setup->ctx = ctx;
	_cairo_gl_context_set_destination(ctx, surface);
	// remember, we have set the current context, we need to release it
	// when done

	if(clip != NULL)
	{
		if(surface->clip != NULL)
		{
			if(!_cairo_clip_equal(clip, surface->clip))
			{
				_cairo_clip_destroy(surface->clip);
				surface->clip = _cairo_clip_copy(clip);
				surface->needs_stencil = TRUE;
				surface->stencil_changed = TRUE;
			}
			else
			{
				surface->needs_stencil = TRUE;
				surface->stencil_changed = FALSE;
			}
		}
		else
		{
			surface->clip = _cairo_clip_copy(clip);
			surface->needs_stencil = TRUE;
			surface->stencil_changed = TRUE;
		}
	}
	else
	{
		if(surface->clip != NULL)
		{
			_cairo_clip_destroy(surface->clip);
			surface->clip = NULL;
			surface->needs_stencil = FALSE;
			surface->stencil_changed = TRUE;
		}
	}
	
	if(surface->needs_stencil == TRUE)
	{
		status = _cairo_gl_clip(clip, setup, ctx, surface);
		if(unlikely(status))
		{
			if(clone != NULL)
				cairo_surface_destroy(&clone->base);
			_cairo_gl_composite_fini(setup);
			free(setup);
			glDisable(GL_STENCIL_TEST);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
			status = _cairo_gl_context_release(ctx, status);
			return status;
		}
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
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		_cairo_gl_composite_fini(setup);
		free(setup);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		status = _cairo_gl_context_release(ctx, status);
		return CAIRO_INT_STATUS_UNSUPPORTED;
	}

	// setup indices
	_cairo_traps_init(&traps);
	status = _cairo_gl_create_indices(&indices);
	indices.setup = setup;

	status = _cairo_path_fixed_fill_to_traps(path, fill_rule, tolerance, &traps);
	for(m = 0; m < traps.num_traps; m++)
	{
		top = _cairo_fixed_to_double(traps.traps[m].top);
		bottom = _cairo_fixed_to_double(traps.traps[m].bottom);
		x1 = _cairo_fixed_to_double(traps.traps[m].left.p1.x);
		x2 = _cairo_fixed_to_double(traps.traps[m].left.p2.x);
		y1 = _cairo_fixed_to_double(traps.traps[m].left.p1.y);
		y2 = _cairo_fixed_to_double(traps.traps[m].left.p2.y);
		dx = x1 - x2;
		dy = y1 - y2;
		x_top_left = _cairo_fixed_from_double(x1 - dx * (y1 - top) / dy);
		x_bottom_left = _cairo_fixed_from_double(x1  - dx * (y1 - bottom) /dy);
		
		x1 = _cairo_fixed_to_double(traps.traps[m].right.p1.x);
		x2 = _cairo_fixed_to_double(traps.traps[m].right.p2.x);
		y1 = _cairo_fixed_to_double(traps.traps[m].right.p1.y);
		y2 = _cairo_fixed_to_double(traps.traps[m].right.p2.y);
		dx = x1 - x2;
		dy = y1 - y2;
		x_top_right = _cairo_fixed_from_double(x1 - dx * (y1 - top) /dy);
		x_bottom_right = _cairo_fixed_from_double(x1  - dx * (y1 - bottom) /dy);
		points[0].x = x_top_left;
		points[0].y = traps.traps[m].top;
		points[1].x = x_bottom_left;
		points[1].y = traps.traps[m].bottom;
		points[2].x = x_bottom_right;
		points[2].y = traps.traps[m].bottom;
		points[3].x = x_top_right;
		points[3].y = traps.traps[m].top;
		status = _cairo_gl_add_convex_quad(&indices, points);
		if(unlikely(status))
		{
			if(clone != NULL)
				cairo_surface_destroy(&clone->base);
			_cairo_traps_fini(&traps);
			glDisable(GL_STENCIL_TEST);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
			status = _cairo_gl_destroy_indices(&indices);
			_cairo_gl_composite_fini(setup);
			free(setup);
		}
	}
	status = _cairo_gl_fill(setup, indices.num_vertices, 
		indices.vertices, NULL, indices.num_indices, indices.indices,
		setup->ctx);
	_cairo_traps_fini(&traps);
	if(clone != NULL)
		cairo_surface_destroy(&clone->base);
	status = _cairo_gl_destroy_indices(&indices);
	_cairo_gl_composite_fini(setup);
	free(setup);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	status = _cairo_gl_context_release(ctx, status);
	surface->needs_new_data_surface = TRUE;
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
    _cairo_gl_context_set_destination (ctx, surface);
	status = _cairo_gl_surface_draw_image(surface, (cairo_image_surface_t *)surface->data_surface,
		x, y, width, height, x, y);
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

