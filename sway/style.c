#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <GLES2/gl2.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_matrix.h>
#include "log.h"
#include "sway/style.h"

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

void style_init(struct sway_style *s) {
	memset(s->transitions, 0, sizeof(s->transitions));
	for (size_t i = 0; i < STYLE_PROPS_SIZE; ++i) {
		s->props[i] = -1.0f;
	}
}

void style_inherit(struct sway_style *s, const struct sway_style *from) {
	memcpy(s->props, from->props, sizeof(s->props));
}

float style_get_scalar(const struct sway_style *s, enum style_scalar prop) {
	return s->props[STYLE_SS_OFFSET + prop];
}

void style_set_scalar(struct sway_style *s, enum style_scalar prop, float val) {
	s->props[STYLE_SS_OFFSET + prop] = val;
}

const float *style_get_vector4(const struct sway_style *s, enum style_vector4 prop) {
	return &s->props[STYLE_SV4_OFFSET + prop * 4];
}

void style_set_vector4(struct sway_style *s,
		enum style_vector4 prop,
		float val[4]) {
	size_t offset = STYLE_SV4_OFFSET + prop * 4;
	memcpy(&s->props[offset], val, sizeof(*s->props) * 4);
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

static float lerp(float v0, float v1, float t) {
	return v0 + t * (v1 - v0);
}

bool style_animate(struct sway_style *s, struct timespec *when) {
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
			s->props[i] = lerp(trans->from, trans->to, t);
			ended = false;
		}
	}
	return ended;
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
"uniform vec4 fg_color;\n"
"uniform vec4 bg_color;\n"
"attribute vec2 pos;\n"
"attribute vec2 texcoord;\n"
"varying vec4 v_fg_color;\n"
"varying vec4 v_bg_color;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	gl_Position = vec4(proj * vec3(pos, 1.0), 1.0);\n"
"	v_fg_color = fg_color;\n"
"	v_bg_color = bg_color;\n"
"	v_texcoord = texcoord;\n"
"}\n";

const GLchar style_shader_fragment_src[] =
"precision mediump float;\n"
"uniform sampler2D tex;\n"
"varying vec4 v_fg_color;\n"
"varying vec4 v_bg_color;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	float c = texture2D(tex, v_texcoord).a;\n"
"	gl_FragColor = v_fg_color*c + v_bg_color*(1.0-c);\n"
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

	style_shader_prog = glCreateProgram();
	glAttachShader(style_shader_prog, vert);
	glAttachShader(style_shader_prog, frag);
	glLinkProgram(style_shader_prog);

	glDetachShader(style_shader_prog, vert);
	glDetachShader(style_shader_prog, frag);
	glDeleteShader(vert);
	glDeleteShader(frag);

	GLint ok;
	glGetProgramiv(style_shader_prog, GL_LINK_STATUS, &ok);
	if (ok == GL_FALSE) {
		glDeleteProgram(style_shader_prog);
		goto error;
	}
	return;

error:
	// TODO fallback to B_NORMAL instead of aborting
	sway_abort("Unable to compile styles shader");
}

// Returns the vertices of two triangles in GL_CCW order composing a rectangle
#define quad_verts(t, r, b, l) r,t,l,t,l,b,l,b,r,b,r,t,

void style_render_shadow(struct sway_style *s, const struct style_box *box,
		const float matrix[static 9]) {
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
	//   oe ┌───┬───┬───┐
	//      │ 0 │ 1 │ 2 │
	//   it ├───┼───┼───┤
	//      │ 3 │ 4 │ 5 │
	//   ib ├───┼───┼───┤
	//      │ 6 │ 7 │ 8 │
	//   ob └───┴───┴───┘
	//      ob  il  ir  oe
	//
	// TODO Possible performance improvement by storing the mesh once in a
	// global VBO and animate it in the vertex shader by passing it, ib, il and
	// ir as uniforms.
	float blur = 2.0f * style_get_scalar(s, SS_BOX_SHADOW_BLUR);
	float ob = 0.0f, oe = 1.0f;
	float ib = blur / (float)box->height;
	float it = oe - ib;
	float il = blur / (float)box->width;
	float ir = oe - il;
	GLfloat verts[] = {
		quad_verts(oe, il, it, ob)
		quad_verts(oe, ir, it, il)
		quad_verts(oe, oe, it, ir)
		quad_verts(it, il, ib, ob)
		quad_verts(it, ir, ib, il)
		quad_verts(it, oe, ib, ir)
		quad_verts(ib, il, ob, ob)
		quad_verts(ib, ir, ob, il)
		quad_verts(ib, oe, ob, ir)
	};

	// Note that pixels should be sampled in the center for proper interpolation
	float px0 = 0.5f/(float)gauss_lut_width;
	float px1 = px0 * (float)(2*gauss_lut_width-1);
	GLfloat texcoord[] = {
		quad_verts(px1, px0, px0, px1)
		quad_verts(px1, px0, px0, px0)
		quad_verts(px1, px1, px0, px0)
		quad_verts(px0, px0, px0, px1)
		quad_verts(px0, px0, px0, px0)
		quad_verts(px0, px1, px0, px0)
		quad_verts(px0, px0, px1, px1)
		quad_verts(px0, px0, px1, px0)
		quad_verts(px0, px1, px1, px0)
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

void style_render_borders(struct sway_style *s, const struct style_box *box,
		const float matrix[static 9]) {
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
	//   oe ┌───┬───┬───┐
	//      │ 0 │ 1 │ 2 │
	//   it ├───┼───┼───┤
	//      │ 3 │ 4 │ 5 │
	//   ib ├───┼───┼───┤
	//      │ 6 │ 7 │ 8 │
	//   ob └───┴───┴───┘
	//      ob  il  ir  oe
	//
	// TODO Possible performance improvement by storing the mesh once in a
	// global VBO and animate it in the vertex shader by passing it, ib, il and
	// ir as uniforms.
	const float *bw = style_get_vector4(s, SV4_BORDER_WIDTH);
	float ob = 0.0f, oe = 1.0f;
	float ib = bw[SE_BOTTOM] / (float)box->height;
	float it = oe - bw[SE_TOP] / (float)box->height;
	float il = bw[SE_LEFT] / (float)box->width;
	float ir = oe - bw[SE_RIGHT] / (float)box->width;
	GLfloat verts[] = {
		quad_verts(oe, il, it, ob)
		quad_verts(oe, ir, it, il)
		quad_verts(oe, oe, it, ir)
		quad_verts(it, il, ib, ob)
		quad_verts(it, ir, ib, il)
		quad_verts(it, oe, ib, ir)
		quad_verts(ib, il, ob, ob)
		quad_verts(ib, ir, ob, il)
		quad_verts(ib, oe, ob, ir)
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
