#ifndef _SWAY_STYLE_H
#define _SWAY_STYLE_H
#include <stdbool.h>
#include <wlr/types/wlr_box.h>
#include <wlr/render/wlr_texture.h>

enum style_edge {
	SE_TOP,
	SE_RIGHT,
	SE_BOTTOM,
	SE_LEFT,

	SE_COUNT
};

// Represent a 4-tuple of scalar value properties, either for
// - rgba color components
// - top, right, bottom, left edge properties
// - topleft, topright, bottomright, bottomleft corners
enum style_vector4 {
	SV4_BORDER_WIDTH,
	SV4_BORDER_RADIUS,
	SV4_MARGIN,
	SV4_PADDING,
	SV4_BACKGROUND_COLOR,
	SV4_BORDER_COLOR,
	SV4_BOX_SHADOW_COLOR,

	SV4_COUNT
};

// Represent a single scalar value property.
// All values are dimensions in pixels, angles in radians or proportions in the
// unit interval [0,1].
enum style_scalar {
	SS_TRANSLATION_X,
	SS_TRANSLATION_Y,
	SS_ROTATION,
	SS_BOX_SHADOW_H_OFFSET,
	SS_BOX_SHADOW_V_OFFSET,
	SS_BOX_SHADOW_BLUR,
	SS_BOX_SHADOW_SPREAD,
	SS_BOX_SHADOW_INSET,
	SS_OPACITY,

	SS_COUNT
};

// Layout of the sway_style.props array
enum {
	STYLE_SV4_OFFSET = 0,
	STYLE_SS_OFFSET  = STYLE_SV4_OFFSET + SV4_COUNT*4,

	STYLE_PROPS_SIZE = STYLE_SS_OFFSET + SS_COUNT,
};

struct sway_style {
	// All stylable properties are stored in an array and should be read with
	// their respective accessor functions.  By storing them this way they can
	// be easily iterated over for e.g. animations.
	float props[STYLE_PROPS_SIZE];

	// TODO transitions[STYLE_PROPS_SIZE];
};

struct style_box {
	float x, y, width, height;
};

void style_init(struct sway_style *style);

void style_inherit(struct sway_style *style, const struct sway_style *from);

float style_get_scalar(const struct sway_style *style, enum style_scalar prop);
void style_set_scalar(struct sway_style *s, enum style_scalar prop, float val);
const float *style_get_vector4(const struct sway_style *style, enum style_vector4 prop);
void style_set_vector4(struct sway_style *s, enum style_vector4 prop, float val[4]);

/**
 * Returns the difference between translation and size of the content box in
 * relation to the border box, i.e. content_box - border_box.
 */
struct style_box style_content_box(const struct sway_style *s);

/**
 * Returns the difference between translation and size of the shadow box in
 * relation to the border box, i.e. content_box - border_box.
 */
struct style_box style_shadow_box(const struct sway_style *s);

/**
 * Returns a box that contains both of the specified boxes.
 */
struct style_box style_box_union(const struct style_box *a,
		const struct style_box *b);

/**
 * TODO document
 */
void style_render_shadow(struct sway_style *s, const struct wlr_box *box,
		const float matrix[static 9]);

void style_shader_init(struct wlr_renderer *renderer);

#endif
