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
#include "cairo-tristrip-private.h"

#define INITIAL_INDICES_CAPACITY 64

cairo_status_t
_cairo_gl_tristrip_indices_init (cairo_gl_tristrip_indices_t *indices)
{
    indices->setup = NULL;

    _cairo_array_init (&indices->mask_texture_coords, sizeof(float));

    _cairo_tristrip_init (&indices->tristrip);
    return CAIRO_STATUS_SUCCESS;
}

void
_cairo_gl_tristrip_indices_destroy (cairo_gl_tristrip_indices_t *indices)
{
    _cairo_tristrip_fini (&indices->tristrip);
    _cairo_array_fini (&indices->mask_texture_coords);
}

cairo_status_t
_cairo_gl_tristrip_indices_add_quad (cairo_gl_tristrip_indices_t *indices,
				     const cairo_point_t	  quad[4])
{
    cairo_tristrip_t *tristrip = &indices->tristrip;

    /* Flush everything if the mesh is very complicated. */
    cairo_gl_composite_t *setup = indices->setup;
    if (tristrip->num_points > MAX_INDEX && setup != NULL) {
	cairo_status_t status = _cairo_gl_fill(indices);
	_cairo_gl_tristrip_indices_destroy (indices);
	status = _cairo_gl_tristrip_indices_init (indices);
	indices->setup = setup;
    }

    _cairo_tristrip_move_to (tristrip, &quad[0]);

    _cairo_tristrip_add_point (tristrip, &quad[0]);
    _cairo_tristrip_add_point (tristrip, &quad[1]);

    /* Cairo stores quad vertices in counter-clockwise order, but we need to
       emit them from top to bottom in the triangle strip, so we need to reverse
       the order of the last two vertices. */
    _cairo_tristrip_add_point (tristrip, &quad[3]);
    _cairo_tristrip_add_point (tristrip, &quad[2]);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_bool_t
_points_equal (cairo_point_t *a, cairo_point_t *b)
{
    return a->x == b->x && a->y == b->y;
}

void
_cairo_tristrip_get_gl_vertices_and_indices (cairo_tristrip_t *tristrip,
					     unsigned short	      **indices_out,
					     int	      *num_indices,
					     GLfloat	      **vertices_out,
					     int	      *num_vertices)
{
    int i;
    int num_points = tristrip->num_points;
    int last_point_index = -1;
    cairo_point_t *last_point = 0;

    GLfloat *vertices = _cairo_malloc_abc (num_points, sizeof (GLfloat), 2);
    unsigned short *indices = _cairo_malloc_ab (num_points, sizeof (unsigned short));

    *vertices_out = vertices;
    *indices_out = indices;

    for (i = 0; i < num_points; i++) {
        cairo_point_t *current_point = &tristrip->points[i];
	if (last_point == NULL || !_points_equal (current_point, last_point)) {
	    last_point = current_point;
	    last_point_index++;
	    vertices[last_point_index * 2] = _cairo_fixed_to_double (last_point->x);
	    vertices[(last_point_index * 2) + 1] = _cairo_fixed_to_double (last_point->y);
	}
	indices[i] = last_point_index;
    }
    *num_indices = num_points;
    *num_vertices = last_point_index + 1;
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

void
_cairo_gl_tristrip_indices_add_mask_texture_coord (cairo_gl_tristrip_indices_t	*indices,
						   float			 x,
						   float			 y)
{
    /* We ignore the status here, because eventually we are going to emit these vertices
      directly to a GL bound buffer. */
    cairo_int_status_t status;
    status = _cairo_array_append (&indices->mask_texture_coords, &x);
    status =_cairo_array_append (&indices->mask_texture_coords, &y);
}
