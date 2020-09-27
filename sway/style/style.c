#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <stddef.h>
#include <time.h>
#include "sway/output.h"
#include "sway/style.h"
#include "sway/tree/view.h"

static float interpolate_linear(float v0, float v1, float t) {
	return v0 + t * (v1 - v0);
}

static float interpolate_ease_out(float v0, float v1, float t) {
	t = -(t-1.0f)*(t-1.0f)+1.0f;
	return interpolate_linear(v0, v1, t);
}

/* static float interpolate_ease_in(float v0, float v1, float t) { */
/* 	t = t*t; */
/* 	return interpolate_linear(v0, v1, t); */
/* } */

/* static float interpolate_ease_inout(float v0, float v1, float t) { */
/* 	t = -2.0f * t * t * (t-1.5f); */
/* 	return interpolate_linear(v0, v1, t); */
/* } */

void style_init(struct sway_style *s) {
	memset(s->transitions, 0, sizeof(s->transitions));
	for (size_t i = 0; i < STYLE_PROPS_SIZE; ++i) {
		s->props[i] = -1.0f;
		s->transitions[i].transition_func = interpolate_linear;
	}
}

// TODO remove
void style_test(struct sway_style *s) {
	for (size_t i = 0; i < STYLE_PROPS_SIZE; ++i) {
		s->transitions[i].to = 0.0f;
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	style_set_vector4(s, SV4_PADDING, (float[]){10.0f, 10.0f, 10.0f, 10.0f});
	style_set_vector4(s, SV4_BACKGROUND_COLOR, (float[]){1.0f, 1.0f, 1.0f, 1.0f});
	style_set_scalar(s, SS_OPACITY, 0.5f);

	style_set_scalar(s, SS_BOX_SHADOW_SPREAD, -10.0f);
	style_set_scalar(s, SS_BOX_SHADOW_V_OFFSET, 10.0f);
	style_set_scalar(s, SS_BOX_SHADOW_H_OFFSET, 0.0f);
	style_set_vector4(s, SV4_BOX_SHADOW_COLOR, (float[]){0.0f, 0.0f, 0.0f, 0.5f});
	style_set_scalar(s, SS_BOX_SHADOW_BLUR, 20.0f);
	struct style_transition *t = &s->transitions[STYLE_SS_OFFSET + SS_BOX_SHADOW_BLUR];
	t->begin = t->end = now;
	t->end.tv_sec += 1;
	t->from = 0.0f;
	t->transition_func = interpolate_ease_out;

	style_set_scalar(s, SS_TRANSLATION_X, 0.0f);
	style_set_scalar(s, SS_TRANSLATION_Y, 0.0f);
	t = &s->transitions[STYLE_SS_OFFSET + SS_TRANSLATION_Y];
	t->begin = t->end = now;
	t->end.tv_sec += 1;
	t->from = 100.0f;
	t->transition_func = interpolate_ease_out;

	style_set_vector4(s, SV4_BORDER_RADIUS, (float[]){3.0f, 3.0f, 3.0f, 3.0f});
	style_set_vector4(s, SV4_BORDER_WIDTH,  (float[]){1.0f, 1.0f, 1.0f, 1.0f});
	style_set_vector4(s, SV4_BORDER_COLOR, (float[]){0.0f, 0.0f, 0.0f, 1.0f});
	t = &s->transitions[STYLE_SV4_OFFSET + SV4_BORDER_COLOR*4];
	t[0].begin = t[0].end = now;
	t[0].end.tv_sec += 1;
	t[0].from = 1.0f;
	t[0].transition_func = interpolate_ease_out;
	t[1].begin = t[1].end = now;
	t[1].end.tv_sec += 3;
	t[1].from = 0.0f;
	t[2].begin = t[2].end = now;
	t[2].end.tv_sec += 3;
	t[2].from = 0.0f;
	t[3].begin = t[3].end = now;

	t = &s->transitions[STYLE_SV4_OFFSET + SV4_BORDER_RADIUS*4];
	for (size_t i = 0; i < 4; ++i) {
		t[i].begin = t[i].end = now;
		t[i].end.tv_sec += 3;
		t[i].from = 100.0f;
		t[i].transition_func = interpolate_ease_out;
	}
	style_animate(s, &now);
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
			output_damage_whole(output);
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

size_t style_titlebar_height(const struct sway_style *s) {
	const float *padding = style_get_vector4(s, SV4_PADDING);
	const float *borders = style_get_vector4(s, SV4_BORDER_WIDTH);
	return config->font_height +
			padding[SE_TOP] + padding[SE_BOTTOM] +
			borders[SE_TOP] + borders[SE_BOTTOM];
}
