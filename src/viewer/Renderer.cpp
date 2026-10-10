#include "viewer/Renderer.hpp"

#include "viewer/Layouts.hpp"
#include "viewer/ViewerState.hpp"
#include "ui/Theme.hpp"

#include <obs.h>
#include <graphics/graphics.h>

#include <algorithm>
#include <cmath>

namespace obsviewer {

namespace {

struct Rect {
	float x, y, w, h;
};

// Maps canvas coordinates to window pixels.
struct Frame {
	float fw = 1920, fh = 1080; // canvas size (OBS base resolution)
	float ox = 0, oy = 0;       // window offset of the letterboxed multiview
	float scale = 1;            // canvas units → window pixels
};

void startRegion(int vx, int vy, int vcx, int vcy, float left, float right, float top, float bottom)
{
	gs_projection_push();
	gs_viewport_push();
	gs_set_viewport(vx, vy, vcx, vcy);
	gs_ortho(left, right, top, bottom, -100.0f, 100.0f);
}

void endRegion()
{
	gs_viewport_pop();
	gs_projection_pop();
}

void drawBox(float x, float y, float w, float h, uint32_t argb)
{
	if (w <= 0.0f || h <= 0.0f)
		return;
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	gs_effect_set_color(color, argb);

	// gs_draw_sprite takes whole units; scale a 1×1 sprite instead so hairlines
	// a fraction of a canvas unit wide still draw at their true size.
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_matrix_scale3f(w, h, 1.0f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, 1, 1);
	gs_matrix_pop();
}

// Outline drawn inside the rect's edge.
void drawFrame(const Rect &r, float t, uint32_t argb)
{
	drawBox(r.x, r.y, r.w, t, argb);
	drawBox(r.x, r.y + r.h - t, r.w, t, argb);
	drawBox(r.x, r.y + t, t, r.h - 2 * t, argb);
	drawBox(r.x + r.w - t, r.y + t, t, r.h - 2 * t, argb);
}

Rect inset(const Rect &r, float d)
{
	return Rect{r.x + d, r.y + d, std::max(0.0f, r.w - 2 * d), std::max(0.0f, r.h - 2 * d)};
}

// Largest rect with the canvas aspect ratio, centred in r.
Rect fitAspect(const Rect &r, float aspect)
{
	float w = r.w, h = r.w / aspect;
	if (h > r.h) {
		h = r.h;
		w = r.h * aspect;
	}
	return Rect{r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h};
}

// Render something drawn in canvas space (0..fw, 0..fh) into rect r.
template<typename Fn> void renderScaled(const Frame &f, const Rect &r, Fn &&draw)
{
	const int vx = static_cast<int>(std::lround(f.ox + r.x * f.scale));
	const int vy = static_cast<int>(std::lround(f.oy + r.y * f.scale));
	const int vcx = static_cast<int>(std::lround(r.w * f.scale));
	const int vcy = static_cast<int>(std::lround(r.h * f.scale));
	if (vcx <= 0 || vcy <= 0)
		return;
	startRegion(vx, vy, vcx, vcy, 0.0f, f.fw, 0.0f, f.fh);
	draw();
	endRegion();
}

void drawLabelAt(obs_source_t *label, float x, float y, float s, uint32_t plate, float padX)
{
	const float lw = static_cast<float>(obs_source_get_width(label)) * s;
	const float lh = static_cast<float>(obs_source_get_height(label)) * s;
	if (lw <= 0.0f || lh <= 0.0f)
		return;
	if (plate)
		drawBox(x - padX, y, lw + 2 * padX, lh, plate);
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_matrix_scale3f(s, s, 1.0f);
	obs_source_video_render(label);
	gs_matrix_pop();
}

// Bottom-centre label inside the picture (ATEM style), sized to the tile.
void drawTileLabel(obs_source_t *label, const Rect &r, float pad, float maxHeight)
{
	if (!label)
		return;
	const float lw = static_cast<float>(obs_source_get_width(label));
	const float lh = static_cast<float>(obs_source_get_height(label));
	if (lw <= 0.0f || lh <= 0.0f)
		return;
	const float s = std::min({1.0f, (r.w - 4 * pad) / lw, maxHeight / lh});
	const float x = r.x + (r.w - lw * s) / 2;
	const float y = r.y + r.h - lh * s - pad;
	drawLabelAt(label, x, y, s, theme::kLabelPlate, pad * 0.6f);
}

// Top-left note inside the picture.
void drawTileNote(obs_source_t *label, const Rect &r, float pad, float maxHeight)
{
	if (!label)
		return;
	const float lw = static_cast<float>(obs_source_get_width(label));
	const float lh = static_cast<float>(obs_source_get_height(label));
	if (lw <= 0.0f || lh <= 0.0f)
		return;
	const float s = std::min({1.0f, (r.w - 4 * pad) / lw, maxHeight / lh});
	drawLabelAt(label, r.x + pad * 1.6f, r.y + pad, s, theme::kLabelPlate, pad * 0.6f);
}

} // namespace

void Renderer::render(const Layout &layout, uint32_t cx, uint32_t cy)
{
	if (cx == 0 || cy == 0)
		return;

	ViewerState::Snapshot snap = ViewerState::get().acquire();

	Frame f;
	f.fw = static_cast<float>(snap.baseWidth);
	f.fh = static_cast<float>(snap.baseHeight);
	const float aspect = f.fw / f.fh;

	// Fit the area the tiles cover (not the whole canvas) into the window.
	const NRect b = layout.bounds();
	const float areaW = f.fw * b.w, areaH = f.fh * b.h;
	const float windowAspect = static_cast<float>(cx) / static_cast<float>(cy);
	if (windowAspect > areaW / areaH) {
		f.scale = static_cast<float>(cy) / areaH;
		f.ox = (static_cast<float>(cx) - areaW * f.scale) / 2;
	} else {
		f.scale = static_cast<float>(cx) / areaW;
		f.oy = (static_cast<float>(cy) - areaH * f.scale) / 2;
	}

	// Line weights in window pixels, converted to canvas units.
	const float px = 1.0f / f.scale;
	const float hair = 1.0f * px;  // idle border
	const float tally = 2.0f * px; // program / preview border
	const float gutter = 3.0f * px;
	const float pad = std::max(4.0f * px, f.fh * 0.008f);

	startRegion(static_cast<int>(std::lround(f.ox)), static_cast<int>(std::lround(f.oy)),
		    static_cast<int>(std::lround(areaW * f.scale)), static_cast<int>(std::lround(areaH * f.scale)), 0.0f,
		    areaW, 0.0f, areaH);

	drawBox(0, 0, areaW, areaH, theme::kBg);

	for (const Tile &tile : layout.tiles) {
		if (tile.kind == TileKind::Status)
			continue; // status now lives in the side bar

		const Rect cell{tile.rect.x * f.fw, tile.rect.y * f.fh, tile.rect.w * f.fw, tile.rect.h * f.fh};
		const Rect r = inset(cell, gutter / 2);

		obs_source_t *source = nullptr;
		obs_source_t *label = nullptr;
		uint32_t edge = theme::kBorder;
		bool active = false;
		bool isProgram = false;
		bool previewFallback = false;

		switch (tile.kind) {
		case TileKind::Program:
			source = snap.program;
			label = snap.programLabel;
			edge = theme::kBad;
			active = true;
			isProgram = true;
			break;
		case TileKind::Preview:
			source = snap.preview;
			label = snap.previewLabel;
			edge = snap.studioMode ? theme::kGood : theme::kBorder;
			active = snap.studioMode;
			previewFallback = !snap.studioMode;
			break;
		case TileKind::Scene:
			if (tile.sceneIndex >= 0 && static_cast<size_t>(tile.sceneIndex) < snap.scenes.size()) {
				source = snap.scenes[tile.sceneIndex];
				label = snap.sceneLabels[tile.sceneIndex];
				if (source && source == snap.program) {
					edge = theme::kBad;
					active = true;
				} else if (source && snap.studioMode && source == snap.preview) {
					edge = theme::kGood;
					active = true;
				}
			}
			break;
		case TileKind::Status:
			break;
		}

		const float t = active ? tally : hair;
		const Rect inner = inset(r, t);
		drawBox(inner.x, inner.y, inner.w, inner.h, (source || isProgram) ? theme::kBlack : theme::kPanel);
		const Rect picture = fitAspect(inner, aspect);

		if (isProgram)
			renderScaled(f, picture, [] { obs_render_main_texture(); });
		else if (source)
			renderScaled(f, picture, [source] { obs_source_video_render(source); });

		drawFrame(r, t, edge);

		if (snap.showLabels) {
			// Label height tracks the tile so small tiles get small labels.
			const float maxLabel = std::max(10.0f * px, inner.h * 0.11f);
			drawTileLabel(label, inner, pad, maxLabel);
			if (previewFallback)
				drawTileNote(snap.studioOffLabel, inner, pad, maxLabel * 0.8f);
		}
	}

	endRegion();

	ViewerState::release(snap);
}

} // namespace obsviewer
