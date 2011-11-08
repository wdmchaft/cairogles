/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2009 T. Zachary Laine
 * Copyright © 2010 Eric Anholt
 * Copyright © 2010 Red Hat, Inc
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
 * The Initial Developer of the Original Code is T. Zachary Laine.
 *
 * Contributor(s):
 *	Benjamin Otte <otte@gnome.org>
 *	Eric Anholt <eric@anholt.net>
 *	T. Zachary Laine <whatwasthataddress@gmail.com>
 *	Alexandros Frantzis <alexandros.frantzis@linaro.org>
 */

#include "cairoint.h"
#include "cairo-gl-private.h"
#include "cairo-error-private.h"
#include "cairo-output-stream-private.h"
#include <string.h>

typedef struct cairo_gl_shader_impl {
    void
    (*compile_shader) (cairo_gl_context_t *ctx, GLuint *shader, GLenum type,
		       const char *text);

    void
    (*link_shader) (cairo_gl_context_t *ctx, GLuint *program, GLuint vert, GLuint frag);

    void
    (*destroy_shader) (cairo_gl_context_t *ctx, GLuint shader);

    void
    (*destroy_program) (cairo_gl_context_t *ctx, GLuint program);

    void
    (*bind_float) (cairo_gl_context_t *ctx,
		   cairo_gl_shader_t *shader,
		   const char *name,
		   float value);

    void
    (*bind_vec2) (cairo_gl_context_t *ctx,
		  cairo_gl_shader_t *shader,
		  const char *name,
		  float value0,
		  float value1);

    void
    (*bind_vec3) (cairo_gl_context_t *ctx,
		  cairo_gl_shader_t *shader,
		  const char *name,
		  float value0,
		  float value1,
		  float value2);

    void
    (*bind_vec4) (cairo_gl_context_t *ctx,
		  cairo_gl_shader_t *shader,
		  const char *name,
		  float value0, float value1,
		  float value2, float value3);

	// Henry Song
	void 
	(*bind_floatv) (cairo_gl_context_t *ctx,
		cairo_gl_shader_t *shader,
		const char *name,
		int count,
		float *values);
	
	void 
	(*bind_vec2v) (cairo_gl_context_t *ctx,
		cairo_gl_shader_t *shader,
		const char *name,
		int count,
		float *values);

	void 
	(*bind_vec3v) (cairo_gl_context_t *ctx,
		cairo_gl_shader_t *shader,
		const char *name,
		int count,
		float *values);

	void 
	(*bind_vec4v) (cairo_gl_context_t *ctx,
		cairo_gl_shader_t *shader,
		const char *name,
		int count,
		float *values);

    void
    (*bind_matrix) (cairo_gl_context_t *ctx,
		    cairo_gl_shader_t *shader,
		    const char *name,
		    cairo_matrix_t* m);

    void
    (*bind_matrix4f) (cairo_gl_context_t *ctx,
		      cairo_gl_shader_t *shader,
		      const char *name,
		      GLfloat* gl_m);

    void
    (*use) (cairo_gl_context_t *ctx,
	    cairo_gl_shader_t *shader);
} shader_impl_t;

static cairo_status_t
_cairo_gl_shader_compile (cairo_gl_context_t *ctx,
			  cairo_gl_shader_t *shader,
			  cairo_gl_var_type_t src,
			  cairo_gl_var_type_t mask,
			  const char *fragment_text);

/* OpenGL Core 2.0 API. */
static void
compile_shader_core_2_0 (cairo_gl_context_t *ctx, GLuint *shader,
			 GLenum type, const char *text)
{
    const char* strings[1] = { text };
    GLint gl_status;
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;

    *shader = dispatch->CreateShader (type);
    dispatch->ShaderSource (*shader, 1, strings, 0);
    dispatch->CompileShader (*shader);
    dispatch->GetShaderiv (*shader, GL_COMPILE_STATUS, &gl_status);
    if (gl_status == GL_FALSE) {
        GLint log_size;
        dispatch->GetShaderiv (*shader, GL_INFO_LOG_LENGTH, &log_size);
        if (0 < log_size) {
            char *log = _cairo_malloc (log_size);
            GLint chars;

            log[log_size - 1] = '\0';
            dispatch->GetShaderInfoLog (*shader, log_size, &chars, log);
            printf ("OpenGL shader compilation failed.  Shader:\n"
                    "%s\n"
                    "OpenGL compilation log:\n"
                    "%s\n",
                    text, log);

            free (log);
        } else {
            printf ("OpenGL shader compilation failed.\n");
        }

	ASSERT_NOT_REACHED;
    }
}

static void
link_shader_core_2_0 (cairo_gl_context_t *ctx, GLuint *program,
		      GLuint vert, GLuint frag)
{
    GLint gl_status;
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;

    *program = dispatch->CreateProgram ();
    dispatch->AttachShader (*program, vert);
    dispatch->AttachShader (*program, frag);

    dispatch->BindAttribLocation (*program, CAIRO_GL_VERTEX_ATTRIB_INDEX,
				  "Vertex");
    dispatch->BindAttribLocation (*program, CAIRO_GL_COLOR_ATTRIB_INDEX,
				  "Color");
    dispatch->BindAttribLocation (*program, CAIRO_GL_TEXCOORD0_ATTRIB_INDEX,
				  "MultiTexCoord0");
    dispatch->BindAttribLocation (*program, CAIRO_GL_TEXCOORD1_ATTRIB_INDEX,
				  "MultiTexCoord1");

    dispatch->LinkProgram (*program);
    dispatch->GetProgramiv (*program, GL_LINK_STATUS, &gl_status);
    if (gl_status == GL_FALSE) {
        GLint log_size;
        dispatch->GetProgramiv (*program, GL_INFO_LOG_LENGTH, &log_size);
        if (0 < log_size) {
            char *log = _cairo_malloc (log_size);
            GLint chars;

            log[log_size - 1] = '\0';
            dispatch->GetProgramInfoLog (*program, log_size, &chars, log);
            printf ("OpenGL shader link failed:\n%s\n", log);

            free (log);
        } else {
            printf ("OpenGL shader link failed.\n");
        }

	ASSERT_NOT_REACHED;
    }
}

static void
destroy_shader_core_2_0 (cairo_gl_context_t *ctx, GLuint shader)
{
    ctx->dispatch.DeleteShader (shader);
}

static void
destroy_program_core_2_0 (cairo_gl_context_t *ctx, GLuint shader)
{
    ctx->dispatch.DeleteProgram (shader);
}

static void
bind_float_core_2_0 (cairo_gl_context_t *ctx,
		     cairo_gl_shader_t *shader,
		     const char *name,
		     float value)
{
    GLuint location;
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    if(strcmp(name, SOURCE_TOTAL_DIST) == 0)
    {
        if(shader->source_total_dist == -1)
            shader->source_total_dist = dispatch->GetUniformLocation (shader->program, name);
        location = shader->source_total_dist;
    }
    else if(strcmp(name, MASK_TOTAL_DIST) == 0)
    {
        if(shader->mask_total_dist == -1)
            shader->mask_total_dist = dispatch->GetUniformLocation (shader->program, name);
        location = shader->mask_total_dist;
    }
    else
        location = dispatch->GetUniformLocation (shader->program, name);
    assert (location != -1);
    dispatch->Uniform1f (location, value);
}

static void
bind_vec2_core_2_0 (cairo_gl_context_t *ctx,
		    cairo_gl_shader_t *shader,
		    const char *name,
		    float value0,
		    float value1)
{
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    GLint location = dispatch->GetUniformLocation (shader->program, name);
    assert (location != -1);
    dispatch->Uniform2f (location, value0, value1);
}

static void
bind_vec3_core_2_0 (cairo_gl_context_t *ctx,
		    cairo_gl_shader_t *shader,
		    const char *name,
		    float value0,
		    float value1,
		    float value2)
{
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    GLint location = dispatch->GetUniformLocation (shader->program, name);
    assert (location != -1);
    dispatch->Uniform3f (location, value0, value1, value2);
}

static void
bind_vec4_core_2_0 (cairo_gl_context_t *ctx,
		    cairo_gl_shader_t *shader,
		    const char *name,
		    float value0,
		    float value1,
		    float value2,
		    float value3)
{
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    GLint location = -1;
    if(strcmp(name, SOURCE_CONSTANT) == 0)
    {
        if(shader->source_constant == -1) 
        {
            shader->source_constant = dispatch->GetUniformLocation(shader->program, name);
        }
        location = shader->source_constant;
    }
    else if(strcmp(name, MASK_CONSTANT) == 0)
    {
        if(shader->mask_constant == -1)
        {
            shader->mask_constant = dispatch->GetUniformLocation(shader->program, name);
        }
        location = shader->mask_constant;
    }
    else
    {
        location = dispatch->GetUniformLocation (shader->program, name);
    }
    assert (location != -1);
    dispatch->Uniform4f (location, value0, value1, value2, value3);
}

// Henry Song
static void
bind_floatv_core_2_0 (cairo_gl_context_t *ctx,
	cairo_gl_shader_t *shader,
	const char *name,
	int count,
	float *values)
{
    GLint location;
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    if(strcmp(name, SOURCE_OFFSETS) == 0)
    {
        if(shader->source_offsets == -1)
            shader->source_offsets = dispatch->GetUniformLocation (shader->program, name);
        location = shader->source_offsets;
    }
    else if(strcmp(name, MASK_OFFSETS) == 0)
    {
        if(shader->mask_offsets == -1)
            shader->mask_offsets = dispatch->GetUniformLocation (shader->program, name);
        location = shader->mask_offsets;
    }
    else
        location = dispatch->GetUniformLocation (shader->program, name);
    assert (location != -1);
    dispatch->Uniform1fv (location, count, values);
}

static void
bind_vec2v_core_2_0 (cairo_gl_context_t *ctx,
	cairo_gl_shader_t *shader,
	const char *name,
	int count,
	float *values)
{
    GLint location;
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    if(strcmp(name, SOURCE_SCALES) == 0)
    {
        if(shader->source_scales == -1)
            shader->source_scales = dispatch->GetUniformLocation (shader->program, name);
        location = shader->source_scales;
    }
    else if(strcmp(name, SOURCE_ENDPOINT) == 0)
    {
        if(shader->source_endpoint == -1)
            shader->source_endpoint = dispatch->GetUniformLocation (shader->program, name);
        location = shader->source_endpoint;
    }
    else if(strcmp(name, SOURCE_MATRIX_1) == 0)
    {
        if(shader->source_matrix_1 == -1)
            shader->source_matrix_1 = dispatch->GetUniformLocation (shader->program, name);
        location = shader->source_matrix_1;
    }
    else if(strcmp(name, SOURCE_MATRIX_2) == 0)
    {
        if(shader->source_matrix_2 == -1)
            shader->source_matrix_2 = dispatch->GetUniformLocation (shader->program, name);
        location = shader->source_matrix_2;
    }
    else if(strcmp(name, SOURCE_TANGENTS_END) == 0)
    {
        if(shader->source_tangents_end == -1)
            shader->source_tangents_end = dispatch->GetUniformLocation (shader->program, name);
        location = shader->source_tangents_end;
    }
    else if(strcmp(name, SOURCE_STOPS) == 0)
    {
        if(shader->source_stops == -1)
            shader->source_stops = dispatch->GetUniformLocation (shader->program, name);
        location = shader->source_stops;
    }
    else if(strcmp(name, SOURCE_DELTA) == 0)
    {
        if(shader->source_delta == -1)
            shader->source_delta = dispatch->GetUniformLocation (shader->program, name);
        location = shader->source_delta;
    }
    else if(strcmp(name, MASK_SCALES) == 0)
    {
        if(shader->mask_scales == -1)
            shader->mask_scales = dispatch->GetUniformLocation (shader->program, name);
        location = shader->mask_scales;
    }
    else if(strcmp(name, MASK_ENDPOINT) == 0)
    {
        if(shader->mask_endpoint == -1)
            shader->mask_endpoint = dispatch->GetUniformLocation (shader->program, name);
        location = shader->mask_endpoint;
    }
    else if(strcmp(name, MASK_MATRIX_1) == 0)
    {
        if(shader->mask_matrix_1 == -1)
            shader->mask_matrix_1 = dispatch->GetUniformLocation (shader->program, name);
        location = shader->mask_matrix_1;
    }
    else if(strcmp(name, MASK_MATRIX_2) == 0)
    {
        if(shader->mask_matrix_2 == -1)
            shader->mask_matrix_2 = dispatch->GetUniformLocation (shader->program, name);
        location = shader->mask_matrix_2;
    }
    else if(strcmp(name, MASK_TANGENTS_END) == 0)
    {
        if(shader->mask_tangents_end == -1)
            shader->mask_tangents_end = dispatch->GetUniformLocation (shader->program, name);
        location = shader->mask_tangents_end;
    }
    else if(strcmp(name, MASK_STOPS) == 0)
    {
        if(shader->mask_stops == -1)
            shader->mask_stops = dispatch->GetUniformLocation (shader->program, name);
        location = shader->mask_stops;
    }
    else if(strcmp(name, MASK_DELTA) == 0)
    {
        if(shader->mask_delta == -1)
            shader->mask_delta = dispatch->GetUniformLocation (shader->program, name);
        location = shader->mask_delta;
    }
    else
        location = dispatch->GetUniformLocation (shader->program, name);
    assert (location != -1);
    dispatch->Uniform2fv (location, count, values);
	//GLenum error = glGetError();
}

static void
bind_vec3v_core_2_0 (cairo_gl_context_t *ctx,
	cairo_gl_shader_t *shader,
	const char *name,
	int count,
	float *values)
{
    GLint location;
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    if(strcmp(name, SOURCE_CIRCLE_1) == 0)
    {
        if(shader->source_circle_1 == -1)
            shader->source_circle_1 = dispatch->GetUniformLocation (shader->program, name);
        location = shader->source_circle_1;
    }
    else if(strcmp(name, SOURCE_CIRCLE_2) == 0)
    {
        if(shader->source_circle_2 == -1)
            shader->source_circle_2 = dispatch->GetUniformLocation (shader->program, name);
        location = shader->source_circle_2;
    }
    else if(strcmp(name, MASK_CIRCLE_1) == 0)
    {
        if(shader->mask_circle_1 == -1)
            shader->mask_circle_1 = dispatch->GetUniformLocation (shader->program, name);
        location = shader->mask_circle_1;
    }
    else if(strcmp(name, MASK_CIRCLE_2) == 0)
    {
        if(shader->mask_circle_2 == -1)
            shader->mask_circle_2 = dispatch->GetUniformLocation (shader->program, name);
        location = shader->mask_circle_2;
    }
    else
        location = dispatch->GetUniformLocation (shader->program, name);
    assert (location != -1);
    dispatch->Uniform3fv (location, count, values);
	//GLenum error = glGetError();
}
	
static void
bind_vec4v_core_2_0 (cairo_gl_context_t *ctx,
	cairo_gl_shader_t *shader,
	const char *name,
	int count,
	float *values)
{
    GLint location;
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    if(strcmp(name, SOURCE_COLORS) == 0)
    {
        if(shader->source_colors == -1)
        {
            shader->source_colors = dispatch->GetUniformLocation(shader->program, name);
        }
        location = shader->source_colors;
    }
    else if(strcmp(name, MASK_COLORS) == 0)
    {
        if(shader->mask_colors == -1)
        {
            shader->mask_colors = dispatch->GetUniformLocation(shader->program, name);
        }
        location = shader->mask_colors;
    }
    else
        location = dispatch->GetUniformLocation (shader->program, name);
    assert (location != -1);
    dispatch->Uniform4fv (location, count, values);
	//GLenum error = glGetError();
}

static void
bind_matrix_core_2_0 (cairo_gl_context_t *ctx,
		      cairo_gl_shader_t *shader,
		      const char *name,
		      cairo_matrix_t* m)
{
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    GLint location = dispatch->GetUniformLocation (shader->program, name);
    float gl_m[16] = {
        m->xx, m->xy, m->x0,
        m->yx, m->yy, m->y0,
        0,     0,     1
    };
    assert (location != -1);
    dispatch->UniformMatrix3fv (location, 1, GL_TRUE, gl_m);
}

static void
bind_matrix4f_core_2_0 (cairo_gl_context_t *ctx,
		        cairo_gl_shader_t *shader,
		        const char *name,
		        GLfloat* gl_m)
{
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    if(shader->modelviewprojection_matrix == -1)
    {
        shader->modelviewprojection_matrix = 
            dispatch->GetUniformLocation (shader->program, name);
    }
    assert (shader->modelviewprojection_matrix != -1);
    dispatch->UniformMatrix4fv (shader->modelviewprojection_matrix, 1, GL_FALSE, gl_m);
}

static void
use_program_core_2_0 (cairo_gl_context_t *ctx,
		      cairo_gl_shader_t *shader)
{
    if (shader)
    {
        /*if(ctx->program_reset == TRUE)
        {
            ctx->program_reset = FALSE;
	        ctx->dispatch.UseProgram (shader->program);
            ctx->current_program = shader->program;
        }
        else if (ctx->current_program != shader->program)
        */
        if(ctx->current_program != shader->program)
        {
	        ctx->dispatch.UseProgram (shader->program);
            ctx->current_program = shader->program;
        }
   }
    //else
	//ctx->dispatch.UseProgram (0);
}

static const cairo_gl_shader_impl_t shader_impl_core_2_0 = {
    compile_shader_core_2_0,
    link_shader_core_2_0,
    destroy_shader_core_2_0,
    destroy_program_core_2_0,
    bind_float_core_2_0,
    bind_vec2_core_2_0,
    bind_vec3_core_2_0,
    bind_vec4_core_2_0,
	// Henry Song
	bind_floatv_core_2_0,
	bind_vec2v_core_2_0,
	bind_vec3v_core_2_0,
	bind_vec4v_core_2_0,

    bind_matrix_core_2_0,
    bind_matrix4f_core_2_0,
    use_program_core_2_0,
};

typedef struct _cairo_shader_cache_entry {
    cairo_cache_entry_t base;

    cairo_gl_operand_type_t src;
    cairo_gl_operand_type_t mask;
    cairo_gl_operand_type_t dest;
    cairo_gl_shader_in_t in;
    GLint src_gl_filter;
    cairo_bool_t src_border_fade;
    GLint mask_gl_filter;
    cairo_bool_t mask_border_fade;

    cairo_gl_context_t *ctx; /* XXX: needed to destroy the program */
    cairo_gl_shader_t shader;
} cairo_shader_cache_entry_t;

static cairo_bool_t
_cairo_gl_shader_cache_equal_desktop (const void *key_a, const void *key_b)
{
    const cairo_shader_cache_entry_t *a = key_a;
    const cairo_shader_cache_entry_t *b = key_b;

    return a->src  == b->src  &&
           a->mask == b->mask &&
           a->dest == b->dest &&
           a->in   == b->in;
}

/*
 * For GLES2 we use more complicated shaders to implement missing GL
 * features. In this case we need more parameters to uniquely identify
 * a shader (vs _cairo_gl_shader_cache_equal_desktop()).
 */
static cairo_bool_t
_cairo_gl_shader_cache_equal_gles2 (const void *key_a, const void *key_b)
{
    const cairo_shader_cache_entry_t *a = key_a;
    const cairo_shader_cache_entry_t *b = key_b;

    return a->src  == b->src  &&
	   a->mask == b->mask &&
	   a->dest == b->dest &&
	   a->in   == b->in   &&
	   a->src_gl_filter == b->src_gl_filter &&
	   a->src_border_fade == b->src_border_fade &&
	   a->mask_gl_filter == b->mask_gl_filter &&
	   a->mask_border_fade == b->mask_border_fade;
}

static unsigned long
_cairo_gl_shader_cache_hash (const cairo_shader_cache_entry_t *entry)
{
    return (entry->src << 24) | (entry->mask << 16) | (entry->dest << 8) | (entry->in);
}

static void
_cairo_gl_shader_cache_destroy (void *data)
{
    cairo_shader_cache_entry_t *entry = data;

    _cairo_gl_shader_fini (entry->ctx, &entry->shader);
    if (entry->ctx->current_shader == &entry->shader)
        entry->ctx->current_shader = NULL;
    free (entry);
}

static void
_cairo_gl_shader_init (cairo_gl_shader_t *shader)
{
    shader->fragment_shader = 0;
    shader->program = 0;
    shader->modelviewprojection_matrix = -1;
    shader->source_sampler = -1;
    shader->mask_sampler = -1;
    shader->source_sampler_set = FALSE;
    shader->mask_sampler_set = FALSE;
    shader->source_constant = -1;
    shader->mask_constant = -1;

    shader->source_colors = -1;
    shader->source_offsets = -1;
    shader->source_circle_1 = -1;
    shader->source_circle_2 = -1;
    shader->source_nstops = -1;
    shader->source_scales = -1;
    shader->source_pad = -1;
    shader->source_moved_center = -1;
    shader->source_endpoint = -1;
    shader->source_matrix_1 = -1;
    shader->source_matrix_2 = -1;
    shader->source_tangents_end = -1;
    shader->source_stops = -1;
    shader->source_total_dist = -1;
    shader->source_delta = -1;
    
    shader->mask_colors = -1;
    shader->mask_offsets = -1;
    shader->mask_circle_1 = -1;
    shader->mask_circle_2 = -1;
    shader->mask_nstops = -1;
    shader->mask_scales = -1;
    shader->mask_pad = -1;
    shader->mask_moved_center = -1;
    shader->mask_endpoint = -1;
    shader->mask_matrix_1 = -1;
    shader->mask_matrix_2 = -1;
    shader->mask_tangents_end = -1;
    shader->mask_stops = -1;
    shader->mask_total_dist = -1;
    shader->mask_delta = -1;
}

cairo_status_t
_cairo_gl_context_init_shaders (cairo_gl_context_t *ctx)
{
    static const char *fill_fs_source =
	"#ifdef GL_ES\n"
	"precision mediump float;\n"
	"#endif\n"
	"uniform vec4 color;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor = color;\n"
	"}\n";
    cairo_status_t status;

    if (_cairo_gl_get_version () >= CAIRO_GL_VERSION_ENCODE (2, 0) ||
	(_cairo_gl_has_extension ("GL_ARB_shader_objects") &&
	 _cairo_gl_has_extension ("GL_ARB_fragment_shader") &&
	 _cairo_gl_has_extension ("GL_ARB_vertex_shader")))
    {
	ctx->shader_impl = &shader_impl_core_2_0;
    }
    else
    {
	ctx->shader_impl = NULL;
	fprintf (stderr, "Error: The cairo gl backend requires shader support!\n");
	return CAIRO_STATUS_DEVICE_ERROR;
    }

    memset (ctx->vertex_shaders, 0, sizeof (ctx->vertex_shaders));

    status = _cairo_cache_init (&ctx->shaders,
                                ctx->gl_flavor == CAIRO_GL_FLAVOR_DESKTOP ?
				    _cairo_gl_shader_cache_equal_desktop :
				    _cairo_gl_shader_cache_equal_gles2,
                                NULL,
                                _cairo_gl_shader_cache_destroy,
                                CAIRO_GL_MAX_SHADERS_PER_CONTEXT);
    if (unlikely (status))
	return status;

    _cairo_gl_shader_init (&ctx->fill_rectangles_shader);
    status = _cairo_gl_shader_compile (ctx,
				       &ctx->fill_rectangles_shader,
				       CAIRO_GL_VAR_NONE,
				       CAIRO_GL_VAR_NONE,
				       fill_fs_source);

    if (unlikely (status))
	return status;

    return CAIRO_STATUS_SUCCESS;
}

void
_cairo_gl_context_fini_shaders (cairo_gl_context_t *ctx)
{
    int i;

    for (i = 0; i <= CAIRO_GL_VAR_TYPE_MAX; i++) {
	if (ctx->vertex_shaders[i])
	    ctx->shader_impl->destroy_shader (ctx, ctx->vertex_shaders[i]);
    }

    _cairo_cache_fini (&ctx->shaders);
}

void
_cairo_gl_shader_fini (cairo_gl_context_t *ctx,
		       cairo_gl_shader_t *shader)
{
    if (shader->fragment_shader)
        ctx->shader_impl->destroy_shader (ctx, shader->fragment_shader);

    if (shader->program)
        ctx->shader_impl->destroy_program (ctx, shader->program);
}

static const char *operand_names[] = { "source", "mask", "dest" };

static cairo_gl_var_type_t
cairo_gl_operand_get_var_type (cairo_gl_operand_type_t type)
{
    switch (type) {
    default:
    case CAIRO_GL_OPERAND_COUNT:
        ASSERT_NOT_REACHED;
    case CAIRO_GL_OPERAND_NONE:
    case CAIRO_GL_OPERAND_CONSTANT:
        return CAIRO_GL_VAR_NONE;
    case CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_NONE:
    case CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_PAD:
    case CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_REPEAT:
    case CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_REFLECT:
    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_NONE_CIRCLE_IN_CIRCLE:
    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_PAD_CIRCLE_IN_CIRCLE:
    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REPEAT_CIRCLE_IN_CIRCLE:
    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REFLECT_CIRCLE_IN_CIRCLE:
    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_NONE_CIRCLE_NOT_IN_CIRCLE:
    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_PAD_CIRCLE_NOT_IN_CIRCLE:
    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REPEAT_CIRCLE_NOT_IN_CIRCLE:
    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REFLECT_CIRCLE_NOT_IN_CIRCLE:
    case CAIRO_GL_OPERAND_TEXTURE:
        return CAIRO_GL_VAR_TEXCOORDS;
    case CAIRO_GL_OPERAND_SPANS:
        return CAIRO_GL_VAR_COVERAGE;
    }
}

static void
cairo_gl_shader_emit_variable (cairo_output_stream_t *stream,
                               cairo_gl_var_type_t type,
                               cairo_gl_tex_t name)
{
    switch (type) {
    default:
        ASSERT_NOT_REACHED;
    case CAIRO_GL_VAR_NONE:
        break;
    case CAIRO_GL_VAR_TEXCOORDS:
        _cairo_output_stream_printf (stream, 
                                     "varying vec2 %s_texcoords;\n", 
                                     operand_names[name]);
        break;
    case CAIRO_GL_VAR_COVERAGE:
        _cairo_output_stream_printf (stream, 
                                     "varying float %s_coverage;\n", 
                                     operand_names[name]);
        break;
    }
}

static void
cairo_gl_shader_emit_vertex (cairo_output_stream_t *stream,
                             cairo_gl_var_type_t type,
                             cairo_gl_tex_t name)
{
    switch (type) {
    default:
        ASSERT_NOT_REACHED;
    case CAIRO_GL_VAR_NONE:
        break;
    case CAIRO_GL_VAR_TEXCOORDS:
        _cairo_output_stream_printf (stream, 
                                     "    %s_texcoords = MultiTexCoord%d.xy;\n",
                                     operand_names[name], name);
        break;
    case CAIRO_GL_VAR_COVERAGE:
        _cairo_output_stream_printf (stream, 
                                     "    %s_coverage = Color.a;\n",
                                     operand_names[name]);
        break;
    }
}

static cairo_status_t
cairo_gl_shader_get_vertex_source (cairo_gl_var_type_t src,
                                   cairo_gl_var_type_t mask,
                                   cairo_gl_var_type_t dest,
				   char **out)
{
    cairo_output_stream_t *stream = _cairo_memory_stream_create ();
    unsigned char *source;
    unsigned long length;
    cairo_status_t status;

    cairo_gl_shader_emit_variable (stream, src, CAIRO_GL_TEX_SOURCE);
    cairo_gl_shader_emit_variable (stream, mask, CAIRO_GL_TEX_MASK);

    _cairo_output_stream_printf (stream,
				 "attribute vec4 Vertex;\n"
				 "attribute vec4 Color;\n"
				 "attribute vec4 MultiTexCoord0;\n"
				 "attribute vec4 MultiTexCoord1;\n"
				 "uniform mat4 ModelViewProjectionMatrix;\n"
				 "void main()\n"
				 "{\n"
				 "    gl_Position = ModelViewProjectionMatrix * Vertex;\n");

    cairo_gl_shader_emit_vertex (stream, src, CAIRO_GL_TEX_SOURCE);
    cairo_gl_shader_emit_vertex (stream, mask, CAIRO_GL_TEX_MASK);

    _cairo_output_stream_write (stream,
				"}\n\0", 3);

    status = _cairo_memory_stream_destroy (stream, &source, &length);
    if (unlikely (status))
	return status;

    *out = (char *) source;
	// Henry Song
	//printf("-------------------> vertex shader -----------------\n%s\n\n", source);
    return CAIRO_STATUS_SUCCESS;
}

/*
 * Returns whether an operand needs a special border fade fragment shader
 * to simulate the GL_CLAMP_TO_BORDER wrapping method that is missing in GLES2.
 */
static cairo_bool_t
_cairo_gl_shader_needs_border_fade (cairo_gl_operand_t *operand)
{
    cairo_extend_t extend =_cairo_gl_operand_get_extend (operand);

    return extend == CAIRO_EXTEND_NONE &&
	   (operand->type == CAIRO_GL_OPERAND_TEXTURE ||
	    operand->type == CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_NONE ||
	    operand->type == CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_PAD ||
	    operand->type == CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_REPEAT ||
	    operand->type == CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_REFLECT ||
	    operand->type == CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_NONE_CIRCLE_IN_CIRCLE ||
	    operand->type == CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_PAD_CIRCLE_IN_CIRCLE ||
	    operand->type == CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REPEAT_CIRCLE_IN_CIRCLE ||
	    operand->type == CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REFLECT_CIRCLE_IN_CIRCLE ||
	    operand->type == CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_NONE_CIRCLE_NOT_IN_CIRCLE ||
	    operand->type == CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_PAD_CIRCLE_NOT_IN_CIRCLE ||
	    operand->type == CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REPEAT_CIRCLE_NOT_IN_CIRCLE ||
	    operand->type == CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REFLECT_CIRCLE_NOT_IN_CIRCLE);
}

static void
cairo_gl_shader_emit_color (cairo_output_stream_t *stream,
                            cairo_gl_context_t *ctx,
                            cairo_gl_operand_t *op,
                            cairo_gl_tex_t name)
{
    const char *namestr = operand_names[name];
    const char *rectstr = (ctx->tex_target == GL_TEXTURE_RECTANGLE ? "Rect" : "");

    switch (op->type) {
    case CAIRO_GL_OPERAND_COUNT:
    default:
        ASSERT_NOT_REACHED;
        break;
    case CAIRO_GL_OPERAND_NONE:
        _cairo_output_stream_printf (stream, 
            "vec4 get_%s()\n"
            "{\n"
            "    return vec4 (0, 0, 0, 1);\n"
            "}\n",
            namestr);
        break;
    case CAIRO_GL_OPERAND_CONSTANT:
        _cairo_output_stream_printf (stream, 
            "uniform vec4 %s_constant;\n"
            "vec4 get_%s()\n"
            "{\n"
            "    return %s_constant;\n"
            "}\n",
            namestr, namestr, namestr);
        break;
    case CAIRO_GL_OPERAND_TEXTURE:
	_cairo_output_stream_printf (stream,
	     "uniform sampler2D%s %s_sampler;\n"
	     "uniform vec2 %s_texdims;\n"
	     "varying vec2 %s_texcoords;\n"
	     "vec4 get_%s()\n"
	     "{\n",
	     rectstr, namestr, namestr, namestr, namestr);
	if (ctx->gl_flavor == CAIRO_GL_FLAVOR_ES &&
	    _cairo_gl_shader_needs_border_fade (op))
	{
	    _cairo_output_stream_printf (stream,
		//"    vec2 border_fade = %s_border_fade (%s_texcoords, %s_texdims);\n"
		//"    vec4 texel = texture2D%s (%s_sampler, %s_texcoords);\n"
		//"    return texel * border_fade.x * border_fade.y;\n"
		"    return texture2D(%s_sampler, %s_texcoords);\n"
		"}\n",
		//namestr, namestr, namestr, rectstr, namestr, namestr);
		namestr, namestr);
	}
	else
	{
	    _cairo_output_stream_printf (stream,
	        "    return texture2D%s (%s_sampler, %s_texcoords);\n"
		"}\n",
		rectstr, namestr, namestr);
	}
        break;
    case CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_NONE:
	_cairo_output_stream_printf (stream,
		"uniform vec2 %s_stops[30];\n"
		"uniform vec4 %s_colors[30];\n"
		"uniform float %s_offsets[30];\n"
		"uniform float %s_total_dist;\n"
		"uniform int %s_nstops;\n"
		"uniform vec2 %s_delta;\n"
        "\n"
        "float %s_precision_scale = 100.0;\n"
		"float %s_get_distance_from_start(vec2 coord)\n"
		"{\n"
		"  if(%s_total_dist == 0.0)\n"
		"    return 1.0;\n"
		"  float d = 1.0 / %s_total_dist;\n"
		"  float dis = dot(%s_delta, coord / %s_precision_scale - %s_stops[0]) *d;\n"
		"    return dis;\n"
		"}\n"
		"int %s_i;\n"
		"float %s_relative_dis;\n"
		"float %s_percent;\n"
		"float %s_dis;\n"
		"vec4 %s_get_color(vec2 coord)\n"
		"{\n"
		"  %s_dis = %s_get_distance_from_start(coord);\n"
		"  if(%s_dis > 1.0 || %s_dis < 0.0)\n"
		"      return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"  else if(%s_dis >= %s_offsets[%s_nstops-1])\n"
		"    return %s_colors[%s_nstops-1];\n"
		"  else if(%s_dis <= %s_offsets[0])\n"
		"    return %s_colors[0];\n"
		"  if(%s_nstops == 2)\n"
		"  {\n"
		"    if(%s_offsets[0] == 0.0 && %s_offsets[1] == 1.0)\n"
		"      return (%s_colors[1] - %s_colors[0]) * %s_dis + %s_colors[0];\n"
		"    else\n"
		"    {\n"
		"      %s_relative_dis = %s_dis - %s_offsets[0];\n"
		"      float d = 1.0 / (%s_offsets[1] - %s_offsets[0]);\n"
		"      %s_percent = %s_relative_dis * d ;\n"
		"      return (%s_colors[1] - %s_colors[0]) * %s_percent + %s_colors[0];\n"
		"    }\n"
		"  }\n"
		"  for(%s_i = 1; %s_i < %s_nstops; %s_i++)\n"
		"  {\n"
		"    if(%s_dis <= %s_offsets[%s_i])\n"
		"    {\n"
		"      %s_relative_dis = %s_dis - %s_offsets[%s_i-1];\n"
		"      float d = 1.0 / (%s_offsets[%s_i] - %s_offsets[%s_i-1]);\n"
		"      %s_percent = %s_relative_dis * d ;\n"
		"      return (%s_colors[%s_i] - %s_colors[%s_i-1]) * %s_percent + %s_colors[%s_i-1];\n"
		"    }\n"
		"  }\n"
		"  return %s_colors[%s_nstops-1];\n"
		"}\n"
		"vec4 get_%s()\n"
		"{\n"
		"	vec4 color = %s_get_color(gl_FragCoord.xy);\n"
        "   vec4 alpha = vec4(color.a, color.a, color.a, 1.0);\n" 
        "   return color * alpha;\n"
		"}\n",
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr);
	break;
    case CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_PAD:
	_cairo_output_stream_printf (stream,
		"uniform vec2 %s_stops[30];\n"
		"uniform vec4 %s_colors[30];\n"
		"uniform float %s_offsets[30];\n"
		"uniform float %s_total_dist;\n"
		"uniform int %s_nstops;\n"
		"uniform vec2 %s_delta;\n"
        "\n"
        "float %s_precision_scale = 100.0;\n"
		"float %s_get_distance_from_start(vec2 coord)\n"
		"{\n"
		"  if(%s_total_dist == 0.0)\n"
		"    return 1.0;\n"
		"  float d = 1.0 / %s_total_dist;\n"
		"  float dis = dot(%s_delta, coord / %s_precision_scale - %s_stops[0]) *d;\n"
		"    return dis;\n"
		"}\n"
		"int %s_i;\n"
		"float %s_relative_dis;\n"
		"float %s_percent;\n"
		"float %s_dis;\n"
		"vec4 %s_get_color(vec2 coord)\n"
		"{\n"
		"  %s_dis = %s_get_distance_from_start(coord);\n"
		"  if(%s_dis >= %s_offsets[%s_nstops-1])\n"
        "  {\n"
        "      return %s_colors[%s_nstops-1];\n"
        "  }\n"
		"  else if(%s_dis <= %s_offsets[0])\n"
        "  {\n"
		"      return %s_colors[0];\n"
        "  }\n"
		"  if(%s_nstops == 2)\n"
		"  {\n"
		"    if(%s_offsets[0] == 0.0 && %s_offsets[1] == 1.0)\n"
		"      return (%s_colors[1] - %s_colors[0]) * %s_dis + %s_colors[0];\n"
		"    else\n"
		"    {\n"
		"      %s_relative_dis = %s_dis - %s_offsets[0];\n"
		"      float d = 1.0 / (%s_offsets[1] - %s_offsets[0]);\n"
		"      %s_percent = %s_relative_dis * d ;\n"
		"      return (%s_colors[1] - %s_colors[0]) * %s_percent + %s_colors[0];\n"
		"    }\n"
		"  }\n"
		"  for(%s_i = 1; %s_i < %s_nstops; %s_i++)\n"
		"  {\n"
		"    if(%s_dis <= %s_offsets[%s_i])\n"
		"    {\n"
		"      %s_relative_dis = %s_dis - %s_offsets[%s_i-1];\n"
		"      float d = 1.0 / (%s_offsets[%s_i] - %s_offsets[%s_i-1]);\n"
		"      %s_percent = %s_relative_dis * d ;\n"
		"      return (%s_colors[%s_i] - %s_colors[%s_i-1]) * %s_percent + %s_colors[%s_i-1];\n"
		"    }\n"
		"  }\n"
		"  return %s_colors[%s_nstops-1];\n"
		"}\n"
		"vec4 get_%s()\n"
		"{\n"
		"	vec4 color = %s_get_color(gl_FragCoord.xy);\n"
        "   vec4 alpha = vec4(color.a, color.a, color.a, 1.0);\n" 
        "   return color * alpha;\n"
		"}\n",
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
        namestr, namestr);
	break;
    case CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_REPEAT:
	_cairo_output_stream_printf (stream,
		"uniform vec2 %s_stops[30];\n"
		"uniform vec4 %s_colors[30];\n"
		"uniform float %s_offsets[30];\n"
		"uniform float %s_total_dist;\n"
		"uniform int %s_nstops;\n"
		"uniform vec2 %s_delta;\n"
		"float %s_reversed;\n"
        "float %s_precision_scale = 100.0;\n"
		"float %s_get_distance_from_start(vec2 coord)\n"
		"{\n"
		"  %s_reversed = 0.0;\n"
		"  if(%s_total_dist == 0.0)\n"
		"    return 1.0;\n"
		"  float d = 1.0 / %s_total_dist;\n"
		"  float dis = dot(%s_delta, coord / %s_precision_scale - %s_stops[0]) *d;\n"
		"  if(dis == 0.0)\n"
		"    return dis;\n"
		"  if(dis < 0.0)\n"
		"  {\n"
		"    %s_reversed = 1.0;\n"
		"    dis = -dis;\n"
		"  }\n"
		"  float dis1 = fract(dis);\n"
		"  if(dis1 == 0.0)\n"
		"    return 1.0;\n"
		"  if(%s_reversed == 0.0)\n"
		"    return dis1;\n"
		"  return 1.0 - dis1;\n"
		"}\n"
		"int %s_i;\n"
		"float %s_relative_dis;\n"
		"float %s_percent;\n"
		"float %s_dis;\n"
		"vec4 %s_get_color(vec2 coord)\n"
		"{\n"
		"  %s_dis = %s_get_distance_from_start(coord);\n"
		"  if(%s_dis >= %s_offsets[%s_nstops-1])\n"
		"    return %s_colors[%s_nstops-1];\n"
		"  else if(%s_dis <= %s_offsets[0])\n"
		"    return %s_colors[0];\n"
		"  if(%s_nstops == 2)\n"
		"  {\n"
		"    if(%s_offsets[0] == 0.0 && %s_offsets[1] == 1.0)\n"
		"      return (%s_colors[1] - %s_colors[0]) * %s_dis + %s_colors[0];\n"
		"    else\n"
		"    {\n"
		"      %s_relative_dis = %s_dis - %s_offsets[0];\n"
		"      float d = 1.0 / (%s_offsets[1] - %s_offsets[0]);\n"
		"      %s_percent = %s_relative_dis * d ;\n"
		"      return (%s_colors[1] - %s_colors[0]) * %s_percent + %s_colors[0];\n"
		"    }\n"
		"  }\n"
		"  for(%s_i = 1; %s_i < %s_nstops; %s_i++)\n"
		"  {\n"
		"    if(%s_dis <= %s_offsets[%s_i])\n"
		"    {\n"
		"      %s_relative_dis = %s_dis - %s_offsets[%s_i-1];\n"
		"      float d = 1.0 / (%s_offsets[%s_i] - %s_offsets[%s_i-1]);\n"
		"      %s_percent = %s_relative_dis * d ;\n"
		"      return (%s_colors[%s_i] - %s_colors[%s_i-1]) * %s_percent + %s_colors[%s_i-1];\n"
		"    }\n"
		"  }\n"
		"  return %s_colors[%s_nstops-1];\n"
		"}\n"
		"vec4 get_%s()\n"
		"{\n"
		"	vec4 color = %s_get_color(gl_FragCoord.xy);\n"
        "   vec4 alpha = vec4(color.a, color.a, color.a, 1.0);\n" 
        "   return color * alpha;\n"
		"}\n",
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr);
	break;
    case CAIRO_GL_OPERAND_LINEAR_GRADIENT_EXT_REFLECT:
	_cairo_output_stream_printf (stream,
		"uniform vec2 %s_stops[30];\n"
		"uniform vec4 %s_colors[30];\n"
		"uniform float %s_offsets[30];\n"
		"uniform float %s_total_dist;\n"
		"uniform int %s_nstops;\n"
		"uniform vec2 %s_delta;\n"
        "float %s_precision_scale = 100.0;\n"
		"vec2 %s_get_distance_from_start(vec2 coord)\n"
		"{\n"
		"  if(%s_total_dist == 0.0)\n"
		"    return vec2(1.0, 0.0);\n"
		"  float d = 1.0 / %s_total_dist;\n"
		"  float dis = dot(%s_delta, coord / %s_precision_scale - %s_stops[0]) *d;\n"
		"  if(dis == 0.0)\n"
		"    return vec2(dis, 0.0);\n"
		"  if(dis < 0.0)\n"
		"  {\n"
		"    dis = -dis;\n"
		"  }\n"
		"  float dis1 = fract(dis);\n"
		"  if(dis1 == 0.0)\n"
		"    return vec2(1.0, 0.0);\n"
		"  float dis2 = mod(dis, 2.0);\n"
		"  if(dis2 > 1.0)\n"
		"    return vec2(dis1, 1.0);\n"
		"  return vec2(dis1, 0.0);\n"
		"}\n"
		"int %s_i;\n"
		"float %s_relative_dis;\n"
		"float %s_percent;\n"
		"float %s_dis;\n"
		"vec2 %s_dis_vec;\n"
		"float %s_odd;\n"
		"vec4 %s_get_color(vec2 coord)\n"
		"{\n"
		"  %s_dis_vec = %s_get_distance_from_start(coord);\n"
		"  %s_dis = %s_dis_vec.x;\n"
		"  %s_odd = %s_dis_vec.y;\n"
		"  if(%s_odd == 1.0)\n"
		"  {\n"
		"    if(%s_dis <= 1.0 - %s_offsets[%s_nstops-1])\n"
		"      return %s_colors[%s_nstops-1];\n"
		"  	 else if(%s_dis >= 1.0 - %s_offsets[0])\n"
		"      return %s_colors[0];\n"
		"  }\n"
		"  else\n"
		"  {\n"
		"    if(%s_dis <= %s_offsets[0])\n"
		"      return %s_colors[0];\n"
		"  	 else if(%s_dis >= %s_offsets[%s_nstops-1])\n"
		"      return %s_colors[%s_nstops-1];\n"
		"  }\n"
		"  if(%s_nstops == 2)\n"
		"  {\n"
		"    if(%s_odd == 1.0)\n"
		"    {\n"
		"      if(%s_offsets[0] == 0.0 && %s_offsets[%s_nstops-1] == 1.0)\n"
		"        return (%s_colors[0] - %s_colors[1]) * %s_dis + %s_colors[1];\n"
		"      else\n"
		"      {\n"
		"        %s_relative_dis = %s_dis - 1.0 + %s_offsets[1];\n"
		"        float d = 1.0 / (%s_offsets[1] - %s_offsets[0]);\n"
		"        %s_percent = %s_relative_dis * d ;\n"
		"        return (%s_colors[0] - %s_colors[1]) * %s_percent + %s_colors[1];\n"
		"      }\n"
		"    }\n"
		"    else\n"
		"    {\n"
		"      if(%s_offsets[0] == 0.0 && %s_offsets[%s_nstops-1] == 1.0)\n"
		"        return (%s_colors[1] - %s_colors[0]) * %s_dis + %s_colors[0];\n"
		"      else\n"
		"      {\n"
		"        %s_relative_dis = %s_dis - %s_offsets[0];\n"
		"        float d = 1.0 / (%s_offsets[1] - %s_offsets[0]);\n"
		"        %s_percent = %s_relative_dis * d ;\n"
		"        return (%s_colors[1] - %s_colors[0]) * %s_percent + %s_colors[0];\n"
		"      }\n"
		"    }\n"
		"  }\n"
		"  if(%s_odd == 0.0)\n"
		"  {\n"
		"    for(%s_i = 1; %s_i < %s_nstops; %s_i++)\n"
		"    {\n"
		"      if(%s_dis <= %s_offsets[%s_i])\n"
		"      {\n"
		"        %s_relative_dis = %s_dis - %s_offsets[%s_i-1];\n"
		"        float d = 1.0 / (%s_offsets[%s_i] - %s_offsets[%s_i-1]);\n"
		"        %s_percent = %s_relative_dis * d ;\n"
		"        return (%s_colors[%s_i] - %s_colors[%s_i-1]) * %s_percent + %s_colors[%s_i-1];\n"
		"      }\n"
		"    }\n"
		"  }\n"
		"  else\n"
		"  {\n"
		"    for(%s_i = %s_nstops - 1; %s_i >= 0; %s_i--)\n"
		"    {\n"
		"      if(%s_dis <= 1.0 - %s_offsets[%s_i])\n"
		"      {\n"
		"        %s_relative_dis =%s_dis - 1.0 + %s_offsets[%s_i+1];\n"
		"        float d = 1.0 / (%s_offsets[%s_i+1] - %s_offsets[%s_i]);\n"
		"        %s_percent = %s_relative_dis * d ;\n"
		"        return (%s_colors[%s_i] - %s_colors[%s_i+1]) * %s_percent + %s_colors[%s_i+1];\n"
		"      }\n"
		"    }\n"
		"  }\n"
		"  return %s_colors[0];\n"
		"}\n"
		"vec4 get_%s()\n"
		"{\n"
		"	vec4 color = %s_get_color(gl_FragCoord.xy);\n"
        "   vec4 alpha = vec4(color.a, color.a, color.a, 1.0);\n" 
        "   return color * alpha;\n"
		"}\n",
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr,
        namestr);
	break;
	/*
    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_A0:
	_cairo_output_stream_printf (stream,
	    "varying vec2 %s_texcoords;\n"
	    "uniform vec2 %s_texdims;\n"
	    "uniform sampler2D%s %s_sampler;\n"
	    "uniform vec3 %s_circle_d;\n"
	    "uniform float %s_radius_0;\n"
	    "\n"
	    "vec4 get_%s()\n"
	    "{\n"
	    "    vec3 pos = vec3 (%s_texcoords, %s_radius_0);\n"
	    "    \n"
	    "    float B = dot (pos, %s_circle_d);\n"
	    "    float C = dot (pos, vec3 (pos.xy, -pos.z));\n"
	    "    \n"
	    "    float t = 0.5 * C / B;\n"
	    "    float is_valid = step (-%s_radius_0, t * %s_circle_d.z);\n",
	    namestr, namestr, rectstr, namestr, namestr, namestr, namestr,
	    namestr, namestr, namestr, namestr, namestr);
	if (ctx->gl_flavor == CAIRO_GL_FLAVOR_ES &&
	    _cairo_gl_shader_needs_border_fade (op))
	{
	    _cairo_output_stream_printf (stream,
		"    float border_fade = %s_border_fade (t, %s_texdims.x);\n"
		"    vec4 texel = texture2D%s (%s_sampler, vec2 (t, 0.5));\n"
		"    return mix (vec4 (0.0), texel * border_fade, is_valid);\n"
		"}\n",
		namestr, namestr, rectstr, namestr);
	}
	else
	{
	    _cairo_output_stream_printf (stream,
		"    return mix (vec4 (0.0), texture2D%s (%s_sampler, vec2(t, 0.5)), is_valid);\n"
		"}\n",
		rectstr, namestr);
	}
	break;
    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_NONE:
	_cairo_output_stream_printf (stream,
	    "varying vec2 %s_texcoords;\n"
	    "uniform vec2 %s_texdims;\n"
	    "uniform sampler2D%s %s_sampler;\n"
	    "uniform vec3 %s_circle_d;\n"
	    "uniform float %s_a;\n"
	    "uniform float %s_radius_0;\n"
	    "\n"
	    "vec4 get_%s()\n"
	    "{\n"
	    "    vec3 pos = vec3 (%s_texcoords, %s_radius_0);\n"
	    "    \n"
	    "    float B = dot (pos, %s_circle_d);\n"
	    "    float C = dot (pos, vec3 (pos.xy, -pos.z));\n"
	    "    \n"
	    "    float det = dot (vec2 (B, %s_a), vec2 (B, -C));\n"
	    "    float sqrtdet = sqrt (abs (det));\n"
	    "    vec2 t = (B + vec2 (sqrtdet, -sqrtdet)) / %s_a;\n"
	    "    \n"
	    "    vec2 is_valid = step (vec2 (0.0), t) * step (t, vec2(1.0));\n"
	    "    float has_color = step (0., det) * max (is_valid.x, is_valid.y);\n"
	    "    \n"
	    "    float upper_t = mix (t.y, t.x, is_valid.x);\n",
	    namestr, namestr, rectstr, namestr, namestr, namestr, namestr,
	    namestr, namestr, namestr, namestr, namestr, namestr);
	if (ctx->gl_flavor == CAIRO_GL_FLAVOR_ES &&
	    _cairo_gl_shader_needs_border_fade (op))
	{
	    _cairo_output_stream_printf (stream,
		"    float border_fade = %s_border_fade (upper_t, %s_texdims.x);\n"
		"    vec4 texel = texture2D%s (%s_sampler, vec2 (upper_t, 0.5));\n"
		"    return mix (vec4 (0.0), texel * border_fade, has_color);\n"
		"}\n",
		namestr, namestr, rectstr, namestr);
	}
	else
	{
	    _cairo_output_stream_printf (stream,
		"    return mix (vec4 (0.0), texture2D%s (%s_sampler, vec2 (upper_t, 0.5)), has_color);\n"
		"}\n",
		rectstr, namestr);
	}
	break;
	*/
    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_NONE_CIRCLE_IN_CIRCLE:
	case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_PAD_CIRCLE_IN_CIRCLE:
	_cairo_output_stream_printf (stream,
		"uniform vec2 %s_stops[30];\n"
		"uniform vec4 %s_colors[30];\n"
		"uniform float %s_offsets[30];\n"
		"uniform int %s_nstops;\n"
		"uniform vec3 %s_circle_1;\n"
		"uniform vec3 %s_circle_2;\n"
		"uniform vec2 %s_scales;\n"
		"uniform int %s_pad;\n"
		"uniform vec2 %s_matrix_1[3];\n"
		"uniform int %s_moved_center;\n"
		"uniform vec2 %s_endpoint;\n"
		"\n"
		"vec2 %s_xy1;\n"
		"vec2 %s_xy2;\n"
		"float %s_r1;\n"
		"float %s_r2;\n"
		"float %s_dr1;\n"
		"float %s_dr2;\n"
        "float %s_precision_scale = 100.0;\n"
		"int %s_determine_opp(vec2 coord)\n"
		"{\n"
		"  float x1, y1;\n"
		"  float new_y1 = coord.y + %s_matrix_1[2].y;\n"
		"  float new_x1 = coord.x + %s_matrix_1[2].x;\n"
		"  //y1 = %s_matrix_1[1].x * new_x1 + %s_matrix_1[1].y * new_y1;\n"
		"  x1 = %s_matrix_1[0].x * new_x1 + %s_matrix_1[0].y * new_y1;\n"
		"  if(x1 >= 0.0)\n" 
		"    return 1;\n"
		"  else\n"
		"    return 0;\n"
		"}\n"
		"vec2 %s_get_xy(vec2 coord)\n"
		"{\n"
		"  vec2 center = %s_xy1;\n"
		"  if(%s_moved_center == 1)\n"
		"    center = %s_endpoint;\n"
		"  if(%s_r1 == 0.0)\n"
		"    return center;\n"
		"  vec2 f = vec2(coord.xy - center);\n"
		"  float d = inversesqrt(dot(f, f));\n"
		"  float x = %s_r1 * f.x * d / %s_scales[0] + center.x;\n"
		"  float y = %s_r1 * f.y * d / %s_scales[1] + center.y;\n"
		"  return vec2(x, y);\n"
		"}\n"
		"\n"
		"float %s_get_distance_from_center(vec2 coord, vec2 center)\n"
		"{\n"
		"  vec2 f = vec2((coord - center) * %s_scales);\n"
		"  //vec2 f = vec2(coord - center);\n"
		"  return sqrt(dot(f, f));\n"
		"}\n"
		"float %s_get_distance_from_start(vec2 coord)\n"
		"{\n"
		"  vec2 focal = vec2(%s_xy1.x, %s_xy1.y);\n"
		"  vec2 cen = vec2(%s_xy2.x, %s_xy2.y);\n"
		"  float dis_from_focal = %s_get_distance_from_center(coord, focal);\n"
		"  float dis_from_center = %s_get_distance_from_center(coord, cen);\n"
		"  if(dis_from_focal <= %s_r1)\n"
		"  {\n"
		"      return 0.0;\n"
		"  }\n"
		"  else if(dis_from_center >= %s_r2)\n"
		"  {\n"
		"      return 1.0;\n"
		"  }\n"
		"  else \n"
		"  {\n"
		"    vec2 xy = %s_get_xy(coord.xy);\n"
		"    vec2 f1 = (coord - xy) * %s_scales; // dx, dy\n"
		"    vec2 f2 = (xy - %s_xy2.xy) * %s_scales; // fx, fy\n"
		"    float d1 = dot(f1, f2);\n"
		"    float d2 = dot(f1, f1) * pow(%s_r2, 2.0);\n"
		"    float d31 = (f1.x * f2.y - f1.y * f2.x);\n"
		"    float d3 = d31 * d31;\n"
		"    float d4 = sqrt(d2 - d3);\n"
		"    float d5 = 1.0 /(pow(%s_r2, 2.0) - dot(f2, f2));\n"
		"    float d6 = (d1 + d4) * d5;\n"
		"    return d6;\n"
		"  }\n"
		"}\n"
		"int %s_i;\n"
		"float %s_relative_dis;\n"
		"float %s_percent;\n"

		"float %s_dis;\n"
		"int %s_reverse;\n"
		"vec4 %s_get_color(vec2 coord)\n"
		"{\n"
        "  coord /= %s_precision_scale;\n"
		"  %s_reverse = 0;\n"
		"  %s_r1 = %s_circle_1.z;\n"
		"  %s_r2 = %s_circle_2.z;\n"
		"  %s_xy1.xy = %s_circle_1.xy;\n"
		"  %s_xy2.xy = %s_circle_2.xy;\n"
		"  if(%s_circle_1[2] > %s_circle_2[2])\n"
		"  {\n"
		"    %s_reverse = 1;\n"
		"    %s_r1 = %s_circle_2.z;\n"
		"    %s_r2 = %s_circle_1.z;\n"
		"    %s_xy1.xy = %s_circle_2.xy;\n"
		"    %s_xy2.xy = %s_circle_1.xy;\n"
		"    %s_xy2.y = %s_circle_1.y;\n"
		"  }\n"
		"  if(%s_moved_center == 1)\n"
		"  {\n"
		"    int opp = %s_determine_opp(coord);\n"
		"    if(opp == 0)\n"
		"      return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"  }\n"
		"  %s_dis = %s_get_distance_from_start(coord);\n"
		"  if(%s_reverse == 1)\n"
		"  {\n"
		"    %s_dis = 1.0 - %s_dis;\n"
		"  }\n"
		"  if(%s_dis >= 1.0 || %s_dis >= %s_offsets[%s_nstops-1])\n"
		"  {\n"
		"    if(%s_pad == 1)\n"
		"      return %s_colors[%s_nstops-1];\n"
		"    else\n"
		"      return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"  }\n"
		"  else if(%s_dis <= 0.0 || %s_dis <= %s_offsets[0])\n"
		"  {\n"
		"    if(%s_pad == 1)\n"
		"      return %s_colors[0];\n"
		"    else\n"
		"      return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"  }\n"
		"\n"
		"  if(%s_nstops == 2)\n"
		"  {\n"
		"    %s_relative_dis = %s_dis - %s_offsets[0];\n"
		"    float d = 1.0 / (%s_offsets[1] - %s_offsets[0]);\n"
		"    %s_percent = %s_relative_dis * d ;\n"
		"    return (%s_colors[1] - %s_colors[0]) * %s_percent + %s_colors[0];\n"
		"  }\n"
		"  for(%s_i = 1; %s_i < %s_nstops; %s_i++)\n"
		"  {\n"
		"    if(%s_dis <= %s_offsets[%s_i])\n"
		"    {\n"
		"      %s_relative_dis = %s_dis - %s_offsets[%s_i-1];\n"
		"      %s_percent = %s_relative_dis / (%s_offsets[%s_i] - %s_offsets[%s_i-1]);\n"
		"      return (%s_colors[%s_i] - %s_colors[%s_i-1]) * %s_percent + %s_colors[%s_i-1];\n"
		"    }\n"
		"  }\n"
		"  return %s_colors[%s_nstops-1];\n"
		"}\n"
		"vec4 get_%s()\n"
		"{\n"
		"	vec4 color = %s_get_color(gl_FragCoord.xy);\n"
        "   vec4 alpha = vec4(color.a, color.a, color.a, 1.0);\n" 
        "   return color * alpha;\n"
		"}\n",
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr);
	break;
	case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REPEAT_CIRCLE_IN_CIRCLE:
	case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REFLECT_CIRCLE_IN_CIRCLE:
	_cairo_output_stream_printf (stream,
		"uniform vec2 %s_stops[30];\n"
		"uniform vec4 %s_colors[30];\n"
		"uniform float %s_offsets[30];\n"
		"uniform int %s_nstops;\n"
		"uniform vec3 %s_circle_1;\n"
		"uniform vec3 %s_circle_2;\n"
		"uniform vec2 %s_scales;\n"
		"uniform int %s_pad;\n"
		"uniform vec2 %s_matrix_1[3];\n"
		"uniform int %s_moved_center;\n"
		"uniform vec2 %s_endpoint;\n"
		"\n"
		"vec2 %s_xy1;\n"
		"vec2 %s_xy2;\n"
		"float %s_r1;\n"
		"float %s_r2;\n"
		"float %s_dr1;\n"
		"float %s_dr2;\n"
        "float %s_precision_scale = 100.0;\n"
		"int %s_determine_opp(vec2 coord)\n"
		"{\n"
		"  float x1, y1;\n"
		"  float new_y1 = coord.y + %s_matrix_1[2].y;\n"
		"  float new_x1 = coord.x + %s_matrix_1[2].x;\n"
		"  //y1 = %s_matrix_1[1].x * new_x1 + %s_matrix_1[1].y * new_y1;\n"
		"  x1 = %s_matrix_1[0].x * new_x1 + %s_matrix_1[0].y * new_y1;\n"
		"  if(x1 >= 0.0)\n" 
		"    return 1;\n"
		"  else\n"
		"    return 0;\n"
		"}\n"
		"vec2 %s_get_xy(vec2 coord)\n"
		"{\n"
		"  vec2 center = %s_xy1;\n"
		"  //if(%s_moved_center == 1)\n"
		"  //  center = %s_endpoint;\n"
		"  if(%s_r1 == 0.0)\n"
		"    return center;\n"
		"  vec2 f = vec2(coord.xy - center);\n"
		"  float d = inversesqrt(dot(f, f));\n"
		"  float x = %s_r1 * f.x * d / %s_scales[0] + center.x;\n"
		"  float y = %s_r1 * f.y * d / %s_scales[1] + center.y;\n"
		"  return vec2(x, y);\n"
		"}\n"
		"vec3 %s_next_big_circle(vec3 small, vec3 big)\n"
		"{\n"
		"  vec2 xy1 = small.xy;\n"
		"  vec2 xy2 = big.xy;\n"
		"  vec2 dxy = big.xy - small.xy;\n"
		"  dxy.x = dxy.x * %s_scales[0];\n"
		"  dxy.y = dxy.y * %s_scales[1];\n"
		"  float d = sqrt(dot(dxy, dxy));\n"
		"  float r = (2.0 * big.z + %s_dr1 + %s_dr2) / 2.0;\n"
		"  float x = xy2.x + (dxy.x / d * (%s_dr2 - %s_dr1) * 0.5) / %s_scales[0];\n"
		"  float y = xy2.y + (dxy.y / d * (%s_dr2 - %s_dr1) * 0.5) / %s_scales[1];\n"
		"  return vec3(x, y, r);\n"
		"}\n"
		"vec3 %s_next_small_circle(vec3 small, vec3 big)\n"
		"{\n"
		"  float x, y;\n"
		"  vec2 xy1 = small.xy;\n"
		"  vec2 xy2 = big.xy;\n"
		"  vec2 dxy = big.xy - small.xy;\n"
		"  dxy.x = dxy.x * %s_scales[0];\n"
		"  dxy.y = dxy.y * %s_scales[1];\n"
		"  float d = sqrt(dot(dxy, dxy));\n"
		"  float r = (2.0 * small.z - %s_dr1 - %s_dr2) / 2.0;\n"
		"  if(r < 0.0)\n"
		"  {\n"
		"    float tr1 = %s_dr1 + %s_dr2;\n"
		"    float tr2 = %s_dr1 - %s_dr2;\n"
		"    x = xy1.x + (dxy.x / d * small.z * tr2 / tr1) / %s_scales[0];\n"
		"    y = xy1.y + (dxy.y / d * small.z * tr2 / tr1) / %s_scales[1];\n"
		"  }\n"
		"  else\n"
		"  {\n"
		"    x = xy1.x + (dxy.x / d * (%s_dr1 - %s_dr2) * 0.5) / %s_scales[0];\n"
		"    y = xy1.y + (dxy.y / d * (%s_dr1 - %s_dr2) * 0.5) / %s_scales[1];\n"
		"  }\n"
		"  return vec3(x, y, r);\n"
		"}\n"
		"\n"
		"float %s_get_distance_from_center(vec2 coord, vec2 center)\n"
		"{\n"
		"  vec2 f = vec2((coord - center) * %s_scales);\n"
		"  //vec2 f = vec2(coord - center);\n"
		"  return sqrt(dot(f, f));\n"
		"}\n"
		"float %s_get_distance_from_start(vec2 coord)\n"
		"{\n"
		"  vec2 focal = vec2(%s_xy1.x, %s_xy1.y);\n"
		"  vec2 cen = vec2(%s_xy2.x, %s_xy2.y);\n"
		"  float dis_from_focal = %s_get_distance_from_center(coord, focal);\n"
		"  float dis_from_center = %s_get_distance_from_center(coord, cen);\n"
		"  if(dis_from_focal <= %s_r1)\n"
		"  {\n"
		"    if(%s_r1 == 0.0)\n"
		"      return 0.0;\n"
		"    else\n"
		"      return dis_from_focal / %s_r1 - 1.0;\n"
		"  }\n"
		"  else if(dis_from_center >= %s_r2)\n"
		"  {\n"
		"    return dis_from_center / %s_r2;\n"
		"  }\n"
		"  else \n"
		"  {\n"
		"    vec2 xy = %s_get_xy(coord.xy);\n"
		"    vec2 f1 = (coord - xy) * %s_scales; // dx, dy\n"
		"    vec2 f2 = (xy - %s_xy2.xy) * %s_scales; // fx, fy\n"
		"    float d1 = dot(f1, f2);\n"
		"    float d2 = dot(f1, f1) * pow(%s_r2, 2.0);\n"
		"    float d31 = (f1.x * f2.y - f1.y * f2.x);\n"
		"    float d3 = d31 * d31;\n"
		"    float d4 = sqrt(d2 - d3);\n"
		"    float d5 = 1.0 /(pow(%s_r2, 2.0) - dot(f2, f2));\n"
		"    float d6 = (d1 + d4) * d5;\n"
		"    return d6;\n"
		"  }\n"
		"}\n"
		"int %s_i;\n"
		"float %s_relative_dis;\n"
		"float %s_percent;\n"

		"float %s_dis;\n"
		"int %s_reverse;\n"
		"vec4 %s_get_color(vec2 coord)\n"
		"{\n"
        "  coord /= %s_precision_scale;\n"
		"  %s_reverse = 0;\n"
		"  %s_r1 = %s_circle_1.z;\n"
		"  %s_r2 = %s_circle_2.z;\n"
		"  %s_xy1.xy = %s_circle_1.xy;\n"
		"  %s_xy2.xy = %s_circle_2.xy;\n"
		"  if(%s_moved_center == 1)\n"
		"  {\n"
		"    int opp = %s_determine_opp(coord);\n"
		"    if(opp == 0)\n"
		"      return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"  }\n"
		"  if(%s_circle_1[2] > %s_circle_2[2])\n"
		"  {\n"
		"    %s_reverse = 1;\n"
		"    %s_r1 = %s_circle_2.z;\n"
		"    %s_r2 = %s_circle_1.z;\n"
		"    %s_xy1.xy = %s_circle_2.xy;\n"
		"    %s_xy2.xy = %s_circle_1.xy;\n"
		"    %s_xy2.y = %s_circle_1.y;\n"
		"  }\n"
		"  %s_dis = %s_get_distance_from_start(coord);\n"
		"  float counter = 0.0;\n"
		"  int bigger = 1;\n"
		"  vec3 next_circle;\n"
		"  vec3 small, big;\n"
		"  vec2 dxy = %s_xy2.xy - %s_xy1.xy;\n"
		"  dxy.x = dxy.x * %s_scales[0];\n"
		"  dxy.y = dxy.y * %s_scales[1];\n"
		"  float dr = %s_r2 - %s_r1;\n"
		"  float d = sqrt(dot(dxy, dxy));\n"
		"  %s_dr1 = %s_r2 - %s_r1 - d;\n"
		"  %s_dr2 = %s_r2 - %s_r1 + d;\n"
		"  if(%s_dis > 1.0)\n"
		"  {\n"
		"    while(%s_dis > 1.0)\n"
		"    {\n"
		"      small.xy = %s_xy1.xy;\n"
		"      small.z = %s_r1;\n"
		"      big.xy = %s_xy2.xy;\n"
		"      big.z = %s_r2;\n"
		"      next_circle = %s_next_big_circle(small, big);\n"
		"      %s_xy1.xy = %s_xy2.xy;\n"
		"      %s_r1 = %s_r2;\n"
		"      %s_xy2.xy = next_circle.xy;\n"
		"      %s_r2 = next_circle.z;\n"
		"      %s_dis = %s_get_distance_from_start(coord);\n"
		"      counter += 1.0;\n"
		"    }\n"
		"  }\n"
		"  else if(%s_dis < 0.0)\n"
		"  {\n"
		"    bigger = 0;\n"
		"    while(%s_dis < 0.0)\n"
		"    {\n"
		"      small.xy = %s_xy1.xy;\n"
		"      small.z = %s_r1;\n"
		"      big.xy = %s_xy2.xy;\n"
		"      big.z = %s_r2;\n"
		"      next_circle = %s_next_small_circle(small, big);\n"
		"      %s_xy2.xy = %s_xy1.xy;\n"
		"      %s_r2 = %s_r1;\n"
		"      %s_xy1.xy = next_circle.xy;\n"
		"      %s_r1 = next_circle.z;\n"
		"      %s_dis = %s_get_distance_from_start(coord);\n"
		"      counter += 1.0;\n"
		"      if(%s_r1 < 0.0)\n"
		"      {\n"
		"        %s_dis = (1.0 + %s_r1 / big.z) * %s_dis - %s_r1 / big.z;\n"
		"        break;\n"
		"      }\n"
		"    }\n"
		"  }\n"
		"  if(%s_reverse == 1)\n"
		"    %s_dis = 1.0 - %s_dis;\n"
		"  if(%s_pad == 3) // reflect\n"
		"  {\n"
		"    if(mod(counter, 2.0) != 0.0)\n"
		"      %s_dis = 1.0 - %s_dis;\n"
		"  }\n"
		"\n"
		"  if(%s_nstops == 2)\n"
		"  {\n"
		"    %s_relative_dis = %s_dis - %s_offsets[0];\n"
		"    float d = 1.0 / (%s_offsets[1] - %s_offsets[0]);\n"
		"    %s_percent = %s_relative_dis * d ;\n"
		"    return (%s_colors[1] - %s_colors[0]) * %s_percent + %s_colors[0];\n"
		"  }\n"
		"  for(%s_i = 1; %s_i < %s_nstops; %s_i++)\n"
		"  {\n"
		"    if(%s_dis <= %s_offsets[%s_i])\n"
		"    {\n"
		"      %s_relative_dis = %s_dis - %s_offsets[%s_i-1];\n"
		"      %s_percent = %s_relative_dis / (%s_offsets[%s_i] - %s_offsets[%s_i-1]);\n"
		"      return (%s_colors[%s_i] - %s_colors[%s_i-1]) * %s_percent + %s_colors[%s_i-1];\n"
		"    }\n"
		"  }\n"
		"  return %s_colors[%s_nstops-1];\n"
		"}\n"
		"vec4 get_%s()\n"
		"{\n"
		"	vec4 color = %s_get_color(gl_FragCoord.xy);\n"
        "   vec4 alpha = vec4(color.a, color.a, color.a, 1.0);\n" 
        "   return color * alpha;\n"
		"}\n",
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr);
/*
	    "varying vec2 %s_texcoords;\n"
	    "uniform sampler2D%s %s_sampler;\n"
	    "uniform vec3 %s_circle_d;\n"
	    "uniform float %s_a;\n"
	    "uniform float %s_radius_0;\n"
	    "\n"
	    "vec4 get_%s()\n"
	    "{\n"
	    "    vec3 pos = vec3 (%s_texcoords, %s_radius_0);\n"
	    "    \n"
	    "    float B = dot (pos, %s_circle_d);\n"
	    "    float C = dot (pos, vec3 (pos.xy, -pos.z));\n"
	    "    \n"
	    "    float det = dot (vec2 (B, %s_a), vec2 (B, -C));\n"
	    "    float sqrtdet = sqrt (abs (det));\n"
	    "    vec2 t = (B + vec2 (sqrtdet, -sqrtdet)) / %s_a;\n"
	    "    \n"
	    "    vec2 is_valid = step (vec2 (-%s_radius_0), t * %s_circle_d.z);\n"
	    "    float has_color = step (0., det) * max (is_valid.x, is_valid.y);\n"
	    "    \n"
	    "    float upper_t = mix (t.y, t.x, is_valid.x);\n"
	    "    return mix (vec4 (0.0), texture2D%s (%s_sampler, vec2 (upper_t, 0.5)), has_color);\n"
	    "}\n",
	    namestr, rectstr, namestr, namestr, namestr, namestr,
	    namestr, namestr, namestr, namestr, namestr,
	    namestr, namestr, namestr, rectstr, namestr);
	*/
	break;
    case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_NONE_CIRCLE_NOT_IN_CIRCLE:
	case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_PAD_CIRCLE_NOT_IN_CIRCLE:
	case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REPEAT_CIRCLE_NOT_IN_CIRCLE:
	case CAIRO_GL_OPERAND_RADIAL_GRADIENT_EXT_REFLECT_CIRCLE_NOT_IN_CIRCLE:
	_cairo_output_stream_printf (stream,
		"uniform vec2 %s_stops[30];\n"
		"uniform vec4 %s_colors[30];\n"
		"uniform float %s_offsets[30];\n"
		"uniform int %s_nstops;\n"
		"uniform vec3 %s_circle_1;\n"
		"uniform vec3 %s_circle_2;\n"
		"uniform vec2 %s_scales;\n"
		"uniform int %s_pad;\n"
		"uniform vec2 %s_tangents[4];\n"
		"uniform vec2 %s_tangents_end[2];\n"
		"uniform vec2 %s_endpoint;\n"
		"uniform vec2 %s_matrix_1[3];\n"
		"uniform vec2 %s_matrix_2[3];\n"
		"vec2 start1, start;\n"
		"vec2 %s_end;\n"
        "float %s_precision_scale = 100.0;\n"
		"int %s_determine_opp(vec2 coord)\n"
		"{\n"
		"  float x1, y1, y2;\n"
		"  float new_y1 = coord.y + %s_matrix_1[2].y;\n"
		"  float new_y2 = coord.y + %s_matrix_2[2].y;\n"
		"  float new_x1 = coord.x + %s_matrix_1[2].x;\n"
		"  float new_x2 = coord.x + %s_matrix_2[2].x;\n"
		"  y1 = %s_matrix_1[1].x * new_x1 + %s_matrix_1[1].y * new_y1;\n"
		"  y2 = %s_matrix_2[1].x * new_x2 + %s_matrix_2[1].y * new_y2;\n"
		"  if(%s_circle_1.z != %s_circle_2.z)\n"
		"  {\n"
		"    if(y1 >= 0.0 && y2 <= 0.0)\n"
		"      return 1;\n"
		"    else\n"
		"      return 0;\n"
		"  }\n"
		"  else\n"
		"  {\n"
		"    x1 = %s_matrix_1[0].x * new_x1 + %s_matrix_1[0].y * new_y1;\n"
		"    if(x1 >= 0.0)\n" 
		"    {\n"
		"      if(y1 >= 0.0 && y2 <= 0.0)\n"
		"        return 1;\n"
		"      else\n"
		"        return 0;\n"
		"    }\n"
		"    else\n"
		"    {\n"
		"      if(y1 >= 0.0 && y2 <= 0.0)\n"
		"        return -1;\n"
		"      else\n"
		"        return 0;\n"
		"    }\n"
		"  }\n"
		"}\n"
		"vec3 %s_compute_endpoint(vec2 coord)\n"
		"{\n"
		"  float dx = %s_circle_1[0] - %s_circle_2[0];\n"
		"  float dy = %s_circle_1[1] - %s_circle_2[1];\n"
		"  float r = %s_circle_1[2];\n"
		"  float m1, n1, m2, n2;\n"
		"  float x, y;\n"
		"  float x1, y1, x2, y2;\n"
		"  if(dx == 0.0)\n"
		"  {\n"
		"    if(coord.x > %s_tangents_end[0].x && coord.x > %s_tangents_end[1].x)\n"
		"      return vec3(0.0, 0.0, 0.0);\n"
		"    else if(coord.x < %s_tangents_end[0].x && coord.x < %s_tangents_end[1].x)\n"
		"      return vec3(0.0, 0.0, 0.0);\n"
		"    else\n"
		"      return vec3(coord.x, %s_tangents_end[0].y, 1.0);\n"
		"  }\n"
		"  else if(dy == 0.0)\n"
		"  {\n"
		"    if(coord.y > %s_tangents_end[0].y && coord.y > %s_tangents_end[1].y)\n"
		"      return vec3(0.0, 0.0, 0.0);\n"
		"    else if(coord.y < %s_tangents_end[0].y && coord.y < %s_tangents_end[1].y)\n"
		"      return vec3(0.0, 0.0, 0.0);\n"
		"    else\n"
		"      return vec3(%s_tangents_end[0].x, coord.y, 1.0);\n"
		"  }\n"
		"  m1 = dy / dx;\n"
		"  n1 = %s_tangents_end[0].y - m1 * %s_tangents_end[0].x;\n"
		"  n2 = %s_tangents_end[1].y - m1 * %s_tangents_end[1].x;\n"
		"  y1 = m1 * coord.x + n1 - coord.y;\n"
		"  y2 = m1 * coord.x + n2 - coord.y;\n"
		"  if(y1 > 0.0 && y2 > 0.0)\n"
		"    return vec3(0.0, 0.0, 0.0);\n"
		"  else if(y1 < 0.0 && y2 < 0.0)\n"
		"    return vec3(0.0, 0.0, 0.0);\n"
		"  n1 = coord.y - m1 * coord.x;\n"
		"  m2 = (%s_tangents_end[0].y - %s_tangents_end[1].y) / (%s_tangents_end[0].x - %s_tangents_end[1].x);\n"
		"  n2 = %s_tangents_end[0].y - m2 * %s_tangents_end[0].x;\n"
		"  x = (n1 - n2) / (m2 - m1);\n"
		"  y = m1 * x + n1;\n"
		"  return vec3(x, y, 1.0);\n"
		"}\n"
		"vec3 %s_compute_abc(vec2 coord, float radius, vec2 center, int angle)\n"
		"{\n"
		"  // if angle == 0, compute x, then y\n"
		"  float dx1, dy1;\n"
		"  float a, b, c;\n"
		"  float r1, r2;\n"
		"  float squareB;\n"
		"  float dis1, dis2;\n"
		"  float x1, y1, x2, y2;\n"
		"  r1 = radius / %s_scales[0];\n"
		"  r2 = radius / %s_scales[1];\n"
		"  //r1 = radius; r2 = radius;\n"
		"  r1 = r1 * r1;\n"
		"  r2 = r2 * r2;\n"
		"  dx1 = %s_end.x - coord.x;\n"
		"  dy1 = %s_end.y - coord.y;\n"
		"  a = r2 * dx1 * dx1 + dy1 * dy1 * r1;\n"
		"  if(angle == 0)\n"
		"  {\n"
		"    b = -2.0 * r1 * dy1 * (dy1 * %s_end.x + dx1 * center.y - dx1 * %s_end.y) - 2.0 * dx1 * dx1 * r2 * center.x;\n"
		"    c = r1 * (dy1 * %s_end.x + dx1 * center.y - dx1 * %s_end.y) * (dy1 * %s_end.x + dx1 * center.y - dx1 * %s_end.y) + center.x * center.x * r2 * dx1 * dx1 - dx1 * dx1 * r1 * r2;\n"
		"  }\n"
		"  else\n"
		"  {\n"
		"    b = -2.0 * r2 * dx1 * (dx1 * %s_end.y + dy1 * center.x - dy1 * %s_end.x) - 2.0 * dy1 * dy1 * r1 * center.y;\n"
		"    c = r2 * (dx1 * %s_end.y + dy1 * center.x - dy1 * %s_end.x) * (dx1 * %s_end.y + dy1 * center.x - dy1 * %s_end.x) + center.y * center.y * r1 * dy1 * dy1 - dy1 * dy1 * r1 * r2;\n"
		"  }\n"
		"  //B = -2.0 * r2 * dx1 * (dx1 * coord.y + dy1 * center.x - dy1 * coord.x) - 2.0 * dy1 * dy1 * r1 * center.y * center.y;\n"
		"  //C = r2 * (dx1 * coord.y + dy1 * center.x - dy1 * coord.x) * (dx1 * coord.y + dy1 * center.x - dy1 * coord.x) + center.y * center.y * r1 * dy1 * dy1 - dy1 * dy1 * r1 * r2;\n"
		"  return vec3(a, b, c);\n"
		"}\n"
		"float %s_in_zone(vec3 abc)\n"
		"{\n"
		"  return abc.y * abc.y - 4.0 * abc.x * abc.z;\n"
		"}\n"
		"vec2 %s_compute_xy_1(vec2 coord, vec2 center, float r, vec3 ABC, float SQRTB, int angle)\n"
		"{\n"
		"  float x, y, dx, dy;\n"
		"  float t;\n"
		"  float r1, r2;\n"
		"  r1 = r / %s_scales[0];\n"
		"  r2 = r / %s_scales[1];\n"
		"  dx = %s_end.x - coord.x;\n"
		"  dy = %s_end.y - coord.y;\n"
		"\n"
		"  if(dy == 0.0)\n"
		"  {\n"
		"    y = coord.y;\n"
		"    t = (y - center.y) * (y - center.y);\n"
		"    x = sqrt(1.0 - t / (r2 * r2)) * r1 + center.x;\n"
		"    //x = dx / dy * (y - coord.y) + coord.x;\n"
		"    //x = r / %s_scales[0] + center.x;\n"
		"    return vec2(x, y);\n"
		"  }\n"
		"  else if(dx == 0.0)\n"
		"  {\n"
		"    x = coord.x;\n"
		"    //y = (-ABC.y + SQRTB)/(2.0 * ABC.x);\n"
		"    t = (x - center.x) * (x - center.x);\n"
		"    y = sqrt(1.0 - t / (r1 * r1)) * r2 + center.y;\n"
		"    //y = r / %s_scales[1] + center.y;\n"
		"    return vec2(x, y);\n"
		"  }\n"
		"\n"
		"  if(ABC.x == 0.0)\n"
		"  {\n"
		"    if(angle == 0)\n"
		"      x = -ABC.z / ABC.y;\n"
		"    else\n"
		"      y = -ABC.z / ABC.y;\n"
		"  }\n"
		"  else\n"
		"  {\n"
		"    if(angle == 0)\n"
		"      x = (-ABC.y - SQRTB)/(2.0 * ABC.x);\n"
		"    else\n"
		"      y = (-ABC.y - SQRTB)/(2.0 * ABC.x);\n"
		"  }\n"
		"  //t = (y - center.y) * (y - center.y);\n"
		"  //x = -sqrt(1.0 - t / (r2 * r2)) * r1 + center.x;\n"
		"  //float t = (y - center.y)/(r/%s_scales[1]);\n"
		"  //x = sqrt(1.0 - (y - t * t)) * r /%s_scales[0] + coord.x;\n"
		"  if(angle == 0)\n"
		"    y = dy / dx * (x - %s_end.x) + %s_end.y;\n"
		"  else\n"
		"    x = dx / dy * (y - coord.y) + coord.x;\n"
		"  return vec2(x, y);\n"
		"}\n"
		"vec2 %s_compute_xy_2(vec2 coord, vec2 center, float r, vec3 ABC, float SQRTB, int angle)\n"
		"{\n"
		"  float x, y, dx, dy;\n"
		"  float t;\n"
		"  float r1, r2;\n"
		"  r1 = r / %s_scales[0];\n"
		"  r2 = r / %s_scales[1];\n"
		"  dx = %s_end.x - coord.x;\n"
		"  dy = %s_end.y - coord.y;\n"
		"\n"
		"  if(dy == 0.0)\n"
		"  {\n"
		"    y = coord.y;\n"
		"    t = (y - center.y) * (y - center.y);\n"
		"    x = -sqrt(1.0 - t / (r2 * r2)) * r1 + center.x;\n"
		"    //x = dx / dy * (y - coord.y) + coord.x;\n"
		"    //x = -r / %s_scales[0] + center.x;\n"
		"    return vec2(x, y);\n"
		"  }\n"
		"  else if(dx == 0.0)\n"
		"  {\n"
		"    x = coord.x;\n"
		"    t = (x - center.x) * (x - center.x);\n"
		"    y = -sqrt(1.0 - t / (r1 * r1)) * r2 + center.y;\n"
		"    //y = (-ABC.y - SQRTB)/(2.0 * ABC.x);\n"
		"    //y = -r / %s_scales[1] + center.y;\n"
		"    return vec2(x, y);\n"
		"  }\n"
		"\n"
		"  if(ABC.x == 0.0)\n"
		"  {\n"
		"    if(angle == 0)\n"
		"      x = -ABC.z / ABC.y;\n"
		"    else\n"
		"      y = -ABC.z / ABC.y;\n"
		"  }\n"
		"  else\n"
		"  {\n"
		"    if(angle == 0)\n"
		"      x = (-ABC.y + SQRTB)/(2.0 * ABC.x);\n"
		"    else\n"
		"      y = (-ABC.y + SQRTB)/(2.0 * ABC.x);\n"
		"  }\n"
		"  //t = (y - center.y) * (y - center.y);\n"
		"  //x = +sqrt(1.0 - t / (r2 * r2)) * r1 + center.x;\n"
		"  if(angle == 0)\n"
		"    y = dy / dx * (x - %s_end.x) + %s_end.y;\n"
		"  else\n"
		"    x = dx / dy * (y - coord.y) + coord.x;\n"
		"  return vec2(x, y);\n"
		"}\n"
		"int %s_i;\n"
		"float %s_relative_dis;\n"
		"float %s_percent;\n"
		"float %s_dis;\n"
		"float %s_distance(vec2 from, vec2 to)\n"
		"{\n"
		"    float dx = from.x - to.x;\n"
		"    float dy = from.y - to.y;\n"
		"    return sqrt(dx * dx + dy * dy);\n"
		"}\n"
		"vec4 %s_get_color(vec2 coord)\n"
		"{\n"
		"  vec3 abc;\n"
		"  float sqrtb;\n"
		"  vec2 center;\n"
		"  vec2 tempvec;\n"
		"  float temp;\n"
		"  float dx, dy;\n"
		"  int angle;\n"
		"  vec2 circle_1_near, circle_1_far, circle_2_near, circle_2_far;\n"
		"  float dis_1_near, dis_1_far, my_dis, dis_2_near, dis_2_far;\n"
		"  int opp;\n"
        "  coord /= %s_precision_scale;\n"
		"  opp = %s_determine_opp(coord);\n"
		"  if(opp == 0)\n"
		"  {\n"
		"      return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"  }\n"
		"  else if(opp == -1)\n"
		"  {\n"
		"    if(%s_pad == 1)\n"
		"      return %s_colors[0];\n"
		"    else if(%s_pad == 0)\n"
		"      return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"  }\n"
		"  center.xy = %s_circle_1.xy;\n"
		"  if(%s_circle_1[2] == %s_circle_2[2])\n"
		"  {\n"
		"    vec3 value = %s_compute_endpoint(coord);\n"
		"    if(value.z == 0.0) // not in area\n"
		"      return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"    %s_end.xy = value.xy;\n"
		"  }\n"
		"  else\n"
		"    %s_end.xy = %s_endpoint.xy;\n"
		"  angle = 0;\n"
		"  dx = %s_end.x - coord.x;\n"
		"  dy = %s_end.y - coord.y;\n"
		"  if(dy == 0.0 && dx == 0.0)\n"
		"    angle = 0;\n"
		"  else if(dy == 0.0)\n"
		"    angle = 0;\n"
		"  else if(dx == 0.0)\n"
		"    angle = 1;\n"
		"  else if(abs(dy/dx) <= 1.0)\n"
		"    angle = 0;\n"
		"  else\n"
		"    angle = 1;\n"
		"\n"
		"  if(%s_circle_1.z != 0.0)\n"
		"  {\n"
		"    if(%s_end.y == coord.y && %s_end.y == %s_circle_1.y)\n"
		"    {\n"
		"      circle_1_near = vec2(%s_circle_1.x + %s_circle_1.z / %s_scales[0], coord.y);\n"
		"      circle_1_far = vec2(%s_circle_1.x - %s_circle_1.z / %s_scales[0], coord.y);\n"
		"    }\n"
		"    else\n"
		"    {\n"
		"      abc = %s_compute_abc(coord, %s_circle_1.z, %s_circle_1.xy, angle);\n"
		"      sqrtb = %s_in_zone(abc);\n"
		"      if(%s_circle_1.z != %s_circle_2.z)\n"
		"      {\n"
		"        //if(sqrtb < 0.0 || (%s_end.y == coord.y && %s_end.y != %s_circle_1.y) || (abc.x == 0.0 && abc.y == 0.0 && abc.z != 0.0))\n"
		"          if(sqrtb < 0.0)\n"
		"          return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"      }\n"
		"      sqrtb = sqrt(sqrtb);\n"
		"      circle_1_near = %s_compute_xy_1(coord, %s_circle_1.xy, %s_circle_1.z, abc, sqrtb, angle);\n"
		"      circle_1_far = %s_compute_xy_2(coord, %s_circle_1.xy, %s_circle_1.z, abc, sqrtb, angle);\n"
		"    }\n"
		"  }\n"
		"  else\n"
		"  {\n"
		"    circle_1_near.xy = %s_circle_1.xy;\n"
		"    circle_1_far.xy = circle_1_near.xy;\n"
		"  }\n"
		"  dis_1_near = %s_distance(circle_1_near, %s_end);\n"
		"  dis_1_far = %s_distance(circle_1_far, %s_end);\n"
		"  if(dis_1_near > dis_1_far)\n"
		"  {\n"
		"    temp = dis_1_near;\n"
		"    dis_1_near = dis_1_far;\n"
		"    dis_1_far = temp;\n"
		"    tempvec.xy = circle_1_near.xy;\n"
		"    circle_1_near.xy = circle_1_far.xy;\n"
		"    circle_1_far.xy = tempvec.xy;\n"
		"  }\n"
		"  my_dis = %s_distance(coord, %s_end);\n"
		"  //float my_dis_circle = %s_distance(circle_1_near, coord);\n"
		"  if(%s_circle_1.z <= %s_circle_2.z)\n"
		"  {\n"
		"    if(my_dis < dis_1_near)\n"
		"    {\n"
		"      if(%s_pad == 0)\n"
		"        return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"      else if(%s_pad == 1)\n"
		"        return %s_colors[0];\n"
		"    }\n"
		"    else if(my_dis == dis_1_near)\n"
		"      return %s_colors[0];\n"
		"  }\n"
		"  if(%s_circle_2.z != 0.0)\n"
		"  {\n"
		"    center.xy = %s_circle_2.xy;\n"
		"    if(%s_end.y == coord.y && %s_end.y == %s_circle_2.y)\n"
		"    {\n"
		"      circle_2_near = vec2(%s_circle_2.x + %s_circle_2.z / %s_scales[0], coord.y);\n"
		"      circle_2_far = vec2(%s_circle_2.x - %s_circle_2.z / %s_scales[0], coord.y);\n"
		"    }\n"
		"    else\n"
		"    {\n"
		"      abc = %s_compute_abc(coord, %s_circle_2.z, center, angle);\n"
		"      sqrtb = %s_in_zone(abc);\n"
		"      if(%s_circle_1.z != %s_circle_2.z)\n"
		"      {\n"
		"      //if(sqrtb < 0.0 || (abc.x == 0.0 && abc.y == 0.0 && abc.z != 0.0) || (%s_end.y == coord.y && %s_end.y != %s_circle_2.y)) \n"
		"       if(sqrtb < 0.0) return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"        //return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"      }\n"
		"      sqrtb = sqrt(sqrtb);\n"
		"      circle_2_near = %s_compute_xy_1(coord, %s_circle_2.xy, %s_circle_2.z, abc, sqrtb, angle);\n"
		"      circle_2_far = %s_compute_xy_2(coord, %s_circle_2.xy, %s_circle_2.z, abc, sqrtb, angle);\n"
		"    }\n"
		"  }\n"
		"  else\n"
		"  {\n"
		"    circle_2_near.xy = %s_circle_2.xy;\n"
		"    circle_2_far.xy = circle_2_near.xy;\n"
		"  }\n"
		"  dis_2_near = %s_distance(circle_2_near, %s_end);\n"
		"  dis_2_far = %s_distance(circle_2_far, %s_end);\n"
		"  if(dis_2_near > dis_2_far)\n"
		"  {\n"
		"    temp = dis_2_near;\n"
		"    dis_2_near = dis_2_far;\n"
		"    dis_2_far = temp;\n"
		"    tempvec.xy = circle_2_near.xy;\n"
		"    circle_2_near.xy = circle_2_far.xy;\n"
		"    circle_2_far.xy = tempvec.xy;\n"
		"  }\n"
		"  float d = dis_2_near - dis_1_near;\n"
		"  if(%s_circle_1.z > %s_circle_2.z)\n"
		"    d = dis_1_far - dis_2_far;\n"
		"\n"
		"  if(%s_circle_1.z > %s_circle_2.z)\n"
		"  {\n"
		"    if(my_dis < dis_2_near)\n"
		"    {\n"
		"      if(%s_pad == 0)\n"
		"        return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"      else if(%s_pad == 1)\n"
		"          return %s_colors[%s_nstops-1];\n"
		"    }\n"
		"    else if(my_dis == dis_2_near)\n"
		"      return %s_colors[%s_nstops-1];\n"
		"  }\n"
		"  if(%s_pad == 2 || %s_pad == 3)\n"
		"  {\n"
		"    if(opp == -1)\n"
		"      my_dis = -my_dis;\n"
		" 	 float rel_dis = my_dis - dis_1_near;\n"
		"    if(%s_pad == 2)\n"
		"    {\n"
		"      if(%s_circle_1.z > %s_circle_2.z)\n"
		"        rel_dis = my_dis - dis_2_far;\n"
		"        %s_dis = fract(rel_dis / d);\n"
		"      if(%s_dis < 0.0)\n"
		"        %s_dis += 1.0;\n"
		"      if(%s_circle_1.z > %s_circle_2.z)\n"
		"        %s_dis = 1.0 - %s_dis;\n"
		"    }\n"
		"    else\n"
		"    {\n"
		"      if(%s_circle_1.z > %s_circle_2.z)\n"
		"        rel_dis = my_dis - dis_2_far;\n"
		"        %s_dis = fract(rel_dis / d);\n"
		"      if(%s_dis < 0.0)\n"
		"        %s_dis += 1.0;\n"
		"      if(%s_circle_1.z > %s_circle_2.z)\n"
		"        %s_dis = 1.0 - %s_dis;\n"
		"      float f = floor(rel_dis / d);\n"
		"      if(mod(f, 2.0) != 0.0)\n"
		"        %s_dis = 1.0 - %s_dis;\n"
		"    }\n"
		"   \n" 
		"  }\n"
		"  else {\n"
		"  if(%s_circle_1.z <= %s_circle_2.z)\n"
		"  {\n"
		"    if(my_dis > dis_2_far)\n"
		"    {\n"
		"      if(%s_pad == 0)\n"
		"        return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"      else if(%s_pad == 1)\n"
		"          return %s_colors[%s_nstops-1];\n"
		"    }\n"
		"    else if(my_dis == dis_2_far)\n"
		"      return %s_colors[%s_nstops-1];\n"
		"    if(dis_1_far <= dis_2_near)\n"
		"    {\n"
		"      if(my_dis > dis_1_near && my_dis <= dis_2_near)\n"
		"        %s_dis = (my_dis - dis_1_near) / (dis_2_near - dis_1_near);\n"
		"      else\n"
		"      {\n"
		"        if(my_dis < dis_2_far)\n"
		"        {\n"
		"          if(%s_pad == 1)\n"
		"            return %s_colors[%s_nstops-1];\n"
		"          else\n"
		"            %s_dis = (my_dis - dis_1_far) / (dis_2_far - dis_1_far);\n"
		"        }\n"
		"      }\n"
		"    }\n"
		"    else // dis_1_far > dis_2_near\n"
		"    {\n"
		"      if(my_dis > dis_1_near && my_dis <= dis_2_near)\n"
		"        %s_dis = (my_dis - dis_1_near) / (dis_2_near - dis_1_near);\n"
		"      else if(my_dis > dis_2_near && my_dis < dis_1_far)\n"
		"      {\n"
		"        if(%s_pad == 1)\n"
		"          return %s_colors[%s_nstops-1];\n"
		"        else\n"
		"          return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"      }\n"
		"      else\n"
		"      {\n"
		"        if(%s_pad == 1)\n"
		"          return %s_colors[%s_nstops-1];\n"
		"        else\n"
		"          %s_dis = (my_dis - dis_1_far) / (dis_2_far - dis_1_far);\n"
		"      }\n"
		"    }\n"
		"  }\n"
		"  else\n"
		"  {\n"
		"    if(my_dis > dis_1_far)\n"
		"    {\n"
		"      if(%s_pad == 0)\n"
		"        return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"      else\n"
		"        return %s_colors[0];\n"
		"    }\n"
		"    else if(my_dis == dis_1_far)\n"
		"      return %s_colors[0];\n"
		"    if(dis_1_near >= dis_2_far)\n"
		"    {\n"
		"      if(my_dis > dis_2_near && my_dis <= dis_2_far)\n"
		"      {\n"
		"        if(%s_pad == 0)\n"
		"          %s_dis = (dis_1_near - my_dis) / (dis_1_near- dis_2_near);\n"
		"        else\n"
		"          return %s_colors[%s_nstops-1];\n"
		"      }\n"
		"      else\n"
		"      {\n"
		"        if(my_dis < dis_1_far)\n"
		"        {\n"
		"            %s_dis = (dis_1_far - my_dis) / (dis_1_far - dis_2_far);\n"
		"        }\n"
		"      }\n"
		"    }\n"
		"    else // dis_1_near < dis_2_far\n"
		"    {\n"
		"      if(my_dis > dis_2_near && my_dis <= dis_1_near)\n"
		"      {\n"
		"        if(%s_pad == 0)\n"
		"          %s_dis = (dis_1_near - my_dis ) / (dis_1_near - dis_2_near);\n"
		"        else\n"
		"          return %s_colors[%s_nstops-1];\n"
		"      }\n"
		"      else if(my_dis > dis_1_near && my_dis < dis_2_far)\n"
		"      {\n"
		"        if(%s_pad == 1)\n"
		"          return %s_colors[%s_nstops-1];\n"
		"        else\n"
		"          return vec4(0.0, 0.0, 0.0, 0.0);\n"
		"      }\n"
		"      else\n"
		"      {\n"
		"          %s_dis = (dis_1_far - my_dis) / (dis_1_far - dis_2_far);\n"
		"      }\n"
		"    }\n"
		"  }\n"
		"  }\n"
		"  if(%s_nstops == 2)\n"
		"  {\n"
		"    %s_relative_dis = %s_dis - %s_offsets[0];\n"
		"    float d = 1.0 / (%s_offsets[1] - %s_offsets[0]);\n"
		"    %s_percent = %s_relative_dis * d ;\n"
		"    return (%s_colors[1] - %s_colors[0]) * %s_percent + %s_colors[0];\n"
		"  }\n"
		"  for(%s_i = 1; %s_i < %s_nstops; %s_i++)\n"
		"  {\n"
		"    if(%s_dis <= %s_offsets[%s_i])\n"
		"    {\n"
		"      %s_relative_dis = %s_dis - %s_offsets[%s_i-1];\n"
		"      %s_percent = %s_relative_dis / (%s_offsets[%s_i] - %s_offsets[%s_i-1]);\n"
		"      return (%s_colors[%s_i] - %s_colors[%s_i-1]) * %s_percent + %s_colors[%s_i-1];\n"
		"    }\n"
		"  }\n"
		"  return %s_colors[%s_nstops-1];\n"
		"}\n"
		"vec4 get_%s()\n"
		"{\n"
		"	vec4 color = %s_get_color(gl_FragCoord.xy);\n"
        "   vec4 alpha = vec4(color.a, color.a, color.a, 1.0);\n" 
        "   return color * alpha;\n"
		"}\n",
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr,
		namestr, namestr, namestr, namestr, namestr, namestr, namestr);
		break;
    case CAIRO_GL_OPERAND_SPANS:
        _cairo_output_stream_printf (stream, 
            "varying float %s_coverage;\n"
            "vec4 get_%s()\n"
            "{\n"
            "    return vec4(0, 0, 0, %s_coverage);\n"
            "}\n",
            namestr, namestr, namestr);
        break;
    }
}

/*
 * Emits the border fade functions used by an operand.
 *
 * If bilinear filtering is used, the emitted function performs a linear
 * fade to transparency effect in the intervals [-1/2n, 1/2n] and
 * [1 - 1/2n, 1 + 1/2n] (n: texture size).
 *
 * If nearest filtering is used, the emitted function just returns
 * 0.0 for all values outside [0, 1).
 */
static void
_cairo_gl_shader_emit_border_fade (cairo_output_stream_t *stream,
				   cairo_gl_operand_t *operand,
				   cairo_gl_tex_t name)
{
    const char *namestr = operand_names[name];
    GLint gl_filter = _cairo_gl_operand_get_gl_filter (operand);

    /* 2D version */
    _cairo_output_stream_printf (stream,
	"vec2 %s_border_fade (vec2 coords, vec2 dims)\n"
	"{\n",
	namestr);

    if (gl_filter == GL_LINEAR)
	_cairo_output_stream_printf (stream,
	    "    return clamp(-abs(dims * (coords - 0.5)) + (dims + vec2(1.0)) * 0.5, 0.0, 1.0);\n");
    else
	_cairo_output_stream_printf (stream,
		// Henry Song
		/*
	    "    bvec2 in_tex1 = greaterThanEqual (coords, vec2 (0.0));\n"
	    "    bvec2 in_tex2 = lessThan (coords, vec2 (1.0));\n"
	    "    return vec2 (float (all (in_tex1) && all (in_tex2)));\n");
		*/
	    "    bvec2 in_tex1 = greaterThanEqual (coords, vec2 (0.0));\n"
	    "    bvec2 in_tex2 = lessThan (coords, vec2 (1.0));\n"
		"	 bool test_1 = all(in_tex1);\n"
		"	 bool test_2 = all(in_tex2);\n"
		"	 if(test_1 && test_2);\n"
		"		return vec2(1.0);\n"
		"	return vec2(0.0);\n");
    _cairo_output_stream_printf (stream, "}\n");

    /* 1D version */
    _cairo_output_stream_printf (stream,
	"float %s_border_fade (float x, float dim)\n"
	"{\n",
	namestr);
    if (gl_filter == GL_LINEAR)
	_cairo_output_stream_printf (stream,
	    "    return clamp(-abs(dim * (x - 0.5)) + (dim + 1.0) * 0.5, 0.0, 1.0);\n");
    else
	_cairo_output_stream_printf (stream,
	    "    bool in_tex = x >= 0.0 && x < 1.0;\n"
	    "    return float (in_tex);\n");

    _cairo_output_stream_printf (stream, "}\n");
}

static cairo_status_t
cairo_gl_shader_get_fragment_source (cairo_gl_context_t *ctx,
                                     cairo_gl_shader_in_t in,
                                     cairo_gl_operand_t *src,
                                     cairo_gl_operand_t *mask,
                                     cairo_gl_operand_type_t dest_type,
				     char **out)
{
    cairo_output_stream_t *stream = _cairo_memory_stream_create ();
    unsigned char *source;
    unsigned long length;
    cairo_status_t status;

    _cairo_output_stream_printf (stream,
	"#ifdef GL_ES\n"
	"precision highp float;\n"
	//"precision mediump float;\n"
	"#endif\n");

    if (ctx->gl_flavor == CAIRO_GL_FLAVOR_ES) {
	if (_cairo_gl_shader_needs_border_fade (src))
	    _cairo_gl_shader_emit_border_fade (stream, src, CAIRO_GL_TEX_SOURCE);
	if (_cairo_gl_shader_needs_border_fade (mask))
	    _cairo_gl_shader_emit_border_fade (stream, mask, CAIRO_GL_TEX_MASK);
    }

    cairo_gl_shader_emit_color (stream, ctx, src, CAIRO_GL_TEX_SOURCE);
    cairo_gl_shader_emit_color (stream, ctx, mask, CAIRO_GL_TEX_MASK);

    _cairo_output_stream_printf (stream,
        "void main()\n"
        "{\n");
    switch (in) {
    case CAIRO_GL_SHADER_IN_COUNT:
    default:
        ASSERT_NOT_REACHED;
    case CAIRO_GL_SHADER_IN_NORMAL:
        _cairo_output_stream_printf (stream,
		// Henry Song
            "    gl_FragColor = get_source() * get_mask().a;\n");
            //"    gl_FragColor = get_source() ;\n");
            //"    gl_FragColor = get_source() * get_mask()[3];\n");
        break;
    case CAIRO_GL_SHADER_IN_CA_SOURCE:
        _cairo_output_stream_printf (stream,
            "    gl_FragColor = get_source() * get_mask();\n");
        break;
    case CAIRO_GL_SHADER_IN_CA_SOURCE_ALPHA:
        _cairo_output_stream_printf (stream,
            "    gl_FragColor = get_source().a * get_mask();\n");
        break;
    }

    _cairo_output_stream_write (stream,
                                "}\n\0", 3);

    status = _cairo_memory_stream_destroy (stream, &source, &length);
    if (unlikely (status))
        return status;

    *out = (char *) source;
	// Henry Song
	//printf("----------------> fragment shader -------------------------->\n%s\n\n", source);
    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_gl_shader_compile (cairo_gl_context_t *ctx,
			  cairo_gl_shader_t *shader,
			  cairo_gl_var_type_t src,
			  cairo_gl_var_type_t mask,
			  const char *fragment_text)
{
    unsigned int vertex_shader;
    cairo_status_t status;

    assert (shader->program == 0);

    vertex_shader = cairo_gl_var_type_hash (src, mask, CAIRO_GL_VAR_NONE);
    if (ctx->vertex_shaders[vertex_shader] == 0) {
	char *source;

	status = cairo_gl_shader_get_vertex_source (src,
						    mask,
						    CAIRO_GL_VAR_NONE,
						    &source);
        if (unlikely (status))
            goto FAILURE;

	ctx->shader_impl->compile_shader (ctx, &ctx->vertex_shaders[vertex_shader],
					  GL_VERTEX_SHADER,
					  source);
	// Henry Song
	//printf("vertex shader: ------------>\n%s\n", source);
        free (source);
    }

    ctx->shader_impl->compile_shader (ctx, &shader->fragment_shader,
				      GL_FRAGMENT_SHADER,
				      fragment_text);
	// Henry Song 
	//printf("fragment shader: -------------->\n%s\n", fragment_text);

    ctx->shader_impl->link_shader (ctx, &shader->program,
				   ctx->vertex_shaders[vertex_shader],
				   shader->fragment_shader);

    return CAIRO_STATUS_SUCCESS;

 FAILURE:
    _cairo_gl_shader_fini (ctx, shader);
    shader->fragment_shader = 0;
    shader->program = 0;

    return status;
}

/* We always bind the source to texture unit 0 if present, and mask to
 * texture unit 1 if present, so we can just initialize these once at
 * compile time.
 */
static void
_cairo_gl_shader_set_samplers (cairo_gl_context_t *ctx,
			       cairo_gl_shader_t *shader, cairo_bool_t set_samplers)
{
    cairo_gl_dispatch_t *dispatch = &ctx->dispatch;
    GLint location;
    GLint saved_program;

    /* We have to save/restore the current program because we might be
     * asked for a different program while a shader is bound.  This shouldn't
     * be a performance issue, since this is only called once per compile.
     */
    //glGetIntegerv (GL_CURRENT_PROGRAM, &saved_program);
    /*if(ctx->program_reset == TRUE)
    {
        dispatch->UseProgram (shader->program);
        ctx->current_program = shader->program;
        ctx->program_reset = FALSE;
    }  
    else */
    {
        if(ctx->current_program != shader->program) 
        {
            dispatch->UseProgram (shader->program);
            ctx->current_program = shader->program;
        }
    }
    
    if(set_samplers == TRUE)
    {
        if(shader->source_sampler == -1)
        {
            shader->source_sampler = dispatch->GetUniformLocation (shader->program, "source_sampler");
            if(shader->source_sampler == -1)
                shader->source_sampler = -999;
        }
        if (shader->source_sampler != -1 && shader->source_sampler != -999) {
            if(shader->source_sampler_set == FALSE)
            {
	            dispatch->Uniform1i (shader->source_sampler, CAIRO_GL_TEX_SOURCE);
                shader->source_sampler_set = TRUE;
            }
        }
    
        if(shader->mask_sampler == -1)
        {
            shader->mask_sampler = dispatch->GetUniformLocation (shader->program, "mask_sampler");
            if(shader->mask_sampler == -1)
                shader->mask_sampler = -999;
        }
        if(shader->mask_sampler != -1 && shader->mask_sampler != -999)
        {
            if(shader->mask_sampler_set == FALSE)
            {
	            dispatch->Uniform1i (shader->mask_sampler, CAIRO_GL_TEX_MASK);
                shader->mask_sampler_set = TRUE;
            }
        }
    }

    //dispatch->UseProgram (saved_program);
}

void
_cairo_gl_shader_bind_float (cairo_gl_context_t *ctx,
			     const char *name,
			     float value)
{
    ctx->shader_impl->bind_float (ctx, ctx->current_shader, name, value);
}

void
_cairo_gl_shader_bind_vec2 (cairo_gl_context_t *ctx,
			    const char *name,
			    float value0,
			    float value1)
{
    ctx->shader_impl->bind_vec2 (ctx, ctx->current_shader, name, value0, value1);
}

void
_cairo_gl_shader_bind_vec3 (cairo_gl_context_t *ctx,
			    const char *name,
			    float value0,
			    float value1,
			    float value2)
{
    ctx->shader_impl->bind_vec3 (ctx, ctx->current_shader, name, value0, value1, value2);
}

void
_cairo_gl_shader_bind_vec4 (cairo_gl_context_t *ctx,
			    const char *name,
			    float value0, float value1,
			    float value2, float value3)
{
    ctx->shader_impl->bind_vec4 (ctx, ctx->current_shader, name, value0, value1, value2, value3);
}

//Henry Song
void
_cairo_gl_shader_bind_floatv(cairo_gl_context_t *ctx,
				const char *name,
				int count,
				float *values)
{
	ctx->shader_impl->bind_floatv(ctx, ctx->current_shader, name, count, values);
}

void
_cairo_gl_shader_bind_vec2v(cairo_gl_context_t *ctx,
				const char *name,
				int count,
				float *values)
{
	ctx->shader_impl->bind_vec2v(ctx, ctx->current_shader, name, count, values);
}

void
_cairo_gl_shader_bind_vec3v(cairo_gl_context_t *ctx,
				const char *name,
				int count,
				float *values)
{
	ctx->shader_impl->bind_vec3v(ctx, ctx->current_shader, name, count, values);
}

void
_cairo_gl_shader_bind_vec4v(cairo_gl_context_t *ctx,
				const char *name,
				int count,
				float *values)
{
	ctx->shader_impl->bind_vec4v(ctx, ctx->current_shader, name, count, values);
}



void
_cairo_gl_shader_bind_matrix (cairo_gl_context_t *ctx,
			      const char *name, cairo_matrix_t* m)
{
    ctx->shader_impl->bind_matrix (ctx, ctx->current_shader, name, m);
}

void
_cairo_gl_shader_bind_matrix4f (cairo_gl_context_t *ctx,
				const char *name, GLfloat* gl_m)
{
    ctx->shader_impl->bind_matrix4f (ctx, ctx->current_shader, name, gl_m);
}

void
_cairo_gl_set_shader (cairo_gl_context_t *ctx,
		      cairo_gl_shader_t *shader)
{
    if (ctx->current_shader == shader)
        return;

    ctx->shader_impl->use (ctx, shader);

    ctx->current_shader = shader;
}

cairo_status_t
_cairo_gl_get_shader_by_type (cairo_gl_context_t *ctx,
                              cairo_gl_operand_t *source,
                              cairo_gl_operand_t *mask,
                              cairo_gl_shader_in_t in,
                              cairo_gl_shader_t **shader)
{
    cairo_shader_cache_entry_t lookup, *entry;
    char *fs_source;
    cairo_status_t status;

    lookup.src = source->type;
    lookup.mask = mask->type;
    lookup.dest = CAIRO_GL_OPERAND_NONE;
    lookup.in = in;
    lookup.src_gl_filter = _cairo_gl_operand_get_gl_filter (source);
    lookup.src_border_fade = _cairo_gl_shader_needs_border_fade (source);
    lookup.mask_gl_filter = _cairo_gl_operand_get_gl_filter (mask);
    lookup.mask_border_fade = _cairo_gl_shader_needs_border_fade (mask);
    lookup.base.hash = _cairo_gl_shader_cache_hash (&lookup);
    lookup.base.size = 1;

    entry = _cairo_cache_lookup (&ctx->shaders, &lookup.base);
    if (entry) {
        assert (entry->shader.program);
        *shader = &entry->shader;
        if(source->type == CAIRO_GL_OPERAND_TEXTURE || 
           mask->type == CAIRO_GL_OPERAND_TEXTURE)
            //_cairo_gl_set_shader (ctx, *shader);
    	    _cairo_gl_shader_set_samplers (ctx, &entry->shader, TRUE);
        else
            _cairo_gl_shader_set_samplers (ctx, &entry->shader, FALSE);
	return CAIRO_STATUS_SUCCESS;
    }

    status = cairo_gl_shader_get_fragment_source (ctx,
						  in,
						  source,
						  mask,
						  CAIRO_GL_OPERAND_NONE,
						  &fs_source);
    if (unlikely (status))
	return status;

    entry = malloc (sizeof (cairo_shader_cache_entry_t));
    if (unlikely (entry == NULL)) {
        free (fs_source);
        return _cairo_error (CAIRO_STATUS_NO_MEMORY);
    }

    memcpy (entry, &lookup, sizeof (cairo_shader_cache_entry_t));

    entry->ctx = ctx;
    _cairo_gl_shader_init (&entry->shader);
    status = _cairo_gl_shader_compile (ctx,
				       &entry->shader,
				       cairo_gl_operand_get_var_type (source->type),
				       cairo_gl_operand_get_var_type (mask->type),
				       fs_source);
    free (fs_source);

    if (unlikely (status)) {
	free (entry);
	return status;
    }

    if(source->type == CAIRO_GL_OPERAND_TEXTURE || 
       mask->type == CAIRO_GL_OPERAND_TEXTURE)
        _cairo_gl_shader_set_samplers (ctx, &entry->shader, TRUE);
    else
        _cairo_gl_shader_set_samplers (ctx, &entry->shader, FALSE);

    status = _cairo_cache_insert (&ctx->shaders, &entry->base);
    if (unlikely (status)) {
	_cairo_gl_shader_fini (ctx, &entry->shader);
	free (entry);
	return status;
    }

    *shader = &entry->shader;

    return CAIRO_STATUS_SUCCESS;
}
