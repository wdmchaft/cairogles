/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2011 Samsung
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
 *	Henry Song <hsong@sisa.samsung.com>
 *	Martin Robinson <mrobinson@igalia.com>
 */

#include "cairoint.h"

#include "cairo-gl-private.h"
#include "cairo-gl-tristrip-indices-private.h"

#define INITIAL_INDICES_CAPACITY 64

cairo_status_t
_cairo_gl_tristrip_indices_init (cairo_gl_tristrip_indices_t *indices)
{
    indices->vertices = (float *)malloc(sizeof(float) * INITIAL_INDICES_CAPACITY * 2);
    indices->mask_vertices = (float *)malloc(sizeof(float) * INITIAL_INDICES_CAPACITY * 2);
    indices->indices = (int *)malloc(sizeof(int) * INITIAL_INDICES_CAPACITY);
    indices->capacity = INITIAL_INDICES_CAPACITY;
    indices->num_indices = 0;
    indices->num_vertices = 0;
    indices->setup = NULL;
    indices->has_mask_vertices = FALSE;
    return CAIRO_STATUS_SUCCESS;
}

cairo_status_t
_cairo_gl_tristrip_indices_increase_capacity (cairo_gl_tristrip_indices_t *indices)
{
    int new_capacity = indices->capacity * 2;
    float *new_vertices = (float *)malloc(sizeof(float) * new_capacity * 2);
    float *new_mask_vertices = (float *)malloc(sizeof(float) * new_capacity * 2);
    int *new_indices = (int *)malloc(sizeof(int) * new_capacity);

    memcpy(new_vertices, indices->vertices, sizeof(float) * indices->num_vertices * 2);
    memcpy(new_mask_vertices, indices->mask_vertices, sizeof(float) * indices->num_vertices * 2);
    memcpy(new_indices, indices->indices, sizeof(int) * indices->num_indices);
    free(indices->vertices);
    free(indices->indices);
    free(indices->mask_vertices);
    indices->vertices = new_vertices;
    indices->mask_vertices = new_mask_vertices;
    indices->indices = new_indices;
    indices->capacity = new_capacity;
    return CAIRO_STATUS_SUCCESS;
}

void
_cairo_gl_tristrip_indices_destroy (cairo_gl_tristrip_indices_t *indices)
{
    free(indices->vertices);
    free(indices->mask_vertices);
    free(indices->indices);
    indices->has_mask_vertices = FALSE;
}

cairo_status_t
_cairo_gl_tristrip_indices_add_quad (cairo_gl_tristrip_indices_t *indices,
				     const cairo_point_t	  quad[4])
{
     int last_index = 0;
     int start_index = 0;
     int i;
     int num_indices,num_vertices;
     cairo_status_t status;

     cairo_gl_composite_t *setup = indices->setup;

    // first off, we need to flush if max
    if(indices->num_indices > MAX_INDEX) {
	if(indices->setup != NULL) {
	    status = _cairo_gl_fill(indices->setup,
				    indices->num_vertices,
				    indices->vertices,
				    NULL,
				    indices->num_indices,
				    indices->indices,
				    indices->setup->ctx);
	    // cleanup
	    _cairo_gl_tristrip_indices_destroy (indices);
	    status = _cairo_gl_tristrip_indices_init (indices);
	    indices->setup = setup;
	}
    }

    // we add a triangle to strip, we add 4 vertices and 6 indices;
    while (indices->num_indices + 5 >= indices->capacity ||
	  indices->num_vertices + 4 >= indices->capacity) {
	// we need to increase
	status = _cairo_gl_tristrip_indices_increase_capacity (indices);
    }
    num_indices = indices->num_indices;
    num_vertices = indices->num_vertices;

    if(num_indices != 0) {
	// we are not the first
	last_index = indices->indices[num_indices-1];
	start_index = last_index + 1;
	indices->indices[num_indices] = last_index;
	indices->indices[num_indices+1] = start_index;
	indices->num_indices += 2;
    }

    num_indices = indices->num_indices;
    indices->has_mask_vertices = FALSE;
    for(i = 0; i < 2; i++) {
	indices->indices[num_indices+i] = start_index + i;
	indices->vertices[num_vertices*2+i*2] = _cairo_fixed_to_double(quad[i].x);
	indices->vertices[num_vertices*2+i*2+1] = _cairo_fixed_to_double(quad[i].y);
    }

    indices->num_indices += 2;
    indices->num_vertices += 2;
    num_indices = indices->num_indices;
    num_vertices = indices->num_vertices;

    // Cairo stores quad vertices in counter-clockwise, but we need to emit them
    // from top to bottom in the triangle strip, so we need to reverse the order
    // of the last two vertices.
    start_index = indices->indices[num_indices-1];
    indices->indices[num_indices] = start_index + 1;
    indices->indices[num_indices+1] = start_index + 2;
    indices->vertices[num_vertices*2] = _cairo_fixed_to_double(quad[3].x);
    indices->vertices[num_vertices*2+1] = _cairo_fixed_to_double(quad[3].y);
    indices->vertices[num_vertices*2+2] = _cairo_fixed_to_double(quad[2].x);
    indices->vertices[num_vertices*2+3] = _cairo_fixed_to_double(quad[2].y);
    indices->num_indices += 2;
    indices->num_vertices += 2;

    return CAIRO_STATUS_SUCCESS;
}

cairo_status_t
_cairo_gl_tristrip_indices_add_traps (cairo_gl_tristrip_indices_t *indices,
				      cairo_traps_t		  *traps)
{
    cairo_status_t status;
    int i;

    for (i = 0; i < traps->num_traps; i++) {
	cairo_point_t quad[4];
	cairo_trapezoid_t *trap = traps->traps + i;

	quad[0].x = _cairo_edge_compute_intersection_x_for_y (&trap->left.p1, &trap->left.p2, trap->top);
	quad[0].y = trap->top;
	quad[1].x = _cairo_edge_compute_intersection_x_for_y (&trap->left.p1, &trap->left.p2, trap->bottom);
	quad[1].y = trap->bottom;
	quad[2].x = _cairo_edge_compute_intersection_x_for_y (&trap->right.p1, &trap->right.p2, trap->bottom);
	quad[2].y = trap->bottom;
	quad[3].x = _cairo_edge_compute_intersection_x_for_y (&trap->right.p1, &trap->right.p2, trap->top);
	quad[3].y = trap->top;

	status = _cairo_gl_tristrip_indices_add_quad (indices, quad);
	if (unlikely (status))
	    return status;
    }
    return CAIRO_STATUS_SUCCESS;
}

cairo_status_t
_cairo_gl_tristrip_indices_add_boxes (cairo_gl_tristrip_indices_t *indices,
				      int			  num_boxes,
				      cairo_box_t		  *boxes)
{
    int i;
    cairo_status_t status;

    for (i = num_boxes - 1; i >= 0; i--) {
	cairo_point_t quad_vertices[4];
	quad_vertices[0].x = boxes[i].p1.x;
	quad_vertices[0].y = boxes[i].p1.y;
	quad_vertices[1].x = boxes[i].p1.x;
	quad_vertices[1].y = boxes[i].p2.y;
	quad_vertices[2].x = boxes[i].p2.x;
	quad_vertices[2].y = boxes[i].p2.y;
	quad_vertices[3].x = boxes[i].p2.x;
	quad_vertices[3].y = boxes[i].p1.y;
	status = _cairo_gl_tristrip_indices_add_quad (indices, quad_vertices);
	if (unlikely (status))
	    return status;
    }
    return CAIRO_STATUS_SUCCESS;
}

cairo_status_t
_cairo_gl_tristrip_indices_add_path (cairo_gl_tristrip_indices_t *indices,
				     cairo_clip_path_t		 *clip_path)
{
    cairo_traps_t traps;
    cairo_status_t status = CAIRO_STATUS_SUCCESS;

    _cairo_traps_init (&traps);
    status = _cairo_path_fixed_fill_to_traps (&(clip_path->path),
					      clip_path->fill_rule,
					      0.1,
					      &traps);
    if (unlikely (status))
	goto CLEANUP;
    if (traps.num_traps == 0) {
	status = CAIRO_STATUS_CLIP_NOT_REPRESENTABLE;
    }

    status = _cairo_gl_tristrip_indices_add_traps (indices, &traps);

CLEANUP:
    _cairo_traps_fini (&traps);
    return status;
}
