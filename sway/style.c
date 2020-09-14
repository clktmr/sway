#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <GLES2/gl2.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_surface.h>
#include "log.h"
#include "sway/output.h"
#include "sway/style.h"
#include "sway/tree/view.h"

// 16x16 lookup table for gaussian blur
GLuint gauss_lut_width = 16;
GLbyte gauss_lut[] =
	"\xff\xfe\xfb\xf4\xe8\xd4\xb7\x93\x6c\x48\x2b\x17\x0b\x04\x01\x00"
	"\xfe\xfd\xfa\xf3\xe7\xd3\xb6\x92\x6c\x48\x2b\x17\x0b\x04\x01\x00"
	"\xfb\xfa\xf7\xf0\xe4\xd0\xb4\x90\x6a\x47\x2b\x17\x0a\x04\x01\x00"
	"\xf4\xf3\xf0\xea\xde\xcb\xaf\x8d\x68\x45\x29\x16\x0a\x04\x01\x00"
	"\xe8\xe7\xe4\xde\xd3\xc1\xa6\x86\x62\x42\x27\x15\x0a\x04\x01\x00"
	"\xd4\xd3\xd0\xcb\xc1\xb0\x98\x7a\x5a\x3c\x24\x13\x09\x03\x01\x00"
	"\xb7\xb6\xb4\xaf\xa6\x98\x83\x69\x4e\x34\x1f\x10\x08\x03\x01\x00"
	"\x93\x92\x90\x8d\x86\x7a\x69\x55\x3e\x2a\x19\x0d\x06\x02\x01\x00"
	"\x6c\x6c\x6a\x68\x62\x5a\x4e\x3e\x2e\x1f\x12\x0a\x04\x02\x00\x00"
	"\x48\x48\x47\x45\x42\x3c\x34\x2a\x1f\x14\x0c\x06\x03\x01\x00\x00"
	"\x2b\x2b\x2b\x29\x27\x24\x1f\x19\x12\x0c\x07\x04\x02\x01\x00\x00"
	"\x17\x17\x17\x16\x15\x13\x10\x0d\x0a\x06\x04\x02\x01\x00\x00\x00"
	"\x0b\x0b\x0a\x0a\x0a\x09\x08\x06\x04\x03\x02\x01\x00\x00\x00\x00"
	"\x04\x04\x04\x04\x04\x03\x03\x02\x02\x01\x01\x00\x00\x00\x00\x00"
	"\x01\x01\x01\x01\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";

GLuint gauss_lut_tex;
GLuint style_shader_prog;
GLuint style_shader_prog_exttex;

static float interpolate_linear(float v0, float v1, float t) {
	return v0 + t * (v1 - v0);
}

static float interpolate_ease_out(float v0, float v1, float t) {
	t = -(t-1.0f)*(t-1.0f)+1.0f;
	return interpolate_linear(v0, v1, t);
}

static float interpolate_ease_in(float v0, float v1, float t) {
	t = t*t;
	return interpolate_linear(v0, v1, t);
}

static float interpolate_ease_inout(float v0, float v1, float t) {
	t = -2.0f * t * t * (t-1.5f);
	return interpolate_linear(v0, v1, t);
}

void style_init(struct sway_style *s) {
	memset(s->transitions, 0, sizeof(s->transitions));
	for (size_t i = 0; i < STYLE_PROPS_SIZE; ++i) {
		s->props[i] = -1.0f;
		s->transitions[i].transition_func = interpolate_linear;
	}
}

void style_inherit(struct sway_style *s, const struct sway_style *from) {
	memcpy(s->props, from->props, sizeof(s->props));
}

float style_get_scalar(const struct sway_style *s, enum style_scalar prop) {
	return s->props[STYLE_SS_OFFSET + prop];
}

void style_set_scalar(struct sway_style *s, enum style_scalar prop, float val) {
	s->transitions[STYLE_SS_OFFSET + prop].to = val;
}

const float *style_get_vector4(const struct sway_style *s, enum style_vector4 prop) {
	return &s->props[STYLE_SV4_OFFSET + prop * 4];
}

void style_set_vector4(struct sway_style *s,
		enum style_vector4 prop,
		float val[4]) {
	size_t offset = STYLE_SV4_OFFSET + prop * 4;
	for (size_t i = 0; i < 4; ++i) {
		s->transitions[offset+i].to = val[i];
	}
}

struct style_box style_content_box(const struct sway_style *s) {
	const float *padding = style_get_vector4(s, SV4_PADDING);
	const float *border_width = style_get_vector4(s, SV4_BORDER_WIDTH);
	struct style_box box = {
		.x = padding[SE_LEFT] + border_width[SE_LEFT],
		.y = padding[SE_TOP] + border_width[SE_TOP],
		.width = 0.0f - padding[SE_LEFT] - padding[SE_RIGHT]
			- border_width[SE_LEFT] - border_width[SE_RIGHT],
		.height = 0.0f - padding[SE_TOP] - padding[SE_BOTTOM]
			- border_width[SE_TOP] - border_width[SE_BOTTOM],
	};
	return box;
}

struct style_box style_shadow_box(const struct sway_style *s) {
	const float blur = style_get_scalar(s, SS_BOX_SHADOW_BLUR);
	const float spread = style_get_scalar(s, SS_BOX_SHADOW_SPREAD);
	const float offset_h = style_get_scalar(s, SS_BOX_SHADOW_H_OFFSET);
	const float offset_v = style_get_scalar(s, SS_BOX_SHADOW_V_OFFSET);
	const float size = blur + spread;
	struct style_box box = {
		.x = offset_h - size,
		.y = offset_v - size,
		.width = 2.0f*size,
		.height = 2.0f*size
	};
	return box;
}

struct style_box style_box_union(const struct style_box *a,
		const struct style_box *b) {
	float left = (a->x < b->x) ? a->x : b->x;
	float top = (a->y < b->y) ? a->y : b->y;
	float right = (a->x+a->width < b->x+b->width) ? a->x+a->width : b->x+b->width;
	float bottom = (a->y+a->height < b->y+b->height) ? a->y+a->height : b->y+b->height;
	struct style_box box = {
		.x = left,
		.y = top,
		.width = right - left,
		.height = bottom - top,
	};
	return box;
}

bool style_animate(struct sway_style *s, const struct timespec *when) {
	bool ended = true;
	for (size_t i = 0; i < STYLE_PROPS_SIZE; ++i) {
		struct style_transition *trans = &s->transitions[i];
		if (when->tv_sec < trans->end.tv_sec ||
				(when->tv_sec == trans->end.tv_sec &&
				when->tv_nsec < trans->end.tv_nsec)) {
			float begin = (float)trans->begin.tv_sec + trans->begin.tv_nsec/1.0e9f;
			float end = (float)trans->end.tv_sec + trans->end.tv_nsec/1.0e9f;
			float now = (float)when->tv_sec + when->tv_nsec/1.0e9f;
			float t = (now-begin)/(end-begin);
			s->props[i] = trans->transition_func(trans->from, trans->to, t);
			ended = false;
		} else {
			// Make sure animations always end exactly at their destination
			s->props[i] = trans->to;
		}
	}
	return ended;
}

void style_animate_containers(struct sway_output *output, list_t *containers,
		struct timespec when) {
	// Snap the animation time to the outputs refresh rate to prevent jitter in
	// the animation.
	if (output->refresh_nsec) {
		when.tv_nsec = (when.tv_nsec / output->refresh_nsec) *
			output->refresh_nsec;
	}

	for (int i = 0; i < containers->length; ++i) {
		struct sway_container *child = containers->items[i];
		struct sway_style new_style = child->style;
		if (!style_animate(&new_style, &when)) {
			output_damage_whole_container(output, child);
			child->style = new_style;
			output_damage_whole_container(output, child);
		}
		if(!child->view) {
			style_animate_containers(output, child->current.children, when);
		}
	}
}

void style_box_scale(struct style_box *box, float scale) {
	box->width = box->width * scale;
	box->height = box->height * scale;
	box->x = box->x * scale;
	box->y = box->y * scale;
}

struct wlr_box style_box_bounds(const struct style_box *box) {
	return (struct wlr_box) {
		.x = floorl(box->x),
		.y = floorl(box->y),
		.width = ceill(box->width)+1,
		.height = ceill(box->height)+1
	};
}

void style_matrix_project_box(float mat[static 9], const struct style_box *box,
		enum wl_output_transform transform, float rotation,
		const float projection[static 9]) {
	float x = box->x;
	float y = box->y;
	float width = box->width;
	float height = box->height;

	wlr_matrix_identity(mat);
	wlr_matrix_translate(mat, x, y);

	if (rotation != 0) {
		wlr_matrix_translate(mat, width/2, height/2);
		wlr_matrix_rotate(mat, rotation);
		wlr_matrix_translate(mat, -width/2, -height/2);
	}

	wlr_matrix_scale(mat, width, height);

	if (transform != WL_OUTPUT_TRANSFORM_NORMAL) {
		wlr_matrix_translate(mat, 0.5, 0.5);
		wlr_matrix_transform(mat, transform);
		wlr_matrix_translate(mat, -0.5, -0.5);
	}

	wlr_matrix_multiply(mat, projection, mat);
}

const GLchar style_shader_vertex_src[] =
"uniform mat3 proj;\n"
"attribute vec2 pos;\n"
"attribute vec2 texcoord;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	gl_Position = vec4(proj * vec3(pos, 1.0), 1.0);\n"
"	v_texcoord = texcoord;\n"
"}\n";

const GLchar style_shader_fragment_src[] =
"precision mediump float;\n"
"uniform vec4 fg_color;\n"
"uniform vec4 bg_color;\n"
"uniform sampler2D tex;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	float c = texture2D(tex, v_texcoord).a;\n"
"	gl_FragColor = fg_color*c + bg_color*(1.0-c);\n"
"}\n";

const GLchar style_shader_fragment_src_extex[] =
"#extension GL_OES_EGL_image_external : require\n\n"
"precision mediump float;\n"
"uniform samplerExternalOES tex;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	gl_FragColor = texture2D(tex, v_texcoord);\n"
"}\n";

static GLuint compile_shader(GLuint type, const GLchar *src) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);

	GLint ok;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (ok == GL_FALSE) {
		glDeleteShader(shader);
		shader = 0;
	}

	return shader;
}

// FIXME free resources in style_shader_deinit()
void style_shader_init(struct wlr_renderer *renderer) {
	// load lookup table for gaussian blurred shadows
	glGenTextures(1, &gauss_lut_tex);
	glBindTexture(GL_TEXTURE_2D, gauss_lut_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, gauss_lut_width, gauss_lut_width,
			0, GL_ALPHA, GL_UNSIGNED_BYTE, gauss_lut);
	glBindTexture(GL_TEXTURE_2D, 0);

	// build shader programs
	GLuint vert = compile_shader(GL_VERTEX_SHADER, style_shader_vertex_src);
	if (!vert) {
		goto error;
	}

	GLuint frag = compile_shader(GL_FRAGMENT_SHADER, style_shader_fragment_src);
	if (!frag) {
		glDeleteShader(vert);
		goto error;
	}

	GLuint frag_exttex = compile_shader(GL_FRAGMENT_SHADER, style_shader_fragment_src_extex);
	if (!frag_exttex) {
		glDeleteShader(frag);
		glDeleteShader(vert);
		goto error;
	}

	GLint ok;
	style_shader_prog = glCreateProgram();
	glAttachShader(style_shader_prog, vert);
	glAttachShader(style_shader_prog, frag);
	glLinkProgram(style_shader_prog);
	glDetachShader(style_shader_prog, vert);
	glDetachShader(style_shader_prog, frag);
	glGetProgramiv(style_shader_prog, GL_LINK_STATUS, &ok);
	if (ok == GL_FALSE) {
		glDeleteProgram(style_shader_prog);
		goto error;
	}

	style_shader_prog_exttex = glCreateProgram();
	glAttachShader(style_shader_prog_exttex, vert);
	glAttachShader(style_shader_prog_exttex, frag_exttex);
	glLinkProgram(style_shader_prog_exttex);
	glDetachShader(style_shader_prog_exttex, vert);
	glDetachShader(style_shader_prog_exttex, frag_exttex);
	glGetProgramiv(style_shader_prog_exttex, GL_LINK_STATUS, &ok);
	if (ok == GL_FALSE) {
		glDeleteProgram(style_shader_prog);
		glDeleteProgram(style_shader_prog_exttex);
		goto error;
	}

	glDeleteShader(vert);
	glDeleteShader(frag);
	glDeleteShader(frag_exttex);

	return;

error:
	// TODO fallback to B_NORMAL instead of aborting
	sway_abort("Unable to compile styles shader");
}

// Returns the vertices of two triangles in GL_CCW order composing a rectangle
#define quad_verts(t, r, b, l) r,t,l,t,l,b,l,b,r,b,r,t,

void style_render_shadow(struct style_render_data *data,
		const float matrix[static 9]) {
	struct sway_style *s = data->style;
	const struct style_box *box = &data->box;
	// OpenGL ES 2 requires the glUniformMatrix3fv transpose parameter to be set
	// to GL_FALSE
	float transposition[9];
	wlr_matrix_transpose(transposition, matrix);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gauss_lut_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glUseProgram(style_shader_prog);

	GLint fg_color_uniform = glGetUniformLocation(style_shader_prog, "fg_color");
	GLint bg_color_uniform = glGetUniformLocation(style_shader_prog, "bg_color");
	GLint proj_uniform = glGetUniformLocation(style_shader_prog, "proj");
	GLint tex_uniform = glGetUniformLocation(style_shader_prog, "tex");
	const float* shadow_color = style_get_vector4(s, SV4_BOX_SHADOW_COLOR);
	glUniform4fv(fg_color_uniform, 1, shadow_color);
	glUniform4fv(bg_color_uniform, 1, (float[]){0.0f, 0.0f, 0.0f, 0.0f});
	glUniform1i(tex_uniform, 0);
	glUniformMatrix3fv(proj_uniform, 1, GL_FALSE, transposition);

	// Generate the illustrated quadratic mesh to render the shadow.  Quad #4
	// will simply render with the box shadow color, while all other quads are
	// used for shadow blur by sampling from a lookup table.
	//
	//   ob ┌───┬───┬───┐
	//      │ 0 │ 1 │ 2 │
	//   it ├───┼───┼───┤
	//      │ 3 │ 4 │ 5 │
	//   ib ├───┼───┼───┤
	//      │ 6 │ 7 │ 8 │
	//   oe └───┴───┴───┘
	//      ob  il  ir  oe
	//
	// TODO Possible performance improvement by storing the mesh once in a
	// global VBO and animate it in the vertex shader by passing it, ib, il and
	// ir as uniforms.
	float blur = 2.0f * style_get_scalar(s, SS_BOX_SHADOW_BLUR) * data->scale;
	float blur_v = blur / (float)box->height;
	float blur_h = blur / (float)box->width;
	float ob = 0.0f, oe = 1.0f;
	float it = blur_v > 0.5f ? 0.5f : blur_v;
	float ib = oe - it;
	float il = blur_h > 0.5f ? 0.5f : blur_h;
	float ir = oe - il;
	GLfloat verts[] = {
		quad_verts(ob, il, it, ob)
		quad_verts(ob, ir, it, il)
		quad_verts(ob, oe, it, ir)
		quad_verts(it, il, ib, ob)
		quad_verts(it, ir, ib, il)
		quad_verts(it, oe, ib, ir)
		quad_verts(ib, il, oe, ob)
		quad_verts(ib, ir, oe, il)
		quad_verts(ib, oe, oe, ir)
	};

	// Note that pixels should be sampled in the center for proper interpolation
	float px0, px0_v, px0_h, px1;
	px0 = px0_v = px0_h = 0.5f/(float)gauss_lut_width;
	px1 = px0 * (float)(2*gauss_lut_width-1);

	// If the blur is greater than 0.5, quad #4 will have a size of zero.  To
	// blur the shadow further, the texture's zero point needs to be moved
	// towards (1,1).
	if(blur_h > 0.5f) {
		px0_h = px1 - (px1-px0)*(0.5f/blur_h);
	}
	if(blur_v > 0.5f) {
		px0_v = px1 - (px1-px0)*(0.5f/blur_v);
	}
	GLfloat texcoord[] = {
		quad_verts(px1,   px0_h, px0_v, px1)
		quad_verts(px1,   px0_h, px0_v, px0_h)
		quad_verts(px1,   px1,   px0_v, px0_h)
		quad_verts(px0_v, px0_h, px0_v, px1)
		quad_verts(px0_v, px0_h, px0_v, px0_h)
		quad_verts(px0_v, px1,   px0_v, px0_h)
		quad_verts(px0_v, px0_h, px1,   px1)
		quad_verts(px0_v, px0_h, px1,   px0_h)
		quad_verts(px0_v, px1,   px1,   px0_h)
	};

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, texcoord);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	glDrawArrays(GL_TRIANGLES, 0, sizeof(verts)/sizeof(*verts)/2);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);

	glBindTexture(GL_TEXTURE_2D, 0);
}

void style_render_borders(struct style_render_data *data,
		const float matrix[static 9]) {
	struct sway_style *s = data->style;
	const struct style_box *box = &data->box;
	// OpenGL ES 2 requires the glUniformMatrix3fv transpose parameter to be set
	// to GL_FALSE
	float transposition[9];
	wlr_matrix_transpose(transposition, matrix);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gauss_lut_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glUseProgram(style_shader_prog);

	GLint fg_color_uniform = glGetUniformLocation(style_shader_prog, "fg_color");
	GLint bg_color_uniform = glGetUniformLocation(style_shader_prog, "bg_color");
	GLint proj_uniform = glGetUniformLocation(style_shader_prog, "proj");
	GLint tex_uniform = glGetUniformLocation(style_shader_prog, "tex");
	const float* border_color = style_get_vector4(s, SV4_BORDER_COLOR);
	const float* bg_color = style_get_vector4(s, SV4_BACKGROUND_COLOR);
	glUniform4fv(fg_color_uniform, 1, border_color);
	glUniform4fv(bg_color_uniform, 1, bg_color);
	glUniform1i(tex_uniform, 0);
	glUniformMatrix3fv(proj_uniform, 1, GL_FALSE, transposition);

	// Generate the illustrated quadratic mesh to render the shadow.  Quad #4
	// will render with the background color, while all other quads are
	// rendered with the border color.
	//
	//   ob ┌───┬───┬───┐
	//      │ 0 │ 1 │ 2 │
	//   it ├───┼───┼───┤
	//      │ 3 │ 4 │ 5 │
	//   ib ├───┼───┼───┤
	//      │ 6 │ 7 │ 8 │
	//   oe └───┴───┴───┘
	//      ob  il  ir  oe
	//
	// TODO Possible performance improvement by storing the mesh once in a
	// global VBO and animate it in the vertex shader by passing it, ib, il and
	// ir as uniforms.
	const float *bw = style_get_vector4(s, SV4_BORDER_WIDTH);
	float ob = 0.0f, oe = 1.0f;
	float ib = oe - bw[SE_BOTTOM] / (float)box->height * data->scale;
	float it = bw[SE_TOP] / (float)box->height * data->scale;
	float il = bw[SE_LEFT] / (float)box->width * data->scale;
	float ir = oe - bw[SE_RIGHT] / (float)box->width * data->scale;
	GLfloat verts[] = {
		quad_verts(ob, il, it, ob)
		quad_verts(ob, ir, it, il)
		quad_verts(ob, oe, it, ir)
		quad_verts(it, il, ib, ob)
		quad_verts(it, ir, ib, il)
		quad_verts(it, oe, ib, ir)
		quad_verts(ib, il, oe, ob)
		quad_verts(ib, ir, oe, il)
		quad_verts(ib, oe, oe, ir)
	};

	// Note that pixels should be sampled in the center for proper interpolation
	float px0 = 0.5f/(float)gauss_lut_width;
	float px1 = px0 * (float)(2*gauss_lut_width-1);
	GLfloat texcoord[] = {
		quad_verts(px0, px0, px0, px0)
		quad_verts(px0, px0, px0, px0)
		quad_verts(px0, px0, px0, px0)
		quad_verts(px0, px0, px0, px0)
		quad_verts(px1, px1, px1, px1)
		quad_verts(px0, px0, px0, px0)
		quad_verts(px0, px0, px0, px0)
		quad_verts(px0, px0, px0, px0)
		quad_verts(px0, px0, px0, px0)
	};

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, texcoord);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	glDrawArrays(GL_TRIANGLES, 0, sizeof(verts)/sizeof(*verts)/2);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);

	glBindTexture(GL_TEXTURE_2D, 0);
}

static void style_render_texture(struct style_render_data *data,
		const float matrix[static 9]) {
	struct wlr_texture *texture = data->texture;

	// OpenGL ES 2 requires the glUniformMatrix3fv transpose parameter to be set
	// to GL_FALSE
	float transposition[9];
	wlr_matrix_transpose(transposition, matrix);

	struct wlr_gles2_texture_attribs attribs;
	wlr_gles2_texture_get_attribs(texture, &attribs);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(attribs.target, attribs.tex);
	glTexParameteri(attribs.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(attribs.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// TODO Check texture attributes (alpha, y_invert) and select shader based
	// on texture target:
	//
	//		switch (attribs.target) {
	//		case GL_TEXTURE_2D:
	//			glUseProgram(style_shader_prog_tex);
	//			break;
	//		case GL_TEXTURE_EXTERNAL_OES:
	//			glUseProgram(style_shader_prog_exttex);
	//			break;
	//		}

	glUseProgram(style_shader_prog_exttex);

	GLint proj_uniform = glGetUniformLocation(style_shader_prog_exttex, "proj");
	GLint tex_uniform = glGetUniformLocation(style_shader_prog_exttex, "tex");
	glUniform1i(tex_uniform, 0);
	glUniformMatrix3fv(proj_uniform, 1, GL_FALSE, transposition);

	GLfloat verts[]    = { quad_verts(0.0f, 1.0f, 1.0f, 0.0f) };
	GLfloat texcoord[] = { quad_verts(0.0f, 1.0f, 1.0f, 0.0f) };

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, texcoord);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	glDrawArrays(GL_TRIANGLES, 0, sizeof(verts)/sizeof(*verts)/2);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);

	glBindTexture(GL_TEXTURE_2D, 0);
}

static void style_render_surface(struct sway_output *output,
		struct sway_view *view, struct wlr_surface *surface,
		struct wlr_box *_box, float rotation, void *_data) {
	struct style_render_data *data = _data;
	struct wlr_output *wlr_output = output->wlr_output;

	data->texture = wlr_surface_get_texture(surface);
	if (!data->texture || !wlr_texture_is_gles2(data->texture)) {
		return;
	}
	data->transform = wlr_output_transform_invert(surface->current.transform);
	data->box = (struct style_box){
		.x = _box->x + style_get_scalar(data->style, SS_TRANSLATION_X),
		.y = _box->y + style_get_scalar(data->style, SS_TRANSLATION_Y),
		.width = _box->width,
		.height = _box->height,
	};
	style_box_scale(&data->box, wlr_output->scale);
	style_render_damaged(wlr_output, style_render_texture, data);

	// XXX Is this ok if we have opacity?
	wlr_presentation_surface_sampled_on_output(server.presentation, surface,
		wlr_output);
}

void style_render_view(struct sway_style *s, struct sway_view *view,
		struct sway_output *output, pixman_region32_t *damage) {
	if (view->saved_buffer) {
		struct wlr_output *wlr_output = output->wlr_output;
		if (!view->saved_buffer || !view->saved_buffer->texture) {
			return;
		}
		struct style_render_data data = {
			.damage = damage,
			.style = s,
			.box = {
				.x = view->container->surface_x - output->lx -
					view->saved_geometry.x +
					style_get_scalar(s, SS_TRANSLATION_X),
				.y = view->container->surface_y - output->ly -
					view->saved_geometry.y +
					style_get_scalar(s, SS_TRANSLATION_Y),
				.width = view->saved_buffer_width,
				.height = view->saved_buffer_height,
			},
			.texture = view->saved_buffer->texture,
			.scale = output->wlr_output->scale,
		};

		struct wlr_box output_box = {
			.width = output->width,
			.height = output->height,
		};
		struct wlr_box intersection;
		struct wlr_box bounds = style_box_bounds(&data.box);
		if (!wlr_box_intersection(&intersection, &output_box, &bounds)) {
			return;
		}

		style_box_scale(&data.box, wlr_output->scale);
		style_render_damaged(wlr_output, style_render_texture, &data);
		return;
	}

	if (!view->surface) {
		return;
	}

	struct style_render_data data = {
		.damage = damage,
		.style = s,
		.scale = output->wlr_output->scale,
	};
	// Render all toplevels without descending into popups
	double ox = view->container->surface_x -
		output->lx - view->geometry.x;
	double oy = view->container->surface_y -
		output->ly - view->geometry.y;
	output_surface_for_each_surface(output, view->surface, ox, oy,
			style_render_surface, &data);
}
