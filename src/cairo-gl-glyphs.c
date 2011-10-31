/* Cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Chris Wilson
 * Copyright © 2010 Intel Corporation
 * Copyright © 2010 Red Hat, Inc
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
 * The Initial Developer of the Original Code is Chris Wilson.
 *
 * Contributors:
 *      Benjamin Otte <otte@gnome.org>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 */

#include "cairoint.h"
#include "cairo-private.h"
#include "cairo-gl-private.h"
#include "cairo-gl-tristrip-indices-private.h"

#include "cairo-error-private.h"
#include "cairo-rtree-private.h"

#include <sys/time.h>

#define GLYPH_CACHE_WIDTH 2048
#define GLYPH_CACHE_HEIGHT 2048
#define GLYPH_CACHE_MIN_SIZE 1
#define GLYPH_CACHE_MAX_SIZE 512

#define FONT_STANDARD_SIZE 8
#define FONT_SIZE_SMOOTH 0

#if 0
// this function is not used
static long _get_tick(void)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return now.tv_sec * 1000000 + now.tv_usec;
}
#endif

typedef struct _cairo_gl_glyph_private {
    cairo_rtree_node_t node;
    cairo_gl_glyph_cache_t *cache;
    struct { float x, y; } p1, p2;
} cairo_gl_glyph_private_t;

static cairo_status_t
_cairo_gl_glyph_cache_add_glyph (cairo_gl_context_t *ctx,
				 cairo_gl_glyph_cache_t *cache,
				 cairo_scaled_glyph_t  *scaled_glyph)
{
    cairo_image_surface_t *glyph_surface = scaled_glyph->surface;
    cairo_gl_surface_t *cache_surface;
    cairo_gl_glyph_private_t *glyph_private;
    cairo_rtree_node_t *node = NULL;
    cairo_int_status_t status;
    int width, height;

    width = glyph_surface->width;
    if (width < GLYPH_CACHE_MIN_SIZE)
	width = GLYPH_CACHE_MIN_SIZE;
    height = glyph_surface->height;
    if (height < GLYPH_CACHE_MIN_SIZE)
	height = GLYPH_CACHE_MIN_SIZE;

    /* search for an available slot */
    status = _cairo_rtree_insert (&cache->rtree, width, height, &node);
    /* search for an unlocked slot */
    if (status == CAIRO_INT_STATUS_UNSUPPORTED) {
	status = _cairo_rtree_evict_random (&cache->rtree,
				            width, height, &node);
	if (status == CAIRO_INT_STATUS_SUCCESS) {
	    status = _cairo_rtree_node_insert (&cache->rtree,
		                               node, width, height, &node);
	}
    }
    if (status)
	return status;

    cache_surface = (cairo_gl_surface_t *) cache->pattern.surface;

    /* XXX: Make sure we use the mask texture. This should work automagically somehow */
    if(ctx->active_texture != GL_TEXTURE1)
    {
        glActiveTexture (GL_TEXTURE1);
        ctx->active_texture = GL_TEXTURE1;
    }
    status = _cairo_gl_surface_draw_image (cache_surface,
                                           glyph_surface,
                                           0, 0,
                                           width, height,
                                           node->x, node->y, TRUE);
    if (unlikely (status))
	return status;

    scaled_glyph->surface_private = node;
    node->owner = &scaled_glyph->surface_private;

    glyph_private = (cairo_gl_glyph_private_t *) node;
    glyph_private->cache = cache;

    /* compute tex coords */
	/*
	if(glyph_surface->width <= 20)
	{
    	glyph_private->p1.x = node->x + 0.05 * glyph_surface->width;
    	glyph_private->p2.x = node->x + glyph_surface->width - 0.10 * glyph_surface->width;
	}
	else*/
	{
    	glyph_private->p1.x = node->x;
    	glyph_private->p2.x = node->x + glyph_surface->width;
	}
	/*
	if(glyph_surface->height <= 20)
	{
    	glyph_private->p1.y = node->y + 0.05 * glyph_surface->height;
    	glyph_private->p2.y = node->y + glyph_surface->height - 0.10 * glyph_surface->height;
	}
	else */
	{
    	glyph_private->p1.y = node->y;
    	glyph_private->p2.y = node->y + glyph_surface->height;
	}

	if (! _cairo_gl_device_requires_power_of_two_textures (&ctx->base)) 
	{
		glyph_private->p1.x /= cache_surface->width;
		glyph_private->p1.y /= cache_surface->height;
		glyph_private->p2.x /= cache_surface->width;
		glyph_private->p2.y /= cache_surface->height;
    }

    return CAIRO_STATUS_SUCCESS;
}

static cairo_gl_glyph_private_t *
_cairo_gl_glyph_cache_lock (cairo_gl_glyph_cache_t *cache,
			    cairo_scaled_glyph_t *scaled_glyph)
{
    return _cairo_rtree_pin (&cache->rtree, scaled_glyph->surface_private);
}

static cairo_status_t
cairo_gl_context_get_glyph_cache (cairo_gl_context_t *ctx,
				  cairo_format_t format,
                  cairo_gl_glyph_cache_t **cache_out)
{
    cairo_gl_glyph_cache_t *cache;
    cairo_content_t content;

    switch (format) 
    {
	case CAIRO_FORMAT_RGB30:
	     break;
    	case CAIRO_FORMAT_RGB16_565:
    	case CAIRO_FORMAT_ARGB32:
    	case CAIRO_FORMAT_RGB24:
			cache = &ctx->glyph_cache[0];
        	content = CAIRO_CONTENT_COLOR_ALPHA;
			break;
    	case CAIRO_FORMAT_A8:
    	case CAIRO_FORMAT_A1:
			cache = &ctx->glyph_cache[1];
        	content = CAIRO_CONTENT_ALPHA;
			break;
    	case CAIRO_FORMAT_INVALID:
			ASSERT_NOT_REACHED;
			return _cairo_error (CAIRO_STATUS_INVALID_FORMAT);
    }

    if (unlikely (cache->pattern.surface == NULL)) 
	{
        cairo_surface_t *surface;
        surface = cairo_gl_surface_create (&ctx->base,
                                           content,
										   //ctx->max_texture_size-1,
										   //ctx->max_texture_size-1);
                                           GLYPH_CACHE_WIDTH,
                                           GLYPH_CACHE_HEIGHT);
        if (unlikely (surface->status)) 
		{
            cairo_status_t status = surface->status;
            cairo_surface_destroy (surface);
            return status;
        }
        _cairo_surface_release_device_reference (surface);
        _cairo_pattern_init_for_surface (&cache->pattern, surface);
        cairo_surface_destroy (surface);
        cache->pattern.base.has_component_alpha = (content == CAIRO_CONTENT_COLOR_ALPHA);
    }
	
	/*if(cache->mask_surface != NULL && 
		(cache->mask_surface->width != dst->width ||
		cache->mask_surface->height != dst->height))
	{
		cairo_surface_destroy(&cache->mask_surfac->base);
		cache->mask_surface = NULL;
	}

	if(cache->mask_surface == NULL)
	{
		cairo_surface_t *mask_surface = cairo_gl_surface_create(&ctx->base,
											content, dst->width,
											dst->height);
       	if (unlikely (mask_surface->status)) 
		{
            cairo_status_t status = mask_surface->status;
            cairo_surface_destroy (mask_surface);
            return status;
        }
        //_cairo_surface_release_device_reference (surface);
		cache->mask_surface = mask_surface;
    }
	*/
    *cache_out = cache;
    return CAIRO_STATUS_SUCCESS;
}

static void
_cairo_gl_glyph_cache_unlock (cairo_gl_glyph_cache_t *cache)
{
    _cairo_rtree_unpin (&cache->rtree);
}

static cairo_bool_t
_cairo_gl_surface_owns_font (cairo_gl_surface_t *surface,
			     cairo_scaled_font_t *scaled_font)
{
    cairo_device_t *font_private;

    font_private = scaled_font->surface_private;
    if ((scaled_font->surface_backend != NULL &&
	 scaled_font->surface_backend != &_cairo_gl_surface_backend) ||
	(font_private != NULL && font_private != surface->base.device))
    {
	return FALSE;
    }

    return TRUE;
}

void
_cairo_gl_surface_scaled_font_fini (cairo_scaled_font_t  *scaled_font)
{
	//int i;
    cairo_list_del (&scaled_font->link);
	// we need to remove corresponding max font
	/*cairo_array_t user_data = (cairo_array_t)(scaled_font->user_data);
	int num = _cairo_array_num_elements(&user_data);
	if(num != 0)
	{
		for(i = 0; i < num; i++)
		{
			cairo_scaled_font_t *font = (cairo_scaled_font_t *)_cairo_array_index(&user_data, i);
			if(font == NULL || font->font_face == NULL)
				continue;
				cairo_scaled_font_destroy(font);
		}
		_cairo_array_truncate(&user_data, 0);
	}*/
}

void
_cairo_gl_surface_scaled_glyph_fini (cairo_scaled_glyph_t *scaled_glyph,
				     cairo_scaled_font_t  *scaled_font)
{
    cairo_gl_glyph_private_t *glyph_private;

    glyph_private = scaled_glyph->surface_private;
    if (glyph_private != NULL) 
	{
		glyph_private->node.owner = NULL;
		if (! glyph_private->node.pinned) 
		{
	    /* XXX thread-safety? Probably ok due to the frozen scaled-font. */
	    	_cairo_rtree_node_remove (&glyph_private->cache->rtree,
		                      &glyph_private->node);
		}
    }
}

static cairo_status_t
_render_glyphs (cairo_gl_surface_t *dst, int dst_width, int dst_height,
	        int dst_x, int dst_y, cairo_bool_t render_to_dst,
	        cairo_operator_t	 op,
		const cairo_pattern_t	*source,
		cairo_glyph_t		*glyphs,
		int			 num_glyphs,
		const cairo_rectangle_int_t *glyph_extents,
		cairo_scaled_font_t	*scaled_font,
		cairo_bool_t		*has_component_alpha,
		cairo_gl_surface_t **mask,
		cairo_clip_t		*clip,
		int			*remaining_glyphs)
{
	int max_font_size_interval = 512 / FONT_STANDARD_SIZE;
    cairo_format_t last_format = CAIRO_FORMAT_INVALID;
    cairo_gl_glyph_cache_t *cache = NULL;
    cairo_gl_context_t *ctx;
    cairo_gl_composite_t setup;
    cairo_int_status_t status;
    cairo_color_t color;
    
    int m = 1;
    cairo_matrix_t min_matrix;
    cairo_matrix_t max_matrix;
    cairo_scaled_font_t *min_font = NULL;
    cairo_scaled_font_t *max_font = NULL;
    cairo_bool_t font_match = FALSE;

    cairo_bool_t setup_mask = FALSE;
    cairo_gl_tristrip_indices_t indices;

    int i = 0;
    cairo_font_options_t options;

    double font_scale = scaled_font->font_matrix.xx;
    double default_font_scale = FONT_STANDARD_SIZE;
    double x_advance;
    double y_advance;
    double min_x;
    double min_y;
    double max_x;
    double max_y;
    cairo_point_t points[4];
    cairo_antialias_t aa;

	//cairo_antialias_t current_antialias = scaled_font->options.antialias;
	//scaled_font->options.antialias = CAIRO_ANTIALIAS_SUBPIXEL;

    *has_component_alpha = FALSE;
	//long now = _get_tick();
	// get context's mask_surface;
	if(render_to_dst == FALSE)
	{
		//printf("render to mask\n");
		if(dst->mask_surface != NULL && 
		   (dst->mask_surface->width != dst->width ||
		    dst->mask_surface->height != dst->height))
		{
			cairo_surface_destroy(&(dst->mask_surface->base));
			dst->mask_surface = NULL;
		}
		if(dst->mask_surface == NULL)
		{
			cairo_surface_t *mask_surface = cairo_surface_create_similar(&dst->base, CAIRO_CONTENT_COLOR_ALPHA, dst->width, dst->height);
			if(mask_surface == NULL || cairo_surface_get_type(mask_surface) != CAIRO_SURFACE_TYPE_GL || unlikely(mask_surface->status))
			{
				cairo_surface_destroy(mask_surface);
				dst->mask_surface = NULL;
			}
			else
			{
				dst->mask_surface = (cairo_gl_surface_t *)mask_surface;
				dst->mask_surface->parent_surface = dst;
				dst->mask_surface->mask_surface = NULL;
			}
		}
		if(dst->mask_surface == NULL)
		{
			//_cairo_gl_composite_fini(&setup);
			//scaled_font->options.antialias = current_antialias;
			return CAIRO_INT_STATUS_UNSUPPORTED;
		}
		
		color.red = 0;
		color.green = 0;
		color.blue = 0;
		color.alpha = 0;
		_cairo_gl_surface_clear(dst->mask_surface, &color);

		
		*mask = dst->mask_surface;
		dst = dst->mask_surface;
	}

	
	/*if(dst->needs_super_sampling == TRUE)
	{
		_cairo_gl_surface_super_sampling(&dst->base);
		dst->needs_super_sampling = FALSE;
	}*/
	//long now = _get_tick();
	
	// we setup composite_t here
	status = _cairo_gl_composite_init(&setup, op, dst, *has_component_alpha, NULL);
	if(unlikely(status))
	{
		//scaled_font->options.antialias = current_antialias;
		_cairo_gl_composite_fini(&setup);
		return status;
	}
	setup.source = source;
	status = _cairo_gl_composite_set_source(&setup, source, 0, 0, 0, 0, 
		dst->width, dst->height, 0, 0, 0);
	if(unlikely(status))
	{
		//scaled_font->options.antialias = current_antialias;
		_cairo_gl_composite_fini(&setup);
		return status;
	}

    status = _cairo_gl_context_acquire (dst->base.device, &ctx);
    if (unlikely (status))
	{
		//scaled_font->options.antialias = current_antialias;
		_cairo_gl_composite_fini(&setup);
		return status;
	}

    //_cairo_gl_disable_stencil_test (ctx);
    //_cairo_gl_disable_scissor_test (ctx);

    // we alway paint to texture, so force blit to texture
    // if default or grey, if multisample not resolve, we set antialias
    // to be NONE
	//	options.antialias = scaled_font->options.antialias;
    aa = scaled_font->options.antialias;
    if(dst->multisample_resolved == FALSE) 
    {
        //printf("paint to multisample\n");
        dst->require_aa = TRUE;
    }
    else
    {
        //printf("paint to single sample\n");
        dst->require_aa = FALSE;
    }
        // we always resolve multisampling
        //if(dst->multisample_resolved == FALSE)
        //    _cairo_gl_context_blit_destination(ctx, dst);	
	
    _cairo_gl_context_set_destination(ctx, dst);
	setup.ctx = ctx;
	// clip
	/*
	if(clip != NULL && render_to_dst == TRUE)
	{
		if(dst->clip.path != NULL)
		{
			if(!_cairo_clip_equal(clip, &(dst->clip)))
			{
				_cairo_clip_fini(&(dst->clip));
				_cairo_clip_init_copy(&(dst->clip), clip);
				dst->needs_stencil = TRUE;
				dst->stencil_changed = TRUE;
			}
			else
			{
				dst->needs_stencil = TRUE;
				dst->stencil_changed = FALSE;
			}
		}
		else
		{
			_cairo_clip_init_copy(&(dst->clip), clip);
			dst->needs_stencil = TRUE;
			dst->stencil_changed = TRUE;
		}
	}
	else
	{
		if(dst->clip.path != NULL)
		{
			_cairo_clip_fini(&(dst->clip));
			dst->clip.path = NULL;
			dst->needs_stencil = FALSE;
			dst->stencil_changed = TRUE;
		}
	}

	if(dst->needs_stencil == TRUE)
	{
		//setup.op = CAIRO_OPERATOR_OVER;
		cairo_pattern_t *pattern = cairo_pattern_create_rgba(1, 1, 1, 1);
		setup.source = pattern;
		status = _cairo_gl_composite_set_source(&setup, pattern, 0, 0, 0, 0, 
		dst->width, dst->height, 0, 0, 0);
		if(unlikely(status))
		{
			_cairo_gl_composite_fini(&setup);
			cairo_pattern_destroy(pattern);
			status = _cairo_gl_context_release(ctx, status);
			return status;
		}
		status = _cairo_gl_clip(clip, &setup, ctx, dst);
		if(unlikely(status))
		{
			_cairo_gl_composite_fini(&setup);
			cairo_pattern_destroy(pattern);
			glDisable(GL_STENCIL_TEST);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
			status = _cairo_gl_context_release(ctx, status);
			return status;
		}
		cairo_pattern_destroy(pattern);
	}*/
	setup.source = source;
	status = _cairo_gl_composite_set_source(&setup, source, 0, 0, 0, 0, 
		dst->width, dst->height, 0, 0, 0);
	if(unlikely(status))
	{
		//scaled_font->options.antialias = current_antialias;
		_cairo_gl_composite_fini(&setup);
		status = _cairo_gl_context_release(ctx, status);
		return status;
	}
	
	//printf("clipped \n");
	// we have done clip
	// We need to generate two fonts
	// we need to create 48/96/144/192/.....
	setup.op = op;
    _cairo_scaled_font_freeze_cache (scaled_font);
	
	//long start = _get_tick();

	//double font_scale = scaled_font->font_matrix.xx;
	//double default_font_scale = FONT_STANDARD_SIZE;

	/*if(ctx->gl_flavor == CAIRO_GL_FLAVOR_ES)
		options.antialias = CAIRO_ANTIALIAS_SUBPIXEL;
	else*/
	//options.antialias = scaled_font->options.antialias;
    options.antialias = aa;
		//options.antialias = CAIRO_ANTIALIAS_SUBPIXEL;
	options.hint_metrics = scaled_font->options.hint_metrics;
	options.hint_style = scaled_font->options.hint_style;
	options.lcd_filter = scaled_font->options.lcd_filter;
	options.round_glyph_positions = CAIRO_SUBPIXEL_ORDER_DEFAULT;
	options.subpixel_order = scaled_font->options.subpixel_order;
	//if(scaled_font->ctm.xx != 0 && scaled_font->ctm.xy != 0)
	if(FONT_SIZE_SMOOTH != 1)
	{
		// we are in rotate mode
		cairo_matrix_init_scale(&max_matrix, 
			scaled_font->font_matrix.xx,
			scaled_font->font_matrix.yy);
		max_font = cairo_scaled_font_create(scaled_font->font_face,
			&max_matrix, &scaled_font->ctm,
			//&scaled_font->options );
			&options);
		min_font = NULL;
		font_match = TRUE;
		default_font_scale = font_scale;
	}
	else
	{
		for(m = 1; m <= max_font_size_interval; m++)
		{
			if(scaled_font->font_matrix.xx <= m * FONT_STANDARD_SIZE)
			{
				cairo_matrix_init_scale(&max_matrix, 
					m * FONT_STANDARD_SIZE, m * FONT_STANDARD_SIZE);
				max_font = cairo_scaled_font_create(scaled_font->font_face,
					&max_matrix, &scaled_font->ctm,
					&options);
				if(m != 1 && scaled_font->font_matrix.xx != m * FONT_STANDARD_SIZE)
				{
					cairo_matrix_init_scale(&min_matrix, 
						(m-1) * FONT_STANDARD_SIZE, (m-1) * FONT_STANDARD_SIZE);
					min_font = cairo_scaled_font_create(scaled_font->font_face,
						&min_matrix, &scaled_font->ctm,
						//&scaled_font->options);
						&options);
					font_scale = scaled_font->font_matrix.xx - min_font->font_matrix.xx;
				}
				if(scaled_font->font_matrix.xx == max_font->font_matrix.xx)
				{
					font_match = TRUE;
					default_font_scale = font_scale;
				}
				break;
			}
		}
	}
	if(max_font == NULL)
		// we could not generate font
	{
		_cairo_gl_composite_fini(&setup);
		//scaled_font->options.antialias = current_antialias;
		_cairo_scaled_font_thaw_cache(scaled_font);
		//glDisable(GL_STENCIL_TEST);
		//glDisable(GL_DEPTH_TEST);
		//glDepthMask(GL_FALSE);
		if(min_font != NULL)
			cairo_scaled_font_destroy(min_font);
		status = _cairo_gl_context_release(ctx, status);

		status = CAIRO_INT_STATUS_UNSUPPORTED;
		return status;
	}
	// we need to get surface for min/max font
	if(font_match == FALSE)
    	_cairo_scaled_font_freeze_cache (max_font);
	if(min_font)
		_cairo_scaled_font_freeze_cache(min_font);
	
	if(!_cairo_gl_surface_owns_font(dst, max_font))
	{
		_cairo_gl_composite_fini(&setup);
		//scaled_font->options.antialias = current_antialias;
		_cairo_scaled_font_thaw_cache(scaled_font);
		if(font_match == FALSE)
		{
			_cairo_scaled_font_thaw_cache(max_font);
			//cairo_scaled_font_destroy(max_font);
		}
		if(min_font)
		{
			_cairo_scaled_font_thaw_cache(min_font);
			cairo_scaled_font_destroy(min_font);
		}
		//glDisable(GL_STENCIL_TEST);
		//glDisable(GL_DEPTH_TEST);
		//glDepthMask(GL_FALSE);
		status = _cairo_gl_context_release(ctx, status);

		status = CAIRO_INT_STATUS_UNSUPPORTED;
		return status;
	}

	if(dst->tex != 0)
		status = _cairo_gl_composite_set_source(&setup, source, 0, 0, 0, 0,
			0, 0, dst->tex, dst->width, dst->height);
	else
		status = _cairo_gl_composite_set_source(&setup, source, 0, 0, 0, 0,
			0, 0, 0, 0, 0);
	if(unlikely(status))
	{
		_cairo_gl_composite_fini(&setup);
		//scaled_font->options.antialias = current_antialias;
		_cairo_scaled_font_thaw_cache(scaled_font);
		if(font_match == FALSE)
		{
			_cairo_scaled_font_thaw_cache(max_font);
			//cairo_scaled_font_destroy(max_font);
		}
		if(min_font)
		{
			_cairo_scaled_font_thaw_cache(min_font);
			cairo_scaled_font_destroy(min_font);
		}
		//glDisable(GL_STENCIL_TEST);
		//glDisable(GL_DEPTH_TEST);
		//glDepthMask(GL_FALSE);
		status = _cairo_gl_context_release(ctx, status);

		status = CAIRO_INT_STATUS_UNSUPPORTED;
		return status;
	}

	if(scaled_font->surface_private == NULL)
	{
		scaled_font->surface_private = ctx;
		scaled_font->surface_backend = &_cairo_gl_surface_backend;
		_cairo_array_append((cairo_array_t *)&(scaled_font->user_data), (void *)max_font);
		//cairo_scaled_font_reference(max_font);
		cairo_list_add(&scaled_font->link, &ctx->fonts);
	}
	if(max_font->surface_private == NULL)
	{
		max_font->surface_private = ctx;
		max_font->surface_backend = &_cairo_gl_surface_backend;
	}

	status = _cairo_gl_tristrip_indices_init (&indices);
	indices.setup = &setup;
	x_advance = glyphs[0].x;
	y_advance = glyphs[0].y;

	//long middle = _get_tick();
	//printf("get font takes %ld usec for size %0.2f\n", middle - start,
	//	scaled_font->font_matrix.xx);

	// Henry Song
	//long max_time = 0;
	//long min_time = 0;

	for(i = 0; i < num_glyphs; i++)
	{
		cairo_scaled_glyph_t *min_glyph = NULL;
		cairo_scaled_glyph_t *max_glyph = NULL;
		cairo_gl_glyph_private_t *glyph = NULL;
		double max_x_offset, max_y_offset;
		double x_offset, y_offset;
		double min_x_offset = 0;
		double min_y_offset = 0;
		double x1, y1, x2, y2;
		cairo_bool_t empty = FALSE;
		double current_x_advance;
		double current_y_advance;
                double min_width = 0;
                double min_height = 0;
                double width;
                double height;

		//cairo_fixed_t x_top_left, x_bottom_left;
		//cairo_fixed_t x_top_right, x_bottom_right;
		
		//long now1 = _get_tick();
		status = _cairo_scaled_glyph_lookup(max_font,
			glyphs[i].index,
			CAIRO_SCALED_GLYPH_INFO_SURFACE, &max_glyph);
		//long then1 = _get_tick() - now1;
		//max_time += then1;

		if(unlikely(status) || 
			(max_glyph->surface->width > GLYPH_CACHE_MAX_SIZE ||
		 	max_glyph->surface->height > GLYPH_CACHE_MAX_SIZE))
		{
			_cairo_gl_composite_fini(&setup);
			//scaled_font->options.antialias = current_antialias;
			_cairo_scaled_font_thaw_cache(scaled_font);
			if(font_match == FALSE)
				_cairo_scaled_font_thaw_cache(max_font);
				//cairo_scaled_font_destroy(max_font);
			if(min_font)
			{
				_cairo_scaled_font_thaw_cache(min_font);
				cairo_scaled_font_destroy(min_font);
			}
			_cairo_gl_tristrip_indices_destroy (&indices);
			//glDisable(GL_STENCIL_TEST);
			//glDisable(GL_DEPTH_TEST);
			//glDepthMask(GL_FALSE);
			status = _cairo_gl_context_release(ctx, status);
			*remaining_glyphs = num_glyphs - i;

			status = CAIRO_INT_STATUS_UNSUPPORTED;
			return status;
		}
		//printf("look for %d's index = %d, width = %d, height = %d\n", i, glyphs[i].index, max_glyph->surface->width, max_glyph->surface->height);
		if(max_glyph->surface->width == 0 ||
	   		max_glyph->surface->height == 0)
		
		{
			empty = TRUE;
		}

		if(max_glyph->surface->format != last_format)
		{
			status = cairo_gl_context_get_glyph_cache(ctx,
				max_glyph->surface->format, &cache);
			if(unlikely(status))
			{
				_cairo_gl_composite_fini(&setup);
				//scaled_font->options.antialias = current_antialias;
				_cairo_scaled_font_thaw_cache(scaled_font);
				if(font_match == FALSE)
					_cairo_scaled_font_thaw_cache(max_font);
					//cairo_scaled_font_destroy(max_font);
				if(min_font)
				{
					_cairo_scaled_font_thaw_cache(min_font);
					cairo_scaled_font_destroy(min_font);
				}
				_cairo_gl_tristrip_indices_destroy (&indices);
				//glDisable(GL_STENCIL_TEST);
				//glDisable(GL_DEPTH_TEST);
				//glDepthMask(GL_FALSE);
				status = _cairo_gl_context_release(ctx, status);
				*remaining_glyphs = num_glyphs - i;
	
				status = CAIRO_INT_STATUS_UNSUPPORTED;
				return status;
			}
			*has_component_alpha |= cache->pattern.base.has_component_alpha;
			// we need to set destination back
            if(dst->multisample_resolved == FALSE)
                _cairo_gl_context_blit_destination(ctx, dst);
			_cairo_gl_context_set_destination(ctx, dst);

			last_format = max_glyph->surface->format;
			if(setup_mask == FALSE)
			{
				cairo_surface_t *mask = NULL;
				cairo_pattern_get_surface(&cache->pattern.base, &mask);
				if(mask != NULL)
				{
					cairo_gl_surface_t *gl_mask = (cairo_gl_surface_t *)mask;
					status = _cairo_gl_composite_set_mask(&setup, &cache->pattern.base, 0, 0, 0, 0, 0, 0, gl_mask->tex, gl_mask->width, gl_mask->height);
					if(unlikely(status))
					{
						_cairo_gl_composite_fini(&setup);
						//scaled_font->options.antialias = current_antialias;
						_cairo_scaled_font_thaw_cache(scaled_font);
						if(font_match == FALSE)
							_cairo_scaled_font_thaw_cache(max_font);
							//cairo_scaled_font_destroy(max_font);
						if(min_font)
						{
							_cairo_scaled_font_thaw_cache(min_font);
							cairo_scaled_font_destroy(min_font);
						}
						_cairo_gl_tristrip_indices_destroy (&indices);
				
						//glDisable(GL_STENCIL_TEST);
						//glDisable(GL_DEPTH_TEST);
						//glDepthMask(GL_FALSE);
						status = _cairo_gl_context_release(ctx, status);
						*remaining_glyphs = num_glyphs - i;
	
						status = CAIRO_INT_STATUS_UNSUPPORTED;
						return status;
					}
					setup_mask = TRUE;
				}
			}
		}
		if(min_font != NULL)
		{
			//long now2 = _get_tick();
			status = _cairo_scaled_glyph_lookup(min_font,
				glyphs[i].index,
				CAIRO_SCALED_GLYPH_INFO_SURFACE,
				&min_glyph);
			//long then2 = _get_tick() - now2;
			//min_time += then2;
			//printf("\tgenerate max font takes %ld usec, min font takes %ld usec\n", then1, then2);
			if(unlikely(status) || 
				(min_glyph->surface->width > GLYPH_CACHE_MAX_SIZE ||
		 		 min_glyph->surface->height > GLYPH_CACHE_MAX_SIZE))
			{
				_cairo_gl_composite_fini(&setup);
				//scaled_font->options.antialias = current_antialias;
				_cairo_scaled_font_thaw_cache(scaled_font);
				if(font_match == FALSE)
					_cairo_scaled_font_thaw_cache(max_font);
					//cairo_scaled_font_destroy(max_font);
				if(min_font)
				{
					_cairo_scaled_font_thaw_cache(min_font);
					cairo_scaled_font_destroy(min_font);
				}
				_cairo_gl_tristrip_indices_destroy (&indices);
				//glDisable(GL_STENCIL_TEST);
				//glDisable(GL_DEPTH_TEST);
				//glDepthMask(GL_FALSE);
				status = _cairo_gl_context_release(ctx, status);
				*remaining_glyphs = num_glyphs - i;

				status = CAIRO_INT_STATUS_UNSUPPORTED;
				return status;
			}
			if(min_glyph->surface->width == 0 ||
	   			min_glyph->surface->height == 0)
		
			{
				//continue;
			}
		}
		
		min_x = 0;
		min_y = 0;
		max_x = max_glyph->metrics.x_advance;
		max_y = max_glyph->metrics.y_advance;
		if(min_font != NULL)
		{
			min_x = min_glyph->metrics.x_advance;
			min_y = min_glyph->metrics.y_advance;
		}
		current_x_advance = (max_x - min_x) * font_scale / default_font_scale + min_x;
		current_y_advance = (max_y - min_y) * font_scale / default_font_scale + min_y;
		cairo_matrix_transform_distance(&(scaled_font->ctm), &current_x_advance, &current_y_advance);
		if(empty == TRUE)
		{
			x_advance += current_x_advance;
			y_advance += current_y_advance;
			continue;
		}

		if(max_glyph->surface_private == NULL)
		{
			status = _cairo_gl_glyph_cache_add_glyph(ctx, cache, max_glyph);
			if(status == CAIRO_INT_STATUS_UNSUPPORTED)
			{
				// cache full in scratch texture
				_cairo_gl_fill(&indices);
				_cairo_gl_tristrip_indices_destroy (&indices);
				_cairo_gl_tristrip_indices_init (&indices);
				indices.setup = &setup;
			
				_cairo_gl_glyph_cache_unlock(cache);
				status = _cairo_gl_glyph_cache_add_glyph(ctx, cache, max_glyph);
				if(unlikely(status))
				{
					_cairo_gl_composite_fini(&setup);
					//scaled_font->options.antialias = current_antialias;
					_cairo_scaled_font_thaw_cache(scaled_font);
					if(font_match == FALSE)
						_cairo_scaled_font_thaw_cache(max_font);
						//cairo_scaled_font_destroy(max_font);
					if(min_font)
					{
						_cairo_scaled_font_thaw_cache(min_font);
						cairo_scaled_font_destroy(min_font);
					}
					//glDisable(GL_STENCIL_TEST);
					//glDisable(GL_DEPTH_TEST);
					//glDepthMask(GL_FALSE);
					_cairo_gl_tristrip_indices_destroy (&indices);
					status = _cairo_gl_context_release(ctx, status);
					*remaining_glyphs = num_glyphs - i;

					status = CAIRO_INT_STATUS_UNSUPPORTED;
					return status;
				}
			}

		}

		// let's setup vertex

		max_x_offset = max_glyph->surface->base.device_transform.x0;
		max_y_offset = max_glyph->surface->base.device_transform.y0;
		//double x_offset, y_offset;
		if(min_font != NULL) 
		{
			min_x_offset = min_glyph->surface->base.device_transform.x0;
			min_y_offset = min_glyph->surface->base.device_transform.y0;
		}
		x_offset = (max_x_offset - min_x_offset) * font_scale / default_font_scale + min_x_offset;
		y_offset = (max_y_offset - min_y_offset) * font_scale / default_font_scale + min_y_offset;

		//double min_width = 0;
		//double min_height = 0;
		if(min_font != NULL)
		{
			min_width = min_glyph->surface->width;
			min_height = min_glyph->surface->height;
		}
		width = (max_glyph->surface->width - min_width) * font_scale / default_font_scale + min_width;
		height = (max_glyph->surface->height - min_height) * font_scale / default_font_scale + min_height;

		//x1 = glyphs[i].x - x_offset;
		//y1 = glyphs[i].y - y_offset;
		x1 = x_advance - x_offset;
		y1 = y_advance - y_offset;
		x2 = x1 + width;
		y2 = y1 + height;

		glyph = _cairo_gl_glyph_cache_lock(cache, max_glyph);
		points[0].x = _cairo_fixed_from_double(x1);
		points[0].y = _cairo_fixed_from_double(y1);
		points[1].x = _cairo_fixed_from_double(x1);
		points[1].y = _cairo_fixed_from_double(y2);
		points[2].x = _cairo_fixed_from_double(x2);
		points[2].y = _cairo_fixed_from_double(y2);
		points[3].x = _cairo_fixed_from_double(x2);
		points[3].y = _cairo_fixed_from_double(y1);

		x_advance += current_x_advance;
		y_advance += current_y_advance;

		_cairo_gl_tristrip_indices_add_mask_texture_coord (&indices, glyph->p1.x, glyph->p1.y);
		_cairo_gl_tristrip_indices_add_mask_texture_coord (&indices, glyph->p1.x, glyph->p2.y);
		_cairo_gl_tristrip_indices_add_mask_texture_coord (&indices, glyph->p2.x, glyph->p1.y);
		_cairo_gl_tristrip_indices_add_mask_texture_coord (&indices, glyph->p2.x, glyph->p2.y);

		status = _cairo_gl_tristrip_indices_add_quad (&indices, points);
		if(unlikely(status))
		{
			_cairo_gl_glyph_cache_unlock(cache);
			_cairo_gl_composite_fini(&setup);
			//scaled_font->options.antialias = current_antialias;
			_cairo_scaled_font_thaw_cache(scaled_font);
			if(font_match == FALSE)
				_cairo_scaled_font_thaw_cache(max_font);
				//cairo_scaled_font_destroy(max_font);
			if(min_font)
			{
				_cairo_scaled_font_thaw_cache(min_font);
				cairo_scaled_font_destroy(min_font);
			}
			_cairo_gl_tristrip_indices_destroy (&indices);
			//glDisable(GL_STENCIL_TEST);
			//glDisable(GL_DEPTH_TEST);
			//glDepthMask(GL_FALSE);
			status = _cairo_gl_context_release(ctx, status);
			*remaining_glyphs = num_glyphs - i;

			status = CAIRO_INT_STATUS_UNSUPPORTED;
			return status;
		}
		_cairo_gl_glyph_cache_unlock(cache);
	}

	//printf("\tgenerate max font takes %ld usec, min font takes %ld usec\n", max_time, min_time);

	status = _cairo_gl_fill(&indices);

	//printf("done text rendering\n");
	//long stop  = _get_tick();
	//printf("get glyphs and draw takes %ld usec for %0.2f size\n\n", stop - middle, 		scaled_font->font_matrix.xx);

	_cairo_gl_composite_fini(&setup);
	//scaled_font->options.antialias = current_antialias;
	_cairo_scaled_font_thaw_cache(scaled_font);
	if(font_match == FALSE)
		_cairo_scaled_font_thaw_cache(max_font);
		//cairo_scaled_font_destroy(max_font);
	if(min_font)
	{
		_cairo_scaled_font_thaw_cache(min_font);
		cairo_scaled_font_destroy(min_font);
	}
	_cairo_gl_tristrip_indices_destroy (&indices);
	//glDisable(GL_STENCIL_TEST);
	//glDisable(GL_DEPTH_TEST);
	//glDepthMask(GL_FALSE);
	status = _cairo_gl_context_release(ctx, status);
	*remaining_glyphs = 0;
	//cairo_surface_write_to_png(dst, "/mnt/ums/test/test.png");

	/*if(dst->needs_extend == TRUE)
	{
		if(_cairo_surface_has_snapshot(&(dst->base),
			&_cairo_gl_surface_backend))
			_cairo_surface_detach_snapshot(&(dst->base));
	}*/

	//dst->needs_new_data_surface = TRUE;
	//long then = _get_tick() - now;
	//printf("render glyphs takes %ld usec\n", then);
	status = CAIRO_STATUS_SUCCESS;
	return status;
}
		


static cairo_int_status_t
_cairo_gl_surface_show_glyphs_via_mask (cairo_gl_surface_t	*dst,
					int dst_width, int dst_height,
			                cairo_operator_t	 op,
					const cairo_pattern_t	*source,
					cairo_glyph_t		*glyphs,
					int			 num_glyphs,
					const cairo_rectangle_int_t *glyph_extents,
					cairo_scaled_font_t	*scaled_font,
					cairo_clip_t		*clip,
					int			*remaining_glyphs)
{
    //cairo_surface_t *mask;
    cairo_status_t status;
    cairo_bool_t has_component_alpha;
    int i;
    cairo_gl_surface_t *mask;

    /* XXX: For non-CA, this should be CAIRO_CONTENT_ALPHA to save memory */
	
    //mask = cairo_gl_surface_create (dst->base.device,
    //                                CAIRO_CONTENT_COLOR_ALPHA,
    //                                //glyph_extents->width,
    //                                //glyph_extents->height);
	//								480, 800);
    //if (unlikely (mask->status))
    //    return mask->status;
	/*
    for (i = 0; i < num_glyphs; i++) 
	{
		glyphs[i].x -= glyph_extents->x;
		glyphs[i].y -= glyph_extents->y;
    }
	*/
	if(dst_width <= 0 || dst_height <= 0)
		return CAIRO_INT_STATUS_UNSUPPORTED;

	//long now =_get_tick();
    //printf("render glyphs via mask\n");
	status = _render_glyphs (dst, dst_width, dst_height, 0, 0, FALSE,
	                     CAIRO_OPERATOR_OVER,
			     //&_cairo_pattern_white.base,
				 		source,
	                     glyphs, num_glyphs, glyph_extents,
			     scaled_font, &has_component_alpha, &mask,
			     NULL, remaining_glyphs);
	//long mid = _get_tick();
	//printf("render glyphs takes %ld usec\n", mid - now);
    if (likely (status == CAIRO_STATUS_SUCCESS)) {
	cairo_surface_pattern_t mask_pattern;

	
        mask->base.is_clear = FALSE;
	_cairo_pattern_init_for_surface (&mask_pattern, (cairo_surface_t*)mask);
	//mask_pattern.base.has_component_alpha = has_component_alpha;
	mask_pattern.base.has_component_alpha = FALSE;
	//cairo_matrix_init_translate (&mask_pattern.base.matrix,
	//	                     -glyph_extents->x, -glyph_extents->y);
	//cairo_surface_write_to_png(mask, "/home/me/openvg/pc/test.png");
	//status = _cairo_surface_mask (&dst->base, op,
	//	                      source, &mask_pattern.base, clip);
	if(op == CAIRO_OPERATOR_SOURCE)
		op = CAIRO_OPERATOR_OVER;
	status = _cairo_surface_paint (&dst->base, op,
		                      &(mask_pattern.base), clip);
	_cairo_pattern_fini (&mask_pattern.base);
	//long then = _get_tick();
	//printf("via mask takes %ld usec\n", then - now);
    } else {
	for (i = 0; i < num_glyphs; i++) {
	    glyphs[i].x += glyph_extents->x;
	    glyphs[i].y += glyph_extents->y;
	}
	*remaining_glyphs = num_glyphs;
    }

    //cairo_surface_destroy (mask);
    //if(clip != NULL && clip->path != NULL)
    //    printf("glyph clip path ref count %d\n", clip->path->ref_count);

    return status;
}

cairo_int_status_t
_cairo_gl_surface_show_glyphs (void			*abstract_dst,
			       cairo_operator_t		 op,
			       const cairo_pattern_t	*source,
			       cairo_glyph_t		*glyphs,
			       int			 num_glyphs,
			       cairo_scaled_font_t	*scaled_font,
			       cairo_clip_t		*clip,
			       int			*remaining_glyphs)
{
    cairo_gl_surface_t *dst = abstract_dst;
    cairo_rectangle_int_t surface_extents;
    cairo_rectangle_int_t extents, *clipped_extents, intersect_extents;
	cairo_rectangle_int_t surface_intersect_extents;
    //cairo_region_t *clip_region = NULL;
    cairo_bool_t overlap;
    //cairo_bool_t use_mask = FALSE;
    cairo_bool_t has_component_alpha;
    cairo_status_t status;
    //int i;
    cairo_gl_surface_t *null_mask = NULL;
    cairo_bool_t needs_clip = FALSE;

	if(num_glyphs == 0)
		return CAIRO_STATUS_SUCCESS;

    if (! _cairo_gl_operator_is_supported (op))
	return UNSUPPORTED ("unsupported operator");

	dst->require_aa = 0;
    
	/* XXX we don't need ownership of the font as we use a global
     * glyph cache -- but we do need scaled_glyph eviction notification. :-(
     */
    //if (! _cairo_gl_surface_owns_font (dst, scaled_font))
	//return UNSUPPORTED ("do not control font");

    /* If the glyphs overlap, we need to build an intermediate mask rather
     * then perform the compositing directly.
     */
    status = _cairo_scaled_font_glyph_device_extents (scaled_font,
						      glyphs, num_glyphs,
						      &extents,
						      &overlap);
    if (unlikely (status))
		return status;
	intersect_extents.x = surface_intersect_extents.x = extents.x;
	intersect_extents.y = surface_intersect_extents.y = extents.y;
	intersect_extents.width = surface_intersect_extents.width = extents.width;
	intersect_extents.height = surface_intersect_extents.height = extents.height;

    //if (! _cairo_operator_bounded_by_mask (op))
	//use_mask |= TRUE;

    /* If any of the glyphs are component alpha, we have to go through a mask,
     * since only _cairo_gl_surface_composite() currently supports component
     * alpha.
     */
	/*
    if (!use_mask && op != CAIRO_OPERATOR_OVER) {
	for (i = 0; i < num_glyphs; i++) {
	    cairo_scaled_glyph_t *scaled_glyph;

	    status = _cairo_scaled_glyph_lookup (scaled_font,
						 glyphs[i].index,
						 CAIRO_SCALED_GLYPH_INFO_SURFACE,
						 &scaled_glyph);
	    if (!_cairo_status_is_error (status) &&
		scaled_glyph->surface->format == CAIRO_FORMAT_ARGB32)
	    {
		use_mask = TRUE;
		break;
	    }
	}
    }*/

    /* For CLEAR, cairo's rendering equation (quoting Owen's description in:
     * http://lists.cairographics.org/archives/cairo/2005-August/004992.html)
     * is:
     *     mask IN clip ? src OP dest : dest
     * or more simply:
     *     mask IN CLIP ? 0 : dest
     *
     * where the ternary operator A ? B : C is (A * B) + ((1 - A) * C).
     *
     * The model we use in _cairo_gl_set_operator() is Render's:
     *     src IN mask IN clip OP dest
     * which would boil down to:
     *     0 (bounded by the extents of the drawing).
     *
     * However, we can do a Render operation using an opaque source
     * and DEST_OUT to produce:
     *    1 IN mask IN clip DEST_OUT dest
     * which is
     *    mask IN clip ? 0 : dest
     */
    if (op == CAIRO_OPERATOR_CLEAR) 
	{
		source = &_cairo_pattern_white.base;
		op = CAIRO_OPERATOR_DEST_OUT;
	}

    /* For SOURCE, cairo's rendering equation is:
     *     (mask IN clip) ? src OP dest : dest
     * or more simply:
     *     (mask IN clip) ? src : dest.
     *
     * If we just used the Render equation, we would get:
     *     (src IN mask IN clip) OP dest
     * or:
     *     (src IN mask IN clip) bounded by extents of the drawing.
     *
     * The trick is that for GL blending, we only get our 4 source values
     * into the blender, and since we need all 4 components of source, we
     * can't also get the mask IN clip into the blender.  But if we did
     * two passes we could make it work:
     *     dest = (mask IN clip) DEST_OUT dest
     *     dest = src IN mask IN clip ADD dest
     *
     * But for now, composite via an intermediate mask.
     */
    //if (op == CAIRO_OPERATOR_SOURCE)
	//use_mask |= TRUE;

    /* XXX we don't need ownership of the font as we use a global
     * glyph cache -- but we do need scaled_glyph eviction notification. :-(
     */
    //if (! _cairo_gl_surface_owns_font (dst, scaled_font))
	//	return UNSUPPORTED ("do not control font");

    /* If the glyphs overlap, we need to build an intermediate mask rather
     * then perform the compositing directly.
     */
    /*status = _cairo_scaled_font_glyph_device_extents (scaled_font,
						      glyphs, num_glyphs,
						      &extents,
						      &overlap);
    if (unlikely (status))
		return status;
	*/

    //use_mask |= overlap;

	
    //if (clip != NULL) {
	//status = _cairo_clip_get_region (clip, &clip_region);
	/* the empty clip should never be propagated this far */
	/*
	assert (status != CAIRO_INT_STATUS_NOTHING_TO_DO);
	if (unlikely (_cairo_status_is_error (status)))
	    return status;

	use_mask |= status == CAIRO_INT_STATUS_UNSUPPORTED;

	if (! _cairo_rectangle_intersect (&extents,
		                          _cairo_clip_get_extents (clip)))
	    goto EMPTY;
    }
	*/
	if(clip != NULL)
	{
		clipped_extents = _cairo_clip_get_extents(clip);
		if (! _cairo_rectangle_intersect (&intersect_extents,
		                          clipped_extents))
	    	goto EMPTY;
	}

    surface_extents.x = surface_extents.y = 0;
    surface_extents.width = dst->width;
    surface_extents.height = dst->height;
    if (! _cairo_rectangle_intersect (&surface_intersect_extents, &surface_extents))
	goto EMPTY;
	
	if(clip != NULL)
	{
		if(clipped_extents->x > extents.x || clipped_extents->y > extents.y ||
			clipped_extents->width + clipped_extents->x < extents.width + extents.x || 
			clipped_extents->height + clipped_extents->y < extents.height + extents.y)
			needs_clip = TRUE;
	}
	
    //if (use_mask) {
	if(needs_clip == TRUE)
	{
	//	dst->needs_super_sampling = TRUE;
	}
	if(op != CAIRO_OPERATOR_OVER || needs_clip == TRUE)
	//if(op != CAIRO_OPERATOR_OVER || clip != NULL)
		return _cairo_gl_surface_show_glyphs_via_mask (dst, dst->width,
											dst->height, op,
			                               source,
			                               glyphs, num_glyphs,
						       &extents,
						       scaled_font,
						       clip,
						       remaining_glyphs);
	
    //}
	else
	{
		//printf("call render glyphs directory\n");
   	 	return _render_glyphs (dst, dst->width, dst->height, 
				extents.x, extents.y,
	            TRUE, op, source,
			   glyphs, num_glyphs, &extents,
			   scaled_font, &has_component_alpha,
			   &null_mask,
			   NULL, remaining_glyphs);
	}
	
EMPTY:
    *remaining_glyphs = 0;
	/*
    if (! _cairo_operator_bounded_by_mask (op))
	return _cairo_surface_paint (&dst->base, op, source, clip);
    else*/
	return CAIRO_STATUS_SUCCESS;
}

void
_cairo_gl_glyph_cache_init (cairo_gl_glyph_cache_t *cache)
{
    _cairo_rtree_init (&cache->rtree,
		       GLYPH_CACHE_WIDTH,
		       GLYPH_CACHE_HEIGHT,
		       GLYPH_CACHE_MIN_SIZE,
		       sizeof (cairo_gl_glyph_private_t));
}

void
_cairo_gl_glyph_cache_fini (cairo_gl_context_t *ctx,
			    cairo_gl_glyph_cache_t *cache)
{
    _cairo_rtree_fini (&cache->rtree);

    if (cache->pattern.surface) {
        _cairo_pattern_fini (&cache->pattern.base);
        cache->pattern.surface = NULL;
    }
}

//#if 0
void cairo_gl_font_extents(cairo_t *cr, cairo_font_extents_t *extents)
{
	// we get the 
	cairo_scaled_font_t *max_font = NULL;
	cairo_scaled_font_t *min_font = NULL;
        cairo_scaled_font_t *scaled_font = NULL;
	int m;
        double font_scale;
        double default_font_scale = FONT_STANDARD_SIZE;
        cairo_bool_t font_match = FALSE;
        cairo_matrix_t max_matrix, min_matrix;
        int max_font_size_interval = 512 / FONT_STANDARD_SIZE;
        cairo_font_extents_t max_extents, min_extents;

	cairo_font_extents(cr, extents);
	if(unlikely(cr->status))
		return;
	
	// get the scaled font
	scaled_font = cairo_get_scaled_font(cr);
	if(unlikely(scaled_font->status))
	{
		_cairo_status_set_error(&cr->status, _cairo_error(scaled_font->status));
		return;
	}

	font_scale = scaled_font->font_matrix.xx;
	
	if(FONT_SIZE_SMOOTH != 1)
	{
		// we are in rotate mode
		cairo_matrix_init_scale(&max_matrix, 
			scaled_font->font_matrix.xx,
			scaled_font->font_matrix.yy);
		max_font = cairo_scaled_font_create(scaled_font->font_face,
			&max_matrix, &scaled_font->ctm,
			&scaled_font->options);
		min_font = NULL;
		font_match = TRUE;
		default_font_scale = font_scale;
	}
	else
	{
		for(m = 1; m <= max_font_size_interval; m++)
		{
			if(scaled_font->font_matrix.xx <= m * FONT_STANDARD_SIZE)
			{
				cairo_matrix_init_scale(&max_matrix, 
					m * FONT_STANDARD_SIZE, m * FONT_STANDARD_SIZE);
				max_font = cairo_scaled_font_create(scaled_font->font_face,
					&max_matrix, &scaled_font->ctm,
					&scaled_font->options);
				if(m != 1 && scaled_font->font_matrix.xx != m * FONT_STANDARD_SIZE)
				{
					cairo_matrix_init_scale(&min_matrix, 
						(m-1) * FONT_STANDARD_SIZE, (m-1) * FONT_STANDARD_SIZE);
					min_font = cairo_scaled_font_create(scaled_font->font_face,
						&min_matrix, &scaled_font->ctm,
						&scaled_font->options);
					font_scale = scaled_font->font_matrix.xx - min_font->font_matrix.xx;
				}
				if(scaled_font->font_matrix.xx == max_font->font_matrix.xx)
				{
					font_match = TRUE;
					default_font_scale = font_scale;
				}
				break;
			}
		}
	}

	if(max_font == NULL)
	{
		if(min_font != NULL)
			cairo_scaled_font_destroy(min_font);
		return;
	}
	if(font_match == TRUE)
	{
		cairo_scaled_font_destroy(max_font);
		if(min_font != NULL)
			cairo_scaled_font_destroy(min_font);
		return;
	}

	// we need to get surface for min/max font
	/*if(font_match == FALSE)
    	_cairo_scaled_font_freeze_cache (max_font);
	if(min_font)
		_cairo_scaled_font_freeze_cache(min_font);
	*/
	
	min_extents.ascent = 0;
	min_extents.descent = 0;
	min_extents.height = 0;
	min_extents.max_x_advance = 0;
	min_extents.max_y_advance = 0;

	cairo_scaled_font_extents(max_font, &max_extents);
	if(unlikely(cairo_scaled_font_status(max_font)))
	{
		cairo_scaled_font_destroy(max_font);
		if(min_font != NULL)
		{
			cairo_scaled_font_destroy(min_font);
		}
		return;
	}
	if(min_font != NULL)
	{
		cairo_scaled_font_extents(min_font, &min_extents);
		if(unlikely(cairo_scaled_font_status(min_font)))
		{
			cairo_scaled_font_destroy(max_font);
			cairo_scaled_font_destroy(min_font);
		}
		return;
	}

	extents->ascent = (max_extents.ascent - min_extents.ascent) * font_scale/default_font_scale + min_extents.ascent;
	extents->descent = (max_extents.descent - min_extents.descent) * font_scale/default_font_scale + min_extents.descent;
	extents->height = (max_extents.height - min_extents.height) * font_scale/default_font_scale + min_extents.height;
	extents->max_x_advance = (max_extents.max_x_advance - min_extents.max_x_advance) * font_scale / default_font_scale + min_extents.max_x_advance;
	extents->max_y_advance = (max_extents.max_y_advance - min_extents.max_y_advance) * font_scale / default_font_scale + min_extents.max_y_advance;

	cairo_scaled_font_destroy(max_font);
	if(min_font)
	{
		cairo_scaled_font_destroy(min_font);
	}
}
//#endif

/*void cairo_gl_glyph_extents(cairo_t *cr, cairo_glyph_t *glyphs,
	int num_glyphs,
	cairo_text_extents_t *extents)
{
	cairo_scaled_font_t *max_font = NULL;
	cairo_scaled_font_t *min_font = NULL;
	int m;
	cairo_glyph_extents(ct, glyphs, num_glyphs, extents);
	if(unlikely(cr->status))
		return;
	
	// get the scaled font
	cairo_scaled_font_t *scaled_font = cr->gstate->scaled_font;

	double font_scale = scaled_font->font_matrix.xx;
	double default_font_scale = FONT_STANDARD_SIZE;
	double font_scale = default_font_scale;
	cairo_bool_t font_match = FALSE;
	double x, y;

	for(m = 1; m <= max_font_size_interval; m++)
	{
		if(scaled_font->font_matrix.xx <= m * FONT_STANDARD_SIZE)
		{
			cairo_matrix_init_scale(&max_matrix, 
				m * FONT_STANDARD_SIZE, m * FONT_STANDARD_SIZE);
			max_font = cairo_scaled_font_create(scaled_font->font_face,
				&max_matrix, &scaled_font->ctm,
				&scaled_font->options);
			if(m != 1 && scaled_font->font_matrix.xx != m * FONT_STANDARD_SIZE)
			{
				cairo_matrix_init_scale(&min_matrix, 
					(m-1) * FONT_STANDARD_SIZE, (m-1) * FONT_STANDARD_SIZE);
				min_font = cairo_scaled_font_create(scaled_font->font_face,
					&min_matrix, &scaled_font->ctm,
					&scaled_font->options);
				font_scale = scaled_font->font_matrix.xx - min_font->font_matrix.xx;
			}
			if(scaled_font->font_matrix.xx == max_font->font_matrix.xx)
			{
				font_match = TRUE;
				default_font_scale = font_scale;
			}
			break;
		}
	}

	if(max_font == NULL)
	{
		if(min_font != NULL)
			cairo_font_destroy(min_font);
		return;
	}
	if(font_match == TRUE)
	{
		cairo_scaled_font_destroy(max_font);
		if(min_font != NULL)
			cairo_scaled_font_destroy(min_font);
		return;
	}

		status = CAIRO_INT_STATUS_UNSUPPORTED;
		return status;
	}
	// we need to get surface for min/max font

	cairo_status_t status;
	cairo_text_extents_t max_extents, min_extents;
	min_extents.x_bearing = 0;
	min_extents.y_bearing = 0;
	min_extents.width = 0;
	min_extents.height = 0;
	min_extents.x_advance = 0;
	min_extents.y_advance = 0;

	cairo_scaled_font_glyph_extents(max_font, glyphs, num_glyphs,
		max_extents);
	if(unlikely(cairo_scaled_font_status(max_font)))
	{
		_cairo_scaled_font_destroy(max_font);
		if(min_font)
		{
			_cairo_scaled_font_destroy(min_font);
		}
		return;
	}
	if(min_font)
	{
		cairo_scaled_font_glyph_extents(min_font, glyphs, num_glyphs,
			min_extents);
		if(unlikely(cairo_scaled_font_status(min_font)))
		{
			_cairo_scaled_font_destroy(max_font);
			_cairo_scaled_font_destroy(min_font);
		}
		return;
	}

	extents->x_bearing = (max_extents.x_bearing - min_extents.x_bearing) * font_scale/default_font_scale + min_extents.x_bearing;
	extents->y_bearing = (max_extents.y_bearing - min_extents.y_bearing) * font_scale/default_font_scale + min_extents.y_bearing;
	extents->width = (max_exten

		cairo_glyph_free(max_glyphs);
*/
//#if 0
void cairo_gl_text_extents(cairo_t *cr, const char *utf8,
	cairo_text_extents_t *extents)
{
	cairo_scaled_font_t *max_font = NULL;
	cairo_scaled_font_t *min_font = NULL;
        cairo_scaled_font_t *scaled_font = NULL;
	int m;
        double font_scale;
        double default_font_scale = FONT_STANDARD_SIZE;
        cairo_bool_t font_match = FALSE;
        double x, y;
        int max_font_size_interval = 512 / FONT_STANDARD_SIZE;
        cairo_matrix_t max_matrix, min_matrix;
        cairo_status_t status;
        cairo_text_extents_t max_extents, min_extents;
        int num_glyphs;
        cairo_glyph_t *glyphs = NULL;

	cairo_text_extents(cr, utf8, extents);
	if(unlikely(cr->status))
		return;
	
	// get the scaled font
	scaled_font = cairo_get_scaled_font(cr);
	if(unlikely(scaled_font->status))
	{
		_cairo_status_set_error(&cr->status, _cairo_error(scaled_font->status));
		return;
	}

	font_scale = scaled_font->font_matrix.xx;
	//font_scale = default_font_scale;

	if(FONT_SIZE_SMOOTH != 1)
	{
		// we are in rotate mode
		cairo_matrix_init_scale(&max_matrix, 
			scaled_font->font_matrix.xx,
			scaled_font->font_matrix.yy);
		max_font = cairo_scaled_font_create(scaled_font->font_face,
			&max_matrix, &scaled_font->ctm,
			&scaled_font->options);
		min_font = NULL;
		font_match = TRUE;
		default_font_scale = font_scale;
	}
	else
	{
		for(m = 1; m <= max_font_size_interval; m++)
		{
			if(scaled_font->font_matrix.xx <= m * FONT_STANDARD_SIZE)
			{
				cairo_matrix_init_scale(&max_matrix, 
					m * FONT_STANDARD_SIZE, m * FONT_STANDARD_SIZE);
				max_font = cairo_scaled_font_create(scaled_font->font_face,
					&max_matrix, &scaled_font->ctm,
					&scaled_font->options);
				if(m != 1 && scaled_font->font_matrix.xx != m * FONT_STANDARD_SIZE)
				{
					cairo_matrix_init_scale(&min_matrix, 
						(m-1) * FONT_STANDARD_SIZE, (m-1) * FONT_STANDARD_SIZE);
					min_font = cairo_scaled_font_create(scaled_font->font_face,
						&min_matrix, &scaled_font->ctm,
						&scaled_font->options);
					font_scale = scaled_font->font_matrix.xx - min_font->font_matrix.xx;
				}
				if(scaled_font->font_matrix.xx == max_font->font_matrix.xx)
				{
					font_match = TRUE;
					default_font_scale = font_scale;
				}
				break;
			}
		}
	}

	if(max_font == NULL)
	{
		if(min_font != NULL)
			cairo_scaled_font_destroy(min_font);
		return;
	}
	if(font_match == TRUE)
	{
		cairo_scaled_font_destroy(max_font);
		if(min_font != NULL)
			cairo_scaled_font_destroy(min_font);
		return;
	}

	// we need to get surface for min/max font
	cairo_get_current_point(cr, &x, &y);

	min_extents.x_bearing = 0;
	min_extents.y_bearing = 0;
	min_extents.width = 0;
	min_extents.height = 0;
	min_extents.x_advance = 0;
	min_extents.y_advance = 0;

	status = cairo_scaled_font_text_to_glyphs(max_font, x, y, utf8,
		strlen(utf8), &glyphs, &num_glyphs, NULL, NULL, NULL);
	if(status != CAIRO_STATUS_SUCCESS)
	{
		if(glyphs != NULL)
			cairo_glyph_free(glyphs);
		cairo_scaled_font_destroy(max_font);
		if(min_font)
			cairo_scaled_font_destroy(min_font);
		return;
	}
	
	cairo_scaled_font_glyph_extents(max_font, glyphs, num_glyphs,
		&max_extents);
	if(unlikely(cairo_scaled_font_status(max_font)))
	{
		cairo_glyph_free(glyphs);
		cairo_scaled_font_destroy(max_font);
		if(min_font)
			cairo_scaled_font_destroy(min_font);
		return;
	}

	cairo_glyph_free(glyphs);
	glyphs = NULL;
	if(min_font)
	{
		status = cairo_scaled_font_text_to_glyphs(min_font, x, y, utf8,
		strlen(utf8), &glyphs, &num_glyphs, NULL, NULL, NULL);
		if(status != CAIRO_STATUS_SUCCESS)
		{
			if(glyphs != NULL)
				cairo_glyph_free(glyphs);
			cairo_scaled_font_destroy(max_font);
			cairo_scaled_font_destroy(min_font);
			return;
		}
	
		cairo_scaled_font_glyph_extents(min_font, glyphs, num_glyphs,
			&min_extents);
		if(unlikely(cairo_scaled_font_status(min_font)))
		{
			cairo_glyph_free(glyphs);
			cairo_scaled_font_destroy(max_font);
			cairo_scaled_font_destroy(min_font);
			return;
		}
	}

	// destroy font
	cairo_scaled_font_destroy(max_font);
	if(min_font)
		cairo_scaled_font_destroy(min_font);
	cairo_glyph_free(glyphs);
	extents->x_bearing = (max_extents.x_bearing - min_extents.x_bearing) * font_scale / default_font_scale + min_extents.x_bearing;
	extents->y_bearing = (max_extents.y_bearing - min_extents.y_bearing) * font_scale / default_font_scale + min_extents.y_bearing;
	extents->width = (max_extents.width - min_extents.width) * font_scale / default_font_scale + min_extents.width;
	extents->height = (max_extents.height - min_extents.height) * font_scale / default_font_scale + min_extents.height;
	extents->x_advance = (max_extents.x_advance - min_extents.x_advance) * font_scale / default_font_scale + min_extents.x_advance;
	extents->y_advance = (max_extents.y_advance - min_extents.y_advance) * font_scale / default_font_scale + min_extents.y_advance;
}

void cairo_gl_scaled_font_extents(cairo_scaled_font_t *scaled_font,
	cairo_font_extents_t *extents)
{
	// we get the 
	cairo_scaled_font_t *max_font = NULL;
	cairo_scaled_font_t *min_font = NULL;
	int m;
	double font_scale = scaled_font->font_matrix.xx;
	double default_font_scale = FONT_STANDARD_SIZE;
	//font_scale = default_font_scale;
	cairo_bool_t font_match = FALSE;
	cairo_matrix_t max_matrix, min_matrix;
	int max_font_size_interval = 512 / FONT_STANDARD_SIZE;
        cairo_font_extents_t max_extents, min_extents;

        cairo_scaled_font_extents(scaled_font, extents);
	if(FONT_SIZE_SMOOTH != 1)
	{
		// we are in rotate mode
		cairo_matrix_init_scale(&max_matrix, 
			scaled_font->font_matrix.xx,
			scaled_font->font_matrix.yy);
		max_font = cairo_scaled_font_create(scaled_font->font_face,
			&max_matrix, &scaled_font->ctm,
			&scaled_font->options);
		min_font = NULL;
		font_match = TRUE;
		default_font_scale = font_scale;
	}
	else
	{
		for(m = 1; m <= max_font_size_interval; m++)
		{
			if(scaled_font->font_matrix.xx <= m * FONT_STANDARD_SIZE)
			{
				cairo_matrix_init_scale(&max_matrix, 
					m * FONT_STANDARD_SIZE, m * FONT_STANDARD_SIZE);
				max_font = cairo_scaled_font_create(scaled_font->font_face,
					&max_matrix, &scaled_font->ctm,
					&scaled_font->options);
				if(m != 1 && scaled_font->font_matrix.xx != m * FONT_STANDARD_SIZE)
				{
					cairo_matrix_init_scale(&min_matrix, 
						(m-1) * FONT_STANDARD_SIZE, (m-1) * FONT_STANDARD_SIZE);
					min_font = cairo_scaled_font_create(scaled_font->font_face,
						&min_matrix, &scaled_font->ctm,
						&scaled_font->options);
					font_scale = scaled_font->font_matrix.xx - min_font->font_matrix.xx;
				}
				if(scaled_font->font_matrix.xx == max_font->font_matrix.xx)
				{
					font_match = TRUE;
					default_font_scale = font_scale;
				}
				break;
			}
		}
	}

	if(max_font == NULL)
	{
		if(min_font != NULL)
			cairo_scaled_font_destroy(min_font);
		return;
	}
	if(font_match == TRUE)
	{
		cairo_scaled_font_destroy(max_font);
		if(min_font != NULL)
			cairo_scaled_font_destroy(min_font);
		return;
	}

	// we need to get surface for min/max font
	/*if(font_match == FALSE)
    	_cairo_scaled_font_freeze_cache (max_font);
	if(min_font)
		_cairo_scaled_font_freeze_cache(min_font);
	*/
	
	min_extents.ascent = 0;
	min_extents.descent = 0;
	min_extents.height = 0;
	min_extents.max_x_advance = 0;
	min_extents.max_y_advance = 0;

	cairo_scaled_font_extents(max_font, &max_extents);
	if(unlikely(cairo_scaled_font_status(max_font)))
	{
		//_cairo_scaled_font_thaw_cache(max_font);
		cairo_scaled_font_destroy(max_font);
		if(min_font != NULL)
		{
			//_cairo_scaled_font_thaw_cache(min_font);
			cairo_scaled_font_destroy(min_font);
		}
		return;
	}
	if(min_font != NULL)
	{
		cairo_scaled_font_extents(min_font, &min_extents);
		if(unlikely(cairo_scaled_font_status(min_font)))
		{
			//_cairo_scaled_font_thaw_cache(max_font);
			cairo_scaled_font_destroy(max_font);
			//_cairo_scaled_font_thaw_cache(min_font);
			cairo_scaled_font_destroy(min_font);
		}
		return;
	}

	extents->ascent = (max_extents.ascent - min_extents.ascent) * font_scale/default_font_scale + min_extents.ascent;
	extents->descent = (max_extents.descent - min_extents.descent) * font_scale/default_font_scale + min_extents.descent;
	extents->height = (max_extents.height - min_extents.height) * font_scale/default_font_scale + min_extents.height;
	extents->max_x_advance = (max_extents.max_x_advance - min_extents.max_x_advance) * font_scale / default_font_scale + min_extents.max_x_advance;
	extents->max_y_advance = (max_extents.max_y_advance - min_extents.max_y_advance) * font_scale / default_font_scale + min_extents.max_y_advance;

	//_cairo_scaled_font_thaw_cache(max_font);
	cairo_scaled_font_destroy(max_font);
	if(min_font)
	{
		//_cairo_scaled_font_thaw_cache(min_font);
		cairo_scaled_font_destroy(min_font);
	}
}

//#endif
