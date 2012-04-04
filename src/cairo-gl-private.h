/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 Eric Anholt
 * Copyright © 2009 Chris Wilson
 * Copyright © 2005,2010 Red Hat, Inc
 * Copyright © 2011 Linaro Limited
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
 *	T. Zachary Laine <whatwasthataddress@gmail.com>
 *	Alexandros Frantzis <alexandros.frantzis@linaro.org>
 */

#ifndef CAIRO_GL_PRIVATE_H
#define CAIRO_GL_PRIVATE_H

#define GL_GLEXT_PROTOTYPES

#include "cairoint.h"

#include "cairo-gl.h"
#include "cairo-gl-gradient-private.h"

#include "cairo-device-private.h"
#include "cairo-error-private.h"
#include "cairo-rtree-private.h"
#include "cairo-scaled-font-private.h"
#include "cairo-spans-compositor-private.h"
#include "cairo-array-private.h"
#include "cairo-stroke-dash-private.h"

#include <assert.h>

#if CAIRO_HAS_GL_SURFACE
#include <GL/gl.h>
#include <GL/glext.h>
#elif CAIRO_HAS_GLESV2_SURFACE
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

#include "cairo-gl-ext-def-private.h"

#define DEBUG_GL 0

#define CAIRO_GL_ENUM_UNINITIALIZED 0xFFFF

#if DEBUG_GL && __GNUC__
#define UNSUPPORTED(reason) ({ \
    fprintf (stderr, \
	     "cairo-gl: hit unsupported operation in %s(), line %d: %s\n", \
	     __FUNCTION__, __LINE__, reason); \
    CAIRO_INT_STATUS_UNSUPPORTED; \
})
#else
#define UNSUPPORTED(reason) CAIRO_INT_STATUS_UNSUPPORTED
#endif

#define CAIRO_GL_VERSION_ENCODE(major, minor) (	\
	  ((major) * 256)			\
	+ ((minor) *   1))

/* maximal number of shaders we keep in the cache.
 * Random number that is hopefully big enough to not cause many cache evictions. */
#define CAIRO_GL_MAX_SHADERS_PER_CONTEXT 64

/* VBO size that we allocate, smaller size means we gotta flush more often,
 * but larger means hogging more memory and can cause trouble for drivers
 * (especially on embedded devices). */
#define CAIRO_GL_VBO_SIZE (16*1024)

#define IMAGE_CACHE_WIDTH 2048
#define IMAGE_CACHE_HEIGHT 2048
#define IMAGE_CACHE_MIN_SIZE 1
#define IMAGE_CACHE_MAX_SIZE 256

typedef struct _cairo_gl_surface cairo_gl_surface_t;

/* GL flavor */
typedef enum cairo_gl_flavor {
    CAIRO_GL_FLAVOR_NONE = 0,
    CAIRO_GL_FLAVOR_DESKTOP = 1,
    CAIRO_GL_FLAVOR_ES = 2
} cairo_gl_flavor_t;

/* Shortcuts for shader uniform locations. Table filled lazily on first access.
 * Using these slots saves per-draw calls to glGetUniformLocation(). */
typedef enum cairo_gl_shader_slot_t {
    /* "ModelViewProjectionMatrix" */
    CAIRO_GL_SHADER_SLOT_MVPMAT = 0,
    /* "source_texdims" */
    CAIRO_GL_SHADER_SLOT_SOURCE_TEXDIMS,
    /* "source_constant" */
    CAIRO_GL_SHADER_SLOT_SOURCE_CONSTANT,
    /* "source_sampler" */
    CAIRO_GL_SHADER_SLOT_SOURCE_SAMPLER,
    /* "source_a" */
    CAIRO_GL_SHADER_SLOT_SOURCE_A,
    /* "source_circle_d" */
    CAIRO_GL_SHADER_SLOT_SOURCE_CIRCLE_D,
    /* "source_radius_0" */
    CAIRO_GL_SHADER_SLOT_SOURCE_RADIUS_0,
    /* "mask_texdims" */
    CAIRO_GL_SHADER_SLOT_MASK_TEXDIMS,
    /* "mask_constant" */
    CAIRO_GL_SHADER_SLOT_MASK_CONSTANT,
    /* "mask_sampler" */
    CAIRO_GL_SHADER_SLOT_MASK_SAMPLER,
    /* "mask_a" */
    CAIRO_GL_SHADER_SLOT_MASK_A,
    /* "mask_circle_d" */
    CAIRO_GL_SHADER_SLOT_MASK_CIRCLE_D,
    /* "mask_radius_0" */
    CAIRO_GL_SHADER_SLOT_MASK_RADIUS_0,

    CAIRO_GL_SHADER_SLOT_MAX

} cairo_gl_shader_slot_t;


/* Indices for vertex attributes used by BindAttribLocation etc */
enum {
    CAIRO_GL_VERTEX_ATTRIB_INDEX = 0,
    CAIRO_GL_COLOR_ATTRIB_INDEX  = 1,
    CAIRO_GL_COVERAGE_ATTRIB_INDEX  = 2,
    CAIRO_GL_TEXCOORD0_ATTRIB_INDEX = 3,
    CAIRO_GL_TEXCOORD1_ATTRIB_INDEX = 4,
    CAIRO_GL_START_COORD0_ATTRIB_INDEX = 5,
    CAIRO_GL_START_COORD1_ATTRIB_INDEX = 6,
    CAIRO_GL_STOP_COORD0_ATTRIB_INDEX = 7,
    CAIRO_GL_STOP_COORD1_ATTRIB_INDEX = 8
};

typedef enum cairo_gl_operand_type {
    CAIRO_GL_OPERAND_NONE,
    CAIRO_GL_OPERAND_CONSTANT,
    CAIRO_GL_OPERAND_TEXTURE,
    CAIRO_GL_OPERAND_LINEAR_GRADIENT,
    CAIRO_GL_OPERAND_RADIAL_GRADIENT_A0,
    CAIRO_GL_OPERAND_RADIAL_GRADIENT_NONE,
    CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT,

    CAIRO_GL_OPERAND_COUNT
} cairo_gl_operand_type_t;

typedef enum cairo_gl_draw_mode {
    CAIRO_GL_VERTEX,
    CAIRO_GL_LINE_STRIP,
    CAIRO_GL_LINES
} cairo_gl_draw_mode_t;

/* This union structure describes a potential source or mask operand to the
 * compositing equation.
 */
typedef struct cairo_gl_operand {
    cairo_gl_operand_type_t type;
    union {
	struct {
	    GLuint tex;
	    cairo_gl_surface_t *surface;
	    cairo_gl_surface_t *owns_surface;
	    cairo_surface_attributes_t attributes;
	    int texgen;
	    cairo_bool_t use_atlas;
	    cairo_extend_t extend;
	    struct { float x, y; } p1, p2;
	} texture;
	struct {
	    GLfloat color[4];
	    cairo_bool_t encode_as_attribute;
	} constant;
	struct {
	    cairo_gl_gradient_t *gradient;
	    cairo_matrix_t m;
	    cairo_circle_double_t circle_d;
	    double radius_0, a;
	    cairo_extend_t extend;
	    int texgen;
	} gradient;
    };
    unsigned int vertex_offset;
} cairo_gl_operand_t;

typedef struct cairo_gl_source {
    cairo_surface_t base;
    cairo_gl_operand_t operand;
} cairo_gl_source_t;

struct _cairo_gl_surface {
    cairo_surface_t base;
    cairo_gl_operand_t operand;

    int width, height;

    GLuint tex; /* GL texture object containing our data. */
    GLuint fb; /* GL framebuffer object wrapping our data. */
    GLuint depth_stencil; /* GL renderbuffer object for holding stencil buffer clip. */

#if CAIRO_HAS_GL_SURFACE
    GLuint msaa_rb; /* The ARB MSAA path uses a renderbuffer. */
    GLuint msaa_fb;
#endif
    GLuint msaa_depth_stencil;

    cairo_bool_t stencil_and_msaa_caps_initialized;
    cairo_bool_t supports_stencil; /* Stencil support for for non-texture surfaces. */
    cairo_bool_t supports_msaa;
    cairo_bool_t msaa_active; /* Whether the multisampling
			         framebuffer is active or not. */
    cairo_clip_t *clip_on_stencil_buffer;

    int owns_tex;
    cairo_bool_t needs_update;

    cairo_region_t *clip_region;
    GLuint bounded_tex;		/* bounded tex for non-texture surface */

    /* Indicate whether we need to cache it in image_cache. */
    cairo_bool_t needs_to_cache;
    /* Damage is too expensive to check, we use this flag. */
    cairo_bool_t content_changed;
};

typedef struct cairo_gl_glyph_cache {
    cairo_rtree_t rtree;
    cairo_gl_surface_t *surface;
} cairo_gl_glyph_cache_t;

typedef enum cairo_gl_tex {
    CAIRO_GL_TEX_SOURCE = 0,
    CAIRO_GL_TEX_MASK = 1,
    CAIRO_GL_TEX_TEMP = 2
} cairo_gl_tex_t;

typedef struct cairo_gl_shader {
    GLuint fragment_shader;
    GLuint program;
    /* Storage table for uniform locations.*/
    GLint uniforms[CAIRO_GL_SHADER_SLOT_MAX];
    /* Validity table for the above: initially set at 0, set to
     * nonzero once corresponding item in uniforms is looked up. */
    char uniforms_valid[CAIRO_GL_SHADER_SLOT_MAX];
} cairo_gl_shader_t;

typedef struct cairo_gl_image_cache {
    cairo_rtree_t rtree;
    cairo_gl_surface_t *surface;
} cairo_gl_image_cache_t;

typedef struct cairo_gl_image {
    cairo_rtree_node_t node;
    cairo_surface_t *original_surface;
    struct { float x, y; } p1, p2;
    cairo_gl_context_t *ctx;
    cairo_bool_t node_removed;
    cairo_bool_t user_data_removed;
} cairo_gl_image_t;

typedef enum cairo_gl_shader_in {
    CAIRO_GL_SHADER_IN_NORMAL,
    CAIRO_GL_SHADER_IN_CA_SOURCE,
    CAIRO_GL_SHADER_IN_CA_SOURCE_ALPHA,

    CAIRO_GL_SHADER_IN_COUNT
} cairo_gl_shader_in_t;


typedef struct _cairo_gl_hairline_closure
{
    cairo_gl_context_t *ctx;
    double tolerance;
    cairo_stroker_dash_t dash;
    cairo_matrix_t *ctm;
    cairo_matrix_t *ctm_inverse;
    cairo_point_t current_point;

    cairo_point_t stroke_first_point;  /* First stroke point at move_to. */
    double stroke_first_dx;
    double stroke_first_dy;
    cairo_bool_t stroke_first_capped;
    cairo_bool_t moved_to_stroke_first_point;

    cairo_line_cap_t cap_style;

    cairo_bool_t line_last_capped;

    cairo_point_t line_last_point;
    double line_last_dx;
    double line_last_dy;

    cairo_bool_t initialized;
} cairo_gl_hairline_closure_t;

typedef enum cairo_gl_var_type {
  CAIRO_GL_VAR_NONE,
  CAIRO_GL_VAR_TEXCOORDS,
  CAIRO_GL_VAR_TEXGEN,
  CAIRO_GL_VAR_COLOR,
} cairo_gl_var_type_t;

typedef enum cairo_gl_primitive_type {
    CAIRO_GL_PRIMITIVE_TYPE_TRIANGLES,
    CAIRO_GL_PRIMITIVE_TYPE_TRISTRIPS
} cairo_gl_primitive_type_t;

typedef void (*cairo_gl_emit_rect_t) (cairo_gl_context_t *ctx,
				      GLfloat x1, GLfloat y1,
				      GLfloat x2, GLfloat y2);

typedef void (*cairo_gl_emit_span_t) (cairo_gl_context_t *ctx,
				      GLfloat x1, GLfloat y1,
				      GLfloat x2, GLfloat y2,
				      uint8_t alpha);

typedef void (*cairo_gl_emit_glyph_t) (cairo_gl_context_t *ctx,
				       GLfloat x1, GLfloat y1,
				       GLfloat x2, GLfloat y2,
				       GLfloat glyph_x1, GLfloat glyph_y1,
				       GLfloat glyph_x2, GLfloat glyph_y2);

#define cairo_gl_var_type_hash(src,mask,src_atlas_extend,mask_atlas_extend,src_use_atlas,mask_use_atlas,spans,dest) ((spans) << 11) | ((mask) << 9 | (src << 7) | (mask_atlas_extend << 5) | (src_atlas_extend << 3) | (mask_use_atlas << 2) | (src_use_atlas << 1) | (dest))
#define CAIRO_GL_VAR_TYPE_MAX (1 << 12)

typedef void (*cairo_gl_generic_func_t)(void);
typedef cairo_gl_generic_func_t (*cairo_gl_get_proc_addr_func_t)(const char *procname);

typedef struct _cairo_gl_dispatch {
    /* Buffers */
    void (*GenBuffers) (GLsizei n, GLuint *buffers);
    void (*BindBuffer) (GLenum target, GLuint buffer);
    void (*BufferData) (GLenum target, GLsizeiptr size,
			  const GLvoid* data, GLenum usage);
    GLvoid *(*MapBuffer) (GLenum target, GLenum access);
    GLboolean (*UnmapBuffer) (GLenum target);

    /* Shaders */
    GLuint (*CreateShader) (GLenum type);
    void (*ShaderSource) (GLuint shader, GLsizei count,
			    const GLchar** string, const GLint* length);
    void (*CompileShader) (GLuint shader);
    void (*GetShaderiv) (GLuint shader, GLenum pname, GLint *params);
    void (*GetShaderInfoLog) (GLuint shader, GLsizei bufSize,
				GLsizei *length, GLchar *infoLog);
    void (*DeleteShader) (GLuint shader);

    /* Programs */
    GLuint (*CreateProgram) (void);
    void (*AttachShader) (GLuint program, GLuint shader);
    void (*DeleteProgram) (GLuint program);
    void (*LinkProgram) (GLuint program);
    void (*UseProgram) (GLuint program);
    void (*GetProgramiv) (GLuint program, GLenum pname, GLint *params);
    void (*GetProgramInfoLog) (GLuint program, GLsizei bufSize,
				 GLsizei *length, GLchar *infoLog);

    /* Uniforms */
    GLint (*GetUniformLocation) (GLuint program, const GLchar* name);
    void (*Uniform1f) (GLint location, GLfloat x);
    void (*Uniform2f) (GLint location, GLfloat x, GLfloat y);
    void (*Uniform3f) (GLint location, GLfloat x, GLfloat y, GLfloat z);
    void (*Uniform4f) (GLint location, GLfloat x, GLfloat y, GLfloat z,
			 GLfloat w);
    void (*UniformMatrix3fv) (GLint location, GLsizei count,
				GLboolean transpose, const GLfloat *value);
    void (*UniformMatrix4fv) (GLint location, GLsizei count,
			      GLboolean transpose, const GLfloat *value);
    void (*Uniform1i) (GLint location, GLint x);

    /* Attributes */
    void (*BindAttribLocation) (GLuint program, GLuint index,
				const GLchar *name);
    void (*VertexAttribPointer) (GLuint index, GLint size, GLenum type,
				 GLboolean normalized, GLsizei stride,
				 const GLvoid *pointer);
    void (*EnableVertexAttribArray) (GLuint index);
    void (*DisableVertexAttribArray) (GLuint index);

    /* Framebuffer objects */
    void (*GenFramebuffers) (GLsizei n, GLuint* framebuffers);
    void (*BindFramebuffer) (GLenum target, GLuint framebuffer);
    void (*FramebufferTexture2D) (GLenum target, GLenum attachment,
				    GLenum textarget, GLuint texture,
				    GLint level);
    GLenum (*CheckFramebufferStatus) (GLenum target);
    void (*DeleteFramebuffers) (GLsizei n, const GLuint* framebuffers);
    void (*GenRenderbuffers) (GLsizei n, GLuint *renderbuffers);
    void (*BindRenderbuffer) (GLenum target, GLuint renderbuffer);
    void (*RenderbufferStorage) (GLenum target, GLenum internal_format,
				 GLsizei width, GLsizei height);
    void (*FramebufferRenderbuffer) (GLenum target, GLenum attachment,
				     GLenum renderbuffer_ttarget, GLuint renderbuffer);
    void (*DeleteRenderbuffers) (GLsizei n, GLuint *renderbuffers);
    void (*BlitFramebuffer) (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
			     GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
			     GLbitfield mask, GLenum filter);
    void (*RenderbufferStorageMultisample) (GLenum target, GLsizei samples,
					    GLenum internalformat,
					    GLsizei width, GLsizei height);
    void (*FramebufferTexture2DMultisample) (GLenum target, GLenum attachment,
					     GLenum textarget, GLuint texture,
					     GLint level, GLsizei samples);
} cairo_gl_dispatch_t;

typedef struct _cairo_gl_states {
    cairo_rectangle_int_t viewport_box;

    GLclampf clear_red;
    GLclampf clear_green;
    GLclampf clear_blue;
    GLclampf clear_alpha;

    cairo_bool_t blend_enabled;

    GLenum src_color_factor;
    GLenum dst_color_factor;
    GLenum src_alpha_factor;
    GLenum dst_alpha_factor;

    GLenum active_texture;

    cairo_bool_t depth_mask;

    cairo_bool_t scissor_test_enabled;
    cairo_bool_t stencil_test_enabled;

} cairo_gl_states_t;

struct _cairo_gl_context {
    cairo_device_t base;

    const cairo_compositor_t *compositor;

    GLuint texture_load_pbo;
    GLint max_framebuffer_size;
    GLint max_texture_size;
    GLint max_textures;
    GLenum tex_target;

    GLint num_samples;
    cairo_bool_t supports_msaa;
    char *vb;

    cairo_bool_t has_shader_support;

    GLuint vertex_shaders[CAIRO_GL_VAR_TYPE_MAX];
    cairo_gl_shader_t fill_rectangles_shader;
    cairo_cache_t shaders;

    cairo_cache_t gradients;

    cairo_gl_glyph_cache_t glyph_cache[2];
    cairo_list_t fonts;

    cairo_gl_surface_t *current_target;
    cairo_operator_t current_operator;
    cairo_gl_shader_t *pre_shader; /* for component alpha */
    cairo_gl_shader_t *current_shader;

    cairo_gl_operand_t operands[2];
    cairo_bool_t spans;

    unsigned int vb_offset;
    unsigned int vertex_size;
    cairo_region_t *clip_region;
    cairo_clip_t *clip;

    cairo_gl_primitive_type_t primitive_type;
    cairo_array_t tristrip_indices;

    cairo_bool_t has_mesa_pack_invert;
    cairo_gl_dispatch_t dispatch;
    GLfloat modelviewprojection_matrix[16];
    cairo_gl_flavor_t gl_flavor;
    cairo_bool_t has_map_buffer;
    cairo_bool_t has_packed_depth_stencil;
    cairo_bool_t has_npot_repeat;
    cairo_bool_t can_read_bgra;

    cairo_bool_t thread_aware;

    cairo_gl_image_cache_t image_cache;
    cairo_gl_draw_mode_t draw_mode;
    cairo_gl_states_t states_cache;

    void (*acquire) (void *ctx);
    void (*release) (void *ctx);

    void (*make_current) (void *ctx, cairo_gl_surface_t *surface);
    void (*swap_buffers)(void *ctx, cairo_gl_surface_t *surface);
    void (*destroy) (void *ctx);
};

typedef struct _cairo_gl_composite {
    cairo_gl_surface_t *dst;
    cairo_operator_t op;
    cairo_region_t *clip_region;

    cairo_gl_operand_t src;
    cairo_gl_operand_t mask;
    cairo_bool_t spans;

    cairo_clip_t *clip;
    cairo_bool_t multisample;
} cairo_gl_composite_t;

typedef struct _cairo_gl_font {
    cairo_scaled_font_private_t		base;
    cairo_device_t			*device;
    cairo_list_t			link;
} cairo_gl_font_t;

static cairo_always_inline GLenum
_cairo_gl_get_error (void)
{
    GLenum err = glGetError();

    if (unlikely (err))
        while (glGetError ());

    return err;
}

static inline cairo_device_t *
_cairo_gl_context_create_in_error (cairo_status_t status)
{
    return (cairo_device_t *) _cairo_device_create_in_error (status);
}

cairo_private cairo_status_t
_cairo_gl_context_init (cairo_gl_context_t *ctx);

cairo_private void
_cairo_gl_context_reset (cairo_gl_context_t *ctx);

cairo_private void
_cairo_gl_surface_init (cairo_device_t *device,
			cairo_gl_surface_t *surface,
			cairo_content_t content,
			int width, int height);

static cairo_always_inline cairo_bool_t cairo_warn
_cairo_gl_surface_is_texture (cairo_gl_surface_t *surface)
{
    return surface->tex != 0;
}

cairo_private cairo_status_t
_cairo_gl_surface_draw_image (cairo_gl_surface_t *dst,
			      cairo_image_surface_t *src,
			      int src_x, int src_y,
			      int width, int height,
			      int dst_x, int dst_y,
			      cairo_bool_t force_flush);

static cairo_always_inline cairo_bool_t
_cairo_gl_device_has_glsl (cairo_device_t *device)
{
    return ((cairo_gl_context_t *) device)->has_shader_support;
}

static cairo_always_inline cairo_bool_t
_cairo_gl_device_requires_power_of_two_textures (cairo_device_t *device)
{
    return ((cairo_gl_context_t *) device)->tex_target == GL_TEXTURE_RECTANGLE;
}

static cairo_always_inline cairo_status_t cairo_warn
_cairo_gl_context_acquire (cairo_device_t *device,
			   cairo_gl_context_t **ctx)
{
    cairo_status_t status;

    status = cairo_device_acquire (device);
    if (unlikely (status))
	return status;

    /* clear potential previous GL errors */
    _cairo_gl_get_error ();

    *ctx = (cairo_gl_context_t *) device;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_always_inline cairo_warn cairo_status_t
_cairo_gl_context_release (cairo_gl_context_t *ctx, cairo_status_t status)
{
    GLenum err;

    err = _cairo_gl_get_error ();

    if (unlikely (err)) {
        cairo_status_t new_status;
	new_status = _cairo_error (CAIRO_STATUS_DEVICE_ERROR);
        if (status == CAIRO_STATUS_SUCCESS)
            status = new_status;
    }

    cairo_device_release (&(ctx)->base);

    return status;
}

cairo_private void
_cairo_gl_activate_surface_as_nonmultisampling (cairo_gl_context_t *ctx,
						cairo_gl_surface_t *surface);

cairo_private void
_cairo_gl_context_set_destination (cairo_gl_context_t *ctx,
				   cairo_gl_surface_t *surface,
				   cairo_bool_t multisampling);

cairo_private cairo_gl_emit_rect_t
_cairo_gl_context_choose_emit_rect (cairo_gl_context_t *ctx);

cairo_private void
_cairo_gl_context_emit_rect (cairo_gl_context_t *ctx,
			     GLfloat x1, GLfloat y1,
			     GLfloat x2, GLfloat y2);

cairo_private cairo_gl_emit_span_t
_cairo_gl_context_choose_emit_span (cairo_gl_context_t *ctx);

cairo_private cairo_gl_emit_glyph_t
_cairo_gl_context_choose_emit_glyph (cairo_gl_context_t *ctx);

cairo_private void
_cairo_gl_context_activate (cairo_gl_context_t *ctx,
                            cairo_gl_tex_t      tex_unit);

cairo_private cairo_bool_t
_cairo_gl_operator_is_supported (cairo_operator_t op);

cairo_private cairo_bool_t
_cairo_gl_ensure_stencil (cairo_gl_context_t *ctx,
			  cairo_gl_surface_t *surface);

static cairo_always_inline void
_disable_stencil_buffer (cairo_gl_context_t *ctx)
{
    if (ctx->states_cache.stencil_test_enabled == TRUE) {
        glDisable (GL_STENCIL_TEST);
        ctx->states_cache.stencil_test_enabled = FALSE;
    }
}

static cairo_always_inline void
_disable_scissor_buffer (cairo_gl_context_t *ctx)
{
    if (ctx->states_cache.scissor_test_enabled == TRUE) {
        glDisable (GL_SCISSOR_TEST);
        ctx->states_cache.scissor_test_enabled = FALSE;
    }
}

static cairo_always_inline void
_enable_stencil_buffer (cairo_gl_context_t *ctx)
{
    if (ctx->states_cache.stencil_test_enabled == FALSE) {
        glEnable (GL_STENCIL_TEST);
        ctx->states_cache.stencil_test_enabled = TRUE;
    }
}

static cairo_always_inline void
_enable_scissor_buffer (cairo_gl_context_t *ctx)
{
    if (ctx->states_cache.scissor_test_enabled == FALSE) {
        glEnable (GL_SCISSOR_TEST);
        ctx->states_cache.scissor_test_enabled = TRUE;
    }
}

cairo_private cairo_status_t
_cairo_gl_composite_init (cairo_gl_composite_t *setup,
                          cairo_operator_t op,
                          cairo_gl_surface_t *dst,
                          cairo_bool_t has_component_alpha);

cairo_private void
_cairo_gl_composite_fini (cairo_gl_composite_t *setup);

cairo_private cairo_status_t
_cairo_gl_composite_set_operator (cairo_gl_composite_t *setup,
				  cairo_operator_t op,
				  cairo_bool_t assume_component_alpha);

cairo_private void
_cairo_gl_composite_set_clip_region (cairo_gl_composite_t *setup,
                                     cairo_region_t *clip_region);

cairo_private void
_cairo_gl_composite_set_clip(cairo_gl_composite_t *setup,
			     cairo_clip_t *clip);

cairo_private cairo_int_status_t
_cairo_gl_composite_set_source (cairo_gl_composite_t *setup,
				const cairo_pattern_t *pattern,
				const cairo_rectangle_int_t *sample,
				const cairo_rectangle_int_t *extents,
				cairo_bool_t use_texgen,
				cairo_bool_t encode_color_as_attribute);

cairo_private void
_cairo_gl_composite_set_solid_source (cairo_gl_composite_t *setup,
				      const cairo_color_t *color);

cairo_private void
_cairo_gl_composite_set_source_operand (cairo_gl_composite_t *setup,
					const cairo_gl_operand_t *source);

cairo_private cairo_int_status_t
_cairo_gl_composite_set_mask (cairo_gl_composite_t *setup,
			      const cairo_pattern_t *pattern,
			      const cairo_rectangle_int_t *sample,
			      const cairo_rectangle_int_t *extents,
			      cairo_bool_t use_texgen);

cairo_private void
_cairo_gl_composite_set_mask_operand (cairo_gl_composite_t *setup,
				      const cairo_gl_operand_t *mask);

cairo_private void
_cairo_gl_composite_set_spans (cairo_gl_composite_t *setup);

cairo_private void
_cairo_gl_composite_set_multisample (cairo_gl_composite_t *setup);

cairo_private cairo_status_t
_cairo_gl_composite_begin (cairo_gl_composite_t *setup,
                           cairo_gl_context_t **ctx);

cairo_private cairo_status_t
_cairo_gl_set_operands_and_operator (cairo_gl_composite_t *setup,
				     cairo_gl_context_t *ctx);

cairo_private void
_cairo_gl_composite_flush (cairo_gl_context_t *ctx);

cairo_private cairo_int_status_t
_cairo_gl_composite_emit_quad_as_tristrip (cairo_gl_context_t	*ctx,
					   cairo_gl_composite_t	*setup,
					   const cairo_point_t	 quad[4]);

cairo_private cairo_int_status_t
_cairo_gl_composite_emit_triangle_as_tristrip (cairo_gl_context_t	*ctx,
					       cairo_gl_composite_t	*setup,
					       const cairo_point_t	 triangle[3]);

cairo_private cairo_int_status_t
_cairo_gl_composite_emit_point_as_tristrip_line (cairo_gl_context_t  *ctx,
						 const cairo_point_t point[2],
						 cairo_bool_t	     start_point);

cairo_private cairo_int_status_t
_cairo_gl_composite_emit_point_as_single_line (cairo_gl_context_t  *ctx,
					        const cairo_point_t point[2]);

cairo_private void
_cairo_gl_context_destroy_operand (cairo_gl_context_t *ctx,
                                   cairo_gl_tex_t tex_unit);

cairo_private cairo_bool_t
_cairo_gl_get_image_format_and_type (cairo_gl_flavor_t flavor,
				     pixman_format_code_t pixman_format,
				     GLenum *internal_format, GLenum *format,
				     GLenum *type, cairo_bool_t *has_alpha,
				     cairo_bool_t *needs_swap);

cairo_private void
_cairo_gl_glyph_cache_init (cairo_gl_glyph_cache_t *cache);

cairo_private void
_cairo_gl_glyph_cache_fini (cairo_gl_context_t *ctx,
			    cairo_gl_glyph_cache_t *cache);

cairo_private cairo_int_status_t
_cairo_gl_surface_show_glyphs (void			*abstract_dst,
			       cairo_operator_t		 op,
			       const cairo_pattern_t	*source,
			       cairo_glyph_t		*glyphs,
			       int			 num_glyphs,
			       cairo_scaled_font_t	*scaled_font,
			       const cairo_clip_t	*clip,
			       int			*remaining_glyphs);

cairo_private cairo_status_t
_cairo_gl_context_init_shaders (cairo_gl_context_t *ctx);

cairo_private void
_cairo_gl_context_fini_shaders (cairo_gl_context_t *ctx);

static cairo_always_inline cairo_bool_t
_cairo_gl_context_is_flushed (cairo_gl_context_t *ctx)
{
    return ctx->vb_offset == 0;
}

cairo_private cairo_status_t
_cairo_gl_get_shader_by_type (cairo_gl_context_t *ctx,
                              cairo_gl_operand_t *source,
                              cairo_gl_operand_t *mask,
			      cairo_bool_t use_coverage,
                              cairo_gl_shader_in_t in,
                              cairo_gl_shader_t **shader);

cairo_private void
_cairo_gl_shader_bind_float (cairo_gl_context_t *ctx,
                 cairo_gl_shader_slot_t slot,
			     float value);

cairo_private void
_cairo_gl_shader_bind_float_name (cairo_gl_context_t *ctx,
                  const char *name,
                  float value);

cairo_private void
_cairo_gl_shader_bind_vec2 (cairo_gl_context_t *ctx,
                cairo_gl_shader_slot_t slot,
                float value0,
                float value1);

cairo_private void
_cairo_gl_shader_bind_vec2_name (cairo_gl_context_t *ctx,
                const char *name,
                float value0,
                float value1);

cairo_private void
_cairo_gl_shader_bind_vec3 (cairo_gl_context_t *ctx,
                cairo_gl_shader_slot_t slot,
                float value0,
                float value1,
                float value2);

cairo_private void
_cairo_gl_shader_bind_vec3_name (cairo_gl_context_t *ctx,
			    const char *name,
			    float value0,
			    float value1,
			    float value2);

cairo_private void
_cairo_gl_shader_bind_vec4 (cairo_gl_context_t *ctx,
                cairo_gl_shader_slot_t slot,
                float value0, float value1,
                float value2, float value3);

cairo_private void
_cairo_gl_shader_bind_vec4_name (cairo_gl_context_t *ctx,
			    const char *name,
			    float value0, float value1,
			    float value2, float value3);

cairo_private void
_cairo_gl_shader_bind_matrix (cairo_gl_context_t *ctx,
                  cairo_gl_shader_slot_t slot,
                  const cairo_matrix_t* m);

cairo_private void
_cairo_gl_shader_bind_matrix_name (cairo_gl_context_t *ctx,
			      const char *name,
			      const cairo_matrix_t* m);

cairo_private void
_cairo_gl_shader_bind_matrix4f (cairo_gl_context_t *ctx,
                cairo_gl_shader_slot_t slot,
                GLfloat* gl_m);

cairo_private void
_cairo_gl_shader_bind_matrix4f_name (cairo_gl_context_t *ctx,
				const char *name,
				GLfloat* gl_m);

cairo_private void
_cairo_gl_set_shader (cairo_gl_context_t *ctx,
		      cairo_gl_shader_t *shader);

cairo_private void
_cairo_gl_shader_fini (cairo_gl_context_t *ctx, cairo_gl_shader_t *shader);

cairo_private int
_cairo_gl_get_version (void);

cairo_private cairo_gl_flavor_t
_cairo_gl_get_flavor (void);

cairo_private cairo_bool_t
_cairo_gl_has_extension (const char *ext);

cairo_private cairo_status_t
_cairo_gl_dispatch_init(cairo_gl_dispatch_t *dispatch,
			cairo_gl_get_proc_addr_func_t get_proc_addr);

cairo_private cairo_int_status_t
_cairo_gl_operand_init (cairo_gl_operand_t *operand,
		        const cairo_pattern_t *pattern,
		        cairo_gl_surface_t *dst,
			const cairo_rectangle_int_t *sample,
			const cairo_rectangle_int_t *extents,
			cairo_bool_t use_texgen,
			cairo_bool_t encode_color_as_attribute);

cairo_private void
_cairo_gl_solid_operand_init (cairo_gl_operand_t *operand,
	                      const cairo_color_t *color);

cairo_private cairo_filter_t
_cairo_gl_operand_get_filter (cairo_gl_operand_t *operand);

cairo_private GLint
_cairo_gl_operand_get_gl_filter (cairo_gl_operand_t *operand);

cairo_private cairo_extend_t
_cairo_gl_operand_get_extend (cairo_gl_operand_t *operand);

cairo_private cairo_extend_t
_cairo_gl_operand_get_atlas_extend (cairo_gl_operand_t *operand);

cairo_private unsigned int
_cairo_gl_operand_get_vertex_size (const cairo_gl_operand_t *operand);

cairo_private cairo_bool_t
_cairo_gl_operand_get_use_atlas (cairo_gl_operand_t *operand);

cairo_private cairo_bool_t
_cairo_gl_operand_needs_setup (cairo_gl_operand_t *dest,
                               cairo_gl_operand_t *source,
                               unsigned int        vertex_offset);

cairo_private void
_cairo_gl_operand_bind_to_shader (cairo_gl_context_t *ctx,
                                  cairo_gl_operand_t *operand,
                                  cairo_gl_tex_t      tex_unit);

cairo_private void
_cairo_gl_operand_emit (cairo_gl_operand_t *operand,
                        GLfloat ** vb,
                        GLfloat x,
                        GLfloat y);

cairo_private void
_cairo_gl_operand_copy (cairo_gl_operand_t *dst,
			const cairo_gl_operand_t *src);

cairo_private void
_cairo_gl_operand_translate (cairo_gl_operand_t *operand,
			     double tx, double ty);

cairo_private void
_cairo_gl_operand_destroy (cairo_gl_operand_t *operand);

cairo_private const cairo_compositor_t *
_cairo_gl_msaa_compositor_get (void);

cairo_private const cairo_compositor_t *
_cairo_gl_span_compositor_get (void);

cairo_private const cairo_compositor_t *
_cairo_gl_traps_compositor_get (void);

cairo_private cairo_int_status_t
_cairo_gl_check_composite_glyphs (const cairo_composite_rectangles_t *extents,
				  cairo_scaled_font_t *scaled_font,
				  cairo_glyph_t *glyphs,
				  int *num_glyphs);

cairo_private cairo_int_status_t
_cairo_gl_composite_glyphs (void			*_dst,
			    cairo_operator_t		 op,
			    cairo_surface_t		*_src,
			    int				 src_x,
			    int				 src_y,
			    int				 dst_x,
			    int				 dst_y,
			    cairo_composite_glyphs_info_t *info);

cairo_private cairo_int_status_t
_cairo_gl_composite_glyphs_with_clip (void			    *_dst,
				      cairo_operator_t		     op,
				      cairo_surface_t		    *_src,
				      int			     src_x,
				      int			     src_y,
				      int			     dst_x,
				      int			     dst_y,
				      cairo_composite_glyphs_info_t *info,
				      cairo_clip_t		    *clip);

cairo_private void
_cairo_gl_image_node_destroy (cairo_rtree_node_t *node);

cairo_private void
_cairo_gl_image_node_fini (void *data);

cairo_private void
_cairo_gl_image_cache_unlock (cairo_gl_context_t *ctx);

cairo_int_status_t
_cairo_gl_image_cache_init (cairo_gl_context_t *ctx);

cairo_private void
_cairo_gl_ensure_framebuffer (cairo_gl_context_t *ctx,
                              cairo_gl_surface_t *surface);

cairo_private cairo_surface_t *
_cairo_gl_surface_create_scratch (cairo_gl_context_t   *ctx,
				  cairo_content_t	content,
				  int			width,
				  int			height);

cairo_private cairo_surface_t *
_cairo_gl_surface_create_scratch_for_caching (cairo_gl_context_t *ctx,
					      cairo_content_t content,
					      int width,
					      int height);

cairo_private cairo_surface_t *
_cairo_gl_pattern_to_source (cairo_surface_t *dst,
			     const cairo_pattern_t *pattern,
			     cairo_bool_t is_mask,
			     const cairo_rectangle_int_t *extents,
			     const cairo_rectangle_int_t *sample,
			     int *src_x, int *src_y);

cairo_private cairo_int_status_t
_cairo_gl_msaa_compositor_draw_clip (cairo_gl_context_t *ctx,
				     cairo_gl_composite_t *setup,
				     cairo_clip_t *clip);

cairo_private cairo_surface_t *
_cairo_gl_white_source (void);

cairo_private void
_cairo_gl_scissor_to_rectangle (cairo_gl_surface_t *surface,
				const cairo_rectangle_int_t *r);

static inline cairo_gl_operand_t *
source_to_operand (cairo_surface_t *surface)
{
    cairo_gl_source_t *source = (cairo_gl_source_t *)surface;
    return source ? &source->operand : NULL;
}

static inline void
_cairo_gl_glyph_cache_unlock (cairo_gl_glyph_cache_t *cache)
{
    _cairo_rtree_unpin (&cache->rtree);
}



cairo_private cairo_bool_t
_cairo_gl_hairline_style_is_hairline (const cairo_stroke_style_t *style,
                                      const cairo_matrix_t       *ctm);

cairo_private cairo_status_t
_cairo_gl_hairline_move_to (void *closure,
                            const cairo_point_t *point);

cairo_private cairo_status_t
_cairo_gl_hairline_line_to (void *closure,
                            const cairo_point_t *point);

cairo_private cairo_status_t
_cairo_gl_hairline_line_to_dashed (void *closure,
                                   const cairo_point_t *point);

cairo_private cairo_status_t
_cairo_gl_hairline_curve_to (void *closure,
                             const cairo_point_t *p0,
                             const cairo_point_t *p1,
                             const cairo_point_t *p2);

cairo_private cairo_status_t
_cairo_gl_hairline_close_path (void *closure);

cairo_private cairo_status_t
_cairo_gl_path_fixed_stroke_to_hairline (const cairo_path_fixed_t *path,
                                         cairo_gl_hairline_closure_t *closure,
                                         const cairo_stroke_style_t *style,
                                         const cairo_matrix_t *ctm,
                                         const cairo_matrix_t *ctm_inverse,
                                         cairo_path_fixed_move_to_func_t *move_to,
                                         cairo_path_fixed_line_to_func_t *line_to,
                                         cairo_path_fixed_curve_to_func_t *curve_to,
                                         cairo_path_fixed_close_path_func_t *close_path);

slim_hidden_proto (cairo_gl_surface_create);
slim_hidden_proto (cairo_gl_surface_create_for_texture);

#endif /* CAIRO_GL_PRIVATE_H */
