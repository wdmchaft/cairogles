/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2012 Samsung Electronics
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
 */

#include "cairo-gl-private.h"
#include "cairo-slope-private.h"

#define SCALE_TOLERANCE 0.0000001
#define POINT_ADJUST 0.5

static inline cairo_bool_t
_add_cap (cairo_gl_hairline_closure_t *hairline,
          double		    slope_dx,
          double		    slope_dy,
          cairo_bool_t 	    lead_cap,
          cairo_point_t	    *outp)
{
    double dx, dy;
    if (lead_cap) {
        if (hairline->cap_style == CAIRO_LINE_CAP_BUTT)
            return FALSE;

        dx = slope_dx * POINT_ADJUST;
        dy = slope_dy * POINT_ADJUST;

        cairo_matrix_transform_distance (hairline->ctm, &dx, &dy);
        outp->x -= _cairo_fixed_from_double (dx);
        outp->y -= _cairo_fixed_from_double (dy);
        return TRUE;
    } else {
        if (hairline->cap_style == CAIRO_LINE_CAP_BUTT)
            return FALSE;

        dx = slope_dx * POINT_ADJUST;
        dy = slope_dy * POINT_ADJUST;
        hairline->line_last_capped = TRUE;
        cairo_matrix_transform_distance (hairline->ctm, &dx, &dy);
        outp->x += _cairo_fixed_from_double (dx);
        outp->y += _cairo_fixed_from_double (dy);
        return TRUE;
    }
}

static inline cairo_bool_t
_compute_normalized_device_slope (double *dx, double *dy,
                                  const cairo_matrix_t *ctm_inverse,
                                  double *mag_out)
{
    double dx0 = *dx, dy0 = *dy;
    double mag;

    cairo_matrix_transform_distance (ctm_inverse, &dx0, &dy0);

    if (dx0 == 0.0 && dy0 == 0.0) {
        if (mag_out)
            *mag_out = 0.0;
        return FALSE;
    }

    if (dx0 == 0.0) {
        *dx = 0.0;
        if (dy0 > 0.0) {
            mag = dy0;
            *dy = 1.0;
        } else {
            mag = -dy0;
            *dy = -1.0;
        }
    } else if (dy0 == 0.0) {
        *dy = 0.0;
        if (dx0 > 0.0) {
            mag = dx0;
            *dx = 1.0;
        } else {
            mag = -dx0;
            *dx = -1.0;
        }
    } else {
        mag = hypot (dx0, dy0);
        *dx = dx0 / mag;
        *dy = dy0 / mag;
    }

    if (mag_out)
        *mag_out = mag;

    return TRUE;
}

/* We implement hairline optimization for stroke. */
cairo_bool_t
_cairo_gl_hairline_style_is_hairline (const cairo_stroke_style_t *style,
                                      const cairo_matrix_t       *ctm)
{
    double x, y;

    cairo_status_t status = _cairo_matrix_compute_basis_scale_factors (ctm, &x, &y, TRUE);

    if (unlikely (status))
        return FALSE;

    x = fabs (x - 1.0);
    y = fabs (y - 1.0);

    return style->line_width == 1.0 &&
        (style->line_join != CAIRO_LINE_JOIN_MITER ||
         style->miter_limit <= 10.0) &&
        (x <= SCALE_TOLERANCE && y <= SCALE_TOLERANCE);
}


static cairo_status_t
_path_add_first_and_last_cap (cairo_gl_hairline_closure_t *hairline)
{
    cairo_point_t p[2];
    cairo_status_t status = CAIRO_STATUS_SUCCESS;
    cairo_bool_t needs_to_cap;

    /* check last point */
    if (hairline->initialized) {
        if (! hairline->line_last_capped) {
            p[0] = hairline->line_last_point;
            p[1] = hairline->line_last_point;
            needs_to_cap = _add_cap (hairline,
                                     hairline->line_last_dx,
                                     hairline->line_last_dy,
                                     FALSE,
                                     &p[1]);
            if (needs_to_cap) {
                status = _cairo_gl_composite_emit_point_as_single_line (hairline->ctx, p);
                if (unlikely(status))
                    return status;
            }
        }
        if (! hairline->stroke_first_capped) {
            p[0] = hairline->stroke_first_point;
            p[1] = hairline->stroke_first_point;
            needs_to_cap = _add_cap (hairline,
                                     hairline->stroke_first_dx,
                                     hairline->stroke_first_dy,
                                     TRUE,
                                     &p[0]);

            if (needs_to_cap) {
                status = _cairo_gl_composite_emit_point_as_single_line (hairline->ctx, p);
                if (unlikely(status))
                    return status;
            }
        }
    }

    return CAIRO_STATUS_SUCCESS;
}

cairo_status_t
_cairo_gl_hairline_move_to (void *closure,
                            const cairo_point_t *point)
{
    cairo_status_t status;

    cairo_gl_hairline_closure_t *hairline = (cairo_gl_hairline_closure_t *)closure;

    hairline->current_point = *point;
    hairline->moved_to_stroke_first_point = FALSE;

    /* check last point */

    if (hairline->initialized) {
        status = _path_add_first_and_last_cap (hairline);
        if (unlikely(status))
            return status;
    }

    hairline->line_last_capped = TRUE;
    hairline->stroke_first_capped = FALSE;
    hairline->stroke_first_point = *point;
    hairline->initialized = TRUE;

    if (hairline->dash.dashed) {
        _cairo_stroker_dash_start (&hairline->dash);
    }

    return CAIRO_STATUS_SUCCESS;
}

cairo_status_t
_cairo_gl_hairline_line_to (void *closure,
                            const cairo_point_t *point)
{
    double slope_dx, slope_dy;
    double mag;
    cairo_gl_hairline_closure_t *hairline = (cairo_gl_hairline_closure_t *)closure;
    cairo_point_t *p1 = &hairline->current_point;
    cairo_slope_t dev_slope;
    cairo_point_t p[2];

    _cairo_slope_init (&dev_slope, p1, point);
    slope_dx = _cairo_fixed_to_double (point->x - p1->x);
    slope_dy = _cairo_fixed_to_double (point->y - p1->y);

    if (! _compute_normalized_device_slope (&slope_dx, &slope_dy,
                                            hairline->ctm_inverse, &mag))
        return CAIRO_STATUS_SUCCESS;

    hairline->line_last_point = *point;
    hairline->line_last_capped = FALSE;
    hairline->line_last_dx = slope_dx;
    hairline->line_last_dy = slope_dy;

    if (! hairline->moved_to_stroke_first_point) {
        hairline->stroke_first_dx = slope_dx;
        hairline->stroke_first_dy = slope_dy;
        hairline->moved_to_stroke_first_point = TRUE;
    }

    p[0] = hairline->current_point;
    p[1] = *point;
    hairline->current_point = *point;

    return _cairo_gl_composite_emit_point_as_single_line (hairline->ctx, p);
}

cairo_status_t
_cairo_gl_hairline_line_to_dashed (void *closure,
                                   const cairo_point_t *point)
{
    cairo_point_t p[2];
    double remain, mag, step_length = 0;
    double slope_dx, slope_dy;
    double dx, dy;
    cairo_line_t segment;
    cairo_gl_hairline_closure_t *hairline = (cairo_gl_hairline_closure_t *)closure;
    cairo_point_t *p1 = &hairline->current_point;
    cairo_slope_t dev_slope;
    cairo_status_t status;
    cairo_bool_t needs_to_cap;

    _cairo_slope_init (&dev_slope, p1, point);
    slope_dx = _cairo_fixed_to_double (point->x - p1->x);
    slope_dy = _cairo_fixed_to_double (point->y - p1->y);

    if (! _compute_normalized_device_slope (&slope_dx, &slope_dy,
                                            hairline->ctm_inverse, &mag))
        return CAIRO_STATUS_SUCCESS;

    remain = mag;
    segment.p1 = *p1;
    while (remain) {
        step_length = MIN (hairline->dash.dash_remain, remain);
        remain -= step_length;

        dx = slope_dx * (mag - remain);
        dy = slope_dy * (mag - remain);
        cairo_matrix_transform_distance (hairline->ctm, &dx, &dy);
        segment.p2.x = _cairo_fixed_from_double (dx) + p1->x;
        segment.p2.y = _cairo_fixed_from_double (dy) + p1->y;


        if (hairline->dash.dash_on) {
            p[0] = segment.p1;
            p[1] = segment.p2;
            /* Check leading cap. */
            if (segment.p1.x == hairline->stroke_first_point.x &&
                segment.p1.y == hairline->stroke_first_point.y) {
                /* We are at the first stroke point, and we have
                   been here, add a leading cap. */
                if (hairline->moved_to_stroke_first_point) {
                    if (hairline->dash.dashes[hairline->dash.dash_index] == hairline->dash.dash_remain)
                        needs_to_cap = _add_cap (hairline,
                                                 slope_dx,
                                                 slope_dy,
                                                 TRUE, &p[0]);
                }
                else {
                    /* We have not been in the first stroke point,
                       this is the first line_to call sinece move_to()
                       save the slope, we need use it later. */
                    hairline->stroke_first_dx = slope_dx;
                    hairline->stroke_first_dy = slope_dy;
                    hairline->moved_to_stroke_first_point = TRUE;
                }
            }
            else if (segment.p1.x == hairline->current_point.x &&
                     segment.p1.y == hairline->current_point.y) {
                /* Start of the line segment, if we have the entire
                   dash length, we are at the begining of a dash,
                   add a leading cap. */
                if (hairline->dash.dashes[hairline->dash.dash_index] == hairline->dash.dash_remain)
                    needs_to_cap = _add_cap (hairline,
                                             slope_dx, slope_dy,
                                             TRUE, &p[0]);
            }
            else
                /* We are in the middle of the line segment, add
                   a leading cap. */
                needs_to_cap = _add_cap (hairline,
                                         slope_dx, slope_dy,
                                         TRUE, &p[0]);

            /* Add trailing cap. We have not exhausted line segment,
               or we have exhausted current dash length, add a
               trailing cap. */
            if (remain ||
                hairline->dash.dash_remain - step_length < CAIRO_FIXED_ERROR_DOUBLE)
                needs_to_cap = _add_cap (hairline,
                                         slope_dx, slope_dy,
                                         FALSE, &p[1]);
            else {
                /* We indicate here that we have not added a trailing
                   cap yet.  If next move is move_to, we will add a
                   trailing cap, otherwise, it will be ignored. */
                hairline->line_last_capped = FALSE;
                hairline->line_last_point = segment.p2;
                hairline->line_last_dx = slope_dx;
                hairline->line_last_dy = slope_dy;
            }

            status = _cairo_gl_composite_emit_point_as_single_line (hairline->ctx, p);
            if (unlikely (status))
                return status;
        }

        _cairo_stroker_dash_step (&hairline->dash, step_length);
        segment.p1 = segment.p2;
    }

    hairline->current_point = *point;
    return CAIRO_STATUS_SUCCESS;
}

cairo_status_t
_cairo_gl_hairline_curve_to (void *closure,
                             const cairo_point_t *p0,
                             const cairo_point_t *p1,
                             const cairo_point_t *p2)
{
    cairo_gl_hairline_closure_t *hairline = (cairo_gl_hairline_closure_t *)closure;
    cairo_spline_t spline;
    cairo_path_fixed_line_to_func_t *line_to;

    line_to = hairline->dash.dashed ?
        _cairo_gl_hairline_line_to_dashed :
        _cairo_gl_hairline_line_to;

    if (! _cairo_spline_init (&spline,
                              (cairo_spline_add_point_func_t)line_to,
                              closure,
                              &hairline->current_point, p0, p1, p2))
        return _cairo_gl_hairline_line_to (closure, p2);

    return _cairo_spline_decompose (&spline, hairline->tolerance);

}

cairo_status_t
_cairo_gl_hairline_close_path (void *closure)
{
    cairo_gl_hairline_closure_t *hairline = (cairo_gl_hairline_closure_t *)closure;

    hairline->line_last_capped = TRUE;
    hairline->stroke_first_capped = TRUE;

    if (hairline->dash.dashed)
        return _cairo_gl_hairline_line_to_dashed (closure,
                                                  &hairline->stroke_first_point);
    return _cairo_gl_hairline_line_to (closure, &hairline->stroke_first_point);
}

cairo_status_t
_cairo_gl_path_fixed_stroke_to_hairline (const cairo_path_fixed_t *path,
                                         cairo_gl_hairline_closure_t *closure,
                                         const cairo_stroke_style_t *style,
                                         const cairo_matrix_t *ctm,
                                         const cairo_matrix_t *ctm_inverse,
                                         cairo_path_fixed_move_to_func_t *move_to,
                                         cairo_path_fixed_line_to_func_t *line_to,
                                         cairo_path_fixed_curve_to_func_t *curve_to,
                                         cairo_path_fixed_close_path_func_t *close_path)
{
    cairo_status_t status;

    _cairo_stroker_dash_init (&closure->dash, style);
    closure->ctm = (cairo_matrix_t *)ctm;
    closure->ctm_inverse = (cairo_matrix_t *)ctm_inverse;
    closure->cap_style = style->line_cap;
    closure->initialized = FALSE;

    status = _cairo_path_fixed_interpret (path,
                                          move_to,
                                          line_to,
                                          curve_to,
                                          close_path,
                                          (void *) closure);
    if (unlikely (status))
        return status;

    return _path_add_first_and_last_cap (closure);
}
