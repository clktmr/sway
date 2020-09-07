#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <stddef.h>
#include "sway/style.h"

void style_init(struct sway_style *s) {
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
	const float inset = style_get_scalar(s, SS_BOX_SHADOW_INSET);
	const float offset_h = style_get_scalar(s, SS_BOX_SHADOW_H_OFFSET);
	const float offset_v = style_get_scalar(s, SS_BOX_SHADOW_V_OFFSET);
	const float size = blur + inset;
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
