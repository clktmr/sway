#define _POSIX_C_SOURCE 200809L
#include <GLES2/gl2.h>
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

	glUseProgram(shaderprog_decorations.prog);

	const float* shadow_color = style_get_vector4(s, SV4_BOX_SHADOW_COLOR);
	glUniform4fv(shaderprog_decorations.uniforms.fg_color, 1, shadow_color);
	glUniform4fv(shaderprog_decorations.uniforms.bg_color, 1, (float[]){0.0f, 0.0f, 0.0f, 0.0f});
	glUniform1i(shaderprog_decorations.uniforms.tex, 0);
	glUniformMatrix3fv(shaderprog_decorations.uniforms.proj, 1, GL_FALSE, transposition);

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

	glVertexAttribPointer(shaderprog_decorations.attributes.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(shaderprog_decorations.attributes.texcoord, 2, GL_FLOAT, GL_FALSE, 0, texcoord);

	glEnableVertexAttribArray(shaderprog_decorations.attributes.pos);
	glEnableVertexAttribArray(shaderprog_decorations.attributes.texcoord);

	glDrawArrays(GL_TRIANGLES, 0, sizeof(verts)/sizeof(*verts)/2);

	glDisableVertexAttribArray(shaderprog_decorations.attributes.pos);
	glDisableVertexAttribArray(shaderprog_decorations.attributes.texcoord);

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

	glUseProgram(shaderprog_decorations.prog);

	const float* border_color = style_get_vector4(s, SV4_BORDER_COLOR);
	const float* bg_color = style_get_vector4(s, SV4_BACKGROUND_COLOR);
	glUniform4fv(shaderprog_decorations.uniforms.fg_color, 1, border_color);
	glUniform4fv(shaderprog_decorations.uniforms.bg_color, 1, bg_color);
	glUniform1i(shaderprog_decorations.uniforms.tex, 0);
	glUniformMatrix3fv(shaderprog_decorations.uniforms.proj, 1, GL_FALSE, transposition);

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

	glEnableVertexAttribArray(shaderprog_decorations.attributes.pos);
	glEnableVertexAttribArray(shaderprog_decorations.attributes.texcoord);

	glDrawArrays(GL_TRIANGLES, 0, sizeof(verts)/sizeof(*verts)/2);

	glDisableVertexAttribArray(shaderprog_decorations.attributes.pos);
	glDisableVertexAttribArray(shaderprog_decorations.attributes.texcoord);

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

	glUseProgram(shaderprog_ext_tex.prog);

	glUniform1i(shaderprog_ext_tex.uniforms.tex, 0);
	glUniformMatrix3fv(shaderprog_ext_tex.uniforms.proj, 1, GL_FALSE, transposition);

	GLfloat verts[]    = { quad_verts(0.0f, 1.0f, 1.0f, 0.0f) };
	GLfloat texcoord[] = { quad_verts(0.0f, 1.0f, 1.0f, 0.0f) };

	glVertexAttribPointer(shaderprog_ext_tex.attributes.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(shaderprog_ext_tex.attributes.texcoord, 2, GL_FLOAT, GL_FALSE, 0, texcoord);

	glEnableVertexAttribArray(shaderprog_ext_tex.attributes.pos);
	glEnableVertexAttribArray(shaderprog_ext_tex.attributes.texcoord);

	glDrawArrays(GL_TRIANGLES, 0, sizeof(verts)/sizeof(*verts)/2);

	glDisableVertexAttribArray(shaderprog_ext_tex.attributes.pos);
	glDisableVertexAttribArray(shaderprog_ext_tex.attributes.texcoord);

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
