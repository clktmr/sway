#ifndef _SWAY_SHADERS_H
#define _SWAY_SHADERS_H
#include <GLES2/gl2.h>
#include <wlr/render/wlr_renderer.h>

extern GLuint gauss_lut_tex;
extern GLuint gauss_lut_width;

/**
 * TODO document
 */
extern struct shaderprog_decorations_t {
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
} shaderprog_decorations;

/**
 * TODO document
 */
extern struct shaderprog_ext_tex_t {
	GLuint prog;
	struct {
		GLint proj;
		GLint tex;
	} uniforms;
	struct {
		GLint pos;
		GLint texcoord;
	} attributes;
} shaderprog_ext_tex;

void style_shader_init(struct wlr_renderer *renderer);

#endif
