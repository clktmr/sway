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
"uniform float outline;\n"
"uniform float blur;\n"
"uniform vec4 color;\n"
"varying vec2 v_texcoord;\n"
"\n"
"void main() {\n"
"	float l = length(v_texcoord);\n"
"	float c = smoothstep(outline+blur, outline-blur, l);\n"
"	gl_FragColor = c * color;\n"
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
	GLuint prog;
	shaderprog_decorations.prog = prog =
		link_program(vertex_src, fragment_src_decorations);
	if (!prog) {
		goto error;
	}
	shaderprog_decorations.attributes.pos = glGetAttribLocation(prog, "pos");
	shaderprog_decorations.attributes.texcoord = glGetAttribLocation(prog, "texcoord");
	shaderprog_decorations.uniforms.proj = glGetUniformLocation(prog, "proj");
	shaderprog_decorations.uniforms.color = glGetUniformLocation(prog, "color");
	shaderprog_decorations.uniforms.blur = glGetUniformLocation(prog, "blur");
	shaderprog_decorations.uniforms.outline = glGetUniformLocation(prog, "outline");

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

