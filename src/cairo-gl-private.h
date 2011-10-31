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

#include "cairo-gl-gradient-private.h"

#include "cairo-device-private.h"
#include "cairo-error-private.h"
#include "cairo-rtree-private.h"

#include <assert.h>

#include "cairo-gl.h"

#if CAIRO_HAS_GL_SURFACE
#include <GL/gl.h>
#include <GL/glext.h>
#elif CAIRO_HAS_GLESV2_SURFACE
#ifndef GL_API_EXT
#define GL_API_EXT
#endif
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

// Henry Song
//typedef char GLchar;

#endif
// Henry Song
#include "cairo-surface-clipper-private.h"

#include "cairo-gl-ext-def-private.h"

#define DEBUG_GL 0
#define MAX_INDEX 10000

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

/* VBO size that we allocate, smaller size means we gotta flush more often */
#define CAIRO_GL_VBO_SIZE 16384

/* GL flavor */
typedef enum cairo_gl_flavor {
    CAIRO_GL_FLAVOR_NONE = 0,
    CAIRO_GL_FLAVOR_DESKTOP = 1,
    CAIRO_GL_FLAVOR_ES = 2
} cairo_gl_flavor_t;

/* Indices for vertex attributes used by BindAttribLocation etc */
enum {
    CAIRO_GL_VERTEX_ATTRIB_INDEX = 0,
    CAIRO_GL_COLOR_ATTRIB_INDEX  = 1,
    CAIRO_GL_TEXCOORD0_ATTRIB_INDEX = 2,
    CAIRO_GL_TEXCOORD1_ATTRIB_INDEX = CAIRO_GL_TEXCOORD0_ATTRIB_INDEX + 1
};

typedef struct cairo_gl_glyph_cache {
    cairo_rtree_t rtree;
    cairo_surface_pattern_t pattern;
	/// Henry Song
	//cairo_gl_surface_t *mask_surface;
} cairo_gl_glyph_cache_t;

typedef enum cairo_gl_tex {
    CAIRO_GL_TEX_SOURCE = 0,
    CAIRO_GL_TEX_MASK = 1,
    CAIRO_GL_TEX_TEMP = 2
} cairo_gl_tex_t;

typedef enum cairo_gl_operand_type {
    CAIRO_GL_OPERAND_NONE,
    CAIRO_GL_OPERAND_CONSTANT,
    CAIRO_GL_OPERAND_TEXTURE,
    CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_NONE,
    CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_PAD,
    CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_REPEAT,
    CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_REFLECT,
    CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_NONE_CIRCLE_IN_CIRCLE,
    CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_PAD_CIRCLE_IN_CIRCLE,
    CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REPEAT_CIRCLE_IN_CIRCLE,
    CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REFLECT_CIRCLE_IN_CIRCLE,
    CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_NONE_CIRCLE_NOT_IN_CIRCLE,
    CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_PAD_CIRCLE_NOT_IN_CIRCLE,
    CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REPEAT_CIRCLE_NOT_IN_CIRCLE,
    CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REFLECT_CIRCLE_NOT_IN_CIRCLE,
    CAIRO_GL_OPERAND_SPANS,

    CAIRO_GL_OPERAND_COUNT
} cairo_gl_operand_type_t;

typedef struct cairo_gl_shader_impl cairo_gl_shader_impl_t;

#define SOURCE_CONSTANT         "source_constant"
#define MASK_CONSTANT           "mask_constant"

#define SOURCE_COLORS           "source_colors"
#define SOURCE_OFFSETS          "source_offsets"
#define SOURCE_CIRCLE_1         "source_circle_1"
#define SOURCE_CIRCLE_2         "source_circle_2"
#define SOURCE_NSTOPS           "source_nstops"
#define SOURCE_SCALES           "source_scales"
#define SOURCE_PAD              "source_pad"
#define SOURCE_MOVED_CENTER     "source_moved_center"
#define SOURCE_ENDPOINT         "source_endpoint"
#define SOURCE_MATRIX_1         "source_matrix_1"
#define SOURCE_MATRIX_2         "source_matrix_2"
#define SOURCE_TANGENTS_END     "source_tangents_end"
#define SOURCE_STOPS            "source_stops"
#define SOURCE_TOTAL_DIST       "source_total_dist"
#define SOURCE_DELTA            "source_delta"

#define MASK_COLORS             "mask_colors"
#define MASK_OFFSETS            "mask_offsets"
#define MASK_CIRCLE_1           "mask_circle_1"
#define MASK_CIRCLE_2           "mask_circle_2"
#define MASK_NSTOPS             "mask_nstops"
#define MASK_SCALES             "mask_scales"
#define MASK_PAD                "mask_pad"
#define MASK_MOVED_CENTER       "mask_moved_center"
#define MASK_ENDPOINT           "mask_endpoint"
#define MASK_MATRIX_1           "mask_matrix_1"
#define MASK_MATRIX_2           "mask_matrix_2"
#define MASK_TANGENTS_END       "mask_tangents_end"
#define MASK_STOPS              "mask_stops"
#define MASK_TOTAL_DIST         "mask_total_dist"
#define MASK_DELTA              "mask_delta"

typedef struct cairo_gl_shader {
    GLuint fragment_shader;
    GLuint program;
    GLint modelviewprojection_matrix;
    GLint source_sampler;
    GLint mask_sampler;
    GLint source_constant;
    GLint mask_constant;
    
    GLint source_colors;
    GLint source_offsets;
    GLint source_circle_1;
    GLint source_circle_2;
    GLint source_nstops;
    GLint source_scales;
    GLint source_pad;
    GLint source_moved_center;
    GLint source_endpoint;
    GLint source_matrix_1;
    GLint source_matrix_2;
    GLint source_tangents_end;
    GLint source_stops;
    GLint source_total_dist;
    GLint source_delta;

    GLint mask_colors;
    GLint mask_offsets;
    GLint mask_circle_1;
    GLint mask_circle_2;
    GLint mask_nstops;
    GLint mask_scales;
    GLint mask_pad;
    GLint mask_moved_center;
    GLint mask_endpoint;
    GLint mask_matrix_1;
    GLint mask_matrix_2;
    GLint mask_tangents_end;
    GLint mask_stops;
    GLint mask_total_dist;
    GLint mask_delta;
} cairo_gl_shader_t;

typedef enum cairo_gl_shader_in {
    CAIRO_GL_SHADER_IN_NORMAL,
    CAIRO_GL_SHADER_IN_CA_SOURCE,
    CAIRO_GL_SHADER_IN_CA_SOURCE_ALPHA,

    CAIRO_GL_SHADER_IN_COUNT
} cairo_gl_shader_in_t;

typedef enum cairo_gl_var_type {
  CAIRO_GL_VAR_NONE,
  CAIRO_GL_VAR_TEXCOORDS,
  CAIRO_GL_VAR_COVERAGE
} cairo_gl_var_type_t;

#define cairo_gl_var_type_hash(src,mask,dest) ((mask) << 2 | (src << 1) | (dest))
#define CAIRO_GL_VAR_TYPE_MAX ((CAIRO_GL_VAR_COVERAGE << 2) | (CAIRO_GL_VAR_TEXCOORDS << 1) | CAIRO_GL_VAR_TEXCOORDS)

/* This union structure describes a potential source or mask operand to the
 * compositing equation.
 */
struct _cairo_gl_tristrip_indices;
typedef struct _cairo_gl_surface {
    cairo_surface_t base;

    int width, height;

    GLuint tex; /* GL texture object containing our data. */
    GLuint fb; /* GL framebuffer object wrapping our data. */
    GLuint depth; /* GL framebuffer object holding depth */
    int owns_tex;
    cairo_bool_t needs_update;

	// Henry Song
	GLuint rb; /* GL render buffer for depth and stencil buffer */
	GLuint ms_rb;
	GLuint ms_fb;
	GLuint ms_stencil_rb;
	GLint internal_format;
	cairo_bool_t require_aa;
	//int msaa_extension; // 0 no msaa, 1 - GL_ARB_framebuffer_object, 2 IMG_multisample_render_to_texture

	GLint tex_img;
	void *indices_buf;
	cairo_bool_t external_tex;
	cairo_surface_t *data_surface;
	cairo_bool_t needs_new_data_surface;
	int orig_width;
	int orig_height;
	float scale_width;
	float scale_height;
	//cairo_surface_t *super_sample_surface;
	//cairo_surface_t *offscreen_surface;
	//cairo_bool_t needs_super_sampling;
	//cairo_bool_t paint_to_self;
    cairo_bool_t multisample_resolved;

	// mask surface
	struct _cairo_gl_surface *mask_surface;
    GLint tex_format;
	struct _cairo_gl_surface *parent_surface;
	cairo_bool_t bound_fbo;
    cairo_clip_t *clip;
    struct _cairo_gl_tristrip_indices *clip_indices;
    cairo_bool_t stencil_buffer_changed;
} cairo_gl_surface_t;

typedef struct cairo_gl_operand {
    cairo_gl_operand_type_t type;
    union {
	struct {
	    GLuint tex;
		// Henry Song
		int width;
		int height;
	    cairo_gl_surface_t *surface;
	    cairo_surface_attributes_t attributes;
	} texture;
	struct {
	    GLfloat color[4];
	} constant;
	struct {
	    cairo_gl_gradient_t *gradient;
		// for linear gradient
		cairo_gradient_pattern_t *pattern;
		GLfloat stops[4];
		GLfloat colors[CAIRO_GL_MAX_STOPS_SIZE*4];
		GLfloat total_dist;
		int nstops;
		GLfloat offsets[CAIRO_GL_MAX_STOPS_SIZE];
		GLfloat delta[2];
		GLfloat scales[2];
		GLfloat circle_1[3];
		GLfloat circle_2[3];
		GLfloat start_offset;
		cairo_bool_t circle_in_circle;

		GLfloat matrix1[6];
		GLfloat matrix2[6];
		GLfloat endpoint[2];
		GLfloat tangents[8];
		GLfloat tangents_end[4];
		int moved_center;

	    cairo_matrix_t m;
	    cairo_circle_double_t circle_d;
	    double radius_0, a;
	    cairo_extend_t extend;
	} gradient;
    };
    unsigned int vertex_offset;
} cairo_gl_operand_t;


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
	// Henry Song
	void (*Uniform1fv) (GLint location, GLsizei count, 
		const GLfloat *value);
	void (*Uniform2fv) (GLint location, GLsizei count,
		const GLfloat *value);
	void (*Uniform3fv) (GLint location, GLsizei count,
		const GLfloat *value);
	void (*Uniform4fv) (GLint location, GLsizei count,
		const GLfloat *value);

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

	// Henry Song
	void (*GenRenderbuffers) (GLsizei n, GLuint *renderbuffers);
	void (*BindRenderbuffer) (GLenum target, GLuint renderbuffer);
	void (*RenderbufferStorage) (GLenum target, GLenum internalformat,
		GLsizei width, GLsizei height);
	void (*FramebufferRenderbuffer) (GLenum target, GLenum attachment,
		GLenum renderbuffertarget, GLuint renderbuffer);
	void (*DeleteRenderbuffers) (GLsizei n, GLuint *renderbuffers);
} cairo_gl_dispatch_t;

struct _cairo_gl_context {
    cairo_device_t base;

    GLuint texture_load_pbo;
    GLuint vbo;
    GLint max_framebuffer_size;
    GLint max_texture_size;
    GLint max_textures;
    GLenum tex_target;
	cairo_bool_t standard_npot;

	GLint max_sample_size;
    cairo_bool_t force_non_msaa;

    const cairo_gl_shader_impl_t *shader_impl;

    GLuint vertex_shaders[CAIRO_GL_VAR_TYPE_MAX + 1];
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


    char *vb;
    char *vb_mem;
    unsigned int vb_offset;
    unsigned int vertex_size;
    cairo_region_t *clip_region;

	// Henry Song
	//cairo_gl_surface_t *mask_surface;

	// Henry Song
	//cairo_clip_t *clip;
    GLuint bound_fb;
    GLint current_program;
    int active_texture;
    int src_color_factor;
    int dst_color_factor;
    int src_alpha_factor;
    int dst_alpha_factor;
    float clear_red;
    float clear_green;
    float clear_blue;
    float clear_alpha;
    cairo_rectangle_int_t scissor_box;
    cairo_rectangle_int_t viewport_box;
    cairo_bool_t depthmask_enabled;
    GLfloat vertices[MAX_INDEX + 10];
    GLfloat tex_vertices[MAX_INDEX + 10];
    GLfloat mask_tex_vertices[MAX_INDEX + 10];

    GLenum draw_buffer;
    cairo_bool_t stencil_test_enabled;
    cairo_bool_t scissor_test_enabled;
    cairo_bool_t blend_enabled;
    cairo_bool_t multisample_enabled;

    cairo_bool_t stencil_test_reset;
    cairo_bool_t program_reset;
    cairo_bool_t scissor_test_reset;
    cairo_bool_t source_texture_attrib_reset;
    cairo_bool_t mask_texture_attrib_reset;
    cairo_bool_t vertex_attrib_reset;

    cairo_bool_t has_mesa_pack_invert;
    cairo_gl_dispatch_t dispatch;
    GLfloat modelviewprojection_matrix[16];
    cairo_gl_flavor_t gl_flavor;
    cairo_bool_t has_map_buffer;

    void (*acquire) (void *ctx);
    void (*release) (void *ctx);

    void (*make_current) (void *ctx, cairo_gl_surface_t *surface);
    void (*swap_buffers)(void *ctx, cairo_gl_surface_t *surface);
    void (*destroy) (void *ctx);
	//Henry Song
	void (*reset) (void *ctx);
};





// Henry Song
#define VERTEX_INC 64
typedef struct _cairo_gl_vertex
{
	float x;
	float y;
} _cairo_gl_vertex_t;

typedef struct _cairo_gl_path_vertices
{
	_cairo_gl_vertex_t *vertices;
	int vertex_size;		// total number of list, each list is a separate path
	int capacity;
} _cairo_gl_path_vertices_t;

typedef struct _cairo_gl_path
{
	struct _cairo_gl_path *prev, *next;	// circled link
	_cairo_gl_path_vertices_t *vertices;
} _cairo_gl_path_t;

typedef struct _cairo_gl_composite {
    cairo_gl_surface_t *dst;
    cairo_operator_t op;
    cairo_region_t *clip_region;
	// Henry Song
	cairo_pattern_t *source;
	//cairo_clip_t *clip;

    cairo_gl_operand_t src;
    cairo_gl_operand_t mask;
	cairo_gl_context_t *ctx;
} cairo_gl_composite_t;

typedef struct _cairo_gl_tristrip_indices
{
    cairo_gl_composite_t *setup;
    cairo_array_t indices;
    cairo_array_t vertices;
    cairo_array_t mask_texture_coords;
} cairo_gl_tristrip_indices_t;

cairo_private extern const cairo_surface_backend_t _cairo_gl_surface_backend;

static cairo_always_inline GLenum
_cairo_gl_get_error (void)
{
    GLenum err = glGetError();
    if(err == GL_OUT_OF_MEMORY) printf("out of memory\n");
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
_cairo_gl_surface_super_sampling(cairo_surface_t *abstract_surface);

cairo_private cairo_status_t
_cairo_gl_surface_draw_image (cairo_gl_surface_t *dst,
			      cairo_image_surface_t *src,
			      int src_x, int src_y,
			      int width, int height,
			      int dst_x, int dst_y,
				  cairo_bool_t keep_size);

static cairo_always_inline cairo_bool_t
_cairo_gl_device_has_glsl (cairo_device_t *device)
{
    return ((cairo_gl_context_t *) device)->shader_impl != NULL;
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
    //_cairo_gl_get_error ();

    *ctx = (cairo_gl_context_t *) device;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_always_inline cairo_warn cairo_status_t
_cairo_gl_context_release (cairo_gl_context_t *ctx, cairo_status_t status)
{
    /*
    GLenum err;

    err = _cairo_gl_get_error ();
    
    if (unlikely (err)) {
        cairo_status_t new_status;
	new_status = _cairo_error (CAIRO_STATUS_DEVICE_ERROR);
    //printf("gl error %x\n", err);
        if (status == CAIRO_STATUS_SUCCESS)
            status = new_status;
    }*/
    
    cairo_device_release (&(ctx)->base);

    return status;
}

cairo_private void
_cairo_gl_context_set_destination (cairo_gl_context_t *ctx, cairo_gl_surface_t *surface);


cairo_private void 
_cairo_gl_context_blit_destination (cairo_gl_context_t *ctx, cairo_gl_surface_t *surface);

cairo_private void
_cairo_gl_context_activate (cairo_gl_context_t *ctx,
                            cairo_gl_tex_t      tex_unit);

cairo_private cairo_bool_t
_cairo_gl_operator_is_supported (cairo_operator_t op);

cairo_private cairo_status_t
_cairo_gl_composite_init (cairo_gl_composite_t *setup,
                          cairo_operator_t op,
                          cairo_gl_surface_t *dst,
                          cairo_bool_t has_component_alpha,
                          const cairo_rectangle_int_t *rect);

cairo_private void
_cairo_gl_composite_fini (cairo_gl_composite_t *setup);

cairo_private void
_cairo_gl_composite_set_clip_region (cairo_gl_composite_t *setup,
                                     cairo_region_t *clip_region);

cairo_private cairo_int_status_t
_cairo_gl_composite_set_source (cairo_gl_composite_t *setup,
			        const cairo_pattern_t *pattern,
                                int src_x, int src_y,
                                int dst_x, int dst_y,
                                int width, int height,
								GLuint tex, int tex_width, int tex_height);

cairo_private cairo_int_status_t
_cairo_gl_composite_set_mask (cairo_gl_composite_t *setup,
			      const cairo_pattern_t *pattern,
                              int src_x, int src_y,
                              int dst_x, int dst_y,
                              int width, int height,
							  GLuint tex, int tex_width, int tex_height);

cairo_private void
_cairo_gl_composite_set_mask_spans (cairo_gl_composite_t *setup);

cairo_private cairo_status_t
_cairo_gl_composite_begin (cairo_gl_composite_t *setup,
                           cairo_gl_context_t **ctx);

// Henry Song
cairo_private cairo_status_t
_cairo_gl_composite_begin_constant_color(cairo_gl_composite_t *setup,
										 int vertices_size, 
										 void *vertices,
										 void *color,
										 void *mask_color,
										 cairo_gl_context_t *ctx);

// Henry Song
cairo_private void
_cairo_gl_composite_fill_constant_color(cairo_gl_context_t *ctx,
	unsigned int count, unsigned short *indices);

cairo_private cairo_status_t
_cairo_gl_surface_clear (cairo_gl_surface_t  *surface,
                         const cairo_color_t *color);

cairo_private cairo_status_t
_cairo_gl_fill (cairo_gl_tristrip_indices_t *indices);

cairo_private void
_cairo_gl_disable_scissor_test (cairo_gl_context_t *ctx);

cairo_private void
_cairo_gl_disable_stencil_test (cairo_gl_context_t *ctx);

cairo_private void
_cairo_gl_enable_stencil_test (cairo_gl_context_t *ctx);

cairo_private void
_cairo_gl_enable_scissor_test (cairo_gl_context_t *ctx,
                               cairo_gl_surface_t *surface,
                               cairo_rectangle_int_t rect);

cairo_private void
_cairo_gl_composite_emit_rect (cairo_gl_context_t *ctx,
                               GLfloat x1,
                               GLfloat y1,
                               GLfloat x2,
                               GLfloat y2,
                               uint8_t alpha);

cairo_private void
_cairo_gl_composite_emit_glyph (cairo_gl_context_t *ctx,
                                GLfloat x1,
                                GLfloat y1,
                                GLfloat x2,
                                GLfloat y2,
                                GLfloat glyph_x1,
                                GLfloat glyph_y1,
                                GLfloat glyph_x2,
                                GLfloat glyph_y2);

cairo_private void
_cairo_gl_composite_flush (cairo_gl_context_t *ctx);

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
_cairo_gl_surface_scaled_font_fini ( cairo_scaled_font_t  *scaled_font);

cairo_private void
_cairo_gl_surface_scaled_glyph_fini (cairo_scaled_glyph_t *scaled_glyph,
				     cairo_scaled_font_t  *scaled_font);

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
			       cairo_clip_t		*clip,
			       int			*remaining_glyphs);

static inline int
_cairo_gl_y_flip (cairo_gl_surface_t *surface, int y)
{
    if (surface->fb)
	return y;
    else
	return (surface->height - 1) - y;
}

cairo_private cairo_status_t
_cairo_gl_context_init_shaders (cairo_gl_context_t *ctx);

cairo_private void
_cairo_gl_context_fini_shaders (cairo_gl_context_t *ctx);

static cairo_always_inline cairo_bool_t
_cairo_gl_context_is_flushed (cairo_gl_context_t *ctx)
{
    return ctx->vb == NULL;
}

cairo_private cairo_status_t
_cairo_gl_get_shader_by_type (cairo_gl_context_t *ctx,
                              cairo_gl_operand_t *source,
                              cairo_gl_operand_t *mask,
                              cairo_gl_shader_in_t in,
                              cairo_gl_shader_t **shader);

cairo_private void
_cairo_gl_shader_bind_float (cairo_gl_context_t *ctx,
			     const char *name,
			     float value);

cairo_private void
_cairo_gl_shader_bind_vec2 (cairo_gl_context_t *ctx,
			    const char *name,
			    float value0, float value1);

cairo_private void
_cairo_gl_shader_bind_vec3 (cairo_gl_context_t *ctx,
			    const char *name,
			    float value0,
			    float value1,
			    float value2);

cairo_private void
_cairo_gl_shader_bind_vec4 (cairo_gl_context_t *ctx,
			    const char *name,
			    float value0, float value1,
			    float value2, float value3);

// Henry Song
cairo_private void
_cairo_gl_shader_bind_floatv(cairo_gl_context_t *ctx,
				const char *name,
				int count,
				float *values);

cairo_private void
_cairo_gl_shader_bind_vec2v(cairo_gl_context_t *ctx,
				const char *name,
				int count,
				float *values);

cairo_private void
_cairo_gl_shader_bind_vec3v(cairo_gl_context_t *ctx,
				const char *name,
				int count,
				float *values);

cairo_private void
_cairo_gl_shader_bind_vec4v(cairo_gl_context_t *ctx,
				const char *name,
				int count,
				float *values);


cairo_private void
_cairo_gl_shader_bind_matrix (cairo_gl_context_t *ctx,
			      const char *name,
			      cairo_matrix_t* m);

cairo_private void
_cairo_gl_shader_bind_matrix4f (cairo_gl_context_t *ctx,
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

cairo_private cairo_filter_t
_cairo_gl_operand_get_filter (cairo_gl_operand_t *operand);

cairo_private GLint
_cairo_gl_operand_get_gl_filter (cairo_gl_operand_t *operand);

cairo_private cairo_extend_t
_cairo_gl_operand_get_extend (cairo_gl_operand_t *operand);

cairo_private cairo_status_t
_cairo_gl_ensure_framebuffer (cairo_gl_context_t *ctx,
                              cairo_gl_surface_t *surface);

cairo_surface_t *
_cairo_gl_surface_create_no_multisample (cairo_device_t *abstract_device,
                                    cairo_content_t content,
                                    int width,
                                    int height);
slim_hidden_proto (cairo_gl_surface_create);
slim_hidden_proto (cairo_gl_surface_create_for_texture);
slim_hidden_proto (cairo_gl_surface_create_for_texture_with_internal_format);
slim_hidden_proto (cairo_gl_surface_resolve);

#endif /* CAIRO_GL_PRIVATE_H */
