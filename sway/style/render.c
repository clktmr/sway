#define _POSIX_C_SOURCE 200809L
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_surface.h>
#include "sway/style.h"
#include "sway/output.h"
#include "sway/style/shaders.h"

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

// Returns the vertices of two triangles in GL_CCW order composing a rectangle
#define quad_verts(t, r, b, l) r,t,l,t,l,b,l,b,r,b,r,t,

// Renders the illustrated mesh in the specified color.
//
//      ob  il  ir  oe
//     0 ┏━━━┳━━━┳━━━┓ ob
//       ┃ ╭─╂───╂─╮─┃───── outline
//       ┣━┿━╋━━━╋━┿━┫ it
//       ┃ │ ┃   ┃ │ ┃
//       ┣━┿━╋━━━╋━┿━┫ ib ┐
//       ┃ ╰─╂───╂─╯ ┃    ├ corner_radius
//     1 ┗━━━┻━━━┻━━━┛ oe ┘
//       0           1
//
// If a corner radius of zero is specified, the center quad will take up all the
// space.  If a corner radius greater zero is specified, the drawn rect shrinks
// in size (illustrated by the thin line).  This can be controlled by the
// outline parameter.  An outline of 1.0 will cause no shrinking at all and an
// outline of 0.0 will shrink the drawn rect by corner_radius.  The outline can
// also be blurred within the [0, 1] range.
//
// TODO Either set corner_shift automatically from blur or make use of it in
// style_render_shadow.
static void style_render_decoration(const float color[static 4],
		const float corner_radius[static 4], const float corner_shift[static 2],
		const float outline, const float blur, const float matrix[static 9]) {
	// OpenGL ES 2 requires the glUniformMatrix3fv transpose parameter to be set
	// to GL_FALSE
	float transposition[9];
	wlr_matrix_transpose(transposition, matrix);

	struct style_shader_prog_decorations *shader = &shaderprog_decorations;
	glUseProgram(shader->prog);

	glUniform4fv(shader->uniforms.color, 1, color);
	glUniform1f(shader->uniforms.blur, blur);
	glUniform1f(shader->uniforms.outline, outline);
	glUniformMatrix3fv(shader->uniforms.proj, 1, GL_FALSE, transposition);

	// TODO Possible performance improvement by storing the mesh once in a
	// global VBO and animate it in the vertex shader by passing it, ib, il and
	// ir as uniforms.  Also consider storing vertices and indices separately.
	float ob = 0.0f, oe = 1.0f;
	float it = ob + corner_radius[SE_TOP];
	float ib = oe - corner_radius[SE_BOTTOM];
	float il = ob + corner_radius[SE_LEFT];
	float ir = oe - corner_radius[SE_RIGHT];
	if (il > ir) {
		il = ir = (il + ir) / 2.0f;
	}
	if (it > ib) {
		it = ib = (it + ib) / 2.0f;
	}

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

	float px0_v = corner_shift[0];
	float px0_h = corner_shift[1];
	float px1 = 1.0f;
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

	glVertexAttribPointer(shader->attributes.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(shader->attributes.texcoord, 2, GL_FLOAT, GL_FALSE, 0, texcoord);

	glEnableVertexAttribArray(shader->attributes.pos);
	glEnableVertexAttribArray(shader->attributes.texcoord);

	glDrawArrays(GL_TRIANGLES, 0, sizeof(verts)/sizeof(*verts)/2);

	glDisableVertexAttribArray(shader->attributes.pos);
	glDisableVertexAttribArray(shader->attributes.texcoord);

	glBindTexture(GL_TEXTURE_2D, 0);
}

void style_render_shadow(struct style_render_data *data,
		const float matrix[static 9]) {
	struct sway_style *s = data->style;
	const struct style_box *box = &data->box;
	const float* shadow_color = style_get_vector4(s, SV4_BOX_SHADOW_COLOR);
	const float* radius_px = style_get_vector4(s, SV4_BORDER_RADIUS);
	float blur_px = style_get_scalar(s, SS_BOX_SHADOW_BLUR);
	// TODO This only renders correctly if all borders have the same radius.  To
	// fix this we need a possibility to pass the blur in pixel to the shader
	// instead of a relation to corner radiuses.
	float blur = 0.5f * blur_px / (blur_px + radius_px[0]);
	float outline = 1.0f - blur;
	float radius[4] = {
		2.0f * data->scale * (blur_px + radius_px[SE_TOP]) / box->height,
		2.0f * data->scale * (blur_px + radius_px[SE_RIGHT]) / box->width,
		2.0f * data->scale * (blur_px + radius_px[SE_BOTTOM]) / box->height,
		2.0f * data->scale * (blur_px + radius_px[SE_LEFT]) / box->width,
	};
	style_render_decoration(shadow_color, radius, (float[]){0.0f, 0.0f}, outline, blur, matrix);
}

void style_render_borders(struct style_render_data *data,
		const float matrix[static 9]) {
	struct sway_style *s = data->style;
	const struct style_box *box = &data->box;
	const float scale = data->scale;
	const float* color = style_get_vector4(s, SV4_BORDER_COLOR);
	const float* radius_px = style_get_vector4(s, SV4_BORDER_RADIUS);
	float radius[4] = {
		2.0f * scale * radius_px[SE_TOP] / box->height,
		2.0f * scale * radius_px[SE_RIGHT] / box->width,
		2.0f * scale * radius_px[SE_BOTTOM] / box->height,
		2.0f * scale * radius_px[SE_LEFT] / box->width,
	};
	// We actually don't want any blur, but setting it to zero will give us
	// alias even though multisampling is enabled.  With a blur of one pixel the
	// borders will still render pixel perfect when aligned to pixels, but also
	// transition nicely between pixels.  The same blur is also used in
	// style_render_borders.
	//
	// TODO This is only an appoximation of 1px and will look bad if radiuses of
	// the corners are different.  To fix this we need a possibility to pass the
	// blur in pixel to the shader instead of a relation to corner radiuses.
	
	const float blur = 0.25f/radius_px[0];

	style_render_decoration(color, radius, (float[]){0.0f, 0.0f}, 1.0f, blur, matrix);
};

void style_render_background(struct style_render_data *data,
		const float matrix[static 9]) {
	struct sway_style *s = data->style;
	const struct style_box *box = &data->box;
	const float scale = data->scale;
	const float* width = style_get_vector4(s, SV4_BORDER_WIDTH);
	const float* bg_color = style_get_vector4(s, SV4_BACKGROUND_COLOR);
	const float* radius_px = style_get_vector4(s, SV4_BORDER_RADIUS);
	float radius[4] = {
		2.0f * scale * (radius_px[SE_TOP] - 0.5f*width[SE_TOP]) / box->height,
		2.0f * scale * (radius_px[SE_RIGHT] - 0.5f*width[SE_RIGHT]) / box->width,
		2.0f * scale * (radius_px[SE_BOTTOM] - 0.5f*width[SE_BOTTOM]) / box->height,
		2.0f * scale * (radius_px[SE_LEFT] - 0.5f*width[SE_LEFT]) / box->width,
	};
	const float blur = 0.25f/radius_px[0];

	style_render_decoration(bg_color, radius, (float[]){0.0f, 0.0f}, 1.0f, blur, matrix);
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

	// FIXME Use filter settings from output, see set_scale_filter.
	glTexParameteri(attribs.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// TODO Check texture attribute `y_invert`
	struct style_shader_prog_tex *shader;
	switch (attribs.target) {
	case GL_TEXTURE_2D:
		if (attribs.has_alpha) {
			shader = &shaderprog_rgba_tex;
		} else {
			shader = &shaderprog_rgb_tex;
		}
		break;
	case GL_TEXTURE_EXTERNAL_OES:
		shader = &shaderprog_ext_tex;
		break;
	}

	glUseProgram(shader->prog);

	glUniform1i(shader->uniforms.tex, 0);
	glUniformMatrix3fv(shader->uniforms.proj, 1, GL_FALSE, transposition);

	GLfloat verts[]    = { quad_verts(0.0f, 1.0f, 1.0f, 0.0f) };
	GLfloat texcoord[] = { quad_verts(0.0f, 1.0f, 1.0f, 0.0f) };

	glVertexAttribPointer(shader->attributes.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(shader->attributes.texcoord, 2, GL_FLOAT, GL_FALSE, 0, texcoord);

	glEnableVertexAttribArray(shader->attributes.pos);
	glEnableVertexAttribArray(shader->attributes.texcoord);

	glDrawArrays(GL_TRIANGLES, 0, sizeof(verts)/sizeof(*verts)/2);

	glDisableVertexAttribArray(shader->attributes.pos);
	glDisableVertexAttribArray(shader->attributes.texcoord);

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
