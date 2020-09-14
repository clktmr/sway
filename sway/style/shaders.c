#define _POSIX_C_SOURCE 200809L
#include <GLES2/gl2.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_surface.h>
#include "log.h"
#include "sway/style/shaders.h"

struct style_shader_prog_decorations shaderprog_decorations;
struct style_shader_prog_tex shaderprog_ext_tex;
struct style_shader_prog_tex shaderprog_rgb_tex;
struct style_shader_prog_tex shaderprog_rgba_tex;

// 16x16 lookup table for gaussian blur
GLuint gauss_lut_tex;
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

static const GLchar vertex_src[] =
"uniform mat3 proj;\n"
"attribute vec2 pos;\n"
"attribute vec2 texcoord;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	gl_Position = vec4(proj * vec3(pos, 1.0), 1.0);\n"
"	v_texcoord = texcoord;\n"
"}\n";

static const GLchar fragment_src_decorations[] =
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

static const GLchar fragment_src_ext_tex[] =
"#extension GL_OES_EGL_image_external : require\n\n"
"precision mediump float;\n"
"uniform samplerExternalOES tex;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	gl_FragColor = texture2D(tex, v_texcoord);\n"
"}\n";

static const GLchar fragment_src_rgba_tex[] =
"precision mediump float;\n"
"uniform sampler2D tex;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	gl_FragColor = texture2D(tex, v_texcoord);\n"
"}\n";

static const GLchar fragment_src_rgb_tex[] =
"precision mediump float;\n"
"uniform sampler2D tex;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	gl_FragColor = vec4(texture2D(tex, v_texcoord).rgb, 1.0);\n"
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

static GLuint link_program(const GLchar *vert_src, const GLchar *frag_src) {
	GLuint vert = compile_shader(GL_VERTEX_SHADER, vert_src);
	if (!vert) {
		goto error;
	}

	GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
	if (!frag) {
		glDeleteShader(vert);
		goto error;
	}

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vert);
	glAttachShader(prog, frag);
	glLinkProgram(prog);

	glDetachShader(prog, vert);
	glDetachShader(prog, frag);
	glDeleteShader(vert);
	glDeleteShader(frag);

	GLint ok;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (ok == GL_FALSE) {
		glDeleteProgram(prog);
		goto error;
	}

	return prog;

error:
	return 0;
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
	GLuint prog;
	shaderprog_decorations.prog = prog =
		link_program(vertex_src, fragment_src_decorations);
	if (!prog) {
		goto error;
	}
	shaderprog_decorations.attributes.pos = glGetAttribLocation(prog, "pos");
	shaderprog_decorations.attributes.texcoord = glGetAttribLocation(prog, "texcoord");
	shaderprog_decorations.uniforms.proj = glGetUniformLocation(prog, "proj");
	shaderprog_decorations.uniforms.tex = glGetUniformLocation(prog, "tex");
	shaderprog_decorations.uniforms.bg_color = glGetUniformLocation(prog, "bg_color");
	shaderprog_decorations.uniforms.fg_color = glGetUniformLocation(prog, "fg_color");

	shaderprog_ext_tex.prog = prog =
		link_program(vertex_src, fragment_src_ext_tex);
	if (!prog) {
		goto error;
	}
	shaderprog_ext_tex.attributes.pos = glGetAttribLocation(prog, "pos");
	shaderprog_ext_tex.attributes.texcoord = glGetAttribLocation(prog, "texcoord");
	shaderprog_ext_tex.uniforms.proj = glGetUniformLocation(prog, "proj");
	shaderprog_ext_tex.uniforms.tex = glGetUniformLocation(prog, "tex");

	shaderprog_rgb_tex.prog = prog =
		link_program(vertex_src, fragment_src_rgb_tex);
	if (!prog) {
		goto error;
	}
	shaderprog_rgb_tex.attributes.pos = glGetAttribLocation(prog, "pos");
	shaderprog_rgb_tex.attributes.texcoord = glGetAttribLocation(prog, "texcoord");
	shaderprog_rgb_tex.uniforms.proj = glGetUniformLocation(prog, "proj");
	shaderprog_rgb_tex.uniforms.tex = glGetUniformLocation(prog, "tex");

	shaderprog_rgba_tex.prog = prog =
		link_program(vertex_src, fragment_src_rgba_tex);
	if (!prog) {
		goto error;
	}
	shaderprog_rgba_tex.attributes.pos = glGetAttribLocation(prog, "pos");
	shaderprog_rgba_tex.attributes.texcoord = glGetAttribLocation(prog, "texcoord");
	shaderprog_rgba_tex.uniforms.proj = glGetUniformLocation(prog, "proj");
	shaderprog_rgba_tex.uniforms.tex = glGetUniformLocation(prog, "tex");

	return;

error:
	// TODO fallback to B_NORMAL instead of aborting
	sway_abort("Unable to compile styles shader");
}

