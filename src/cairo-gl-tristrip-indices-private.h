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

#include <cairo-gl-private.h>

#define MAX_INDEX 10000

cairo_private cairo_status_t
_cairo_gl_tristrip_indices_init (cairo_gl_tristrip_indices_t *indices);

cairo_private void
_cairo_gl_tristrip_indices_destroy (cairo_gl_tristrip_indices_t *indices);

cairo_private cairo_status_t
_cairo_gl_tristrip_indices_append_vertex_indices (cairo_gl_tristrip_indices_t	*tristrip_indices,
						  unsigned short		 number_of_new_indices);

cairo_private cairo_status_t
_cairo_gl_tristrip_add_vertex (cairo_gl_tristrip_indices_t	*indices,
			       const cairo_point_t		*vertex);

cairo_private cairo_status_t
_cairo_gl_tristrip_indices_add_quad (cairo_gl_tristrip_indices_t *indices,
				     const cairo_point_t	 quad[4]);

cairo_private cairo_status_t
_cairo_gl_tristrip_indices_add_path (cairo_gl_tristrip_indices_t *indices,
				     cairo_clip_path_t		 *clip_path);

cairo_private cairo_status_t
_cairo_gl_tristrip_indices_add_boxes (cairo_gl_tristrip_indices_t *indices,
				      int			  num_boxes,
				      cairo_box_t		  *boxes);

cairo_private cairo_status_t
_cairo_gl_tristrip_indices_add_traps (cairo_gl_tristrip_indices_t *indices,
				      cairo_traps_t		  *traps);

cairo_private cairo_status_t
_cairo_gl_tristrip_indices_add_traps_with_mask (cairo_gl_tristrip_indices_t *indices,
				      cairo_traps_t		  *traps,
                      cairo_matrix_t *matrix,
                      cairo_gl_surface_t     *mask);

cairo_private void
_cairo_gl_tristrip_indices_add_mask_texture_coord (cairo_gl_tristrip_indices_t	*indices,
						   float			 x,
						   float			 y);
