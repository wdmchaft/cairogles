/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Eric Anholt
 * Copyright © 2009 Chris Wilson
 * Copyright © 2005,2010 Red Hat, Inc
 * Copyright © 2010 Linaro Limited
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
 *	Alexandros Frantzis <alexandros.frantzis@linaro.org>
 */

#include "cairoint.h"

#include "cairo-error-private.h"
#include "cairo-gl-private.h"

#define SAMPLE_SIZE 4
#include <sys/time.h>
static long _get_tick()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec * 1000000 + now.tv_usec;
}
	
static void
_gl_lock (void *device)
{
    cairo_gl_context_t *ctx = (cairo_gl_context_t *) device;

    ctx->acquire (ctx);
}

static void
_gl_unlock (void *device)
{
    cairo_gl_context_t *ctx = (cairo_gl_context_t *) device;

    ctx->release (ctx);
}

static cairo_status_t
_gl_flush (void *device)
{
    cairo_gl_context_t *ctx;
    cairo_status_t status;

    status = _cairo_gl_context_acquire (device, &ctx);
    if (unlikely (status))
        return status;

    _cairo_gl_composite_flush (ctx);

    _cairo_gl_context_destroy_operand (ctx, CAIRO_GL_TEX_SOURCE);
    _cairo_gl_context_destroy_operand (ctx, CAIRO_GL_TEX_MASK);

    if (ctx->clip_region) {
        cairo_region_destroy (ctx->clip_region);
        ctx->clip_region = NULL;
    }

    ctx->current_target = NULL;
    ctx->current_operator = -1;
    ctx->vertex_size = 0;
    ctx->pre_shader = NULL;
    _cairo_gl_set_shader (ctx, NULL);

    ctx->dispatch.BindBuffer (GL_ARRAY_BUFFER, 0);

    glDisable (GL_SCISSOR_TEST);
    glDisable (GL_BLEND);

    return _cairo_gl_context_release (ctx, status);
}

static void
_gl_finish (void *device)
{
    cairo_gl_context_t *ctx = device;

    _gl_lock (device);

    _cairo_cache_fini (&ctx->gradients);

    _cairo_gl_context_fini_shaders (ctx);

	//if(ctx->mask_surface != NULL)
	//	cairo_surface_destroy(&ctx->mask_surface->base);

    _gl_unlock (device);
}

static void
_gl_destroy (void *device)
{
    cairo_gl_context_t *ctx = device;
    cairo_scaled_font_t *scaled_font, *next_scaled_font;
    int n;

    ctx->acquire (ctx);

    cairo_list_foreach_entry_safe (scaled_font,
				   next_scaled_font,
				   cairo_scaled_font_t,
				   &ctx->fonts,
				   link)
    {
	_cairo_scaled_font_revoke_ownership (scaled_font);
    }

    for (n = 0; n < ARRAY_LENGTH (ctx->glyph_cache); n++)
	_cairo_gl_glyph_cache_fini (ctx, &ctx->glyph_cache[n]);

    cairo_region_destroy (ctx->clip_region);

    if (ctx->vb_mem)
	free (ctx->vb_mem);

    ctx->destroy (ctx);

    free (ctx);
}

static const cairo_device_backend_t _cairo_gl_device_backend = {
    CAIRO_DEVICE_TYPE_GL,

    _gl_lock,
    _gl_unlock,

    _gl_flush, /* flush */
    _gl_finish,
    _gl_destroy,
};

cairo_status_t
_cairo_gl_context_init (cairo_gl_context_t *ctx)
{
    cairo_status_t status;
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    int gl_version = _cairo_gl_get_version ();
    cairo_gl_flavor_t gl_flavor = _cairo_gl_get_flavor ();
    int n;

    ctx->bound_fb = 0;
    ctx->current_program = -1;
    ctx->active_texture = -9999;
    ctx->src_factor = -9999;
    ctx->dst_factor = -9999;
    ctx->stencil_test_enabled = FALSE;
    ctx->scissor_test_enabled = FALSE;
    ctx->blend_enabled = FALSE;
    ctx->multisample_enabled = FALSE;
    
    ctx->stencil_test_reset = TRUE;
    ctx->scissor_test_reset = TRUE;
    ctx->program_reset = TRUE;
    ctx->source_texture_attrib_reset = TRUE;
    ctx->mask_texture_attrib_reset = TRUE;
    ctx->vertex_attrib_reset = TRUE;
    
    _cairo_device_init (&ctx->base, &_cairo_gl_device_backend);

    memset (ctx->glyph_cache, 0, sizeof (ctx->glyph_cache));
    cairo_list_init (&ctx->fonts);

    /* Support only GL version >= 1.3 */
    if (gl_version < CAIRO_GL_VERSION_ENCODE (1, 3))
	return _cairo_error (CAIRO_STATUS_DEVICE_ERROR);

	/* check for multisampling */
	if (gl_flavor == CAIRO_GL_FLAVOR_DESKTOP) {
#if CAIRO_HAS_GL_SURFACE
	if(_cairo_gl_has_extension ("GL_ARB_framebuffer_object")) {
	    glGetIntegerv(GL_MAX_SAMPLES_EXT, &ctx->max_sample_size);
	    //ctx->msaa_extension = 1;
        //ctx->max_sample_size = 1;
	}
#endif
	}
	else if(gl_flavor == CAIRO_GL_FLAVOR_ES) {
#if CAIRO_HAS_GLESV2_SURFACE
	if(_cairo_gl_has_extension ("GL_IMG_multisampled_render_to_texture")) {
	    glGetIntegerv(GL_MAX_SAMPLES_IMG, &ctx->max_sample_size);
		//ctx->msaa-extension = 2;
        ctx->max_sample_size = 1;
	}
#endif
	}
	else {
	    ctx->max_sample_size = 0;
		//ctx->msaa_extension = 0;
	}

    /* Check for required extensions */
    if (gl_flavor == CAIRO_GL_FLAVOR_DESKTOP) {
	if (_cairo_gl_has_extension ("GL_ARB_texture_non_power_of_two"))
	{
	    ctx->tex_target = GL_TEXTURE_2D;
		ctx->standard_npot = TRUE;
	}
	else if (_cairo_gl_has_extension ("GL_ARB_texture_rectangle"))
	    ctx->tex_target = GL_TEXTURE_RECTANGLE;
	else
	    return _cairo_error (CAIRO_STATUS_DEVICE_ERROR);
    }
    else {
	// Henry Song
	//if (_cairo_gl_has_extension ("GL_OES_texture_npot"))
	if (_cairo_gl_has_extension ("GL_OES_texture_npot") || _cairo_gl_has_extension ("GL_IMG_texture_npot")) {
	    ctx->tex_target = GL_TEXTURE_2D;
		if(_cairo_gl_has_extension("GL_OES_texture_npot"))
			ctx->standard_npot = TRUE;
		else
			ctx->standard_npot = FALSE;
	}
	else
	    return _cairo_error (CAIRO_STATUS_DEVICE_ERROR);
	if(!_cairo_gl_has_extension("GL_OES_packed_depth_stencil"))
		return _cairo_error(CAIRO_STATUS_DEVICE_ERROR);
    }

    if (gl_flavor == CAIRO_GL_FLAVOR_DESKTOP &&
	gl_version < CAIRO_GL_VERSION_ENCODE (2, 1) &&
	! _cairo_gl_has_extension ("GL_ARB_pixel_buffer_object"))
	return _cairo_error (CAIRO_STATUS_DEVICE_ERROR);

    if (gl_flavor == CAIRO_GL_FLAVOR_ES &&
	! _cairo_gl_has_extension ("GL_EXT_texture_format_BGRA8888"))
	return _cairo_error (CAIRO_STATUS_DEVICE_ERROR);

    ctx->has_map_buffer = (gl_flavor == CAIRO_GL_FLAVOR_DESKTOP ||
			   (gl_flavor == CAIRO_GL_FLAVOR_ES &&
			    _cairo_gl_has_extension ("GL_OES_mapbuffer")));

    ctx->has_mesa_pack_invert =
	_cairo_gl_has_extension ("GL_MESA_pack_invert");

    ctx->current_operator = -1;
    ctx->gl_flavor = gl_flavor;

    status = _cairo_gl_context_init_shaders (ctx);
    if (unlikely (status))
        return status;

    status = _cairo_cache_init (&ctx->gradients,
                                _cairo_gl_gradient_equal,
                                NULL,
                                (cairo_destroy_func_t) _cairo_gl_gradient_destroy,
                                CAIRO_GL_GRADIENT_CACHE_SIZE);
    if (unlikely (status))
        return status;

    if (! ctx->has_map_buffer) {
	ctx->vb_mem = _cairo_malloc_ab (CAIRO_GL_VBO_SIZE, 1);
	if (unlikely (ctx->vb_mem == NULL)) {
	    _cairo_cache_fini (&ctx->gradients);
	    return _cairo_error (CAIRO_STATUS_NO_MEMORY);
	}
    }

    /* PBO for any sort of texture upload */
    dispatch->GenBuffers (1, &ctx->texture_load_pbo);
    dispatch->GenBuffers (1, &ctx->vbo);

    ctx->max_framebuffer_size = 0;
    glGetIntegerv (GL_MAX_RENDERBUFFER_SIZE, &ctx->max_framebuffer_size);
    ctx->max_texture_size = 0;
    glGetIntegerv (GL_MAX_TEXTURE_SIZE, &ctx->max_texture_size);
    ctx->max_textures = 0;
    glGetIntegerv (GL_MAX_TEXTURE_IMAGE_UNITS, &ctx->max_textures);

    for (n = 0; n < ARRAY_LENGTH (ctx->glyph_cache); n++)
	_cairo_gl_glyph_cache_init (&ctx->glyph_cache[n]);

    ctx->force_non_msaa = FALSE;
    return CAIRO_STATUS_SUCCESS;
}

void
_cairo_gl_context_activate (cairo_gl_context_t *ctx,
                            cairo_gl_tex_t      tex_unit)
{
    if (ctx->max_textures <= (GLint) tex_unit) {
        if (tex_unit < 2) {
            _cairo_gl_composite_flush (ctx);
            _cairo_gl_context_destroy_operand (ctx, ctx->max_textures - 1);   
        }
        glActiveTexture (ctx->max_textures - 1);
    } else {
        glActiveTexture (GL_TEXTURE0 + tex_unit);
    }
}

static cairo_status_t _cairo_gl_check_framebuffer_status(cairo_gl_dispatch_t *dispatch)
{
	GLenum status;
    cairo_status_t state = CAIRO_STATUS_SUCCESS;
	status = dispatch->CheckFramebufferStatus (GL_FRAMEBUFFER);
 	if (status != GL_FRAMEBUFFER_COMPLETE) {
	const char *str;
	switch (status) {
	//case GL_FRAMEBUFFER_UNDEFINED: str= "undefined"; break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: str= "incomplete attachment"; break;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: str= "incomplete/missing attachment"; break;
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER: str= "incomplete draw buffer"; break;
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER: str= "incomplete read buffer"; break;
	case GL_FRAMEBUFFER_UNSUPPORTED: str= "unsupported"; break;
	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: str= "incomplete multiple"; break;
	default: str = "unknown error"; break;
	}

	fprintf (stderr,
		 "destination is framebuffer incomplete: %s [%#x]\n",
		 str, status);
    return CAIRO_STATUS_NO_MEMORY;
	}
    return state;
}

#if CAIRO_HAS_GL_SURFACE
static cairo_status_t
_cairo_gl_ensure_framebuffer_for_gl (cairo_gl_context_t *ctx,
                              cairo_gl_surface_t *surface)
{
    cairo_status_t status = CAIRO_STATUS_SUCCESS;
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
	int sample_size = ctx->max_sample_size > SAMPLE_SIZE ? SAMPLE_SIZE : ctx->max_sample_size;
	
    if(likely(surface->fb))
		return status;

    /* Create a framebuffer object wrapping the texture so that we can render
     * to it.
     */
	//GLenum err;

	// first create color renderbuffer
    if(sample_size > 1) {
        glEnable(GL_MULTISAMPLE);

	    dispatch->GenFramebuffers (1, &(surface->ms_fb));
	    dispatch->BindFramebuffer (GL_FRAMEBUFFER, surface->ms_fb);
        dispatch->GenRenderbuffers(1, &(surface->ms_rb));
	    dispatch->BindRenderbuffer(GL_RENDERBUFFER, surface->ms_rb);
	    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 
		                                 sample_size, 
                                         surface->internal_format,
		                                 surface->width, 
                                         surface->height);
	    dispatch->FramebufferRenderbuffer(GL_FRAMEBUFFER,
		                                  GL_COLOR_ATTACHMENT0, 
                                          GL_RENDERBUFFER, 
		                                  surface->ms_rb);
	    // create stencil buffer
        dispatch->GenRenderbuffers(1, &(surface->ms_stencil_rb));
	    dispatch->BindRenderbuffer(GL_RENDERBUFFER, surface->ms_stencil_rb);
	
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,
		                                 sample_size, 
                                         GL_DEPTH24_STENCIL8,
		                                 surface->width, 
                                         surface->height);

	    dispatch->FramebufferRenderbuffer(GL_FRAMEBUFFER,
		                                  GL_DEPTH_STENCIL_ATTACHMENT, 
                                          GL_RENDERBUFFER, 
                                          surface->ms_stencil_rb);
	// check status
	    status = _cairo_gl_check_framebuffer_status(dispatch);
        if (unlikely (status))
        return status;
    }
	// create single sample buffer
	dispatch->GenFramebuffers (1, &(surface->fb));
    dispatch->BindFramebuffer(GL_FRAMEBUFFER, surface->fb);
	
    dispatch->FramebufferTexture2D (GL_FRAMEBUFFER,
	                                GL_COLOR_ATTACHMENT0,
	                                ctx->tex_target,
	                                surface->tex,
	                                0);
    dispatch->GenRenderbuffers(1, &(surface->rb));
	dispatch->BindRenderbuffer(GL_RENDERBUFFER, surface->rb);
	glRenderbufferStorage(GL_RENDERBUFFER,
		                  GL_DEPTH24_STENCIL8,
		                  surface->width, 
                          surface->height);
	dispatch->FramebufferRenderbuffer(GL_FRAMEBUFFER,
		                              GL_DEPTH_ATTACHMENT, 
                                      GL_RENDERBUFFER, 
                                      surface->rb);
	dispatch->FramebufferRenderbuffer(GL_FRAMEBUFFER,
		                              GL_STENCIL_ATTACHMENT, 
                                      GL_RENDERBUFFER, surface->rb);
	// check status
	status = _cairo_gl_check_framebuffer_status(dispatch);
   
    return status;
}
#endif

#if CAIRO_HAS_GLESV2_SURFACE
static cairo_status_t
_cairo_gl_ensure_framebuffer_for_gles (cairo_gl_context_t *ctx,
                              cairo_gl_surface_t *surface)
{
    cairo_status_t status = CAIRO_STATUS_SUCCESS;
/*    cairo_status_t status = CAIROSTATUS_SUCCESS;
    PFNGLRENDERBUFFERSTORAGEMULTISAMPLEIMGPROC
        pglRenderbufferStorageMultisampleIMG;
    PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEIMGPROC 
        pglFramebufferTexture2DMultisampleIMG;
*/    
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
	int sample_size = ctx->max_sample_size > SAMPLE_SIZE ? SAMPLE_SIZE : ctx->max_sample_size;
	
    if(likely(surface->fb))
		return status;

    /* Create a framebuffer object wrapping the texture so that we can render
     * to it.
     */
	//GLenum err;

	// first create color renderbuffer
  /*  if(sample_size > 1) {
	    dispatch->GenFramebuffers (1, &(surface->fb));
	    dispatch->BindFramebuffer (GL_FRAMEBUFFER, surface->fb);
        pglFramebufferTexture2DMultisampleIMG = 
            (PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEIMGPROC)
            eglGetProcAddress("glFramebufferTexture2DMultisampleIMG");
        if (!pglFramebufferTexture2DMultisampleIMG)
            ctx->force_non_msaa = TRUE;
        else
	        pglFramebufferTexture2DMultisampleIMG(GL_FRAMEBUFFER,
		                                          GL_COLOR_ATTACHMENT0, 
                                                  ctx->tex_target, 
		                                          surface->tex, 
                                                  0, 
                                                  sample_size);
        pglRenderbufferStorageMultisampleIMG =
            (PFNGLRENDERBUFFERSTORAGEMULTISAMPLEIMGPROC)
            eglGetProcAddress("glRenderbufferStorageMultisampleIMG");
        if(!pglRenderbufferStorageMultisampleIMG)
            ctx->force_non_msaa = TRUE;
        else {
            dispatch->GenRenderbuffers(1, &(surface->rb));
	        dispatch->BindRenderbuffer(GL_RENDERBUFFER, 
                                       surface->rb);
	        pglRenderbufferStorageMultisampleIMG(GL_RENDERBUFFER,
		                                         sample_size,
		                                         GL_DEPTH24_STENCIL8_OES, 
		                                         surface->width, 
                                                 surface->height);
	        dispatch->FramebufferRenderbuffer(GL_FRAMEBUFFER,
		                                      GL_DEPTH_ATTACHMENT, 
                                              GL_RENDERBUFFER, 
                                              surface->rb);
	        dispatch->FramebufferRenderbuffer(GL_FRAMEBUFFER,
		                                      GL_STENCIL_ATTACHMENT, 
                                              GL_RENDERBUFFER, 
                                              surface->rb);
        }
    }
    else*/ {
	// create single sample buffer
	    dispatch->GenFramebuffers (1, &(surface->fb));
        dispatch->BindFramebuffer(GL_FRAMEBUFFER, surface->fb);
	
        dispatch->FramebufferTexture2D (GL_FRAMEBUFFER,
	                                    GL_COLOR_ATTACHMENT0,
	                                    ctx->tex_target,
	                                    surface->tex,
	                                    0);
        dispatch->GenRenderbuffers(1, &(surface->rb));
	    dispatch->BindRenderbuffer(GL_RENDERBUFFER, surface->rb);
	    glRenderbufferStorage(GL_RENDERBUFFER,
		                      GL_DEPTH24_STENCIL8_OES,
		                      surface->width, 
                           surface->height);
	    dispatch->FramebufferRenderbuffer(GL_FRAMEBUFFER,
		                                  GL_DEPTH_ATTACHMENT, 
                                          GL_RENDERBUFFER, 
                                          surface->rb);
	    dispatch->FramebufferRenderbuffer(GL_FRAMEBUFFER,
		                                  GL_STENCIL_ATTACHMENT, 
                                          GL_RENDERBUFFER, surface->rb);
    }
	// check status
	status = _cairo_gl_check_framebuffer_status(dispatch);
    return status;
}
#endif

cairo_status_t
_cairo_gl_ensure_framebuffer (cairo_gl_context_t *ctx,
                              cairo_gl_surface_t *surface)
{
#if CAIRO_HAS_GLESV2_SURFACE
    return _cairo_gl_ensure_framebuffer_for_gles (ctx, surface);
#elif CAIRO_HAS_GL_SURFACE
    return _cairo_gl_ensure_framebuffer_for_gl (ctx, surface);
#endif
}



/*
 * Stores a parallel projection transformation in matrix 'm',
 * using column-major order.
 *
 * This is equivalent to:
 *
 * glLoadIdentity()
 * gluOrtho2D()
 *
 * The calculation for the ortho tranformation was taken from the
 * mesa source code.
 */
static void
_gl_identity_ortho (GLfloat *m,
		    GLfloat left, GLfloat right,
		    GLfloat bottom, GLfloat top)
{
#define M(row,col)  m[col*4+row]
    M(0,0) = 2.f / (right - left);
    M(0,1) = 0.f;
    M(0,2) = 0.f;
    M(0,3) = -(right + left) / (right - left);

    M(1,0) = 0.f;
    M(1,1) = 2.f / (top - bottom);
    M(1,2) = 0.f;
    M(1,3) = -(top + bottom) / (top - bottom);

    M(2,0) = 0.f;
    M(2,1) = 0.f;
    M(2,2) = -1.f;
    M(2,3) = 0.f;

    M(3,0) = 0.f;
    M(3,1) = 0.f;
    M(3,2) = 0.f;
    M(3,3) = 1.f;
#undef M
}

#if CAIRO_HAS_GL_SURFACE
void
_cairo_gl_context_set_destination_for_gl (cairo_gl_context_t *ctx,
                                   cairo_gl_surface_t *surface)
{
    int sample_size = ctx->max_sample_size > SAMPLE_SIZE ? SAMPLE_SIZE : ctx->max_sample_size;
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    ctx->current_target = surface;
    surface->needs_update = FALSE;

    cairo_bool_t bounded = FALSE;
    //surface->require_aa = FALSE;
    if (ctx->force_non_msaa == TRUE)
        sample_size = 1;

    if (_cairo_gl_surface_is_texture (surface)) {
        // we ensure framebuffer and renderbuffer are created
        _cairo_gl_ensure_framebuffer (ctx, surface);
        if(surface->require_aa == TRUE && sample_size > 1)
            glEnable(GL_MULTISAMPLE);
        else
            glDisable(GL_MULTISAMPLE);
        
        if(surface->require_aa && sample_size > 1) {
            // set up draw buffer
            if(surface->multisample_resolved == TRUE) {
			    dispatch->BindFramebuffer (GL_READ_FRAMEBUFFER, 
                                       surface->fb);
			    dispatch->BindFramebuffer (GL_DRAW_FRAMEBUFFER, 
                                           surface->ms_fb);
			    glBlitFramebuffer(0, 0, surface->width, surface->height,
				                  0, 0, surface->width, surface->height, 
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
         
                surface->multisample_resolved = FALSE;
                ctx->bound_fb = surface->ms_fb;
            }
            else {
                if(ctx->bound_fb != surface->ms_fb)
                {
                    //printf("rebind multisample framebuffer 1\n");
			        dispatch->BindFramebuffer (GL_FRAMEBUFFER, 
                                               surface->ms_fb);
                }
                else {
                    bounded = TRUE;
                    //printf("bound multisample framebuffer 1\n");
                }
                surface->multisample_resolved = FALSE;
                ctx->bound_fb = surface->ms_fb;
            
            }
		}
		else {
            if(surface->multisample_resolved == FALSE)
                // we need to blit multisample renderbuffer to texture
                if(sample_size > 1)
                    _cairo_gl_context_blit_destination(ctx, surface);
			// we are using either non AA or multisampling is not there
            if(ctx->bound_fb != surface->fb)
                dispatch->BindFramebuffer(GL_FRAMEBUFFER, surface->fb);
            else
                bounded = TRUE;
            surface->multisample_resolved = TRUE;
            ctx->bound_fb = surface->fb;
		}
        if(bounded == FALSE) {
		    glDrawBuffer (GL_COLOR_ATTACHMENT0);
		    glReadBuffer (GL_COLOR_ATTACHMENT0);
        }
    } else {
        ctx->make_current (ctx, surface);
        surface->multisample_resolved = TRUE;
		if(surface->require_aa)
			glEnable(GL_MULTISAMPLE);
		else
			glDisable(GL_MULTISAMPLE);
        
        if(ctx->bound_fb != 0)
            ctx->dispatch.BindFramebuffer (GL_FRAMEBUFFER, 0);
        else
            bounded = TRUE;
        ctx->bound_fb = 0;
        if(bounded == FALSE) {
            glDrawBuffer (GL_BACK_LEFT);
            glReadBuffer (GL_BACK_LEFT);
        }
    }

    glViewport (0, 0, surface->width, surface->height);

    if (_cairo_gl_surface_is_texture (surface))
	_gl_identity_ortho (ctx->modelviewprojection_matrix,
			    0, surface->width, 0, surface->height);
    else
	_gl_identity_ortho (ctx->modelviewprojection_matrix,
			    0, surface->width, surface->height, 0);
}
#endif

#if CAIRO_HAS_GLESV2_SURFACE
void
_cairo_gl_context_set_destination_for_gles (cairo_gl_context_t *ctx,
                                   cairo_gl_surface_t *surface)
{
    int sample_size = ctx->max_sample_size > SAMPLE_SIZE ? SAMPLE_SIZE : ctx->max_sample_size;
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    ctx->current_target = surface;
    surface->needs_update = FALSE;
    cairo_bool_t bounded = FALSE;

    if (_cairo_gl_surface_is_texture (surface)) {
        // we ensure framebuffer and renderbuffer are created
        _cairo_gl_ensure_framebuffer (ctx, surface);
        surface->multisample_resolved = TRUE;
        if(ctx->bound_fb != surface->fb)
            dispatch->BindFramebuffer (GL_FRAMEBUFFER,
                                       surface->fb);
           
        ctx->bound_fb = surface->fb;
	}
    else {
        ctx->make_current (ctx, surface);
        surface->multisample_resolved = TRUE;
        
        if(ctx->bound_fb != 0)
            ctx->dispatch.BindFramebuffer (GL_FRAMEBUFFER, 0);
        else
            bounded = TRUE;
        ctx->bound_fb = 0;
    }

    glViewport (0, 0, surface->width, surface->height);

    if (_cairo_gl_surface_is_texture (surface))
	_gl_identity_ortho (ctx->modelviewprojection_matrix,
			    0, surface->width, 0, surface->height);
    else
	_gl_identity_ortho (ctx->modelviewprojection_matrix,
			    0, surface->width, surface->height, 0);
}
#endif

void
_cairo_gl_context_set_destination (cairo_gl_context_t *ctx,
                                   cairo_gl_surface_t *surface)
{
#if CAIRO_HAS_GL_SURFACE
    _cairo_gl_context_set_destination_for_gl (ctx, surface);
#elif CAIRO_HAS_GLESV2_SURFACE
    _cairo_gl_context_set_destination_for_gles (ctx, surface);
#endif
}


void 
_cairo_gl_context_blit_destination (cairo_gl_context_t *ctx,
                                   cairo_gl_surface_t *surface)
{
#if CAIRO_HAS_GL_SURFACE
   	cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
	int sample_size = ctx->max_sample_size > SAMPLE_SIZE ? 
                                             SAMPLE_SIZE : 
                                             ctx->max_sample_size;
    
    if (_cairo_gl_surface_is_texture (surface)) {
		if(sample_size > 1) {
		    // we ensure framebuffer and renderbuffer are created
	       	_cairo_gl_ensure_framebuffer (ctx, surface);
				dispatch->BindFramebuffer (GL_DRAW_FRAMEBUFFER, 
                                           surface->fb);
			dispatch->BindFramebuffer (GL_READ_FRAMEBUFFER, surface->ms_fb);
			glBlitFramebuffer(0, 0, surface->width, surface->height,
				              0, 0, surface->width, surface->height, 
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer (GL_FRAMEBUFFER, surface->fb);
            ctx->bound_fb = surface->fb;
		}
	}
#endif
    surface->multisample_resolved = TRUE;
	surface->require_aa = 0;
}
	

