
#include "graphics/render.h"

#include "graphics/material.h"
#include "graphics/matrix.h"
#include "graphics/paths/PathRenderer.h"
#include "graphics/software/FSFont.h"
#include "graphics/software/NVGFont.h"
#include "graphics/software/VFNTFont.h"
#include "graphics/software/font_internal.h"
#include "localization/localize.h"
#include "mod_table/mod_table.h"
#include "render/3d.h"

static void gr_flash_internal(int r, int g, int b, int a, bool alpha_flash)
{
	CLAMP(r, 0, 255);
	CLAMP(g, 0, 255);
	CLAMP(b, 0, 255);
	CLAMP(a, 0, 255);

	int x1 = (gr_screen.clip_left + gr_screen.offset_x);
	int y1 = (gr_screen.clip_top + gr_screen.offset_y);
	int x2 = (gr_screen.clip_right + gr_screen.offset_x) + 1;
	int y2 = (gr_screen.clip_bottom + gr_screen.offset_y) + 1;

	material render_material;
	// By default materials use the immediate mode shader which is fine for us
	render_material.set_depth_mode(ZBUFFER_TYPE_NONE);

	if (alpha_flash) {
		render_material.set_color(r, g, b, a);
		render_material.set_blend_mode(ALPHA_BLEND_ALPHA_BLEND_ALPHA);
	} else {
		render_material.set_color(r, g, b, 255);
		render_material.set_blend_mode(ALPHA_BLEND_ALPHA_ADDITIVE);
	}

	int glVertices[8] = { x1, y1, x1, y2, x2, y1, x2, y2 };

	vertex_layout vert_def;

	vert_def.add_vertex_component(vertex_format_data::SCREEN_POS, sizeof(int) * 2, 0);

	gr_render_primitives_2d_immediate(&render_material, PRIM_TYPE_TRISTRIP, &vert_def, 4, glVertices, sizeof(int) * 8);
}

void gr_flash(int r, int g, int b) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	if (!(r || g || b)) {
		return;
	}

	gr_flash_internal(r, g, b, 255, false);
}

void gr_flash_alpha(int r, int g, int b, int a) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	if (!(r || g || b || a)) {
		return;
	}

	gr_flash_internal(r, g, b, a, true);
}

static void
draw_textured_quad(material* mat, float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2) {
	GR_DEBUG_SCOPE("Draw textured quad");

	float glVertices[4][4] = {{ x1, y1, u1, v1 },
							  { x1, y2, u1, v2 },
							  { x2, y1, u2, v1 },
							  { x2, y2, u2, v2 }};

	vertex_layout vert_def;

	vert_def.add_vertex_component(vertex_format_data::POSITION2, sizeof(float) * 4, 0);
	vert_def.add_vertex_component(vertex_format_data::TEX_COORD2, sizeof(float) * 4, sizeof(float) * 2);

	gr_render_primitives_immediate(mat, PRIM_TYPE_TRISTRIP, &vert_def, 4, glVertices, sizeof(float) * 4 * 4);
}

static void bitmap_ex_internal(int x,
							   int y,
							   int w,
							   int h,
							   int sx,
							   int sy,
							   int resize_mode,
							   bool aabitmap,
							   bool mirror,
							   color* clr,
							   float scale_factor = 1.0f) {
	if ((w < 1) || (h < 1)) {
		return;
	}

	if (aabitmap && !clr->is_alphacolor) {
		return;
	}

	float u0, u1, v0, v1;
	float x1, x2, y1, y2;
	int bw, bh;

	bool do_resize;
	if (resize_mode != GR_RESIZE_NONE && (gr_screen.custom_size || (gr_screen.rendering_to_texture != -1))) {
		do_resize = true;
	} else {
		do_resize = false;
	}

	bm_get_info(gr_screen.current_bitmap, &bw, &bh);

	if (scale_factor != 1.0f) {
		bw = static_cast<int>(bw * scale_factor);
		bh = static_cast<int>(bh * scale_factor);
	}

	u0 = (i2fl(sx) / i2fl(bw));
	v0 = (i2fl(sy) / i2fl(bh));

	u1 = (i2fl(sx + w) / i2fl(bw));
	v1 = (i2fl(sy + h) / i2fl(bh));

	x1 = i2fl(x + ((do_resize) ? gr_screen.offset_x_unscaled : gr_screen.offset_x));
	y1 = i2fl(y + ((do_resize) ? gr_screen.offset_y_unscaled : gr_screen.offset_y));
	x2 = x1 + i2fl(w);
	y2 = y1 + i2fl(h);

	if (do_resize) {
		gr_resize_screen_posf(&x1, &y1, NULL, NULL, resize_mode);
		gr_resize_screen_posf(&x2, &y2, NULL, NULL, resize_mode);
	}

	if (mirror) {
		float temp = u0;
		u0 = u1;
		u1 = temp;
	}

	material render_mat;
	render_mat.set_blend_mode(ALPHA_BLEND_ALPHA_BLEND_ALPHA);
	render_mat.set_depth_mode(ZBUFFER_TYPE_NONE);
	render_mat.set_texture_map(TM_BASE_TYPE, gr_screen.current_bitmap);
	render_mat.set_color(clr->red, clr->green, clr->blue, clr->alpha);
	render_mat.set_cull_mode(false);

	if (aabitmap) {
		render_mat.set_texture_type(material::TEX_TYPE_AABITMAP);
	} else {
		if (bm_has_alpha_channel(gr_screen.current_bitmap)) {
			render_mat.set_texture_type(material::TEX_TYPE_XPARENT);
		} else {
			render_mat.set_texture_type(material::TEX_TYPE_NORMAL);
		}
	}

	draw_textured_quad(&render_mat, x1, y1, u0, v0, x2, y2, u1, v1);
}

void gr_aabitmap(int x, int y, int resize_mode, bool mirror, float scale_factor) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	GR_DEBUG_SCOPE("Draw AA-bitmap");

	int w, h, do_resize;

	bm_get_info(gr_screen.current_bitmap, &w, &h);

	if (scale_factor != 1.0f) {
		w = static_cast<int>(w * scale_factor);
		h = static_cast<int>(h * scale_factor);
	}

	if (resize_mode != GR_RESIZE_NONE && (gr_screen.custom_size || (gr_screen.rendering_to_texture != -1))) {
		do_resize = 1;
	} else {
		do_resize = 0;
	}

	int dx1 = x;
	int dx2 = x + w - 1;
	int dy1 = y;
	int dy2 = y + h - 1;
	int sx = 0, sy = 0;

	int clip_left = ((do_resize) ? gr_screen.clip_left_unscaled : gr_screen.clip_left);
	int clip_right = ((do_resize) ? gr_screen.clip_right_unscaled : gr_screen.clip_right);
	int clip_top = ((do_resize) ? gr_screen.clip_top_unscaled : gr_screen.clip_top);
	int clip_bottom = ((do_resize) ? gr_screen.clip_bottom_unscaled : gr_screen.clip_bottom);

	if ((dx1 > clip_right) || (dx2 < clip_left)) {
		return;
	}

	if ((dy1 > clip_bottom) || (dy2 < clip_top)) {
		return;
	}

	if (dx1 < clip_left) {
		sx = clip_left - dx1;
		dx1 = clip_left;
	}

	if (dy1 < clip_top) {
		sy = clip_top - dy1;
		dy1 = clip_top;
	}

	if (dx2 > clip_right) {
		dx2 = clip_right;
	}

	if (dy2 > clip_bottom) {
		dy2 = clip_bottom;
	}

	if ((sx < 0) || (sy < 0)) {
		return;
	}

	if ((sx >= w) || (sy >= h)) {
		return;
	}

	// Draw bitmap bm[sx,sy] into (dx1,dy1)-(dx2,dy2)
	bitmap_ex_internal(dx1,
					   dy1,
					   (dx2 - dx1 + 1),
					   (dy2 - dy1 + 1),
					   sx,
					   sy,
					   resize_mode,
					   true,
					   mirror,
					   &GR_CURRENT_COLOR,
					   scale_factor);
}
void gr_aabitmap_ex(int x, int y, int w, int h, int sx, int sy, int resize_mode, bool mirror, float scale_factor) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	int reclip;
#ifndef NDEBUG
	int count = 0;
#endif

	int bw, bh, do_resize;

	bm_get_info(gr_screen.current_bitmap, &bw, &bh);

	if (scale_factor != 1.0f) {
		bw = fl2i(bw * scale_factor);
		bh = fl2i(bh * scale_factor);

		// If we're scaling then we need to scale these to make sure the right and bottom clip planes are scaled as well
		w = fl2i(w * scale_factor);
		h = fl2i(h * scale_factor);

		sx = fl2i(sx * scale_factor);
		sy = fl2i(sy * scale_factor);
	}

	int dx1 = x;
	int dx2 = x + w - 1;
	int dy1 = y;
	int dy2 = y + h - 1;

	if (resize_mode != GR_RESIZE_NONE && (gr_screen.custom_size || (gr_screen.rendering_to_texture != -1))) {
		do_resize = 1;
	} else {
		do_resize = 0;
	}

	int clip_left = ((do_resize) ? gr_screen.clip_left_unscaled : gr_screen.clip_left);
	int clip_right = ((do_resize) ? gr_screen.clip_right_unscaled : gr_screen.clip_right);
	int clip_top = ((do_resize) ? gr_screen.clip_top_unscaled : gr_screen.clip_top);
	int clip_bottom = ((do_resize) ? gr_screen.clip_bottom_unscaled : gr_screen.clip_bottom);

	do {
		reclip = 0;

#ifndef NDEBUG
		if (count > 1) {
			Int3();
		}

		count++;
#endif

		if ((dx1 > clip_right) || (dx2 < clip_left)) {
			return;
		}

		if ((dy1 > clip_bottom) || (dy2 < clip_top)) {
			return;
		}

		if (dx1 < clip_left) {
			sx += clip_left - dx1;
			dx1 = clip_left;
		}

		if (dy1 < clip_top) {
			sy += clip_top - dy1;
			dy1 = clip_top;
		}

		if (dx2 > clip_right) {
			dx2 = clip_right;
		}

		if (dy2 > clip_bottom) {
			dy2 = clip_bottom;
		}

		if (sx < 0) {
			dx1 -= sx;
			sx = 0;
			reclip = 1;
		}

		if (sy < 0) {
			dy1 -= sy;
			sy = 0;
			reclip = 1;
		}

		w = dx2 - dx1 + 1;
		h = dy2 - dy1 + 1;

		if (sx + w > bw) {
			w = bw - sx;
			dx2 = dx1 + w - 1;
		}

		if (sy + h > bh) {
			h = bh - sy;
			dy2 = dy1 + h - 1;
		}

		if ((w < 1) || (h < 1)) {
			// clipped away!
			return;
		}
	} while (reclip);

	// Make sure clipping algorithm works
#ifndef NDEBUG
	Assert(w > 0);
	Assert(h > 0);
	Assert(w == (dx2 - dx1 + 1));
	Assert(h == (dy2 - dy1 + 1));
	Assert(sx >= 0);
	Assert(sy >= 0);
	Assert(sx + w <= bw);
	Assert(sy + h <= bh);
	Assert(dx2 >= dx1);
	Assert(dy2 >= dy1);
	Assert((dx1 >= clip_left) && (dx1 <= clip_right));
	Assert((dx2 >= clip_left) && (dx2 <= clip_right));
	Assert((dy1 >= clip_top) && (dy1 <= clip_bottom));
	Assert((dy2 >= clip_top) && (dy2 <= clip_bottom));
#endif

	// We now have dx1,dy1 and dx2,dy2 and sx, sy all set validly within clip regions.
	bitmap_ex_internal(dx1,
		dy1,
		(dx2 - dx1 + 1),
		(dy2 - dy1 + 1),
		sx,
		sy,
		resize_mode,
		true,
		mirror,
		&GR_CURRENT_COLOR,
		scale_factor);
}
//these are penguins bitmap functions
void gr_bitmap_ex(int x, int y, int w, int h, int sx, int sy, int resize_mode, bool mirror, float scale_factor) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	int reclip;
#ifndef NDEBUG
	int count = 0;
#endif

	int bw, bh, do_resize;

	bm_get_info(gr_screen.current_bitmap, &bw, &bh);

	if (scale_factor != 1.0f) {
		bw = fl2i(bw * scale_factor);
		bh = fl2i(bh * scale_factor);

		// If we're scaling then we need to scale these to make sure the right and bottom clip planes are scaled as well
		w = fl2i(w * scale_factor);
		h = fl2i(h * scale_factor);

		sx = fl2i(sx * scale_factor);
		sy = fl2i(sy * scale_factor);
	}

	int dx1 = x;
	int dx2 = x + w - 1;
	int dy1 = y;
	int dy2 = y + h - 1;

	if (resize_mode != GR_RESIZE_NONE && (gr_screen.custom_size || (gr_screen.rendering_to_texture != -1))) {
		do_resize = 1;
	} else {
		do_resize = 0;
	}

	int clip_left = ((do_resize) ? gr_screen.clip_left_unscaled : gr_screen.clip_left);
	int clip_right = ((do_resize) ? gr_screen.clip_right_unscaled : gr_screen.clip_right);
	int clip_top = ((do_resize) ? gr_screen.clip_top_unscaled : gr_screen.clip_top);
	int clip_bottom = ((do_resize) ? gr_screen.clip_bottom_unscaled : gr_screen.clip_bottom);

	do {
		reclip = 0;

#ifndef NDEBUG
		if (count > 1) {
			Int3();
		}

		count++;
#endif

		if ((dx1 > clip_right) || (dx2 < clip_left)) {
			return;
		}

		if ((dy1 > clip_bottom) || (dy2 < clip_top)) {
			return;
		}

		if (dx1 < clip_left) {
			sx += clip_left - dx1;
			dx1 = clip_left;
		}

		if (dy1 < clip_top) {
			sy += clip_top - dy1;
			dy1 = clip_top;
		}

		if (dx2 > clip_right) {
			dx2 = clip_right;
		}

		if (dy2 > clip_bottom) {
			dy2 = clip_bottom;
		}

		if (sx < 0) {
			dx1 -= sx;
			sx = 0;
			reclip = 1;
		}

		if (sy < 0) {
			dy1 -= sy;
			sy = 0;
			reclip = 1;
		}

		w = dx2 - dx1 + 1;
		h = dy2 - dy1 + 1;

		if ((sx + w) > bw) {
			w = bw - sx;
			dx2 = dx1 + w - 1;
		}

		if ((sy + h) > bh) {
			h = bh - sy;
			dy2 = dy1 + h - 1;
		}

		if ((w < 1) || (h < 1)) {
			// clipped away!
			return;
		}
	} while (reclip);

	// Make sure clipping algorithm works
#ifndef NDEBUG
	Assert(w > 0);
	Assert(h > 0);
	Assert(w == (dx2 - dx1 + 1));
	Assert(h == (dy2 - dy1 + 1));
	Assert(sx >= 0);
	Assert(sy >= 0);
	Assert((sx + w) <= bw);
	Assert((sy + h) <= bh);
	Assert(dx2 >= dx1);
	Assert(dy2 >= dy1);
	Assert((dx1 >= clip_left) && (dx1 <= clip_right));
	Assert((dx2 >= clip_left) && (dx2 <= clip_right));
	Assert((dy1 >= clip_top) && (dy1 <= clip_bottom));
	Assert((dy2 >= clip_top) && (dy2 <= clip_bottom));
#endif

	color clr;
	gr_init_alphacolor(&clr, 255, 255, 255, fl2i(gr_screen.current_alpha * 255.0f));

	// We now have dx1,dy1 and dx2,dy2 and sx, sy all set validly within clip regions.
	bitmap_ex_internal(dx1, dy1, (dx2 - dx1 + 1), (dy2 - dy1 + 1), sx, sy, resize_mode, false, mirror, &clr, scale_factor);
}

#define MAX_VERTS_PER_DRAW 300
struct v4 {
	float x, y, u, v;
};
static v4 String_render_buff[MAX_VERTS_PER_DRAW];

namespace font {
extern int get_char_width_old(font* fnt, ubyte c1, ubyte c2, int* width, int* spacing);
}

static void gr_string_old(float sx,
	float sy,
	const char* s,
	const char* end,
	font::font* fontData,
	float height,
	bool canAutoScale,
	bool canScale,
	int resize_mode,
	float scaleMultiplier)
{
	GR_DEBUG_SCOPE("Render VFNT string");

	float x = sx;
	float y = sy;

	material render_mat;
	render_mat.set_blend_mode(ALPHA_BLEND_ALPHA_BLEND_ALPHA);
	render_mat.set_depth_mode(ZBUFFER_TYPE_NONE);
	render_mat.set_texture_map(TM_BASE_TYPE, fontData->bitmap_id);
	render_mat.set_color(GR_CURRENT_COLOR.red,
		GR_CURRENT_COLOR.green,
		GR_CURRENT_COLOR.blue,
		GR_CURRENT_COLOR.alpha);
	render_mat.set_cull_mode(false);
	render_mat.set_texture_type(material::TEX_TYPE_AABITMAP);

	int buffer_offset = 0;

	int ibw, ibh;

	bm_get_info(fontData->bitmap_id, &ibw, &ibh);

	float bw = i2fl(ibw);
	float bh = i2fl(ibh);

	bool do_resize;
	if (resize_mode != GR_RESIZE_NONE && (gr_screen.custom_size || (gr_screen.rendering_to_texture != -1))) {
		do_resize = true;
	} else {
		do_resize = false;
	}

	int clip_left = ((do_resize) ? gr_screen.clip_left_unscaled : gr_screen.clip_left);
	int clip_right = ((do_resize) ? gr_screen.clip_right_unscaled : gr_screen.clip_right);
	int clip_top = ((do_resize) ? gr_screen.clip_top_unscaled : gr_screen.clip_top);
	int clip_bottom = ((do_resize) ? gr_screen.clip_bottom_unscaled : gr_screen.clip_bottom);

	vertex_layout vert_def;

	vert_def.add_vertex_component(vertex_format_data::POSITION2, sizeof(v4), (int)offsetof(v4, x));
	vert_def.add_vertex_component(vertex_format_data::TEX_COORD2, sizeof(v4), (int)offsetof(v4, u));

	gr_set_2d_matrix();

	float scale_factor = (canScale && !Fred_running) ? get_font_scale_factor() : 1.0f;

	if (canAutoScale && !Fred_running) {
		float autoSizedFont = calculate_auto_font_size(height);

		// Calculate the auto scale factor
		float auto_scale_factor = autoSizedFont / height;
		scale_factor *= auto_scale_factor;
	}

	scale_factor *= scaleMultiplier;

	int letter;
	while (s < end) {
		// Handle line breaks
		while (*s == '\n') {
			s++;
			y += height * scale_factor;
			x = sx;
		}

		if (*s == 0) {
			break;
		}

		// Get character width and spacing
		int raw_width = 0, raw_spacing = 0;
		letter = font::get_char_width_old(fontData,
			(ubyte)s[0],
			(ubyte)s[1],
			&raw_width,
			&raw_spacing);
		s++;

		// Not in font, draw as space
		if (letter < 0) {
			x += raw_spacing * scale_factor;
			continue;
		}

		// UV coordinates
		int u = fontData->bm_u[letter];
		int v = fontData->bm_v[letter];
		float char_width = i2fl(raw_width);
		float char_height = i2fl(height);

		// Scale output dimensions and positions
		float xc = x; // Keep the X position unscaled
		float yc = y; // Keep the Y position unscaled
		float wc = char_width * scale_factor; // Scale width
		float hc = char_height * scale_factor; // Scale height

		// Check if the character is completely out of bounds. This uses scaled width and height
		if ((xc > clip_right) || ((xc + wc) < clip_left) || (yc > clip_bottom) || ((yc + hc) < clip_top)) {
			x += raw_spacing * scale_factor;
			continue;
		}

		// Apply offsets
		float x1 = xc + ((do_resize) ? gr_screen.offset_x_unscaled : gr_screen.offset_x);
		float y1 = yc + ((do_resize) ? gr_screen.offset_y_unscaled : gr_screen.offset_y);
		float x2 = x1 + wc;
		float y2 = y1 + hc;

		// Resize screen positions
		if (do_resize) {
			gr_resize_screen_posf(&x1, &y1, NULL, NULL, resize_mode);
			gr_resize_screen_posf(&x2, &y2, NULL, NULL, resize_mode);
		}

		// Get the character from the UV
		float u0 = u / bw;
		float v0 = v / bh;
		float u1 = (u + char_width) / bw;
		float v1 = (v + char_height) / bh;

		// Add vertices for the character
		String_render_buff[buffer_offset++] = {x1, y1, u0, v0};
		String_render_buff[buffer_offset++] = {x1, y2, u0, v1};
		String_render_buff[buffer_offset++] = {x2, y1, u1, v0};
		String_render_buff[buffer_offset++] = {x1, y2, u0, v1};
		String_render_buff[buffer_offset++] = {x2, y1, u1, v0};
		String_render_buff[buffer_offset++] = {x2, y2, u1, v1};

		// If the buffer is full, render it now
		if (buffer_offset == MAX_VERTS_PER_DRAW) {
			gr_render_primitives_immediate(&render_mat,
				PRIM_TYPE_TRIS,
				&vert_def,
				buffer_offset,
				String_render_buff,
				sizeof(v4) * buffer_offset);
			buffer_offset = 0;
		}

		// Advance x for the next character
		x += raw_spacing * scale_factor;
	}

	// Render remaining vertices in the buffer
	if (buffer_offset) {
		gr_render_primitives_immediate(&render_mat,
			PRIM_TYPE_TRIS,
			&vert_def,
			buffer_offset,
			String_render_buff,
			sizeof(v4) * buffer_offset);
	}

	gr_end_2d_matrix();
}



namespace {
bool buffering_nanovg = false; //!< flag for when NanoVG buffering is enabled

void setupDrawingState(graphics::paths::PathRenderer* path) {
	path->resetState();
}

void setupTransforms(graphics::paths::PathRenderer* path, int resize_mode) {
	float x = 0.0f;
	float y = 0.0f;
	float w = 1.0f;
	float h = 1.0f;
	bool do_resize = gr_resize_screen_posf(&x, &y, &w, &h, resize_mode);

	if (gr_screen.rendering_to_texture != -1) {
		// Flip the Y-axis when rendering to texture
		path->translate(0.f, i2fl(gr_screen.max_h));
		path->scale(1.f, -1.f);
	}

	path->translate(x, y);
	path->scale(w, h);

	int clip_width = ((do_resize) ? gr_screen.clip_width_unscaled : gr_screen.clip_width);
	int clip_height = ((do_resize) ? gr_screen.clip_height_unscaled : gr_screen.clip_height);

	int offset_x = ((do_resize) ? gr_screen.offset_x_unscaled : gr_screen.offset_x);
	int offset_y = ((do_resize) ? gr_screen.offset_y_unscaled : gr_screen.offset_y);

	path->translate(i2fl(offset_x), i2fl(offset_y));

	path->scissor(0.0f, 0.0f, i2fl(clip_width), i2fl(clip_height));
}

graphics::paths::PathRenderer* beginDrawing(int resize_mode) {
	auto path = graphics::paths::PathRenderer::instance();

	path->saveState();
	setupDrawingState(path);

	if (!buffering_nanovg) {
		// If buffering is enabled then this has already been called
		path->beginFrame();
	}
	setupTransforms(path, resize_mode);

	path->beginPath();

	path->setStrokeWidth(GR_CURRENT_LINE_WIDTH);

	return path;
}

void endDrawing(graphics::paths::PathRenderer* path) {
	if (!buffering_nanovg) {
		// If buffering is enabled then this will be called later
		path->endFrame();
	}
	path->restoreState();
}
}

void gr_string(float sx, float sy, const char* s, int resize_mode, float scaleMultiplier, size_t in_length)
{
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	GR_DEBUG_SCOPE("Render string");

	using namespace font;
	using namespace graphics::paths;
	namespace fo = font;

	Assertion(s != NULL, "NULL pointer passed to gr_string!");

	if (!FontManager::isReady() || (*s == '\0')) {
		return;
	}

	size_t length;
	if (in_length == std::string::npos) {
		length = strlen(s);
	} else {
		length = in_length;
	}

	FSFont* currentFont = FontManager::getCurrentFont();

	if (currentFont->getType() == VFNT_FONT) {
		VFNTFont* fnt = static_cast<VFNTFont*>(currentFont);
		fo::font* fontData = fnt->getFontData();

		gr_string_old(sx, sy, s, s + length, fontData, fnt->getHeight(), currentFont->getAutoScaleBehavior(), currentFont->getScaleBehavior(), resize_mode, scaleMultiplier);
	} else if (currentFont->getType() == NVG_FONT) {
		GR_DEBUG_SCOPE("Render TTF string");

		auto path = beginDrawing(resize_mode);

		auto nvgFont = static_cast<NVGFont*>(currentFont);

		float scale_factor = (nvgFont->getScaleBehavior() && !Fred_running) ? get_font_scale_factor() : 1.0f;
		scale_factor *= scaleMultiplier;

		float originalSize = nvgFont->getSize();
		float scaledSize = originalSize * scale_factor;

		// Calculate the offset to center the text
		float offsetX = 0.0f;
		// This is a compromise to try and size the text around center to minimize text offsets during scaling behavior.
		// TODO Update this if multiline text is found to be negatively affected or a proper method of setting text anchors is added
		float offsetY = (scaledSize - originalSize) * 0.5f;

		path->translate(sx - offsetX, sy - offsetY);

		path->fontFaceId(nvgFont->getHandle());
		path->fontSize(scaledSize);
		path->textLetterSpacing(nvgFont->getLetterSpacing());
		path->textAlign(static_cast<TextAlign>(ALIGN_TOP | ALIGN_LEFT));

		float scaleX = 1.0f;
		float scaleY = 1.0f;
		gr_resize_screen_posf(nullptr, nullptr, &scaleX, &scaleY, resize_mode);

		float invscaleX = 1.f / scaleX;

		bool twoPassRequired = false;

		path->setFillColor(&GR_CURRENT_COLOR);

		// Do a two pass algorithm, first render text using NanoVG, then render old characters
		for (int pass = 0; pass < 2; ++pass) {
			const char* text = s;
			size_t textLen = length;
			float x = 0.0f;
			float y = 0.0f;

			size_t tokenLength;
			while ((tokenLength = NVGFont::getTokenLength(text, textLen)) > 0) {
				textLen -= tokenLength;

				bool doRender = true;
				bool specialChar = false;
				if (tokenLength == 1) {
					// We may have encountered a special character
					switch (*text) {
						case '\n':
							doRender = false;

							y += nvgFont->getHeight();
							x = 0;
							break;
						case '\t':
							doRender = false;

							x += nvgFont->getTabWidth();
							break;
						case '\r':
							// Ignore Carriage return chars
							doRender = false;
							break;
						default:
							// Only do special character handling if we are not in unicode mode. Otherwise this is just a normal character for us
							if (!Unicode_text_mode) {
								if (*text >= Lcl_special_chars || *text < 0) {
									specialChar = true;
									twoPassRequired = true;
								}
								else {
									doRender = true;
								}
							}

							break;
					}
				}

				if (specialChar) {
					if (pass == 1) {
						// We compute the top offset of the special character by aligning it to the base line of the string
						// This is done by moving to the base line of the string by adding the ascender value and then
						// accounting for the height of the text with the height of the special font
						auto yOffset = nvgFont->getTopOffset() +
							(nvgFont->getAscender() - nvgFont->getSpecialCharacterFont()->h);

						gr_string_old(sx + x * scaleX,
									  sy + (y + yOffset) * scaleY,
									  text,
									  text + 1,
									  nvgFont->getSpecialCharacterFont(),
									  nvgFont->getHeight(),
							          nvgFont->getAutoScaleBehavior(),
									  nvgFont->getScaleBehavior(),
									  resize_mode,
									  scaleMultiplier);
					}

					int width;
					int spacing;
					get_char_width_old(nvgFont->getSpecialCharacterFont(), (ubyte)*text, (ubyte)'\0', &width, &spacing);

					x += i2fl(spacing) * invscaleX;
				} else if (doRender) {
					if (doRender && tokenLength > 0) {
						float advance;
						float currentX = x * scaleX;
						float currentY = y + nvgFont->getTopOffset();

						if (pass == 0) {
							path->text(currentX, currentY, text, text + tokenLength);
						}

						advance = path->textBounds(0.f, 0.f, text, text + tokenLength, nullptr);
						x += advance * invscaleX;
					}
				}

				text = text + tokenLength;
			}

			if (pass == 0) {
				endDrawing(path);
			}

			if (!twoPassRequired) {
				break;
			}
		}
	} else {
		Error(LOCATION, "Invalid type enumeration for font \"%s\". Get a coder!", currentFont->getName().c_str());
	}
}

static void gr_line(float x1, float y1, float x2, float y2, int resize_mode) {
	auto path = beginDrawing(resize_mode);

	if ((x1 == x2) && (y1 == y2)) {
		path->circle(x1, y1, 1.5);

		path->setFillColor(&GR_CURRENT_COLOR);
		path->fill();
	} else {
		path->moveTo(x1, y1);
		path->lineTo(x2, y2);

		path->setStrokeColor(&GR_CURRENT_COLOR);
		path->stroke();
	}

	endDrawing(path);
}

void gr_line(int x1, int y1, int x2, int y2, int resize_mode) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	gr_line(i2fl(x1), i2fl(y1), i2fl(x2), i2fl(y2), resize_mode);
}

void gr_aaline(vertex* v1, vertex* v2) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	float x1 = v1->screen.xyw.x;
	float y1 = v1->screen.xyw.y;
	float x2 = v2->screen.xyw.x;
	float y2 = v2->screen.xyw.y;

	// AA is now standard
	gr_line(x1, y1, x2, y2, GR_RESIZE_NONE);
}

void gr_gradient(int x1, int y1, int x2, int y2, int resize_mode) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	if (!GR_CURRENT_COLOR.is_alphacolor) {
		gr_line(x1, y1, x2, y2, resize_mode);
		return;
	}

	auto path = beginDrawing(resize_mode);

	color endColor = GR_CURRENT_COLOR;
	endColor.alpha = 0;

	auto gradientPaint =
		path->createLinearGradient(i2fl(x1), i2fl(y1), i2fl(x2), i2fl(y2), &GR_CURRENT_COLOR, &endColor);

	path->moveTo(i2fl(x1), i2fl(y1));
	path->lineTo(i2fl(x2), i2fl(y2));

	path->setStrokePaint(gradientPaint);
	path->stroke();

	endDrawing(path);
}
void gr_pixel(int x, int y, int resize_mode) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	gr_line(x, y, x, y, resize_mode);
}

void gr_circle(int xc, int yc, int d, int resize_mode) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	auto path = beginDrawing(resize_mode);

	path->circle(i2fl(xc), i2fl(yc), d / 2.0f);
	path->setFillColor(&GR_CURRENT_COLOR);
	path->fill();

	endDrawing(path);
}
void gr_unfilled_circle(int xc, int yc, int d, int resize_mode) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	auto path = beginDrawing(resize_mode);

	path->circle(i2fl(xc), i2fl(yc), d / 2.0f);
	path->setStrokeColor(&GR_CURRENT_COLOR);
	path->stroke();

	endDrawing(path);
}
void gr_arc(int xc, int yc, float r, float angle_start, float angle_end, bool fill, int resize_mode) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	// Ensure that angle_start < angle_end
	if (angle_end < angle_start) {
		float temp = angle_start;
		angle_start = angle_end;
		angle_end = temp;
	}

	using namespace graphics::paths;

	auto path = beginDrawing(resize_mode);

	if (fill) {
		path->arc(i2fl(xc), i2fl(yc), r, fl_radians(angle_start), fl_radians(angle_end), DIR_CW);
		path->lineTo(i2fl(xc), i2fl(yc));

		path->setFillColor(&GR_CURRENT_COLOR);
		path->fill();
	} else {
		path->arc(i2fl(xc), i2fl(yc), r, fl_radians(angle_start), fl_radians(angle_end), DIR_CW);
		path->setStrokeColor(&GR_CURRENT_COLOR);
		path->stroke();
	}

	endDrawing(path);
}
void gr_curve(int xc, int yc, int r, int direction, int resize_mode) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	using namespace graphics::paths;

	auto path = beginDrawing(resize_mode);
	float centerX, centerY;
	float beginAngle, endAngle;

	switch (direction) {
		case 0: {
			centerX = i2fl(xc + r);
			centerY = i2fl(yc + r);
			beginAngle = fl_radians(180.f);
			endAngle = fl_radians(270.f);
			break;
		}
		case 1: {
			centerX = i2fl(xc);
			centerY = i2fl(yc + r);
			beginAngle = fl_radians(270.f);
			endAngle = fl_radians(360.f);
			break;
		}
		case 2: {
			centerX = i2fl(xc + r);
			centerY = i2fl(yc);
			beginAngle = fl_radians(90.f);
			endAngle = fl_radians(180.f);
			break;
		}
		case 3: {
			centerX = i2fl(xc);
			centerY = i2fl(yc);
			beginAngle = fl_radians(0.f);
			endAngle = fl_radians(90.f);
			break;
		}
		default:
			return;
	}

	path->arc(centerX, centerY, i2fl(r), beginAngle, endAngle, DIR_CW);
	path->setStrokeColor(&GR_CURRENT_COLOR);
	path->stroke();

	endDrawing(path);
}

void gr_rect(int x, int y, int w, int h, int resize_mode, float angle) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	auto path = beginDrawing(resize_mode);
	if (angle != 0) {
		// If we don't do this translation before and after rotating, the rotation will use 0,0 as the pivot, flinging the rectangle far away. 
		float offsetX = x + w / 2.0f;
		float offsetY = y + h / 2.0f;
		path->translate(offsetX, offsetY);
		path->rotate(angle);
		path->translate(-offsetX, -offsetY);
	}
	path->rectangle(i2fl(x), i2fl(y), i2fl(w), i2fl(h));
	path->setFillColor(&GR_CURRENT_COLOR);
	path->fill();

	endDrawing(path);
}

void gr_shade(int x, int y, int w, int h, int resize_mode) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	auto r = (int) gr_screen.current_shader.r;
	auto g = (int) gr_screen.current_shader.g;
	auto b = (int) gr_screen.current_shader.b;
	auto a = (int) gr_screen.current_shader.c;

	color clr;
	gr_init_alphacolor(&clr, r, g, b, a);

	auto path = beginDrawing(resize_mode);

	path->rectangle(i2fl(x), i2fl(y), i2fl(w), i2fl(h));
	path->setFillColor(&clr);
	path->fill();

	endDrawing(path);
}

void gr_2d_start_buffer() {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	Assertion(!buffering_nanovg, "Tried to enable 2D buffering but it was already enabled!");

	buffering_nanovg = true;
	auto path = graphics::paths::PathRenderer::instance();

	path->beginFrame();
}

void gr_2d_stop_buffer() {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	Assertion(buffering_nanovg, "Tried to stop 2D buffering but it was not enabled!");

	buffering_nanovg = false;
	auto path = graphics::paths::PathRenderer::instance();

	path->endFrame();
}

gr_buffer_handle gr_immediate_buffer_handle;
static size_t immediate_buffer_offset = 0;
static size_t immediate_buffer_size = 0;
static const size_t IMMEDIATE_BUFFER_RESIZE_BLOCK_SIZE = 2048;

size_t gr_add_to_immediate_buffer(size_t size, void* data) {
	if (gr_screen.mode == GR_STUB) {
		return 0;
	}

	GR_DEBUG_SCOPE("Add data to immediate buffer");

	if (!gr_immediate_buffer_handle.isValid()) {
		gr_immediate_buffer_handle = gr_create_buffer(BufferType::Vertex, BufferUsageHint::Dynamic);
	}

	Assert(size > 0 && data != NULL);

	if ( immediate_buffer_offset + size > immediate_buffer_size ) {
		// incoming data won't fit the immediate buffer. time to reallocate.
		immediate_buffer_offset = 0;
		immediate_buffer_size += MAX(IMMEDIATE_BUFFER_RESIZE_BLOCK_SIZE, size);

		gr_update_buffer_data(gr_immediate_buffer_handle, immediate_buffer_size, NULL);
	}

	// only update a section of the immediate vertex buffer
	gr_update_buffer_data_offset(gr_immediate_buffer_handle, immediate_buffer_offset, size, data);

	auto old_offset = immediate_buffer_offset;

	immediate_buffer_offset += size;

	return old_offset;
}
void gr_reset_immediate_buffer() {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	if (!gr_immediate_buffer_handle.isValid()) {
		// we haven't used the immediate buffer yet
		return;
	}

	// orphan the immediate buffer so we can start fresh in a new frame
	gr_update_buffer_data(gr_immediate_buffer_handle, immediate_buffer_size, NULL);

	// bring our offset to the beginning of the immediate buffer
	immediate_buffer_offset = 0;
}

void gr_render_primitives_immediate(material* material_info,
									primitive_type prim_type,
									vertex_layout* layout,
									int n_verts,
									void* data,
									size_t size) {
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	auto offset = gr_add_to_immediate_buffer(size, data);

	gr_render_primitives(material_info, prim_type, layout, 0, n_verts, gr_immediate_buffer_handle, offset);
}

void gr_render_primitives_2d_immediate(material* material_info,
	primitive_type prim_type,
	vertex_layout* layout,
	int n_verts,
	void* data,
	size_t size)
{
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	gr_set_2d_matrix();

	gr_render_primitives_immediate(material_info, prim_type, layout, n_verts, data, size);

	gr_end_2d_matrix();
}

// _->NEW<-_ NEW new bitmap functions -Bobboau
// takes a list of rectangles that have assosiated rectangles in a texture
static void draw_bitmap_list(bitmap_rect_list* list, int n_bm, int resize_mode, material* render_mat, float angle = 0.f)
{
	GR_DEBUG_SCOPE("2D Bitmap list");

	// adapted from g3_draw_2d_poly_bitmap_list

	for (int i = 0; i < n_bm; i++) {
		bitmap_2d_list* l = &list[i].screen_rect;

		// if no valid hight or width values were given get some from the bitmap
		if ((l->w <= 0) || (l->h <= 0)) {
			bm_get_info(gr_screen.current_bitmap, &l->w, &l->h, nullptr, nullptr, nullptr);
		}

		if (resize_mode != GR_RESIZE_NONE && (gr_screen.custom_size || (gr_screen.rendering_to_texture != -1))) {
			gr_resize_screen_pos(&l->x, &l->y, &l->w, &l->h, resize_mode);
		}
	}

	auto vert_list = new vertex[6 * n_bm];
	float sw = 0.1f;


	for (int i = 0; i < n_bm; i++) {
		// stuff coords
		
		bitmap_2d_list* b = &list[i].screen_rect;
		texture_rect_list* t = &list[i].texture_rect;
		
		float centerX = 0;
		float centerY = 0;

		if (angle != 0.f) {
			centerX = (b->x + (b->x + b->w)) / 2.0f;
			centerY = (b->y + (b->y + b->h)) / 2.0f;
		}

		// tri one
		vertex* V = &vert_list[i * 6];
		
		if (angle != 0.f) {
			V->screen.xyw.x = cosf(angle) * (b->x - centerX) - sinf(angle) * (b->y - centerY) + centerX;
			V->screen.xyw.y = sinf(angle) * (b->x - centerX) + cosf(angle) * (b->y - centerY) + centerY;
		}
		else {			
			V->screen.xyw.x = (float)b->x;
			V->screen.xyw.y = (float)b->y;
		}
		
		V->screen.xyw.w = sw;
		V->texture_position.u = (float)t->u0;
		V->texture_position.v = (float)t->v0;
		V->flags = PF_PROJECTED;
		V->codes = 0;

		V++;
		if (angle != 0.f) {
			V->screen.xyw.x = cosf(angle) * (b->x + b->w - centerX) - sinf(angle) * (b->y - centerY) + centerX;
			V->screen.xyw.y = sinf(angle) * (b->x + b->w - centerX) + cosf(angle) * (b->y - centerY) + centerY;
		}
		else {
			V->screen.xyw.x = (float)(b->x + b->w);
			V->screen.xyw.y = (float)b->y;
		}

		V->screen.xyw.w = sw;
		V->texture_position.u = (float)t->u1;
		V->texture_position.v = (float)t->v0;
		V->flags = PF_PROJECTED;
		V->codes = 0;

		V++;
		if (angle != 0.f) {
			V->screen.xyw.x = cosf(angle) * (b->x + b->w - centerX) - sinf(angle) * (b->y + b->h - centerY) + centerX;
			V->screen.xyw.y = sinf(angle) * (b->x + b->w - centerX) + cosf(angle) * (b->y + b->h - centerY) + centerY;
		}
		else {
			V->screen.xyw.x = (float)(b->x + b->w);
			V->screen.xyw.y = (float)(b->y + b->h);
		}

		V->screen.xyw.w = sw;
		V->texture_position.u = (float)t->u1;
		V->texture_position.v = (float)t->v1;
		V->flags = PF_PROJECTED;
		V->codes = 0;

		// tri two
		V++;
		if (angle != 0.f) {
			V->screen.xyw.x = cosf(angle) * (b->x - centerX) - sinf(angle) * (b->y - centerY) + centerX;
			V->screen.xyw.y = sinf(angle) * (b->x - centerX) + cosf(angle) * (b->y - centerY) + centerY;
		}
		else {			
			V->screen.xyw.x = (float)b->x;
			V->screen.xyw.y = (float)b->y;
		}

		V->screen.xyw.w = sw;
		V->texture_position.u = (float)t->u0;
		V->texture_position.v = (float)t->v0;
		V->flags = PF_PROJECTED;
		V->codes = 0;

		V++;
		if (angle != 0.f) {
			V->screen.xyw.x = cosf(angle) * (b->x + b->w - centerX) - sinf(angle) * (b->y + b->h - centerY) + centerX;
			V->screen.xyw.y = sinf(angle) * (b->x + b->w - centerX) + cosf(angle) * (b->y + b->h - centerY) + centerY;
		}
		else {
			V->screen.xyw.x = (float)(b->x + b->w);
			V->screen.xyw.y = (float)(b->y + b->h);
		}

		V->screen.xyw.w = sw;
		V->texture_position.u = (float)t->u1;
		V->texture_position.v = (float)t->v1;
		V->flags = PF_PROJECTED;
		V->codes = 0;

		V++;
		if (angle != 0.f) {
			V->screen.xyw.x = cosf(angle) * (b->x - centerX) - sinf(angle) * (b->y + b->h - centerY) + centerX;
			V->screen.xyw.y = sinf(angle) * (b->x - centerX) + cosf(angle) * (b->y + b->h - centerY) + centerY;
		}
		else {
			V->screen.xyw.x = (float)b->x;
			V->screen.xyw.y = (float)(b->y + b->h);
		}

		V->screen.xyw.w = sw;
		V->texture_position.u = (float)t->u0;
		V->texture_position.v = (float)t->v1;
		V->flags = PF_PROJECTED;
		V->codes = 0;
	}

	g3_render_primitives_textured(render_mat, vert_list, 6 * n_bm, PRIM_TYPE_TRIS, true);

	delete[] vert_list;
}

void gr_bitmap_list(bitmap_rect_list* list, int n_bm, int resize_mode, float angle)
{
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	material mat_params;
	material_set_interface(&mat_params,
		gr_screen.current_bitmap,
		gr_screen.current_alphablend_mode == GR_ALPHABLEND_FILTER,
		gr_screen.current_alpha);

	draw_bitmap_list(list, n_bm, resize_mode, &mat_params, angle);
}

void gr_aabitmap_list(bitmap_rect_list* list, int n_bm, int resize_mode, float angle)
{
	if (gr_screen.mode == GR_STUB) {
		return;
	}

	material render_mat;
	render_mat.set_blend_mode(ALPHA_BLEND_ALPHA_BLEND_ALPHA);
	render_mat.set_depth_mode(ZBUFFER_TYPE_NONE);
	render_mat.set_texture_map(TM_BASE_TYPE, gr_screen.current_bitmap);
	render_mat.set_color(GR_CURRENT_COLOR);
	render_mat.set_cull_mode(false);
	render_mat.set_texture_type(material::TEX_TYPE_AABITMAP);

	draw_bitmap_list(list, n_bm, resize_mode, &render_mat, angle);
}
