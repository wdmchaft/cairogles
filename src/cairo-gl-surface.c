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
#include "cairo-error-private.h"
#include "cairo-gl-private.h"

// Henry Song
#include <sys/time.h>
#include "cairo-surface-clipper-private.h"
#include <math.h>
//Henry Song test
//cairo_path_fixed_t	*my_path = NULL;

static long _get_tick(void)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return now.tv_sec * 1000000 + now.tv_usec;
}

static cairo_bool_t 
_cairo_gl_need_extend(int size, int *out_size, float *scale)
{
	if(size <= 0)
	{
		*out_size = 0;
		*scale = 0.0;
		return FALSE;
	}
	cairo_bool_t need_extend = FALSE;
	float f;
	int d;
	int extend_size = 1;
	if(size == 1)
	{
		need_extend = 1;
		*out_size = 2;
		*scale = 2.0;
		return TRUE;
	}

	f = logf(size) / logf(2.0);
	d = (int)f;
	if(f == 0.0)
	{
		*out_size = size;
		*scale = 1.0;
		return FALSE;
	}
					
	if(f - d <= 0.5)
		extend_size = extend_size << d;
	else
		extend_size = extend_size << (d+1);
	*out_size = extend_size;
	*scale = (float)extend_size / (float)size;
	return TRUE;
}

/*cairo_status_t
_cairo_gl_surface_upload_image(cairo_gl_surface_t *dst,
	cairo_image_surface_t *image_surface,
	int src_x, int src_y,
	int width, int height,
	int dst_x, int dst_y);
*/
static cairo_int_status_t
_cairo_gl_surface_fill_rectangles (void			   *abstract_dst,
				   cairo_operator_t	    op,
				   const cairo_color_t     *color,
				   cairo_rectangle_int_t   *rects,
				   int			    num_rects);
// Henry Song
/*
cairo_status_t _cairo_gl_add_triangle(void *closure,
	const cairo_point_t triangle[3]);
cairo_status_t _cairo_gl_add_triangle_fan(void *closure,
	const cairo_point_t *midpt,
	const cairo_point_t *points,
	int npoints);
cairo_status_t _cairo_gl_add_convex_quad(void *closure,
	const cairo_point_t quad[4]);
cairo_status_t _cairo_gl_add_convex_quad_for_clip(void *closure,
	const cairo_point_t quad[4]);
*/
static cairo_int_status_t
_cairo_gl_surface_mask(cairo_surface_t *abstract_surface,
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

// Henry Song
static void
_cairo_gl_surface_remove_from_cache(cairo_surface_t *abstract_surface);
// Henry Song
static cairo_status_t
_cairo_gl_line_to(void *closure, const cairo_point_t *point);

static cairo_status_t
_cairo_gl_move_to(void *closure, const cairo_point_t *point);

static cairo_status_t
_cairo_gl_curve_to(void *closure, const cairo_point_t *p0,
	const cairo_point_t *p1, const cairo_point_t *p2);

static cairo_status_t
_cairo_gl_close_path(void *closure);

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
	double *mask_v = NULL;
	//cairo_gl_context_t *ctx;
	cairo_gl_composite_t *setup = (cairo_gl_composite_t *)closure;
	//printf("gl fill indices = %d, vertices = %d\n", npoints, vpoints);
	//if(npoints > 10000)
	int index = 0;
	int stride = 4 * sizeof(GLfloat);
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
		src_v = (double *)malloc(sizeof(double)*vpoints*2);
		src_colors = (char *)malloc(sizeof(GLfloat)*2*vpoints);
		cairo_matrix_init_scale(&m, 1.0, 1.0);
		cairo_matrix_multiply(&m, &m, &(setup->source->matrix));
		cairo_matrix_init_scale(&m1, 1.0 / setup->src.texture.width,
			1.0 / setup->src.texture.height);
		cairo_matrix_multiply(&m, &m, &m1);
		GLfloat *st = src_colors;
		for(index = 0; index < vpoints; index++)
		{
			src_v[index*2] = vertices[index*2];
			src_v[index*2+1] = vertices[index*2+1];
			cairo_matrix_transform_point(&m, &src_v[index*2], &src_v[index*2+1]); 
			st[index*2] = src_v[index*2];
			st[index*2+1] = src_v[index*2+1];
		}
		/*
		if(vpoints == 4)
		{
			printf("exceed, npoints = %d, vpoints = %d, setup = %x\n", npoints, vpoints, setup);
			int i = 0;
			for(i = 0; i < npoints; i++)
				printf("%d, ", indices[i]);
			printf("\n");
			for(i = 0; i < vpoints; i++)
				printf("(%0.5f, %0.5f) ", vertices[i*2], vertices[i*2+1]);
			printf("\n");
		}
		if(vpoints == 4)
		{
			for(index = 0; index < vpoints; index++)
				printf("st[%d].s = %0.5f, st[%d].t = %0.5f\n", index, st[index*2], index, st[index*2+1]);
		}*/
	}

	if(setup->mask.type == CAIRO_GL_OPERAND_CONSTANT)
	{
		mask_colors = (char *)malloc(sizeof(GLfloat)*4*vpoints);
		while(index < vpoints)
		{
			memcpy(mask_colors+index*stride, &(setup->mask.constant.color), stride);
			index++;
		}
	}
	/*
	else if(setup->mask.type == CAIRO_GL_OPERAND_TEXTURE)
	{
		cairo_matrix_t m, m1;
		v = (double *)malloc(sizeof(double)*vpoints*2);
		colors = (char *)malloc(sizeof(GLfloat)*2*vpoints);
		cairo_matrix_init_scale(&m, 1.0, 1.0);
		cairo_matrix_multiply(&m, &m, &(setup->source->matrix));
		cairo_matrix_init_scale(&m1, 1.0 / setup->src.texture.width,
			1.0 / setup->src.texture.height);
		cairo_matrix_multiply(&m, &m, &m1);
		GLfloat *st = colors;
		for(index = 0; index < vpoints; index++)
		{
			v[index*2] = vertices[index*2];
			v[index*2+1] = vertices[index*2+1];
			cairo_matrix_transform_point(&m, &v[index*2], &v[index*2+1]); 
			st[index*2] = v[index*2];
			st[index*2+1] = v[index*2+1];
		}
	}*/
		// we need to fill colors with st values
	cairo_status_t status = _cairo_gl_composite_begin_constant_color(setup, 
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
	//status = _cairo_gl_context_release(ctx, status);
	//printf("fill color finished\n");
	//return CAIRO_STATUS_SUCCESS;
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
	_cairo_gl_index_t *indices = (_cairo_gl_index_t *)closure;
	cairo_gl_composite_t *setup = indices->setup;
	// first off, we need to flush if max
	//printf("before add triangle indices = %d\n", indices->num_indices);
	if(indices->num_indices > MAX_INDEX)
	{
		if(indices->setup != NULL)
			_cairo_gl_fill(indices->setup, indices->num_vertices,
				indices->vertices, NULL, indices->num_indices, 
				indices->indices, indices->setup->ctx);
		// cleanup
		_cairo_gl_destroy_indices(indices);
		_cairo_gl_create_indices(indices);
		indices->setup = setup;
	}
	// we add a triangle to strip, we add 3 vertices and 5 indices;
	while(indices->num_indices + 5 >= indices->capacity ||
		indices->num_vertices + 3 >= indices->capacity)
	{
		// we need to increase
		_cairo_gl_increase_indices(indices);
	}
	int last_index = 0;
	int start_index = 0;
	int i;
	int num_indices = indices->num_indices;
	int num_vertices = indices->num_vertices;
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
	//printf("after add triangle indices = %d\n", indices->num_indices);
	return CAIRO_STATUS_SUCCESS;
}

cairo_status_t _cairo_gl_add_triangle_fan(void *closure,
	const cairo_point_t *midpt,
	const cairo_point_t *points,
	int npoints)
{
	_cairo_gl_index_t *indices = (_cairo_gl_index_t *)closure;
	cairo_gl_composite_t *setup = indices->setup;
	//printf("before add triangle fan indices = %d\n", indices->num_indices);
	// first off, we need to flush if max
	if(indices->num_indices > MAX_INDEX)
	{
		if(indices->setup != NULL)
			_cairo_gl_fill(indices->setup, indices->num_vertices,
				indices->vertices, NULL, indices->num_indices, 
				indices->indices, indices->setup->ctx);
		// cleanup
		_cairo_gl_destroy_indices(indices);
		_cairo_gl_create_indices(indices);
		indices->setup = setup;
	}
	// we add a triangle fan to strip, we add npoints+1 vertices and (npoints-2)*2 indices;
	while(indices->num_indices + (npoints - 2) * 2 + npoints + 1 >= indices->capacity ||
		indices->num_vertices + npoints + 1 >= indices->capacity)
	{
		// we need to increase
		_cairo_gl_increase_indices(indices);
	}
	int last_index = 0;
	int start_index = 0;
	int i;
	int num_indices = indices->num_indices;
	int num_vertices = indices->num_vertices;
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
	int mid_index = start_index;
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
	//printf("after add triangle fan indices = %d\n", indices->num_indices);
	return CAIRO_STATUS_SUCCESS;
}

cairo_status_t _cairo_gl_add_convex_quad_for_clip(void *closure,
	const cairo_point_t quad[4])
{
	_cairo_gl_index_t *indices = (_cairo_gl_index_t *)closure;
	cairo_gl_composite_t *setup = indices->setup;
	// first off, we need to flush if max
	//printf("before add quad indices = %d, setup = %x\n", indices->num_indices, indices->setup);
	if(indices->num_indices > MAX_INDEX)
	{
		//if(indices->setup == NULL)
		//	printf("setup = NULL\n");
		if(indices->setup != NULL)
		{
			// let's create surface->indices_buf
			_cairo_gl_index_buf_t *buf = (_cairo_gl_index_buf_t *)(indices->setup->dst->indices_buf);
			if(buf != NULL)
			{
				while(buf->next != NULL)
					buf = buf->next;
			}
			_cairo_gl_index_buf_t *new_buf = (_cairo_gl_index_buf_t *)malloc(sizeof(_cairo_gl_index_buf_t));
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

			_cairo_gl_fill(indices->setup, indices->num_vertices,
				indices->vertices, NULL, indices->num_indices, 
				indices->indices, indices->setup->ctx);
			// cleanup
			_cairo_gl_destroy_indices(indices);
			_cairo_gl_create_indices(indices);
			indices->setup = setup;
		}
	}
	// we add a triangle to strip, we add 4 vertices and 6 indices;
	while(indices->num_indices + 5 >= indices->capacity ||
		indices->num_vertices + 4 >= indices->capacity)
	{
		// we need to increase
		_cairo_gl_increase_indices(indices);
	}
	int last_index = 0;
	int start_index = 0;
	int i;
	int num_indices = indices->num_indices;
	int num_vertices = indices->num_vertices;
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
	//printf("after add quad indices = %d\n", indices->num_indices);
	return CAIRO_STATUS_SUCCESS;
}

cairo_status_t _cairo_gl_add_convex_quad(void *closure,
	const cairo_point_t quad[4])
{
	_cairo_gl_index_t *indices = (_cairo_gl_index_t *)closure;
	cairo_gl_composite_t *setup = indices->setup;
	// first off, we need to flush if max
	//printf("before add quad indices = %d, setup = %x\n", indices->num_indices, indices->setup);
	if(indices->num_indices > MAX_INDEX)
	{
		//if(indices->setup == NULL)
		//	printf("setup = NULL\n");
		if(indices->setup != NULL)
		{
			_cairo_gl_fill(indices->setup, indices->num_vertices,
				indices->vertices, NULL, 
				indices->num_indices, 
				indices->indices, indices->setup->ctx);
			// cleanup
			_cairo_gl_destroy_indices(indices);
			_cairo_gl_create_indices(indices);
			indices->setup = setup;
		}
	}
	// we add a triangle to strip, we add 4 vertices and 6 indices;
	while(indices->num_indices + 5 >= indices->capacity ||
		indices->num_vertices + 4 >= indices->capacity)
	{
		// we need to increase
		_cairo_gl_increase_indices(indices);
	}
	int last_index = 0;
	int start_index = 0;
	int i;
	int num_indices = indices->num_indices;
	int num_vertices = indices->num_vertices;
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
	//printf("after add quad indices = %d\n", indices->num_indices);
	return CAIRO_STATUS_SUCCESS;
}

cairo_status_t _cairo_gl_add_convex_quad_with_mask(void *closure,
	const cairo_point_t quad[4], const float *mask_quad)
{
	_cairo_gl_index_t *indices = (_cairo_gl_index_t *)closure;
	cairo_gl_composite_t *setup = indices->setup;
	// first off, we need to flush if max
	//printf("before add quad indices = %d, setup = %x\n", indices->num_indices, indices->setup);
	if(indices->num_indices > MAX_INDEX)
	{
		//if(indices->setup == NULL)
		//	printf("setup = NULL\n");
		if(indices->setup != NULL)
		{
			if(indices->has_mask_vertices == TRUE)
				_cairo_gl_fill(indices->setup, indices->num_vertices,
					indices->vertices, indices->mask_vertices, 
					indices->num_indices, 
					indices->indices, indices->setup->ctx);
			else
				_cairo_gl_fill(indices->setup, indices->num_vertices,
					indices->vertices, NULL, 
					indices->num_indices, 
					indices->indices, indices->setup->ctx);
			// cleanup
			_cairo_gl_destroy_indices(indices);
			_cairo_gl_create_indices(indices);
			indices->setup = setup;
		}
	}
	// we add a triangle to strip, we add 4 vertices and 6 indices;
	while(indices->num_indices + 5 >= indices->capacity ||
		indices->num_vertices + 4 >= indices->capacity)
	{
		// we need to increase
		_cairo_gl_increase_indices(indices);
	}
	int last_index = 0;
	int start_index = 0;
	int i;
	int num_indices = indices->num_indices;
	int num_vertices = indices->num_vertices;
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
	//printf("after add quad indices = %d\n", indices->num_indices);
	return CAIRO_STATUS_SUCCESS;
}

static void _cairo_gl_free_path(_cairo_gl_path_t *path)
{
	_cairo_gl_path_t *temp;
	//_cairo_gl_path_t *next = path->next;
	_cairo_gl_path_t *current = path;
	if(path == NULL)
		return;
	
	while(current != NULL)
	{
		free(current->vertices->vertices);
		free(current->vertices);
		temp = current->prev;
		if(temp != NULL)
			temp->next = NULL;
		temp = current;
		current = current->next;
		if(current != NULL)
			current->prev = NULL;
		free(temp);
		temp->next = NULL;
		temp->prev = NULL;
	}
}

static _cairo_gl_path_vertices_t *
_cairo_gl_create_increase_path_vertices_capacity(_cairo_gl_path_vertices_t *current)
{
	_cairo_gl_path_vertices_t *new_vertices = (_cairo_gl_path_vertices_t *)malloc(sizeof(_cairo_gl_path_vertices_t));
	if(current != NULL)
	{
		new_vertices->vertices = (_cairo_gl_vertex_t *)malloc(sizeof(_cairo_gl_vertex_t)*(current->capacity *2));
		memcpy(new_vertices->vertices, current->vertices, current->vertex_size * sizeof(_cairo_gl_vertex_t));
		new_vertices->capacity = current->capacity *2;
		new_vertices->vertex_size = current->vertex_size;
	}
	else
	{
		new_vertices->vertices = (_cairo_gl_vertex_t *)malloc(sizeof(_cairo_gl_vertex_t)*(VERTEX_INC));
		new_vertices->capacity = VERTEX_INC;
		new_vertices->vertex_size = 0;
	}
		
	return new_vertices;
}

static cairo_status_t
_cairo_gl_curve_to(void *closure, const cairo_point_t *p0,
	const cairo_point_t *p1, const cairo_point_t *p2)
{
	double x0, y0, x1, y1, x2, y2;
	double prevx, prevy;

	double m_ax, m_ay, m_bx, m_by, m_cx, m_cy, m_dx, m_dy;
	int index, i;
	double x, y;
	double t;
	double increment = 1.0 / VERTEX_INC;

	//printf("start curve\n");
	_cairo_gl_path_t *head = (_cairo_gl_path_t *)closure;
	_cairo_gl_path_t *current_path = head->prev;

	_cairo_gl_path_vertices_t *vertices = current_path->vertices;
	if(vertices->vertex_size + VERTEX_INC >= vertices->capacity)
	{
		// make new one
		_cairo_gl_path_vertices_t *new_vertices = 
			_cairo_gl_create_increase_path_vertices_capacity(vertices);
		// remove current
		free(vertices->vertices);
		free(vertices);
		current_path->vertices = new_vertices;
	}

	x0 = _cairo_fixed_to_double(p0->x);
	y0 = _cairo_fixed_to_double(p0->y);
	x1 = _cairo_fixed_to_double(p1->x);
	y1 = _cairo_fixed_to_double(p1->y);
	x2 = _cairo_fixed_to_double(p2->x);
	y2 = _cairo_fixed_to_double(p2->y);
	prevx = current_path->vertices->vertices[current_path->vertices->vertex_size-1].x;
	prevy = current_path->vertices->vertices[current_path->vertices->vertex_size-1].y;
	//printf(" p0 (%0.2f, %0.2f), p2 (%0.2f, %0.2f), prev (%0.2f, %0.2f)\n", x0, y0, x2, y2, prevx, prevy);
	m_ax = prevx;
	m_ay = prevy;
	m_bx = -3.0 * prevx + 3.0 * x0;
	m_by = -3.0 * prevy + 3.0 * y0;
	m_cx = 3.0 * prevx - 6.0 * x0 + 3.0 * x1;
	m_cy = 3.0 * prevy - 6.0 * y0 + 3.0 * y1;
	m_dx = -1.0 * prevx + 3.0 * x0 - 3.0 * x1 + x2;
	m_dy = -1.0 * prevy + 3.0 * y0 - 3.0 * y1 + y2;

	t = increment;
	//FILE *f = fopen("points", "a");
	for(i = 0; i < VERTEX_INC; i++, t += increment)
	{
		x = m_ax + t * (m_bx + t * (m_cx + t * m_dx));
		y = m_ay + t * (m_by + t * (m_cy + t * m_dy));
		//printf("add curve to point (%0.2f, %0.2f)\n", x, y);
		/*if(f)
		{
			fwrite((void *)&x, sizeof(double), 1, f);
			fwrite((void *)&y, sizeof(double), 1, f);
		}*/
		index = current_path->vertices->vertex_size;
		current_path->vertices->vertices[index].x = x;
		current_path->vertices->vertices[index].y = y;
		current_path->vertices->vertex_size ++;
	}
	/*if(f)
		fclose(f);
	*/


	return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_gl_close_path(void *closure)
{
/*
	double x, y;
	int index;
	_cairo_gl_path_t *head = (_cairo_gl_path_t *)closure;
	_cairo_gl_path_t *current_path = head->prev;

	_cairo_gl_path_vertices_t *vertices = current_path->vertices;
	if(vertices->vertex_size == vertices->capacity)
	{
		// make new one
		_cairo_gl_path_vertices_t *new_vertices =
			_cairo_gl_create_increase_path_vertices_capacity(vertices);
		// remove current
		free(vertices->vertices);
		free(vertices);
		current_path->vertices = new_vertices;
	}
	x = current_path->vertices->vertices[0].x;
	y = current_path->vertices->vertices[0].y;
	//printf("add close path (%0.2f, %0.2f)\n", x, y);
	index = current_path->vertices->vertex_size;
	current_path->vertices->vertices[index].x = x;
	current_path->vertices->vertices[index].y = y;
	current_path->vertices->vertex_size ++;
*/
	return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_gl_line_to(void *closure, const cairo_point_t *point)
{
	double x, y;
	int index;
	_cairo_gl_path_t *head = (_cairo_gl_path_t *)closure;
	_cairo_gl_path_t *current_path = head->prev;

	_cairo_gl_path_vertices_t *vertices = current_path->vertices;
	if(vertices->vertex_size == vertices->capacity)
	{
		// make new one
		_cairo_gl_path_vertices_t *new_vertices =
			_cairo_gl_create_increase_path_vertices_capacity(vertices);
		// remove current
		free(vertices->vertices);
		free(vertices);
		current_path->vertices = new_vertices;
	}
	x = _cairo_fixed_to_double(point->x);
	y = _cairo_fixed_to_double(point->y);
	//printf("add line to point (%0.2f, %0.2f)\n", x, y);
	index = current_path->vertices->vertex_size;
	current_path->vertices->vertices[index].x = x;
	current_path->vertices->vertices[index].y = y;
	current_path->vertices->vertex_size ++;

	return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_gl_move_to(void *closure, const cairo_point_t *point)
{
	double x, y;
	int index;
	_cairo_gl_path_t *head = (_cairo_gl_path_t *)closure;
	_cairo_gl_path_t *tail = head->prev;
	// we need to create a new _cairo_gl_path_t;
	_cairo_gl_path_t *new_path = (_cairo_gl_path_t *)malloc(sizeof(_cairo_gl_path_t));
	new_path->next = head;
	new_path->prev = tail;
	tail->next = new_path;
	head->prev = new_path;

	new_path->vertices = 
		_cairo_gl_create_increase_path_vertices_capacity(NULL);

	x = _cairo_fixed_to_double(point->x);
	y = _cairo_fixed_to_double(point->y);
	//printf("add move to point (%0.2f, %0.2f)\n", x, y);

	/*FILE *f = fopen("points", "w+");
	if(f != NULL)
	{
		fwrite((void *)&x, sizeof(double), 1, f);
		fwrite((void *)&y, sizeof(double), 1, f);
		fclose(f);
	}*/
	index = new_path->vertices->vertex_size;
	new_path->vertices->vertices[index].x = x;
	new_path->vertices->vertices[index].y = y;
	new_path->vertices->vertex_size ++;

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
	cairo_path_fixed_t path;
	cairo_clip_path_t *clip_path = NULL;
	_cairo_gl_index_t indices;
	cairo_traps_t traps;
	
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
			//glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
			//glStencilFunc(GL_EQUAL, 1, 1);
			return CAIRO_STATUS_SUCCESS;
		}
		glEnable(GL_STENCIL_TEST);
		glClear(GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDisable(GL_DEPTH_TEST);
		//glEnable(GL_DEPTH_TEST);
		glStencilOp(GL_REPLACE,  GL_REPLACE, GL_REPLACE);
		glStencilFunc(GL_ALWAYS, 1, 0xffffffff);
		glColorMask(0, 0, 0, 0);
		//printf("using cache\n");
		while(exist_buf != NULL)
		{
			/*int i;
			printf("existing vertex\n");
			for(i = 0; i < exist_buf->indices->num_vertices; i++)
			{
				printf("vertex (%0.1f, %0.1f)\n", exist_buf->indices->vertices[i*2], exist_buf->indices->vertices[i*2+1]);
			}*/
			_cairo_gl_index_t *exist_indices = exist_buf->indices;
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
		//glEnable(GL_STENCIL_TEST);
		glColorMask(1, 1, 1, 1);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		glStencilFunc(GL_EQUAL, 1, 1);
		return CAIRO_STATUS_SUCCESS;
	}

	// clean up clip cache
	_cairo_gl_index_buf_t *current, *temp;
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
		//free((_cairo_gl_index_buf_t *)(surface->indices_buf));
		surface->indices_buf = NULL;
	}
	//cairo_polygon_t polygons
	//_cairo_gl_index_t indices;
	if(clip->path != NULL)
	{
		fill_rule = clip->path->fill_rule;
		path = clip->path->path;
		clip_path = clip->path;
	}
	
	cairo_point_t points[4];
	
	glEnable(GL_STENCIL_TEST);
	glClear(GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glStencilOp(GL_REPLACE,  GL_REPLACE, GL_REPLACE);
	glStencilFunc(GL_ALWAYS, 1, 0xffffffff);
	glColorMask(0, 0, 0, 0);
	int got_traps = 0;
	int remaining_boxes = clip->num_boxes;

	// Henry Song test
	//status = _cairo_path_fixed_fill_to_traps(&(clip_path->path), fill_rule, tolerance, &traps);
	while(clip_path != NULL || remaining_boxes != 0)
	{
		// Let's analyze box first
		//path = clip_path->path;
		//_cairo_polygon_init(&polygon);
		status = _cairo_gl_create_indices(&indices);
		indices.setup = setup;
		
		cairo_fixed_t x_top_left, x_bottom_left;
		cairo_fixed_t x_top_right, x_bottom_right;
		double x1, x2;
		double y1, y2;
		double dx, dy;
		double top, bottom;
		int box_index;
		
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
				//glDisable(GL_STENCIL_TEST);
				_cairo_gl_destroy_indices(&indices);
				return status;
			}
			remaining_boxes -= 1;
		}
		
		if(clip_path != NULL)
		{
			_cairo_traps_init(&traps);
			status = _cairo_path_fixed_fill_to_traps(&(clip_path->path), fill_rule, tolerance, &traps);
			int m;
			if(traps.num_traps != 0)
			{
				got_traps = 1;
			}
			//printf("\n==================\n\n=========== attention\n");	
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
				/*if(_cairo_fixed_to_double(points[0].x) == 0.0 &&
				   _cairo_fixed_to_double(points[0].y) == 0.0 &&
				   _cairo_fixed_to_double(points[1].x) == 0.0 &&
				   _cairo_fixed_to_double(points[1].y) == 400.0 &&
				   _cairo_fixed_to_double(points[2].x) == 400.0 &&
				   _cairo_fixed_to_double(points[2].y) == 400.0 &&
				   _cairo_fixed_to_double(points[3].x) == 400.0 &&
				   _cairo_fixed_to_double(points[3].y) == 0.0)
				{*/
				/*printf("add clip path left (%0.5f, %0.5f) - (%0.5f, %0.5f), right (%0.5f, %0.5f) - (%0.1f, %0.1f)\n", 
					_cairo_fixed_to_double(points[0].x),
					_cairo_fixed_to_double(points[0].y),
					_cairo_fixed_to_double(points[1].x),
					_cairo_fixed_to_double(points[1].y),
					_cairo_fixed_to_double(points[2].x),
					_cairo_fixed_to_double(points[2].y),
					_cairo_fixed_to_double(points[3].x),
					_cairo_fixed_to_double(points[3].y));
				//}*/
				status = _cairo_gl_add_convex_quad_for_clip(&indices, points);
				if(unlikely(status))
				{
					_cairo_traps_fini(&traps);
					glDisable(GL_DEPTH_TEST);
					glDisable(GL_STENCIL_TEST);
		
					glColorMask(1, 1, 1, 1);
					//glDisable(GL_STENCIL_TEST);
					_cairo_gl_destroy_indices(&indices);
					return status;
				}
			}
			_cairo_traps_fini(&traps);
			clip_path = clip_path->prev;
		}
		// first off, we need to flush if max
		// let's create surface->indices_buf
		_cairo_gl_index_buf_t *buf = (_cairo_gl_index_buf_t *)(indices.setup->dst->indices_buf);
		if(buf != NULL)
		{
			while(buf->next != NULL)
				buf = buf->next;
		}
		_cairo_gl_index_buf_t *new_buf = (_cairo_gl_index_buf_t *)malloc(sizeof(_cairo_gl_index_buf_t));
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
		/*	int i;
	
		for(i = 0; i < new_buf->indices->num_vertices; i++)
		{
			printf("vertex (%0.1f, %0.1f)\n", new_buf->indices->vertices[i*2], new_buf->indices->vertices[i*2+1]);
		}*/
		status = _cairo_gl_fill(setup, indices.num_vertices, 
			indices.vertices, NULL, indices.num_indices, indices.indices, setup->ctx);
		_cairo_gl_destroy_indices(&indices);
		if(unlikely(status))
		{
			glDisable(GL_STENCIL_TEST);
			glDisable(GL_DEPTH_TEST);
			//glEnable(GL_STENCIL_TEST);
			glColorMask(1, 1, 1, 1);
			return status;
		}
		//if(clip_path != NULL)
		//	printf("we have prev clip path\n");
	}
	if(got_traps == 0)
	{
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_DEPTH_TEST);
		
		glColorMask(1, 1, 1, 1);
		//glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		//glStencilFunc(GL_EQUAL, 1, 1);
		return CAIRO_STATUS_SUCCESS;
	}
	// we done stencil test
	//glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	//glEnable(GL_STENCIL_TEST);
	glColorMask(1, 1, 1, 1);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glStencilFunc(GL_EQUAL, 1, 1);
	//_cairo_traps_fini(&traps);
	//_cairo_gl_destroy_indices(&indices);
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
    cairo_bool_t is_big_endian = _cairo_gl_is_big_endian ();

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
    _cairo_surface_init (&surface->base,
			 &_cairo_gl_surface_backend,
			 device,
			 content);

    surface->orig_width = width;
    surface->orig_height = height;
    surface->needs_update = FALSE;
	// Henry Song
	surface->clip = NULL;
	surface->needs_stencil = FALSE;
	surface->tex_img = 0;
	surface->indices_buf = NULL;
	surface->stencil_changed = FALSE;
	surface->external_tex = FALSE;
	surface->scale = 1.0;
	surface->width = width;
	surface->height = height;
	float width_scale, height_scale;
	int scaled_width, scaled_height;
    
	cairo_status_t status = _cairo_gl_context_acquire (device, &ctx);
    if (unlikely (status))
	return;
	if(width > ctx->max_texture_size || height > ctx->max_texture_size)
	{
		width_scale = (float)ctx->max_texture_size / (float)width;
		height_scale = (float)ctx->max_texture_size / (float)height;
		if(width_scale < height_scale)
			surface->scale = width_scale;
		else
			surface->scale = height_scale;
		surface->width = (int)(surface->scale * surface->orig_width);
		surface->height = (int)(surface->scale * surface->orig_height);
	}
    status = _cairo_gl_context_release (ctx, status);
	// we need to setup extend
	float *scale;
	cairo_bool_t width_need_snapshot = _cairo_gl_need_extend(surface->width,
		&(surface->extend_width), &(surface->extend_width_scale));
	surface->extend_width_scale = 1.0;
	cairo_bool_t height_need_snapshot = _cairo_gl_need_extend(surface->height, 
		&(surface->extend_height), &(surface->extend_height_scale));
	surface->extend_height_scale = 1.0;
	if(width_need_snapshot || height_need_snapshot)
		surface->needs_extend = TRUE;
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

	// we need to setup extend
	float *scale;
	cairo_bool_t width_need_snapshot = _cairo_gl_need_extend(surface->width,
		&(surface->extend_width), &(surface->extend_width_scale));
	surface->extend_width_scale = 1.0;
	cairo_bool_t height_need_snapshot = _cairo_gl_need_extend(surface->height, 
		&(surface->extend_height), &(surface->extend_height_scale));
	surface->extend_height_scale = 1.0;
	if(width_need_snapshot || height_need_snapshot)
		surface->needs_extend = TRUE;

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
	int orig_width = width;
	int orig_height = height;
	float width_scale;
	float height_scale;
	float scale = 1.0;

    glGenTextures (1, &tex);
	//if(tex == 7)
	//	printf("generate tex = %d\n", tex);
	/*if(width > ctx->max_texture_size || height > ctx->max_texture_size)
	{
		width_scale = (float)ctx->max_texture_size / (float)width;
		height_scale = (float)ctx->max_texture_size / (float)height;
		if(width_scale < height_scale)
			scale = width_scale;
		else
			scale = height_scale;
		width = (int)(scale * orig_width);
		height = (int)(scale * orig_height);
	}*/
    surface = (cairo_gl_surface_t *)
	_cairo_gl_surface_create_scratch_for_texture (ctx, content,
						      tex, width, height);
    if (unlikely (surface->base.status))
	return &surface->base;

    surface->owns_tex = TRUE;
	//float *scale;
	cairo_bool_t width_extend = _cairo_gl_need_extend(width, &(surface->extend_width), &(surface->extend_width_scale));
	surface->extend_width_scale = 1.0;
	cairo_bool_t height_extend = _cairo_gl_need_extend(height, &(surface->extend_height), &(surface->extend_height_scale));
	surface->extend_height_scale = 1.0;
	if(width_extend && height_extend)
		surface->needs_extend = FALSE;
	else
		surface->needs_extend = TRUE;

    /* adjust the texture size after setting our real extents */
    if (width < 1)
	width = 1;
    if (height < 1)
	height = 1;

	surface->orig_width = orig_width;
	surface->orig_height = orig_height;
	//surface->scale = scale;

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
	if(surface->needs_extend == TRUE)
	{
		if(_cairo_surface_has_snapshot(&(surface->base), &_cairo_gl_surface_backend))
			_cairo_surface_detach_snapshot(&(surface->base));
	}
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
	// Henry Song
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
	float scale = 1.0;
	int orig_width = width;
	int orig_height = height;
	float new_width_scale;
	float new_height_scale;

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

    surface = (cairo_gl_surface_t *)
	_cairo_gl_surface_create_scratch_for_texture (ctx, content,
						      tex, width, height);
    status = _cairo_gl_context_release (ctx, status);

	surface->external_tex = TRUE;
	surface->owns_tex = FALSE;
	cairo_bool_t width_extend = _cairo_gl_need_extend(width, &(surface->extend_width), &(surface->extend_width_scale));
	cairo_bool_t height_extend = _cairo_gl_need_extend(height, &(surface->extend_height), &(surface->extend_height_scale));
	if(width_extend && height_extend)
		surface->needs_extend = FALSE;
	else
		surface->needs_extend = TRUE;
	surface->extend_height_scale = 1.0;
	surface->extend_width_scale = 1.0;
	surface->scale = scale;
	surface->orig_width = orig_width;
	surface->orig_height = orig_height;

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
    cairo_status_t status;

    if (unlikely (abstract_surface->status))
	return;
    if (unlikely (abstract_surface->finished)) {
	status = _cairo_surface_set_error (abstract_surface,
		                           _cairo_error (CAIRO_STATUS_SURFACE_FINISHED));
        return;
    }

    if (! _cairo_surface_is_gl (abstract_surface) ||
        _cairo_gl_surface_is_texture (surface)) {
	status = _cairo_surface_set_error (abstract_surface,
		                           _cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH));
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
	//printf("abstract surface status = %d\n", abstract_surface->status);
    if (unlikely (abstract_surface->finished)) {
	status = _cairo_surface_set_error (abstract_surface,
		                           _cairo_error (CAIRO_STATUS_SURFACE_FINISHED));
	//printf("finished = %d\n", abstract_surface->finished);
        return;
    }
	// Henry Song
	//printf("surface is gl = %d\n", _cairo_surface_is_gl(abstract_surface));
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

    if (width < 1 || height < 1)
        return cairo_image_surface_create (_cairo_format_from_content (content),
                                           width, height);

    status = _cairo_gl_context_acquire (surface->device, &ctx);
    if (unlikely (status))
	return _cairo_surface_create_in_error (status);

	orig_width = width;
	orig_height = height;
	float scale = 1.0;
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
	if(cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_GL)
	{
		cairo_gl_surface_t *g = (cairo_gl_surface_t *)surface;
		g->scale = scale;
		g->orig_width = orig_width;
		g->orig_height = orig_height;
	}
RELEASE:
    status = _cairo_gl_context_release (ctx, status);
    if (unlikely (status)) {
        cairo_surface_destroy (surface);
        return _cairo_surface_create_in_error (status);
    }

	// we need to setup extend
	if(surface != NULL && surface->type == CAIRO_SURFACE_TYPE_GL)
	{
		cairo_gl_surface_t *gl = (cairo_gl_surface_t *)surface;
		cairo_bool_t width_need_snapshot = _cairo_gl_need_extend(gl->width,
			&(gl->extend_width), &(gl->extend_width_scale));
		gl->extend_width_scale = 1.0;
		cairo_bool_t height_need_snapshot = _cairo_gl_need_extend(gl->height, 
			&(gl->extend_height), &(gl->extend_height_scale));
		gl->extend_height_scale = 1.0;
		if(width_need_snapshot || height_need_snapshot)
			gl->needs_extend = TRUE;
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
	if(cairo_surface_get_type(src) == CAIRO_SURFACE_TYPE_GL)
	{
		cairo_gl_surface_t *s = (cairo_gl_surface_t *)src;
		if(extend == 0)
			return (cairo_gl_surface_t *)cairo_surface_reference(src);
		else
		{
			cairo_surface_t *snapshot = _cairo_surface_has_snapshot((cairo_surface_t *)src, &_cairo_gl_surface_backend);
			if(snapshot != NULL)
			{
				// src surface has snapshot
				//cairo_gl_surface_t *s = (cairo_gl_surface_t *)snapshot;
				//if(s->needs_extend == FALSE)
					return (cairo_gl_surface_t *)cairo_surface_reference(snapshot);
				// get snapshot of snapshot
				/*else
				{
					cairo_surface_t *s1 = _cairo_surface_has_snapshot(snapshot, &_cairo_gl_surface_backend);
					if(s1 != NULL)
						return (cairo_gl_surface_t *)cairo_surface_reference(s1);
				}*/
			}
			else // snapshot == NULL
			{
				// we need to generate a snapshot
				cairo_gl_surface_t *t = (cairo_gl_surface_t *)src;
				cairo_surface_t *snap1 = cairo_surface_create_similar(src,
					src->content, t->extend_width, t->extend_height);
				if(snap1 == NULL)
					return NULL;
				cairo_gl_surface_t *snap1_gl = (cairo_gl_surface_t *)snap1;
				//printf("--------------------------generate pot clone, tex = %d\n", snap1_gl->tex);
				cairo_bool_t width_need_snapshot = 
					_cairo_gl_need_extend(t->width,
					&(snap1_gl->extend_width), 
					&(snap1_gl->extend_width_scale));
				cairo_bool_t height_need_snapshot = 
					_cairo_gl_need_extend(t->height, 
					&(snap1_gl->extend_height), 
					&(snap1_gl->extend_height_scale));
				if(width_need_snapshot || height_need_snapshot)
					snap1_gl->needs_extend = TRUE;
				snap1_gl->scale = t->scale;
				cairo_gl_composite_t *setup;
				cairo_operator_t op = CAIRO_OPERATOR_OVER;

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
				cairo_pattern_t *pat = cairo_pattern_create_for_surface(src);
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

				//_cairo_gl_composite_set_mask_spans(setup);
				cairo_gl_context_t *ctx;
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
				GLfloat vertices[] = {0, 0, 0, 0, 0, 0, 0, 0};
				//GLfloat mask_vertices[] = {0, 0, 0, 0, 0, 0, 0, 0};
				double v[] = {0, 0, 0, 0, 0, 0, 0, 0};
				GLfloat st[] = { 0, 0, 1, 0, 1, 1, 0, 1};
				//GLfloat mask_st[] = { 0, 0, 1, 0, 1, 1, 0, 1};
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
				//cairo_surface_write_to_png(snap1, "./test_1.png");
	
				_cairo_surface_attach_snapshot(src, &snap1_gl->base, _cairo_gl_surface_remove_from_cache);
				return (cairo_gl_surface_t *)cairo_surface_reference(&(snap1_gl->base));
				//cairo_surface_destroy(&(snap1_gl->base));
//				_cairo_surface_detach_snapshot(&snap1_gl->base);
				//return snap1_gl;
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
	void *extra;
	//printf("cannot find clone\n");
	status = _cairo_surface_acquire_source_image(src, &img_src, &extra);
	if(unlikely(status))
		return NULL;
	img_src = (cairo_image_surface_t *)src;
	clone = (cairo_gl_surface_t *)
		_cairo_gl_surface_create_similar(&surface->base, 
			((cairo_surface_t *)img_src)->content,
			img_src->width, img_src->height);

	if(clone == NULL || cairo_surface_get_type(clone) == CAIRO_SURFACE_TYPE_IMAGE)
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
	status = _cairo_gl_surface_upload_image(clone, img_src, 0, 0, 
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

	// setup image surface snapshot of cloned gl surface
	_cairo_surface_attach_snapshot(src, &clone->base, _cairo_gl_surface_remove_from_cache);
	return _cairo_gl_generate_clone(surface, clone, extend);

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

	//long now = _get_tick();
	// Henry Song

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
		if (_cairo_gl_is_big_endian ()) 
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
		//cpp = 4;
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

	//long then = _get_tick();
	//printf("download image takes %ld usec\n", then - now);
	cairo_image_surface_t *argb32_image = _cairo_image_surface_coerce_to_format(image, CAIRO_FORMAT_ARGB32);
	cairo_surface_destroy(&image->base);
	image = argb32_image;
	image->base.is_clear = 0;
	//cairo_surface_write_to_png(&image->base, "./test2.png");
	//cairo_surface_write_to_png(&image->base, "./test1.png");

	// Henry Song
	/*if(surface->external_tex == 0)
	{
		unsigned char *upside_down_data = (unsigned char *)malloc(sizeof(unsigned char)*(image->stride * image->height));
		int i;
		unsigned long last_low = (image->height -1) * image->stride;
		for(i = 0; i < image->height; i++)
		{
			memcpy(upside_down_data+i*image->stride, image->data+last_low - i*image->stride, image->stride);
		}
		memcpy(image->data, upside_down_data, image->stride*image->height);
		free(upside_down_data);
	}*/

	// we have the image, we need to scale if scale != 1.0
	if(surface->scale != 1.0 || !_cairo_gl_surface_is_texture(surface))
	{
		float reverse_scale = 1.0 / surface->scale;
		int orig_width = (int)(cairo_image_surface_get_width(&image->base) * reverse_scale);
		int orig_height = (int)(cairo_image_surface_get_height(&image->base) * reverse_scale);
		cairo_image_surface_t *orig_image = (cairo_image_surface_t *)
			cairo_surface_create_similar(&image->base,
				cairo_surface_get_content(&image->base),
				orig_width,
				orig_height);
		cairo_t *cr = cairo_create(&orig_image->base);
		if(_cairo_gl_surface_is_texture(surface))
		{
			cairo_scale(cr, reverse_scale, reverse_scale);
			cairo_set_source_surface(cr, &image->base, 0, 0);
		}
		else
		{
			cairo_scale(cr, reverse_scale, -reverse_scale);
			cairo_set_source_surface(cr, &image->base, 0, -orig_height);
		}
		cairo_paint(cr);
		cairo_destroy(cr);

		cairo_surface_destroy(&image->base);
		image = orig_image;

		// fix interest
		interest->x *= reverse_scale;
		interest->y *= reverse_scale;
		interest->width *= reverse_scale;
		interest->height *= reverse_scale;
	}

	image->base.is_clear = 0;

    *image_out = image;
	//cairo_surface_write_to_png(&image->base, "/root/test2.png");
    if (rect_out != NULL)
	*rect_out = *interest;

	//now  = _get_tick() - then;
	//printf("convert image in cairo takes %ld usec\n", now);
    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_gl_surface_finish (void *abstract_surface)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_status_t status;
    cairo_gl_context_t *ctx;

	//if(surface->tex == 7)
	//printf("0-------------- in surface_finish, tex = %d, \n", surface->tex);
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
		glDeleteTextures(1, &surface->tex_img);

	// release clip
	if(surface->clip != NULL)
	{
		_cairo_clip_destroy(surface->clip);
		surface->clip = NULL;
	}
	_cairo_gl_index_buf_t *current, *temp;
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
		//free((_cairo_gl_index_buf_t *)(surface->indices_buf));
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

    //status = _cairo_gl_surface_draw_image (abstract_surface, image,
	status = _cairo_gl_surface_upload_image(abstract_surface, image, 
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
	if (clone == NULL || cairo_surface_get_type(clone) == CAIRO_SURFACE_TYPE_IMAGE)
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

/** Creates a cairo-gl pattern surface for the given trapezoids */
static cairo_status_t
_cairo_gl_get_traps_pattern (cairo_gl_surface_t *dst,
			     int dst_x, int dst_y,
			     int width, int height,
			     cairo_trapezoid_t *traps,
			     int num_traps,
			     cairo_antialias_t antialias,
			     cairo_surface_pattern_t *pattern)
{
    pixman_format_code_t pixman_format;
    pixman_image_t *image;
    cairo_surface_t *surface;
    int i;

    pixman_format = antialias != CAIRO_ANTIALIAS_NONE ? PIXMAN_a8 : PIXMAN_a1,
    image = pixman_image_create_bits (pixman_format, width, height, NULL, 0);
    if (unlikely (image == NULL))
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    for (i = 0; i < num_traps; i++) {
	pixman_trapezoid_t trap;

	trap.top = _cairo_fixed_to_16_16 (traps[i].top);
	trap.bottom = _cairo_fixed_to_16_16 (traps[i].bottom);

	trap.left.p1.x = _cairo_fixed_to_16_16 (traps[i].left.p1.x);
	trap.left.p1.y = _cairo_fixed_to_16_16 (traps[i].left.p1.y);
	trap.left.p2.x = _cairo_fixed_to_16_16 (traps[i].left.p2.x);
	trap.left.p2.y = _cairo_fixed_to_16_16 (traps[i].left.p2.y);

	trap.right.p1.x = _cairo_fixed_to_16_16 (traps[i].right.p1.x);
	trap.right.p1.y = _cairo_fixed_to_16_16 (traps[i].right.p1.y);
	trap.right.p2.x = _cairo_fixed_to_16_16 (traps[i].right.p2.x);
	trap.right.p2.y = _cairo_fixed_to_16_16 (traps[i].right.p2.y);

	pixman_rasterize_trapezoid (image, &trap, -dst_x, -dst_y);
    }

    surface = _cairo_image_surface_create_for_pixman_image (image,
							    pixman_format);
    if (unlikely (surface->status)) {
	pixman_image_unref (image);
	return surface->status;
    }

    _cairo_pattern_init_for_surface (pattern, surface);
    cairo_surface_destroy (surface);

    return CAIRO_STATUS_SUCCESS;
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
            if (status != CAIRO_INT_STATUS_UNSUPPORTED)
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
_cairo_gl_surface_composite_trapezoids (cairo_operator_t op,
					const cairo_pattern_t *pattern,
					void *abstract_dst,
					cairo_antialias_t antialias,
					int src_x, int src_y,
					int dst_x, int dst_y,
					unsigned int width,
					unsigned int height,
					cairo_trapezoid_t *traps,
					int num_traps,
					cairo_region_t *clip_region)
{
    cairo_gl_surface_t *dst = abstract_dst;
    cairo_surface_pattern_t traps_pattern;
    cairo_int_status_t status;

    if (! _cairo_gl_operator_is_supported (op))
	return UNSUPPORTED ("unsupported operator");

    status = _cairo_gl_get_traps_pattern (dst,
					  dst_x, dst_y, width, height,
					  traps, num_traps, antialias,
					  &traps_pattern);
    if (unlikely (status))
	return status;

    status = _cairo_gl_surface_composite (op,
					  pattern, &traps_pattern.base, dst,
					  src_x, src_y,
					  0, 0,
					  dst_x, dst_y,
					  width, height,
					  clip_region);

    _cairo_pattern_fini (&traps_pattern.base);

    assert (status != CAIRO_INT_STATUS_UNSUPPORTED);
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

static cairo_status_t
_cairo_gl_render_bounded_spans (void *abstract_renderer,
				int y, int height,
				const cairo_half_open_span_t *spans,
				unsigned num_spans)
{
    cairo_gl_surface_span_renderer_t *renderer = abstract_renderer;

    if (num_spans == 0)
	return CAIRO_STATUS_SUCCESS;

    do {
	if (spans[0].coverage) {
            _cairo_gl_composite_emit_rect (renderer->ctx,
                                           spans[0].x, y,
                                           spans[1].x, y + height,
                                           spans[0].coverage);
	}

	spans++;
    } while (--num_spans > 1);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_gl_render_unbounded_spans (void *abstract_renderer,
				  int y, int height,
				  const cairo_half_open_span_t *spans,
				  unsigned num_spans)
{
    cairo_gl_surface_span_renderer_t *renderer = abstract_renderer;

    if (y > renderer->ymin) {
        _cairo_gl_composite_emit_rect (renderer->ctx,
                                       renderer->xmin, renderer->ymin,
                                       renderer->xmax, y,
                                       0);
    }

    if (num_spans == 0) {
        _cairo_gl_composite_emit_rect (renderer->ctx,
                                       renderer->xmin, y,
                                       renderer->xmax, y + height,
                                       0);
    } else {
        if (spans[0].x != renderer->xmin) {
            _cairo_gl_composite_emit_rect (renderer->ctx,
                                           renderer->xmin, y,
                                           spans[0].x,     y + height,
                                           0);
        }

        do {
            _cairo_gl_composite_emit_rect (renderer->ctx,
                                           spans[0].x, y,
                                           spans[1].x, y + height,
                                           spans[0].coverage);
            spans++;
        } while (--num_spans > 1);

        if (spans[0].x != renderer->xmax) {
            _cairo_gl_composite_emit_rect (renderer->ctx,
                                           spans[0].x,     y,
                                           renderer->xmax, y + height,
                                           0);
        }
    }

    renderer->ymin = y + height;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_gl_finish_unbounded_spans (void *abstract_renderer)
{
    cairo_gl_surface_span_renderer_t *renderer = abstract_renderer;

    if (renderer->ymax > renderer->ymin) {
        _cairo_gl_composite_emit_rect (renderer->ctx,
                                       renderer->xmin, renderer->ymin,
                                       renderer->xmax, renderer->ymax,
                                       0);
    }

    return _cairo_gl_context_release (renderer->ctx, CAIRO_STATUS_SUCCESS);
}

static cairo_status_t
_cairo_gl_finish_bounded_spans (void *abstract_renderer)
{
    cairo_gl_surface_span_renderer_t *renderer = abstract_renderer;

    return _cairo_gl_context_release (renderer->ctx, CAIRO_STATUS_SUCCESS);
}

static void
_cairo_gl_surface_span_renderer_destroy (void *abstract_renderer)
{
    cairo_gl_surface_span_renderer_t *renderer = abstract_renderer;

    if (!renderer)
	return;

    _cairo_gl_composite_fini (&renderer->setup);

    free (renderer);
}

static cairo_bool_t
_cairo_gl_surface_check_span_renderer (cairo_operator_t	       op,
				       const cairo_pattern_t  *pattern,
				       void		      *abstract_dst,
				       cairo_antialias_t       antialias)
{
    if (! _cairo_gl_operator_is_supported (op))
	return FALSE;

    return TRUE;

    (void) pattern;
    (void) abstract_dst;
    (void) antialias;
}

static cairo_span_renderer_t *
_cairo_gl_surface_create_span_renderer (cairo_operator_t	 op,
					const cairo_pattern_t	*src,
					void			*abstract_dst,
					cairo_antialias_t	 antialias,
					const cairo_composite_rectangles_t *rects,
					cairo_region_t		*clip_region)
{
    cairo_gl_surface_t *dst = abstract_dst;
    cairo_gl_surface_span_renderer_t *renderer;
    cairo_status_t status;
    const cairo_rectangle_int_t *extents;

    renderer = calloc (1, sizeof (*renderer));
    if (unlikely (renderer == NULL))
	return _cairo_span_renderer_create_in_error (CAIRO_STATUS_NO_MEMORY);

    renderer->base.destroy = _cairo_gl_surface_span_renderer_destroy;
    if (rects->is_bounded) {
	renderer->base.render_rows = _cairo_gl_render_bounded_spans;
        renderer->base.finish =      _cairo_gl_finish_bounded_spans;
	extents = &rects->bounded;
    } else {
	renderer->base.render_rows = _cairo_gl_render_unbounded_spans;
        renderer->base.finish =      _cairo_gl_finish_unbounded_spans;
	extents = &rects->unbounded;
    }
    renderer->xmin = extents->x;
    renderer->xmax = extents->x + extents->width;
    renderer->ymin = extents->y;
    renderer->ymax = extents->y + extents->height;

    status = _cairo_gl_composite_init (&renderer->setup,
                                       op, dst,
                                       FALSE, extents);
    if (unlikely (status))
        goto FAIL;

    status = _cairo_gl_composite_set_source (&renderer->setup, src,
                                             extents->x, extents->y,
                                             extents->x, extents->y,
                                             extents->width, extents->height,
											 0, 0, 0);
    if (unlikely (status))
        goto FAIL;

    _cairo_gl_composite_set_mask_spans (&renderer->setup);
    _cairo_gl_composite_set_clip_region (&renderer->setup, clip_region);

    status = _cairo_gl_composite_begin (&renderer->setup, &renderer->ctx);
    if (unlikely (status))
        goto FAIL;

    return &renderer->base;

FAIL:
    _cairo_gl_composite_fini (&renderer->setup);
    free (renderer);
    return _cairo_span_renderer_create_in_error (status);
}

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
	cairo_rectangle_int_t source_extents, *clip_extents = NULL;
	cairo_gl_composite_t *setup;
	cairo_gl_context_t *ctx;
	char *colors;
	int stride;
	GLfloat stencil_color[] ={ 0, 0, 0, 1};
	cairo_image_surface_t *image_surface;
	void *image_extra;

	cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;

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
	// Henry Song
	// let's take care of CAIRO_PATTERN_TYPE_SURFACE
	return _cairo_gl_surface_mask(abstract_surface,
	op,
	source,
	NULL,
	clip);
		
    //return CAIRO_INT_STATUS_UNSUPPORTED;
}

static cairo_int_status_t
_cairo_gl_surface_mask (cairo_surface_t *abstract_surface,
	cairo_operator_t op,
	const cairo_pattern_t *source,
	const cairo_pattern_t *mask,
	cairo_clip_t *clip)
{
    cairo_gl_surface_t *surface = abstract_surface;
    cairo_composite_rectangles_t extents, mas_extents;
    cairo_box_t boxes_stack[32], *clip_boxes = boxes_stack;
    cairo_clip_t local_clip;
    cairo_bool_t have_clip = FALSE;
    int num_boxes = ARRAY_LENGTH (boxes_stack);
    cairo_polygon_t polygon;
    cairo_status_t status;
	cairo_gl_context_t *ctx;
	_cairo_gl_path_t *current;
	//char *colors;
	int stride;
	int index;

	// check whether needs to super sampling
	if(clip != NULL)
	{
	//	surface->needs_super_sampling = TRUE;
	}
	/*if(surface->needs_super_sampling)
	{
		_cairo_gl_surface_super_sampling(abstract_surface);

		surface->needs_super_sampling = FALSE;
	}*/

	// Henry Song
	cairo_gl_composite_t *setup;




	// Henry Song
	//cairo_surface_write_to_png(abstract_surface, "/home/me/openvg/pc/test_dst.png");
	long now = _get_tick();
    struct timeval start, stop;
	int spent;
	//status = _cairo_surface_clipper_set_clip(&surface->clipper, clip);
	//gettimeofday(&start, NULL);
	// Henry Song
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
	cairo_gl_surface_t *clone = NULL;
	cairo_surface_t *snapshot = NULL;
	cairo_solid_pattern_t *solid = NULL;
	int extend = 0;
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
		//cairo_surface_write_to_png(abstract_surface, "/root/test.png");
		//cairo_surface_write_to_png(clone, "/root/test1.png");
		//printf("write clone\n");
	}
	else if(source->type == CAIRO_PATTERN_TYPE_SOLID)
		solid = (cairo_solid_pattern_t *)source;
	//printf("generate clone takes %ld usec\n", _get_tick() - now);
	/*now = _get_tick() - now;
	printf("clone takes %ld us\n", now);
	now = _get_tick();*/
	// Henry Song
    //if (_cairo_clip_contains_extents (clip, &extents))
	//clip = NULL;

#if 0
    if (extents.is_bounded && clip != NULL) {
	cairo_clip_path_t *clip_path;

	if (((clip_path = _clip_get_single_path (clip)) != NULL) &&
	    _cairo_path_fixed_equal (&clip_path->path, path))
	{
	    clip = NULL;
	}
    }
#endif

    //if (clip != NULL) {
	//clip = _cairo_clip_init_copy (&local_clip, clip);
	//have_clip = TRUE;
    //}

	// Henry Song
	//gettimeofday(&start, NULL);
    //status = _cairo_clip_to_boxes (&clip, &extents, &clip_boxes, &num_boxes);
	// Henry Song
	//gettimeofday(&stop, NULL);
	//spent = stop.tv_usec - start.tv_usec;
	//spent += 1000000 * (stop.tv_sec - start.tv_sec);
	//printf("_cairo_clip_to_boxes takes %d usec\n", spent);

    //if (unlikely (status)) {
	//if (have_clip)
	//    _cairo_clip_fini (&local_clip);
	//
	//return status;
    //}

	// Henry Song
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
	setup->source = source;

	// set up source
	if(clone == NULL)
		status = _cairo_gl_composite_set_source(setup,
			source, extents.bounded.x, extents.bounded.y,
			extents.bounded.x, extents.bounded.y, 
			extents.bounded.width, extents.bounded.height,
			0, 0, 0);
	else
	{
		float temp_width = clone->width / clone->extend_width_scale / clone->scale;
		float temp_height = clone->height / clone->extend_height_scale / clone->scale;

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

	//_cairo_gl_composite_set_mask_spans(setup);
	status = _cairo_gl_context_acquire (surface->base.device, &ctx);
	if(unlikely(status))
	{
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		_cairo_gl_composite_fini(setup);
		free(setup);
		return status;
	}
	//printf("context acquire taks %ld usec\n", _get_tick() - now);
	/*now = _get_tick() - now;
	printf("acquire context takes %ld us\n", now);
	now = _get_tick();*/
	setup->ctx = ctx;
	_cairo_gl_context_set_destination(ctx, surface);
	//printf("set destination taks %ld usec\n", _get_tick() - now);
	//now = _get_tick() - now;
	//printf("set destination takes %ld us\n", now);
	// Henry Song
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
	//now = _get_tick();
	// set up solid color mask
	cairo_gl_surface_t *mask_clone = NULL;
	cairo_surface_t *mask_snapshot = NULL;
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

	_cairo_gl_context_set_destination(ctx, surface);
	//if(clone != NULL)
	//	printf("got clone, width = %d, height = %d\n", clone->orig_width, clone->orig_height);
	if(mask_clone != NULL)
	{
		//printf("got mask clone, width = %d, height = %d\n", mask_clone->orig_width, mask_clone->orig_height);
		float temp_width = mask_clone->width / mask_clone->extend_width_scale * mask_clone->scale;
		float temp_height = mask_clone->height / mask_clone->extend_height_scale * mask_clone->scale;

		_cairo_gl_composite_set_mask(setup, mask, 
			extents.bounded.x, extents.bounded.y,
			extents.bounded.x, extents.bounded.y, 
			extents.bounded.width, extents.bounded.height,
			mask_clone->tex, (int)temp_width, (int)temp_height); 
	}
	else
		_cairo_gl_composite_set_mask(setup, mask, 
			extents.bounded.x, extents.bounded.y,
			extents.bounded.x, extents.bounded.y, 
			extents.bounded.width, extents.bounded.height,
			0, 0, 0);
	//now = _get_tick() - now;
	//printf("mask clone takes %ld us\n", now);
	//now = _get_tick();
	/*		
	if(mask != NULL && mask->type == CAIRO_PATTERN_TYPE_SOLID)
	{
		setup->src.type = CAIRO_GL_OPERAND_CONSTANT;
		_cairo_gl_composite_set_mask(setup, mask, 
			0, 0,
			0, 0, 
			0, 0);
	}*/
	if(source->type == CAIRO_PATTERN_TYPE_SURFACE)
		setup->src.type = CAIRO_GL_OPERAND_TEXTURE;
	else if(source->type == CAIRO_PATTERN_TYPE_SOLID)
		setup->src.type = CAIRO_GL_OPERAND_CONSTANT;
	else if(source->type == CAIRO_PATTERN_TYPE_LINEAR)
		setup->src.type = CAIRO_GL_OPERAND_LINEAR_GRADIENT;
	else if(source->type == CAIRO_PATTERN_TYPE_RADIAL)
		setup->src.type == CAIRO_GL_OPERAND_RADIAL_GRADIENT_NONE;
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
	
/*	status = _cairo_gl_composite_set_source(setup,
		source, extents.bounded.x, extents.bounded.y,
		extents.bounded.x, extents.bounded.y, 
		extents.bounded.width, extents.bounded.height);
	if(unlikely(status))
	{
		_cairo_gl_composite_fini(setup);
		free(setup);
		return status;
	}
*/
//	_cairo_gl_composite_set_mask_spans(setup);

	// we have the image uploaded, we need to setup vertices
	GLfloat vertices[] = {0, 0, 0, 0, 0, 0, 0, 0};
	GLfloat mask_vertices[] = {0, 0, 0, 0, 0, 0, 0, 0};
	double v[] = {0, 0, 0, 0, 0, 0, 0, 0};
	GLfloat st[] = { 0, 0, 1, 0, 1, 1, 0, 1};
	GLfloat mask_st[] = { 0, 0, 1, 0, 1, 1, 0, 1};
	GLfloat colors[] = {0, 0, 0, 0,
					    0, 0, 0, 0,
						0, 0, 0, 0,
						0, 0, 0, 0};
	v[0] = extents.bounded.x;
	v[1] = extents.bounded.y;
	v[2] = extents.bounded.x + extents.bounded.width;
	v[3] = extents.bounded.y;
	v[4] = extents.bounded.x + extents.bounded.width;
	v[5] = extents.bounded.y + extents.bounded.height;
	v[6] = extents.bounded.x;
	v[7] = extents.bounded.y + extents.bounded.height;
	int i = 0;
	for(i = 0; i < 8; i++)
		vertices[i] = v[i];
	cairo_matrix_t m, m1;
	if(source->type == CAIRO_PATTERN_TYPE_SURFACE)
	{
		// compute s, t for bounding box
		cairo_matrix_init_scale(&m, 1.0, 1.0);
		cairo_matrix_multiply(&m, &m, &source->matrix);
		cairo_matrix_init_scale(&m1, 1.0 / clone->width,
			1.0 / clone->height);
		cairo_matrix_multiply(&m, &m, &m1);
		cairo_matrix_init_scale(&m1, 1.0 * clone->extend_width_scale * clone->scale,
			1.0 * clone->extend_height_scale * clone->scale);
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
			cairo_matrix_init_scale(&m1, 1.0 * mask_clone->extend_width_scale * mask_clone->scale,
				1.0 * mask_clone->extend_height_scale * mask_clone->scale);
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
			cairo_status_t status1 = _cairo_gl_context_release(ctx, status);
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
			//cairo_surface_write_to_png(mask_clone, "/home/me/test.png");
			cairo_matrix_init_scale(&m, 1.0, 1.0);
			cairo_matrix_multiply(&m, &m, &mask->matrix);
			cairo_matrix_init_scale(&m1, 1.0 / mask_clone->width, 
				1.0 / mask_clone->height);
			cairo_matrix_multiply(&m, &m, &m1);
			cairo_matrix_init_scale(&m1, 1.0 *mask_clone->extend_width_scale * mask_clone->scale,
				1.0 *mask_clone->extend_height_scale * mask_clone->scale);
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
			cairo_matrix_init_scale(&m1, 1.0 *mask_clone->extend_width_scale * mask_clone->scale,
				1.0 * mask_clone->extend_height_scale * mask_clone->scale);
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
	//glDisable(GL_STENCIL_TEST);
	//cairo_surface_write_to_png(abstract_surface, "/home/me/openvg/pc/test.png");
	_cairo_gl_composite_fini(setup);
	if(clone != NULL)
	{
		//cairo_surface_write_to_png(&(clone->base), "/home/me/openvg/pc/test_src.png");
		cairo_surface_destroy(&clone->base);
	}
	if(mask_clone != NULL)
	{
		//cairo_surface_write_to_png(&(mask_clone->base), "/home/me/openvg/pc/test_mask.png");
		cairo_surface_destroy(&mask_clone->base);
	}
	free(setup);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	//_cairo_gl_context_release(ctx, status);
	//glDeleteTextures(1, &surface->tex);
	status = _cairo_gl_context_release(ctx, status);
	//now = _get_tick() - now;
	//printf("paint takes %ld us\n", now);
	if(surface->needs_extend == TRUE)
	{
		if(_cairo_surface_has_snapshot(&(surface->base), &_cairo_gl_surface_backend))
			_cairo_surface_detach_snapshot(&(surface->base));
	}
	surface->needs_new_data_surface = TRUE;
	// Henry Song
	long then = _get_tick() - now;
	//printf("cairo surface mask takes %ld usec\n", then);
	return status;
}

static cairo_int_status_t
_cairo_gl_surface_polygon (cairo_gl_surface_t *dst,
                           cairo_operator_t op,
                           const cairo_pattern_t *src,
                           cairo_polygon_t *polygon,
                           cairo_fill_rule_t fill_rule,
                           cairo_antialias_t antialias,
                           const cairo_composite_rectangles_t *extents,
                           cairo_clip_t *clip)
{
    cairo_status_t status;
    cairo_region_t *clip_region = NULL;

    if (clip != NULL) {
	clip_region = _cairo_clip_get_region (clip);
#if 0
	if (unlikely (status == CAIRO_INT_STATUS_NOTHING_TO_DO))
	    return CAIRO_STATUS_SUCCESS;
	if (unlikely (_cairo_status_is_error (status)))
	    return status;

	if (status == CAIRO_INT_STATUS_UNSUPPORTED)
            return UNSUPPORTED ("a clip surface would be required");
#endif
    }

    if (! _cairo_surface_check_span_renderer (op, src, &dst->base, antialias))
        return UNSUPPORTED ("no span renderer");

    if (op == CAIRO_OPERATOR_SOURCE)
        return UNSUPPORTED ("SOURCE compositing doesn't work in GL");
    if (op == CAIRO_OPERATOR_CLEAR) {
        op = CAIRO_OPERATOR_DEST_OUT;
        src = &_cairo_pattern_white.base;
    }

    status = _cairo_surface_composite_polygon (&dst->base,
                                               op,
                                               src,
                                               fill_rule,
                                               antialias,
                                               extents,
                                               polygon,
                                               clip_region);
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
    cairo_box_t boxes_stack[32], *clip_boxes = boxes_stack;
    int num_boxes = ARRAY_LENGTH (boxes_stack);
    cairo_clip_t local_clip;
    cairo_bool_t have_clip = FALSE;
    cairo_polygon_t polygon;
    cairo_status_t status;

	_cairo_gl_index_t indices;

	if(antialias != CAIRO_ANTIALIAS_NONE || clip != NULL)
	{
		//printf("stroke set needs super sampling\n");
		//surface->needs_super_sampling = TRUE;
	}
	
	// Henry Song
	cairo_gl_composite_t *setup;
	cairo_gl_context_t *ctx;


    status = _cairo_composite_rectangles_init_for_stroke (&extents,
							  surface->width,
							  surface->height,
							  op, source,
							  path, style, ctm,
							  clip);
    if (unlikely (status))
		return status;
	
	/*status = _cairo_gl_context_acquire (surface->base.device, &ctx);
	if(unlikely(status))
	{
		return status;
	}
	_cairo_gl_context_set_destination(ctx, surface);
	*/
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
			//_cairo_gl_context_release(ctx, status);
			return CAIRO_INT_STATUS_UNSUPPORTED;
		}
		
		cairo_color_t color;
		color.red = 0;
		color.green = 0;
		color.blue = 0;
		color.alpha = 0;
		_cairo_gl_surface_clear(surface->mask_surface, &color);
		surface->mask_surface->bound_fbo = TRUE;
		
		status = _cairo_gl_surface_stroke(surface->mask_surface, 
			CAIRO_OPERATOR_OVER, source, path, style, ctm, ctm_inverse,
				tolerance, antialias, NULL);
		if(unlikely(status))
		{
			//_cairo_gl_context_release(ctx, status);
			return status;
		}
		//long then = _get_tick() - now;
		//printf("------ fill to mask takes %ld usec\n", then);
		//now = _get_tick();
		//printf("++++++++++ start paint mask\n");
		surface->mask_surface->bound_fbo = TRUE;
		cairo_surface_pattern_t mask_pattern;
		surface->mask_surface->base.is_clear = FALSE;
		_cairo_pattern_init_for_surface(&mask_pattern, surface->mask_surface);
		mask_pattern.base.has_component_alpha = FALSE;
		status = _cairo_surface_paint(&surface->base, op, &(mask_pattern.base), clip);
		_cairo_pattern_fini(&mask_pattern.base);
		surface->mask_surface->bound_fbo = FALSE;
		//then = _get_tick() - now;
		//printf("+++++++++++ paint mask takes %ld usec\n", then);

		//_cairo_gl_context_release(ctx, status);
		return status;
	}


	status = _cairo_gl_context_acquire (surface->base.device, &ctx);
	if(unlikely(status))
		return status;

	// upload image
	cairo_gl_surface_t *clone = NULL;
	cairo_surface_t *snapshot = NULL;
	cairo_solid_pattern_t *solid = NULL;

	int extend = 0;
	if(source->type == CAIRO_PATTERN_TYPE_SURFACE)
	{
		cairo_surface_t *src = ((cairo_surface_pattern_t *)source)->surface;
		if(source->extend == CAIRO_EXTEND_REPEAT || 
		   source->extend == CAIRO_EXTEND_REFLECT)
		   	extend = 1;
		clone = _cairo_gl_generate_clone(surface, src, extend);
		if(clone == NULL)
		{
			_cairo_gl_context_release(ctx, status);
			return UNSUPPORTED("create_clone failed");
		}
	}
	else if(source->type == CAIRO_PATTERN_TYPE_SOLID)
		solid = (cairo_solid_pattern_t *)source;

	
	setup = (cairo_gl_composite_t *)malloc(sizeof(cairo_gl_composite_t));
	
	status = _cairo_gl_composite_init(setup, op, surface, FALSE,
		&extents.bounded);
	if(unlikely (status))
	{
		_cairo_gl_composite_fini(setup);
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		free(setup);
		_cairo_gl_context_release(ctx, status);
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
		float temp_width = clone->width / clone->extend_width_scale * clone->scale;
		float temp_height = clone->height / clone->extend_height_scale * clone->scale;
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
		_cairo_gl_context_release(ctx, status);
		return status;
	}


	//_cairo_gl_composite_set_mask_spans(setup);
	/*status = _cairo_gl_context_acquire(surface->base.device, &ctx);
	if(unlikely(status))
	{
		_cairo_gl_composite_fini(setup);
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		free(setup);
		return status;
	}*/

	setup->ctx = ctx;
	_cairo_gl_context_set_destination(ctx, surface);

	// Henry Song
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

/*
	int i;
	printf("indices %d { \n", indices.num_indices);
	for(i = 0; i < indices.num_indices; i++)
		printf("%d, ", indices.indices[i]);
	printf(" }\n");
	printf("vertices %d \n", indices.num_vertices);
	for(i = 0; i < indices.num_vertices; i++)
		printf("(%0.1f, %0.1f), ", indices.vertices[i*2], indices.vertices[i*2+1]);
	printf("\n");
*/
	int v, idx;
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &v);
	//glGetIntegerv(GL_MAX_ELEMENTS_INDICES);
	// fill it, we fix t later
	status = _cairo_gl_fill(setup, indices.num_vertices, 
		indices.vertices, NULL, indices.num_indices, indices.indices,
		setup->ctx);
	
	//glDisable(GL_STENCIL_TEST);
	if(clone != NULL)
		cairo_surface_destroy(&clone->base);
	_cairo_gl_destroy_indices(&indices);
	_cairo_gl_composite_fini(setup);
	free(setup);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	status = _cairo_gl_context_release(ctx, status);
	if(surface->needs_extend == TRUE)
	{
		if(_cairo_surface_has_snapshot(&(surface->base), &_cairo_gl_surface_backend))
			_cairo_surface_detach_snapshot(&(surface->base));
	}
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
    cairo_box_t boxes_stack[32], *clip_boxes = boxes_stack;
    cairo_clip_t local_clip;
    cairo_bool_t have_clip = FALSE;
    int num_boxes = ARRAY_LENGTH (boxes_stack);
    cairo_polygon_t polygon;
    cairo_status_t status;
	cairo_gl_context_t *ctx;
	_cairo_gl_path_t *current;
	char *colors;
	int stride;
	int index;

	// Henry Song
	cairo_gl_composite_t *setup;

	if(antialias != CAIRO_ANTIALIAS_NONE || clip != NULL)
	{
		//printf("fill set needs super sampling\n");
		//surface->needs_super_sampling = TRUE;
	}
		

	// Henry Song

	//printf("need stencil = %d\n", surface->needs_stencil);
	// Henry Song
	//long now = _get_tick();
    struct timeval start, stop;
	int spent;

	/*if(antialias == CAIRO_ANTIALIAS_NONE )
		glDisable(GL_MULTISAMPLE);
	else
		glEnable(GL_MULTISAMPLE);
	*/
	//status = _cairo_surface_clipper_set_clip(&surface->clipper, clip);
	//gettimeofday(&start, NULL);
	// Henry Song
	// we keep this such that when clip and path do not intersect, 
	// we simply return without actual draw - optimization
	status = _cairo_composite_rectangles_init_for_fill (&extents,
							surface->width,
							surface->height,
							op, source, path,
							clip);
	// Henry Song
	//gettimeofday(&stop, NULL);
	//spent = stop.tv_usec - start.tv_usec;
	//spent += 1000000 * (stop.tv_sec - start.tv_sec);
	//printf("_cairo_composite_rectangles_init_for_fill takes %d usec\n", spent);
    if (unlikely (status))
	return status;
	
	/*status = _cairo_gl_context_acquire (surface->base.device, &ctx);
	if(unlikely(status))
	{
		return status;
	}
	_cairo_gl_context_set_destination(ctx, surface);
	*/
	if(extents.is_bounded == 0)
	{
		// it is unbounded operator
		// get this surface's mask
		//printf("+++++++++++++++++++ start paint to mask\n");
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
			//status = _cairo_gl_context_release(ctx, status);
			return CAIRO_INT_STATUS_UNSUPPORTED;
		}
		
		
		cairo_color_t color;
		color.red = 0;
		color.green = 0;
		color.blue = 0;
		color.alpha = 0;
		_cairo_gl_surface_clear(surface->mask_surface, &color);
		surface->mask_surface->bound_fbo = TRUE;
		
		status = _cairo_gl_surface_fill(surface->mask_surface, 
			CAIRO_OPERATOR_OVER, source, path, fill_rule,
				tolerance, antialias, NULL);
		if(unlikely(status))
		{
			//_cairo_gl_context_release(ctx, status);
			return status;
		}
		//printf("\n++++++++++++++++++++ start paint mask to dest\n");
		surface->mask_surface->bound_fbo = TRUE;
		//long then = _get_tick() - now;
		//printf("------ fill to mask takes %ld usec\n", then);
		//now = _get_tick();
		//printf("++++++++++ start paint mask\n");
		cairo_surface_pattern_t mask_pattern;
		surface->mask_surface->base.is_clear = FALSE;
		_cairo_pattern_init_for_surface(&mask_pattern, surface->mask_surface);
		mask_pattern.base.has_component_alpha = FALSE;
		status = _cairo_surface_paint(&surface->base, op, &(mask_pattern.base), clip);
		_cairo_pattern_fini(&mask_pattern.base);
		surface->mask_surface->bound_fbo = FALSE;
		//printf("+++++++++++++ finish fill \n");
		//then = _get_tick() - now;
		//printf("+++++++++++ paint mask takes %ld usec\n", then);

		//_cairo_gl_context_release(ctx, status);
		return status;
	}

	// upload image
	cairo_gl_surface_t *clone = NULL;
	cairo_surface_t *snapshot = NULL;
	cairo_solid_pattern_t *solid = NULL;

	//if(surface->tex == 7)
	//	printf("paint to 7\n");
	int extend = 0;
	if(source->type == CAIRO_PATTERN_TYPE_SURFACE)
	{
		//printf("inside get clone for fill\n");
		cairo_surface_t *src = ((cairo_surface_pattern_t *)source)->surface;
		if(source->extend == CAIRO_EXTEND_REPEAT || 
		   source->extend == CAIRO_EXTEND_REFLECT)
		   	extend = 1;
		//printf("could not find snapshot\n");
		clone = _cairo_gl_generate_clone(surface, src, extend);
		//printf("======= clone width = %d, height = %d, width_scale = %0.2f, height_scale = %0.2f\n", clone->width, clone->height, clone->extend_width_scale, clone->extend_height_scale);
		if(clone == NULL)
		{
			_cairo_gl_context_release(ctx, status);
			return UNSUPPORTED("create_clone failed");
		}
	}
	else if(source->type == CAIRO_PATTERN_TYPE_SOLID)
		solid = (cairo_solid_pattern_t *)source;
	

	
	status = _cairo_gl_context_acquire (surface->base.device, &ctx);
	if(unlikely(status))
	{
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		return status;
	}
	/*_cairo_gl_context_set_destination(ctx, surface);
	status = _cairo_gl_context_release(ctx, status);
	if(unlikely(status))
	{
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		return status;
	}*/
	setup = (cairo_gl_composite_t *)malloc(sizeof(cairo_gl_composite_t));
	
	status = _cairo_gl_composite_init(setup, op, surface, FALSE,
		&extents.bounded);
	if(unlikely (status))
	{
		if(clone != NULL)
			cairo_surface_destroy(&clone->base);
		_cairo_gl_composite_fini(setup);
		free(setup);
		_cairo_gl_context_release(ctx, status);
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
		float temp_width = clone->width / clone->extend_width_scale / clone->scale;
		float temp_height = clone->height / clone->extend_height_scale / clone->scale;
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
		_cairo_gl_context_release(ctx, status);
		return status;
	}

	// let's acquire context, set surface
	//long now = _get_tick();
	//status = _cairo_gl_context_acquire (surface->base.device, &ctx);
	//if(unlikely(status))
	//	return status;
	setup->ctx = ctx;
	//now = _get_tick() - now;
	//printf("context_acquire  %d ms\n", now);
	//now = _get_tick();
	_cairo_gl_context_set_destination(ctx, surface);
	//now = _get_tick() - now;
	//printf("set destination %d ms\n", now);

	// remember, we have set the current context, we need to release it
	// when done

//	_cairo_gl_composite_set_mask_spans(setup);
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
		// Henry Song test
		//my_path = path;
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
		setup->src.type = CAIRO_GL_OPERAND_LINEAR_GRADIENT;
	else if(source->type == CAIRO_PATTERN_TYPE_RADIAL)
		setup->src.type == CAIRO_GL_OPERAND_RADIAL_GRADIENT_NONE;
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
	cairo_traps_t traps;
	//cairo_polygon_t polygons
	_cairo_gl_index_t indices;
	_cairo_traps_init(&traps);
	//_cairo_polygon_init(&polygon);
	status = _cairo_gl_create_indices(&indices);
	indices.setup = setup;
	cairo_point_t points[4];

	status = _cairo_path_fixed_fill_to_traps(path, fill_rule, tolerance, &traps);
	int m;
	cairo_fixed_t x_top_left, x_bottom_left;
	cairo_fixed_t x_top_right, x_bottom_right;
	double x1, x2;
	double y1, y2;
	double dx, dy;
	double top, bottom;
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
/*
		printf("left (%0.1f, %0.1f) (%0.1f, %0.1f), right (%0.1f, %0.1f) (%0.1f, %0.1f), top (%0.1f), bottom (%01.f)\n", 
			_cairo_fixed_to_double(x_top_left),
			_cairo_fixed_to_double(traps.traps[m].top),
			_cairo_fixed_to_double(x_bottom_left),
			_cairo_fixed_to_double(traps.traps[m].bottom),
			_cairo_fixed_to_double(x_bottom_right),
			_cairo_fixed_to_double(traps.traps[m].bottom),
			_cairo_fixed_to_double(x_top_right),
			_cairo_fixed_to_double(traps.traps[m].top),
			_cairo_fixed_to_double(traps.traps[m].top),
			_cairo_fixed_to_double(traps.traps[m].bottom));
*/	
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
			_cairo_gl_destroy_indices(&indices);
			_cairo_gl_composite_fini(setup);
			free(setup);
		}
	}
	/*
	int i;
	printf("indices %d { \n", indices.num_indices);
	for(i = 0; i < indices.num_indices; i++)
		printf("%d, ", indices.indices[i]);
	printf(" }\n");
	printf("vertices %d \n", indices.num_vertices);
	for(i = 0; i < indices.num_vertices; i++)
		printf("(%0.1f, %0.1f), ", indices.vertices[i*2], indices.vertices[i*2+1]);
	printf("\n");
	*/
	//now = _get_tick();
	status = _cairo_gl_fill(setup, indices.num_vertices, 
		indices.vertices, NULL, indices.num_indices, indices.indices,
		setup->ctx);
	_cairo_traps_fini(&traps);
	//now = _get_tick() - now;
	//printf("gl_fill takes %d ms\n", now);
	if(clone != NULL)
		cairo_surface_destroy(&clone->base);
	//glDisable(GL_STENCIL_TEST);
	_cairo_gl_destroy_indices(&indices);
	_cairo_gl_composite_fini(setup);
	free(setup);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	//now = _get_tick();
	status = _cairo_gl_context_release(ctx, status);
	if(surface->needs_extend == TRUE)
	{
		if(_cairo_surface_has_snapshot(&(surface->base), &_cairo_gl_surface_backend))
			_cairo_surface_detach_snapshot(&(surface->base));
	}
	//now = _get_tick() - now;
	//printf("context_release takes %d ms\n", now);
	//now = _get_tick() - now;
	//printf("fill takes %ld us\n", now);
    //    cairo_surface_write_to_png(&surface->base, "./test.png");
	surface->needs_new_data_surface = TRUE;
	/*if(op == CAIRO_OPERATOR_IN)
	{
		cairo_surface_write_to_png(abstract_surface, "/root/test.png");
		printf("write to png\n");
	}*/
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
	// Henry
    _cairo_gl_surface_mask, /* mask */
    _cairo_gl_surface_stroke,
    _cairo_gl_surface_fill,
    _cairo_gl_surface_show_glyphs, /* show_glyphs */
    NULL  /* snapshot */
};

cairo_status_t
_cairo_gl_surface_upload_image(cairo_gl_surface_t *dst,
	cairo_image_surface_t *image_surface,
	int src_x, int src_y,
	int width, int height,
	int dst_x, int dst_y)
{
    GLenum internal_format, format, type;
    cairo_bool_t has_alpha, needs_swap;
    cairo_gl_context_t *ctx;
    int cpp;
	//int width, height;
	cairo_image_surface_t *src;
    cairo_status_t status = CAIRO_STATUS_SUCCESS;
	cairo_image_surface_t *clone = NULL;
	GLenum error;

	int orig_width = width;
	int orig_height = height;

    //GLenum format;
    //GLuint tex;
	
	// lock gl context
    status = _cairo_gl_context_acquire (dst->base.device, &ctx);
    if (unlikely (status))
	{
		//glDeleteTextures(1, &tex);
		return status;
	}

	//glGenTextures(1, &tex);
	//error = glGetError();
	//if(dst->tex_img != 0)
	//	glDeleteTextures(1, &dst->tex_img);
	//dst->tex_img = tex;
	/*
	_cairo_gl_context_activate(ctx, CAIRO_GL_TEX_TEMP);
	glBindTexture(ctx->tex_target, dst->tex);
	glTexParameteri(ctx->tex_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(ctx->tex_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	src = image_surface;
	switch(src->base.content)
	{
		default:
			ASSERT_NOT_REACHED;
		case CAIRO_CONTENT_COLOR_ALPHA:
			format = GL_RGBA;
			break;
		case CAIRO_CONTENT_ALPHA:
			format = GL_RGBA;
			break;
		case CAIRO_CONTENT_COLOR:
			format = GL_RGBA;
			break;
	}
	glTexImage2D(ctx->tex_target, 0, format, src->width, src->height,
		0, format, GL_UNSIGNED_BYTE, NULL);
	error = glGetError();
	*/


	src = image_surface;
    if (! _cairo_gl_get_image_format_and_type (ctx->gl_flavor,
					       image_surface->pixman_format,
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
		if(is_supported == FALSE)
			goto FAIL;
		assert (is_supported);
		assert (!needs_swap);
		src = clone;
   	}

    cpp = PIXMAN_FORMAT_BPP (src->pixman_format) / 8;

    status = _cairo_gl_surface_flush (&dst->base);
    if (unlikely (status))
		goto FAIL;
	
	cairo_image_surface_t *shrink_image = NULL;
	if(dst->scale != 1.0)
	{
		//we have to shrink image
		//cairo_surface_write_to_png(image_surface, "./test5.png");
		shrink_image = (cairo_image_surface_t *)cairo_surface_create_similar(&image_surface->base,
			cairo_surface_get_content(&image_surface->base),
			dst->scale * orig_width,
			dst->scale * orig_height);
		cairo_t *cr = cairo_create(&shrink_image->base);
		cairo_scale(cr, dst->scale, dst->scale);
		cairo_set_source_surface(cr, &image_surface->base, 0, 0);
		cairo_paint(cr);
		cairo_destroy(cr);
		image_surface = shrink_image;
		width = dst->scale * width;
		height = dst->scale * height;
		//cairo_surface_write_to_png(image_surface, "./test4.png");
	}

	if(ctx->gl_flavor == CAIRO_GL_FLAVOR_DESKTOP)
		glPixelStorei(GL_UNPACK_ROW_LENGTH, image_surface->stride / cpp);
	error = glGetError();
    //if (_cairo_gl_surface_is_texture (dst)) 
	{
		void *data_start = image_surface->data + src_y * image_surface->stride + src_x * cpp;
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
	   		(image_surface->width * cpp < src->stride - 3) ||
			width != image_surface->width)
		{
    		glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
		   	 status = _cairo_gl_surface_extract_image_data (image_surface, 
								src_x, src_y,
							   width, height,
							   &data_start_gles2);
	  		if (unlikely (status))
				goto FAIL;
		}
		else
		{
	    	glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
		}
		error = glGetError();
//        _cairo_gl_context_activate (ctx, CAIRO_GL_TEX_TEMP);

		_cairo_gl_context_activate (ctx, CAIRO_GL_TEX_TEMP);
		error = glGetError();
		glBindTexture (ctx->tex_target, dst->tex);
		error = glGetError();
		glTexParameteri (ctx->tex_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		error = glGetError();
		glTexParameteri (ctx->tex_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		error = glGetError();

		/*char *data = data_start_gles2;
		if(data == NULL)
			data = data_start;
		int scan = 0;
		*/
		//struct timeval start, stop;
		//gettimeofday(&start, NULL);
		/*int i;
		for(i = 0; i < image_surface->height; i++)
		{
			glTexSubImage2D(ctx->tex_target, 0, 0, i, image_surface->width, 1, format, type, data + i * image_surface->stride);
		}*/
		//gettimeofday(&stop, NULL);
		//printf("glTexSubImage2D takes %d usec\n", (stop.tv_usec - start.tv_usec) + 1000000 * (stop.tv_sec - start.tv_sec));
		glTexSubImage2D (ctx->tex_target, 0,
				 dst_x, dst_y, width, height,
				 format, type,
				 data_start_gles2 != NULL ? data_start_gles2 :
						    data_start);
		error = glGetError();
		//printf("upload image error = %x, dest (%d, %d)\n", error, dst_x, dst_y);

		if (data_start_gles2)
	    	free (data_start_gles2);

		//cairo_surface_write_to_png(&dst->base, "/mnt/ums/test.png");
	}

FAIL:
	if(ctx->gl_flavor == CAIRO_GL_FLAVOR_DESKTOP)
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	//if(unlikely (status))
		//glDeleteTextures(1, &dst->tex);

	if(clone)
		cairo_surface_destroy(&clone->base);
	
	if(shrink_image)
		cairo_surface_destroy(&shrink_image->base);

	status = _cairo_gl_context_release(ctx, status);
	//if(dst->tex == 8)
	//cairo_surface_write_to_png(&(dst->base), "/home/me/test3.png");
	return status;
}

static void
_cairo_gl_surface_remove_from_cache(cairo_surface_t *abstract_surface)
{
	cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;
	//if(surface->tex == 6)
	//printf("-------------------removed from cache, tex = %d\n", surface->tex);
	/*if(surface->tex != 0)
	{
		glDeleteTextures(1, &surface->tex);
		surface->tex = 0;
	}*/
	cairo_surface_destroy(&surface->base);
}

void cairo_gl_reset_device(cairo_device_t *device)
{
	if(device == NULL)
		return;
	cairo_gl_context_t *ctx = (cairo_gl_context_t *)device;
	ctx->reset(ctx);
}


cairo_status_t
_cairo_gl_surface_mark_dirty_rectangle(cairo_surface_t *abstract_surface,
	int x, int y, int width, int height)
{
	cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;
	if(surface->data_surface == NULL)
		return CAIRO_STATUS_SUCCESS;

	// we need to upload the portion to 
	cairo_gl_context_t *ctx;
	cairo_status_t status;
    status = _cairo_gl_context_acquire (surface->base.device, &ctx);
    if (unlikely (status))
		return status;
    _cairo_gl_composite_flush (ctx);
    _cairo_gl_context_set_destination (ctx, surface);
	_cairo_gl_surface_upload_image(surface, surface->data_surface,
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
	cairo_gl_surface_t *surface = (cairo_gl_surface_t *)abstract_surface;
	if(!_cairo_surface_is_gl(abstract_surface))
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
    cairo_rectangle_int_t extents;

    void *image_extra = NULL;

	_cairo_gl_surface_acquire_source_image (abstract_surface,
					&(surface->data_surface),
					&image_extra);
	//cairo_surface_write_to_png(surface->data_surface, "/root/test1.png");
	
	return cairo_image_surface_get_data(surface->data_surface);
}

int 
cairo_gl_surface_get_stride(cairo_surface_t *abstract_surface)
{
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
    cairo_rectangle_int_t extents;

    void *image_extra = NULL;

	_cairo_gl_surface_acquire_source_image (abstract_surface,
					&(surface->data_surface),
					&image_extra);
	return cairo_image_surface_get_stride(surface->data_surface);
}

cairo_format_t 
cairo_gl_surface_get_format(cairo_surface_t *abstract_surface)
{
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
    cairo_rectangle_int_t extents;

    void *image_extra = NULL;

	_cairo_gl_surface_acquire_source_image (abstract_surface,
					&(surface->data_surface),
					&image_extra);
	return cairo_image_surface_get_format(surface->data_surface);
}

cairo_surface_t *
cairo_gl_surface_get_image_surface(cairo_surface_t *abstract_surface)
{
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
    cairo_rectangle_int_t extents;

    void *image_extra = NULL;

	_cairo_gl_surface_acquire_source_image (abstract_surface,
					&(surface->data_surface),
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

