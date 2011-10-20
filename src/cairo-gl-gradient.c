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

static void compute_distance(double x1, double y1, double x2, double y2, 
	double *x, double *y)
{
	*x = x2 - x1;
	*y = y2 - y1;
}

static void compute_radius_difference(double r1, double r2, double *r)
{
	*r = r2 - r1;
}

static void compute_A(double x, double y, double r, double x1, double x2, 
	double y1, double y2, double *a)
{
	//*a = x * x + y * y + (x2 * x2 - x1 * x1) + (y2 * y2 - y1 * y1);
	*a = 2.0 * r * r - x * x - y * y - (x2 * x2 - x1 * x1) - (y2 * y2 - y1 * y1);
}

static void compute_abc (double A, double X, double Y, double R, double x2, double y2,
	double *a, double *b, double *c)
{
	//double m = A - 2 * X * x2;
	//*a =  1 + Y * Y / (X * X);
	//*b = -(2 * y2 + m * Y / (X * X));
	//*c = m * m / (4 * X * X) - R * R + y2 * y2;
	*a =  1 + Y * Y / (X * X);
	*b = -2.0 * y2 + 2.0 * Y / X * (A / (2.0 * X) + x2);
	*c = y2 * y2 + (A / (2.0 * X) + x2) * (A / (2.0 * X) + x2) - R * R;
}

static void compute_y(double a, double b, double c, double *y1, double *y2)
{
	double x = sqrt(b * b - 4 * a * c);
	*y1 = (-b + x) / (2 * a);
	*y2 = (-b - x) / (2 * a);
}

static void compute_x(double X, double Y, double A, double cx1, double cy1,
	double cx2, double cy2, double y1, double y2,
	double *x1, double *x2)
{
	*x1 = (-A - 2 * Y * y1) / (2 * X);
	*x2 = (-A - 2 * Y * y2) / (2 * X);
}

static void compute_xy (double cx1, double cy1, double cx2, double cy2, double r1,
	double r2, double *x1, double *y1, double *x2, double *y2)
{
	double X, Y, R, A;
	double a, b, c;
	double dx, dy;
	// compute center distance
	compute_distance(cx1, cy1, cx2, cy2, &X, &Y); 
	// compute radius difference
	compute_radius_difference(r1, r2, &R);
	if(X == 0 && Y == 0)
	{
		*x1 = cx1;
		*x2 = cx2;
		*y1 = cy1;
		*y2 = cy2;
		return;
	}
	if(r1 == r2)
	{
		*x1 = *x2 = cx2;
		*y1 = *y2 = cy2;
		return;
	}
	if(X == 0)
	{
		*y1 = *y2 = (Y * Y - 2 * R * R + cx2 * cx2 - cx1 * cx1 + cy2 * cy2 - cy1 * cy1) / (2 * Y);
		dy = *y1 - cy2;
		dx = sqrt(R * R - dy * dy);
		*x1 = dx + cx2;
		*x2 = -dx + cx2;
	}
	else if(Y == 0)
	{
		*x1 = *x2 = (X * X - 2 * R * R + cx2 * cx2 - cx1 * cx1 + cy2 * cy2 - cy1 * cy1) / (2 * X);
		dx = *x1 - cx2;
		dy = sqrt(R * R - dx * dx);
		*y1 = dy + cy2;
		*y2 = -dy + cy2;
	}
	else
	{
		// compute A
		compute_A(X, Y, R, cx1, cx2, cy1, cy2, &A);
		// compute a, b, c
		compute_abc(A, X, Y, R, cx2, cy2, &a, &b, &c);
		// compute final y
		compute_y(a, b, c, y1, y2);
		// compute final x
		compute_x(X, Y, A, cx1, cy1, cx2, cy2, *y1, *y2, x1, x2);
	}
}

static void compute_tangent_2(double cx1, double cy1, double cx2, double cy2, 
	double r1, double r2, double x1, double y1, double x2, double y2, 
	double *tx1, double *ty1, double *tx2, double *ty2)
{
	double X, Y, R, a, b, c, s;
	if(r1 != r2)
	{
		*tx1 = (r2 / (r2 - r1)) * (x1 - cx2) + cx2;
		*ty1 = (r2 / (r2 - r1)) * (y1 - cy2) + cy2;
		*tx2 = (r2 / (r2 - r1)) * (x2 - cx2) + cx2;
		*ty2 = (r2 / (r2 - r1)) * (y2 - cy2) + cy2;
	}
	else
	{
		X = cx2 - cx1;
		Y = cy2 - cy1;
		R = X * X + Y * Y - cx1 * cx1 + cx2 * cx2 - cy1 * cy1 + cy2 * cy2;
		if(X != 0)
		{
			a = 4 * (Y * Y + X * X);
			b = -4 * (R - 2 * cx2 * X) * Y - 8 * X * X * cy2;
			c =  (R - 2 * cx2 * X) * (R - 2 * cx2 * X) + 4 * (X * X * cy2 * cy2 - X * X * r2 * r2);
			s = sqrt(b * b - 4 * a * c);
			*ty1 = (-b + s) / (2 * a);
			*ty2 = (-b - s) / (2 * a);
			*tx1 = (R - 2 * Y * *ty1) / (2 * X);
			*tx2 = (R - 2 * Y * *ty2) / (2 * X);
		}
		else
		{
			a = 4 * (Y * Y + X * X);
			b = -4 * (R - 2 * cy2 * Y) * X - 8 * Y * Y * cx2;
			c =  (R - 2 * cy2 * Y) * (R - 2 * cy2 * Y) + 4 * (Y * Y * cx2 * cx2 - Y * Y * r2 * r2);
			s = sqrt(b * b - 4 * a * c);
			*tx1 = (-b + s) / (2 * a);
			*tx2 = (-b - s) / (2 * a);
			*ty1 = (R - 2 * X * *tx1) / (2 * Y);
			*ty2 = (R - 2 * X * *tx2) / (2 * Y);
		}
			
	}


		
}

static void compute_tangent_1(double cx1, double cy1, double cx2, double cy2,
	double r1, double r2, double x1, double y1, double x2, double y2,
	double *x, double *y)
{
	double X1, Y1, X2, Y2;
	
	compute_distance(cx2, cy2, x2, y2, &X1, &Y1);

	compute_distance(cx1, cy1, x1, y1, &X2, &Y2);

	if(Y2 == 0)
	{
		*y = y2;
		*x = cx1;
	}
	else if(Y1 == 0)
	{
		*x = x2;
		*y = cy1;
	}
	else
	{
		*y = (X1 / Y1 * cy1 - X2 / Y2 * y2 + x2 - cx1) / (X1 / Y1 - X2 / Y2);
		*x = X2 / Y2 * *y - X2 / Y2 * y2 + x2;
	}

}

static void compute_end(double cx1, double cy1, double cx2, double cy2,
	double x1, double y1, double x2, double y2, double *x, double *y)
{
	double a1, a2, b1, b2;
	double dcx = cx2 - cx1;
	double dcy = cy2 - cy1;
	double dx = x2 - x1;
	double dy = y2 - y1;

	if(dx == 0)
	{
		*x = x1;
		a2 = dcy / dcx;
		b2 = cy2 - a2 * cx2;
		*y = (*x) * a2 + b2;
	}
	else if(dcx == 0)
	{
		a1 = dy / dx;
		b1 = y2 - a1 * x2;
		*x = cx1;
		*y = (*x) * a1 + b1;
	}
	else if(dy == 0)
	{
		*y = y1;
		a2 = dcy / dcy;
		b2 = cx2 - a2 * cy2;
		*x = (*y) * a2 + b2;
	}
	else if(dcy == 0)
	{
		*y = cy1;
		a1 = dx / dy;
		b1 = x2 - a1 * y2;
		*x = (*y) * a1 + b1;
	}
	else
	{
		a1 = dy / dx;
		b1 = y2 - a1 * x2;
		a2 = dcy / dcx;
		b2 = cy2 - a2 * cx2;

		*x = (b1 - b2) / (a2 - a1);
		
		a1 = dx / dy;
		b1 = x2 - a1 * y2;
		a2 = dcx / dcy;
		b2 = cx2 - a2 * cy2;
		*y = (b1 - b2) / (a2 - a1);
	}
}

static void compute_end_equal(double cx1, double cy1, double cx2, double cy2,
	double r, double tx1, double ty1, double tx2, double ty2,
	double *x1, double *y1, double *x2, double *y2)
{
	double dcx = cx2 - cx1;
	double dcy = cy2 - cy1;
	double dx, dy;
	double angle;

	if(dcx == 0)
	{
		*x1 = tx1;
		*x2 = tx2;
		if(dcy > 0)
		{
			*y1 = ty1 - r;
			*y2 = ty2 - r;
		}
		else
		{
			*y1 = ty1 + r;
			*y2 = ty2 + r;
		}
		return;
	}
	else if(dcy == 0)
	{
		*y1 = ty1;
		*y2 = ty2;
		if(dcx > 0)
		{
			*x1 = tx1 - r;
			*x2 = tx2 - r;
		}
		else
		{
			*x1 = tx1 + r;
			*x2 = tx2 + r;
		}
		return;
	}
	angle = atan(dcy/dcx);
	if(angle < 0)
		angle = -angle;
	dx = cos(angle) * r;
	dy = sin(angle) * r;

	if(dcx > 0)
	{
		*x1 = tx1 - dx;
		*x2 = tx2 - dx;
	}
	else
	{
		*x1 = tx1 + dx;
		*x2 = tx2 + dx;
	}
	if(dcy > 0)
	{
		*y1 = ty1 - dy;
		*y2 = ty2 - dy;
	}
	else
	{
		*y1 = ty1 + dy;
		*y2 = ty2 + dy;
	}
}

static void matrix_rotate(cairo_matrix_t *matrix, double radian)
{
	double s, c;
	s = sin(radian);
	c = cos(radian);
	matrix->xx = c; matrix->yx = -s;
	matrix->xy = s; matrix->yy = c;
}

static void matrix_transform_point(cairo_matrix_t *matrix, double *x, double *y)
{
	double new_x = *x + matrix->x0;
	double new_y = *y + matrix->y0;
	*x = matrix->xx * new_x + matrix->xy * new_y;
	*y = matrix->yx * new_x + matrix->yy * new_y;
}

/*int main(int argc, char **argv)
{
	double cx1;
	double cy1;
	double cx2;
	double cy2;
	double r1;
	double r2;
	double x1, y1, x2, y2;
	double tx1, tx2, tx3, tx4, ty1, ty2, ty3, ty4;
	double ex, ey;
	if(argc != 7)
	{
		printf("usage %s cx1, cy1, r1, cx2, cy2, r2\n", argv[0]);
		return 1;
	}
	cx1 = atof(argv[1]);
	cy1 = atof(argv[2]);
	r1 = atof(argv[3]);
	cx2 = atof(argv[4]);
	cy2 = atof(argv[5]);
	r2 = atof(argv[6]);

	compute_xy(cx1, cy1, cx2, cy2, r1, r2, &x1, &y1, &x2, &y2);
	printf("(%0.2f, %0.2f), (%0.2f, %0.2f)\n", x1, y1, x2, y2);
	compute_tangent_2(cx1, cy1, cx2, cy2, r1, r2, x1, y1, x2, y2, 
		&tx3, &ty3, &tx4, &ty4);
	printf("tangent point (%0.2f, %0.2f), (%0.2f, %0.2f)\n", tx3, ty3, tx4, ty4);
	compute_tangent_1(cx1, cy1, cx2, cy2, r1, r2, x1, y1, tx3, ty3, &tx1, &ty1); 
	compute_tangent_1(cx1, cy1, cx2, cy2, r1, r2, x2, y2, tx4, ty4, &tx2, &ty2); 
	printf("tangent point (%0.2f, %0.2f), (%0.2f, %0.2f)\n", tx1, ty1, tx2, ty2);
	if(r1 != r2)
	{
		compute_end(cx1, cy1, cx2, cy2, tx1, ty1, tx3, ty3, &ex, &ey);
		printf("end point (%0.2f, %0.2f)\n", ex, ey);
	}

	double angle = (ty2 - ty4)/(tx2 - tx4);
	cairo_matrix_t m;
	cairo_matrix_init_identity(&m);
	cairo_matrix_translate(&m, -ex, -ey);
	cairo_matrix_rotate(&m, 0.5);
	double ttx1, tty1;
	ttx1 = tx1;
	tty1 = ty1;
	cairo_matrix_transform_point(&m, &ttx1, &tty1);
	printf("angle = %0.2f\n", angle); 
}
*/
			
// Henry Song
// we want to get start and end stops location, number of stops - offsets
// colors for seach section of rbga, number of offsets
cairo_status_t
_cairo_gl_gradient_digest_linear_gradient(const cairo_gradient_pattern_t *pattern, float surface_height, float *stops, float *colors, float *offsets, float *total_dist, int *nstops, float *delta, cairo_bool_t upsidedown)
{
	double a, b, c, d;
	unsigned int i;
	cairo_status_t status;
        cairo_matrix_t matrix;
        cairo_linear_pattern_t *linear = NULL;
    float precision_scale = 100.0f;
	if(pattern->n_stops > CAIRO_GL_MAX_STOPS_SIZE)
		return CAIRO_INT_STATUS_UNSUPPORTED;

	// TODO: we take care of CAIRO_EXTEND_NONE later
	// get matrix
	memcpy(&matrix, &(pattern->base.matrix), sizeof(double)*6);
	status = cairo_matrix_invert(&matrix);
	// get transformed points
	linear = (cairo_linear_pattern_t *)pattern;

	a = linear->pd1.x;
	b = linear->pd1.y;
	c = linear->pd2.x;
	d = linear->pd2.y;
	cairo_matrix_transform_point(&matrix, &a, &b);
	cairo_matrix_transform_point(&matrix, &c, &d);

	stops[0] = a / precision_scale;
	if(upsidedown)
		stops[1] = (surface_height - b) / precision_scale;
	else
		stops[1] = b / precision_scale;
	stops[2] = c / precision_scale;
	if(upsidedown)
		stops[3] = (surface_height - d) / precision_scale;
	else
	stops[3] = d / precision_scale;
	
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
_cairo_gl_gradient_digest_radial_gradient(const cairo_gradient_pattern_t *pattern, float surface_height, float *scales, float *colors, float *offsets, int *nstops, float *circle_1, float *circle_2, cairo_bool_t *circle_in_circle, cairo_matrix_t *matrix_1, cairo_matrix_t *matrix_2, float *tangents, float *end_point, float *tangents_end, int *moved_center, cairo_bool_t upsidedown)
{
	cairo_status_t status;
	double a, b, c, d;
	double dx1, dy1, dx2, dy2;
  	cairo_matrix_t matrix;
	double x1, x2, y1, y2;
	double tangent_1_x, tangent_1_y;
	double tangent_2_x, tangent_2_y;
	double tangent_3_x, tangent_3_y;
	double tangent_4_x, tangent_4_y;
	double end_x, end_y;
	double dx, dy;
	double angle;
	double tangent_end_1_x, tangent_end_1_y;
	double tangent_end_2_x, tangent_end_2_y;
    float precision_scale = 100.0f;
	//int n;
	cairo_matrix_t temp_matrix;
	double mx1, my1, mx2, my2;
	unsigned int i;

	cairo_bool_t parallel = TRUE;
    cairo_radial_pattern_t *radial = NULL;
	*moved_center = 0;


	if(pattern->n_stops > CAIRO_GL_MAX_STOPS_SIZE)
		return CAIRO_INT_STATUS_UNSUPPORTED;

	// TODO: we take care of CAIRO_EXTEND_NONE later
	// get matrix
	memcpy(&matrix, &(pattern->base.matrix), sizeof(double)*6);
	status = cairo_matrix_invert(&matrix);
	// get transformed points
	radial = (cairo_radial_pattern_t *)pattern;

	a = radial->cd1.center.x;
	b = radial->cd1.center.y;
	c = radial->cd2.center.x;
	d = radial->cd2.center.y;
	cairo_matrix_transform_point(&matrix, &a, &b);
	cairo_matrix_transform_point(&matrix, &c, &d);
	// we have to transform radius
	dx2= 100.0; 
	dy2 = 0.0;
	dx1 = 0.0;
	dy1 = 0.0;
	cairo_matrix_transform_point(&matrix, &dx1, &dy1);
	cairo_matrix_transform_point(&matrix, &dx2, &dy2);

	scales[0] = 100.0 / sqrt((dx1 - dx2)*(dx1 - dx2) + (dy1 - dy2)*(dy1 - dy2));
	dx2 = 0.0;
	dy2 = 100.0;
	cairo_matrix_transform_point(&matrix, &dx2, &dy2);
	scales[1] = 100.0 / sqrt((dx1 - dx2)*(dx1 - dx2) + (dy1 - dy2)*(dy1 - dy2));
	
	circle_1[0] = a / precision_scale;
	if(upsidedown)
		circle_1[1] = (surface_height - b) / precision_scale;
	else
		circle_1[1] = b / precision_scale;
	circle_1[2] = radial->cd1.radius / precision_scale;
	
	circle_2[0] = c / precision_scale;
	if(upsidedown)
		circle_2[1] = (surface_height - d) / precision_scale;
	else
		circle_2[1] = d / precision_scale;
	circle_2[2] = radial->cd2.radius / precision_scale;

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

	// compute whether circle in circle
	a = radial->cd1.center.x - radial->cd2.center.x;
	b = radial->cd1.center.y - radial->cd2.center.y;
	c = sqrt(a * a + b * b);
	if(radial->cd1.radius >= c + radial->cd2.radius)
	{
		*circle_in_circle = TRUE;
		if(radial->cd1.radius == c + radial->cd2.radius)
		{
			*moved_center = 1;
			if(circle_1[0] > circle_2[0])
				end_point[0] = circle_2[0] + 0.01 * circle_1[2];
			else
				end_point[0] = circle_2[0] - 0.01 * circle_1[2];
			if(circle_1[1] > circle_2[1])
				end_point[1] = circle_2[1] + 0.01 * circle_1[2];
			else
				end_point[1] = circle_2[1] - 0.01 * circle_1[2];
		}
	}
	else if(radial->cd2.radius >= c + radial->cd1.radius)
	{
		*circle_in_circle = TRUE;
		if(radial->cd2.radius == c + radial->cd1.radius)
		{
			*moved_center = 1;
			if(circle_2[0] > circle_1[0])
				end_point[0] = circle_1[0] + 0.01 * circle_2[2];
			else
				end_point[0] = circle_1[0] - 0.01 * circle_2[2];
			if(circle_2[1] > circle_1[0])
				end_point[1] = circle_1[1] + 0.01 * circle_2[2];
			else
				end_point[1] = circle_1[1] - 0.01 * circle_2[2];
		}
	}
	if(*circle_in_circle == TRUE && *moved_center == 1)
	{
		if(radial->cd2.radius >= radial->cd1.radius)
		{
			dx = circle_2[0] - circle_1[0];
			dy = circle_2[1] - circle_1[1];
		}
		else
		{
			dx = circle_1[0] - circle_2[0];
			dy = circle_1[1] - circle_2[1];
		}
		if(dx == 0.0)
		{
			if(dy > 0.0)
				cairo_matrix_init_translate(matrix_1, -circle_2[0], -(circle_2[1] - radial->cd2.radius / (scales[1] * precision_scale)));
			else
				cairo_matrix_init_translate(matrix_1, -circle_2[0], -(circle_2[1] + radial->cd2.radius / (scales[1] * precision_scale)));
		}
		else if(dy == 0.0)
		{
			if(dx > 0.0)
				cairo_matrix_init_translate(matrix_1, -(circle_2[0] - radial->cd2.radius / (scales[0] * precision_scale)), -circle_2[1]);
			else
				cairo_matrix_init_translate(matrix_1, -(circle_2[0] + radial->cd2.radius / (scales[0] * precision_scale)), -circle_2[1]);
		}
		else
		{
			if(dx > 0.0 && dy > 0.0)
				cairo_matrix_init_translate(matrix_1, -(circle_2[0] - radial->cd2.radius / (scales[0] * precision_scale)), -(circle_2[1] + radial->cd2.radius / (scales[1] * precision_scale)));
			else if(dx > 0.0 && dy < 0.0)
				cairo_matrix_init_translate(matrix_1, -(circle_2[0] - radial->cd2.radius / (scales[0] * precision_scale)), -(circle_2[1] - radial->cd2.radius / (scales[1] * precision_scale)));
			else if(dx < 0.0 && dy > 0.0)
			{
				if(upsidedown)
					cairo_matrix_init_translate(matrix_1, -(circle_2[0] + radial->cd2.radius / (scales[0] * precision_scale)), -(surface_height / precision_scale - (circle_2[1] + radial->cd2.radius / (scales[1] * precision_scale))));
				else
					cairo_matrix_init_translate(matrix_1, -(circle_2[0] + radial->cd2.radius / (scales[0] * precision_scale)), -((circle_2[1] + radial->cd2.radius / (scales[1] * precision_scale))));
			}
			else
			{
				if(upsidedown)
					cairo_matrix_init_translate(matrix_1, -(circle_2[0] + radial->cd2.radius / (scales[0] * precision_scale)), -(circle_2[1] + radial->cd2.radius / (scales[1] * precision_scale)));
				else
					cairo_matrix_init_translate(matrix_1, -(circle_2[0] + radial->cd2.radius / (scales[0] * precision_scale)), -(circle_2[1] + radial->cd2.radius / (scales[1] * precision_scale)));
			}
		}

		if(dy >= 0)
		{
			// pi/2
			if(dx == 0)
			{
				matrix_rotate(matrix_1, +M_PI /2.0);
			}
			else if(dx > 0)
			{
				angle = atan(dy/dx);
				matrix_rotate(matrix_1, angle);
			}
			else
			{
				angle = atan(dy/dx);
				matrix_rotate(matrix_1, angle + M_PI);
			}
		}
		else
		{
			if(dx == 0)
			{
				matrix_rotate(matrix_1, -M_PI / 2.0);
			}
			else if(dx > 0)
			{
				angle = atan(dy/dx);
				matrix_rotate(matrix_1, angle);
			}
			else
			{
				angle = atan(dy/dx);
				matrix_rotate(matrix_1, angle - M_PI);
				angle = angle - M_PI;
			}
		}
		return CAIRO_STATUS_SUCCESS;
	}


	// compute tangent points for 2nd circle
	a = radial->cd1.center.x;
	b = radial->cd1.center.y;
	c = radial->cd2.center.x;
	d = radial->cd2.center.y;
	if(radial->cd2.radius != 0.0)
	{
		compute_xy(radial->cd1.center.x, radial->cd1.center.y, 
			   radial->cd2.center.x, radial->cd2.center.y,
			   radial->cd1.radius, radial->cd2.radius,
			   &x1, &y1, &x2, &y2);
		compute_tangent_2(radial->cd1.center.x, radial->cd1.center.y,
					  radial->cd2.center.x, radial->cd2.center.y,
					  radial->cd1.radius, radial->cd2.radius,
					  x1, y1, x2, y2,
					  &tangent_3_x, &tangent_3_y,
					  &tangent_4_x, &tangent_4_y);
		compute_tangent_1(radial->cd1.center.x, radial->cd1.center.y,
					  radial->cd2.center.x, radial->cd2.center.y,
					  radial->cd1.radius, radial->cd2.radius, 
					  x1, y1,
					  tangent_3_x, tangent_3_y,
					  &tangent_1_x, &tangent_1_y);
		compute_tangent_1(radial->cd1.center.x, radial->cd1.center.y,
					  radial->cd2.center.x, radial->cd2.center.y,
					  radial->cd1.radius, radial->cd2.radius,
					  x2, y2,
					  tangent_4_x, tangent_4_y,
					  &tangent_2_x, &tangent_2_y);
	}
	else
	{
		compute_xy(radial->cd2.center.x, radial->cd2.center.y, 
			   radial->cd1.center.x, radial->cd1.center.y,
			   radial->cd2.radius, radial->cd1.radius,
			   &x1, &y1, &x2, &y2);
		compute_tangent_2(radial->cd2.center.x, radial->cd2.center.y,
					  radial->cd1.center.x, radial->cd1.center.y,
					  radial->cd2.radius, radial->cd1.radius,
					  x1, y1, x2, y2,
					  &tangent_3_x, &tangent_3_y,
					  &tangent_4_x, &tangent_4_y);
		compute_tangent_1(radial->cd2.center.x, radial->cd2.center.y,
					  radial->cd1.center.x, radial->cd1.center.y,
					  radial->cd2.radius, radial->cd1.radius, 
					  x1, y1,
					  tangent_3_x, tangent_3_y,
					  &tangent_1_x, &tangent_1_y);
		compute_tangent_1(radial->cd2.center.x, radial->cd2.center.y,
					  radial->cd1.center.x, radial->cd1.center.y,
					  radial->cd2.radius, radial->cd1.radius,
					  x2, y2,
					  tangent_4_x, tangent_4_y,
					  &tangent_2_x, &tangent_2_y);
	}
	if(radial->cd1.radius != radial->cd2.radius)
	{
		compute_end(radial->cd1.center.x, radial->cd1.center.y,
				    radial->cd2.center.x, radial->cd2.center.y,
					tangent_1_x, tangent_1_y,
					tangent_3_x, tangent_3_y,
					&end_x, &end_y);
		parallel = FALSE;
	}
	else
	{
		compute_end_equal(radial->cd1.center.x, radial->cd1.center.y,
						  radial->cd2.center.x, radial->cd2.center.y,
						  radial->cd1.radius, 
						  tangent_1_x, tangent_1_y,
						  tangent_2_x, tangent_2_y,
						  &tangent_end_1_x, &tangent_end_1_y,
						  &tangent_end_2_x, &tangent_end_2_y);
		cairo_matrix_transform_point(&matrix, &tangent_end_1_x, &tangent_end_1_y);
		tangents_end[0] = tangent_end_1_x / precision_scale;
		if(upsidedown)
			tangents_end[1] = (surface_height - tangent_end_1_y) / precision_scale;
		else
			tangents_end[1] = tangent_end_1_y / precision_scale;
		cairo_matrix_transform_point(&matrix, &tangent_end_2_x, &tangent_end_2_y);
		tangents_end[2] = tangent_end_2_x / precision_scale;
		if(upsidedown)
			tangents_end[3] = (surface_height - tangent_end_2_y) / precision_scale;
		else
			tangents_end[3] = tangent_end_2_y / precision_scale;
	}						  
	// transform in cairo
	if(parallel == FALSE)
	{
		cairo_matrix_transform_point(&matrix, &end_x, &end_y);
		end_point[0] = end_x / precision_scale;
		if(upsidedown)
			end_point[1] = (surface_height - end_y) / precision_scale;
		else
			end_point[1] = end_y / precision_scale;
	}
	cairo_matrix_transform_point(&matrix, &tangent_1_x, &tangent_1_y);
	tangents[0] = tangent_1_x / precision_scale;
	if(upsidedown)
		tangents[1] = (surface_height - tangent_1_y) / precision_scale;
	else
		tangents[1] = tangent_1_y / precision_scale;
	
	cairo_matrix_transform_point(&matrix, &tangent_2_x, &tangent_2_y);
	tangents[2] = tangent_2_x / precision_scale;
	if(upsidedown)
		tangents[3] = (surface_height - tangent_2_y) / precision_scale;
	else
		tangents[3] = tangent_2_y / precision_scale;
	cairo_matrix_transform_point(&matrix, &tangent_3_x, &tangent_3_y);
	tangents[4] = tangent_3_x / precision_scale;
	if(upsidedown)
		tangents[5] = (surface_height - tangent_3_y) / precision_scale;
	else
		tangents[5] = tangent_3_y / precision_scale;
	
	cairo_matrix_transform_point(&matrix, &tangent_4_x, &tangent_4_y);
	tangents[6] = tangent_4_x / precision_scale;
	if(upsidedown)
		tangents[7] = (surface_height - tangent_4_y) / precision_scale;
	else
		tangents[7] = tangent_4_y / precision_scale;

	// compute transformation 1
	if(parallel == FALSE)
	{
		if(upsidedown)
		{
			cairo_matrix_init_translate(matrix_1, -end_x / precision_scale, -(surface_height - end_y) / precision_scale);
			cairo_matrix_init_translate(matrix_2, -end_x / precision_scale, -(surface_height - end_y) / precision_scale);
		}
		else
		{
			cairo_matrix_init_translate(matrix_1, -end_x / precision_scale, -end_y / precision_scale);
			cairo_matrix_init_translate(matrix_2, -end_x / precision_scale, -end_y / precision_scale);
		}
	}
	else
	{
		if(upsidedown)
		{
			cairo_matrix_init_translate(matrix_1, -tangent_end_1_x / precision_scale, -(surface_height - tangent_end_1_y) / precision_scale);
			cairo_matrix_init_translate(matrix_2, -tangent_end_2_x / precision_scale, -(surface_height - tangent_end_2_y) / precision_scale);
		}
		else
		{
			cairo_matrix_init_translate(matrix_1, -tangent_end_1_x / precision_scale, -tangent_end_1_y / precision_scale);
			cairo_matrix_init_translate(matrix_2, -tangent_end_2_x / precision_scale, -tangent_end_2_y / precision_scale);
		}
	}		
	//double nx1 = 200; 
	//double ny1 = 600;
	//double nx2 = 200;
	//double ny2 = 600;
	//cairo_matrix_transform_point(matrix_1, &nx1, &ny1);
	//cairo_matrix_transform_point(matrix_2, &nx2, &ny2);
	if(parallel == FALSE)
	{
		dx = tangent_3_x - end_x;
		dy = -tangent_3_y + end_y;
		if(dx == 0.0 && dy == 0.0)
		{
			dx = tangent_1_x - end_x;
			dy = -tangent_1_y + end_y;
		}
	}
	else
	{
		dx = tangent_3_x - tangent_1_x;
		dy = -tangent_3_y + tangent_1_y;
	}
	if(dy >= 0)
	{
		// pi/2
		if(dx == 0)
		{
			matrix_rotate(matrix_1, +M_PI /2.0);
		}
		else if(dx > 0)
		{
			angle = atan(dy/dx);
			matrix_rotate(matrix_1, angle);
		}
		else
		{
			angle = atan(dy/dx);
			matrix_rotate(matrix_1, angle + M_PI);
		}
	}
	else
	{
		if(dx == 0)
		{
			matrix_rotate(matrix_1, -M_PI / 2.0);
		}
		else if(dx > 0)
		{
			angle = atan(dy/dx);
			matrix_rotate(matrix_1, angle);
		}
		else
		{
			angle = atan(dy/dx);
			matrix_rotate(matrix_1, angle - M_PI);
			angle = angle - M_PI;
		}
	}
	// compute matrix_2
	if(parallel == FALSE)
	{
		dx = tangent_4_x - end_x;
		dy = -tangent_4_y + end_y;
		if(dy == 0.0 && dx == 0.0)
		{
			dx = tangent_2_x - end_x;
			dy = -tangent_2_y + end_y;
		}
	}
	else
	{
		dx = tangent_4_x - tangent_2_x;
		dy = -tangent_4_y + tangent_2_y;
	}
	if(dy >= 0)
	{
		// pi/2
		if(dx == 0)
		{
			matrix_rotate(matrix_2, M_PI / 2.0);
		}
		else if(dx > 0)
		{
			angle = atan(dy/dx);
			matrix_rotate(matrix_2, angle);
		}
		else
		{
			angle = atan(dy/dx);
			matrix_rotate(matrix_2, angle + M_PI);
		}
	}
	else
	{
		if(dx == 0)
		{
			matrix_rotate(matrix_2, - M_PI / 2.0);
		}
		else if(dx > 0)
		{
			angle = atan(dy/dx);
			matrix_rotate(matrix_2, angle);
		}
		else
		{
			angle = atan(dy/dx);
			matrix_rotate(matrix_2, angle - M_PI);
		}
	}
	//let's distinquish two matrix
	if(radial->cd1.radius != 0.0)
	{
		mx1 =  radial->cd1.center.x;
		my1 = radial->cd1.center.y;
	}
	else
	{
		mx1 =  radial->cd2.center.x;
		my1 = radial->cd2.center.y;
	}
	cairo_matrix_transform_point(&matrix, &mx1, &my1);
    mx1 /= precision_scale;
	if(upsidedown)
		my1 = surface_height - my1;
    my1 /= precision_scale;

	mx2 = mx1;
	my2 = my1;
	matrix_transform_point(matrix_1, &mx1, &my1);
	matrix_transform_point(matrix_2, &mx2, &my2);
	if(my1 < 0.0)
	{
		memcpy((void *)&temp_matrix, matrix_1, 6 * sizeof(double));
		memcpy(matrix_1, matrix_2, 6 * sizeof(double));
		memcpy(matrix_2, (void *)&temp_matrix, 6 * sizeof(double));
	}

	/*double mx1 = 200;
	double my1 = 600;
	matrix_transform_point(matrix_1, &mx1, &my1);
	double mx2 = 200;
	double my2 = 600;
	matrix_transform_point(matrix_2, &mx2, &my2);
	*/
	

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
