#ifndef _SWAY_SHADERS_H
#define _SWAY_SHADERS_H
#include <GLES2/gl2.h>
#include <wlr/render/wlr_renderer.h>

struct style_shader_prog_decorations {
	GLuint prog;
	struct {
		GLint proj;
		GLint bg_color;
		GLint fg_color;
		GLint tex;
	} uniforms;
	struct {
		GLint pos;
		GLint texcoord;
	} attributes;
};

struct style_shader_prog_tex {
	GLuint prog;
	struct {
		GLint proj;
		GLint tex;
	} uniforms;
	struct {
		GLint pos;
		GLint texcoord;
	} attributes;
};

extern GLuint gauss_lut_tex;
extern GLuint gauss_lut_width;

extern struct style_shader_prog_decorations shaderprog_decorations;
extern struct style_shader_prog_tex shaderprog_ext_tex;
extern struct style_shader_prog_tex shaderprog_rgb_tex;
extern struct style_shader_prog_tex shaderprog_rgba_tex;

void style_shader_init(struct wlr_renderer *renderer);

#endif
