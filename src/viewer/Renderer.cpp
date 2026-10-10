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

// Maps canvas coordinates (0..fw, 0..fh) to window pixels.
struct Frame {
	float fw = 1920, fh = 1080; // canvas size
	float ox = 0, oy = 0;       // window offset of the letterboxed canvas
	float scale = 1;            // canvas → pixels
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

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, static_cast<uint32_t>(std::lround(w)), static_cast<uint32_t>(std::lround(h)));
	gs_matrix_pop();
}

// Outline of a rectangle, drawn inside its edge.
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
	const int vx = static_cast<int>(f.ox + r.x * f.scale);
	const int vy = static_cast<int>(f.oy + r.y * f.scale);
	const int vcx = static_cast<int>(r.w * f.scale);
	const int vcy = static_cast<int>(r.h * f.scale);
	if (vcx <= 0 || vcy <= 0)
		return;
	startRegion(vx, vy, vcx, vcy, 0.0f, f.fw, 0.0f, f.fh);
	draw();
	endRegion();
}

// Draws a text label at (x, y) scaled by s, on a translucent plate.
void drawLabelAt(obs_source_t *label, float x, float y, float s, uint32_t plate)
{
	const float lw = static_cast<float>(obs_source_get_width(label)) * s;
	const float lh = static_cast<float>(obs_source_get_height(label)) * s;
	if (lw <= 0.0f || lh <= 0.0f)
		return;
	if (plate)
		drawBox(x, y, lw, lh, plate);
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_matrix_scale3f(s, s, 1.0f);
	obs_source_video_render(label);
	gs_matrix_pop();
}

// Bottom-centre label inside tile rect r (ATEM style).
void drawTileLabel(obs_source_t *label, const Rect &r, float pad)
{
	if (!label)
		return;
	const float lw = static_cast<float>(obs_source_get_width(label));
	const float lh = static_cast<float>(obs_source_get_height(label));
	if (lw <= 0.0f || lh <= 0.0f)
		return;
	const float s = std::min({1.0f, (r.w - 2 * pad) / lw, (r.h * 0.3f) / lh});
	const float x = r.x + (r.w - lw * s) / 2;
	const float y = r.y + r.h - lh * s - pad;
	drawLabelAt(label, x, y, s, theme::kLabelPlate);
}

// Top-left note inside tile rect r.
void drawTileNote(obs_source_t *label, const Rect &r, float pad)
{
	if (!label)
		return;
	const float lw = static_cast<float>(obs_source_get_width(label));
	const float lh = static_cast<float>(obs_source_get_height(label));
	if (lw <= 0.0f || lh <= 0.0f)
		return;
	const float s = std::min(1.0f, (r.w - 2 * pad) / lw);
	drawLabelAt(label, r.x + pad, r.y + pad, s, theme::kLabelPlate);
}

// A status "lamp": coloured box with a centred label. Returns its width.
float drawLamp(obs_source_t *label, float x, float y, float h, uint32_t fill, uint32_t edge, float t)
{
	if (!label)
		return 0.0f;
	const float lw = static_cast<float>(obs_source_get_width(label));
	const float lh = static_cast<float>(obs_source_get_height(label));
	if (lw <= 0.0f || lh <= 0.0f)
		return 0.0f;
	const float s = std::min(1.0f, (h * 0.7f) / lh);
	const float padX = h * 0.35f;
	const float w = lw * s + 2 * padX;
	drawBox(x, y, w, h, fill);
	drawFrame(Rect{x, y, w, h}, t, edge);
	drawLabelAt(label, x + padX, y + (h - lh * s) / 2, s, 0);
	return w;
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

	// Letterbox the whole multiview to the canvas aspect ratio.
	const float windowAspect = static_cast<float>(cx) / static_cast<float>(cy);
	if (windowAspect > aspect) {
		f.scale = static_cast<float>(cy) / f.fh;
		f.ox = (static_cast<float>(cx) - f.fw * f.scale) / 2;
	} else {
		f.scale = static_cast<float>(cx) / f.fw;
		f.oy = (static_cast<float>(cy) - f.fh * f.scale) / 2;
	}

	const float thin = std::max(2.0f, f.fh * 0.003f);
	const float thick = thin * 2.5f;
	const float pad = f.fh * 0.012f;

	startRegion(static_cast<int>(f.ox), static_cast<int>(f.oy), static_cast<int>(f.fw * f.scale),
		    static_cast<int>(f.fh * f.scale), 0.0f, f.fw, 0.0f, f.fh);

	drawBox(0, 0, f.fw, f.fh, theme::kBg);

	for (const Tile &tile : layout.tiles) {
		const Rect cell{tile.rect.x * f.fw, tile.rect.y * f.fh, tile.rect.w * f.fw, tile.rect.h * f.fh};
		const Rect r = inset(cell, thin * 0.5f); // small gutter between tiles

		if (tile.kind == TileKind::Status) {
			drawBox(r.x, r.y, r.w, r.h, theme::kPanel);
			drawFrame(r, thin, theme::kBorder);

			const float lampH = r.h * 0.42f;
			const float y = r.y + (r.h - lampH) / 2;
			float x = r.x + pad * 2;

			const uint32_t onAirFill = snap.streaming ? theme::kBad : theme::kPanelRaised;
			x += drawLamp(snap.onAirLabel, x, y, lampH, onAirFill, snap.streaming ? theme::kBad : theme::kBorder,
				      thin) +
			     pad * 1.5f;

			uint32_t recFill = theme::kPanelRaised, recEdge = theme::kBorder;
			if (snap.recording) {
				recFill = snap.recordingPaused ? theme::kWarn : theme::kBad;
				recEdge = recFill;
			}
			x += drawLamp(snap.recLabel, x, y, lampH, recFill, recEdge, thin) + pad * 3;

			if (snap.programNameLabel) {
				const float lh = static_cast<float>(obs_source_get_height(snap.programNameLabel));
				const float lw = static_cast<float>(obs_source_get_width(snap.programNameLabel));
				if (lh > 0 && lw > 0) {
					const float s = std::min({1.0f, (lampH * 0.8f) / lh, (r.x + r.w - pad - x) / lw});
					if (s > 0)
						drawLabelAt(snap.programNameLabel, x, r.y + (r.h - lh * s) / 2, s, 0);
				}
			}
			continue;
		}

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
			edge = theme::kGood;
			active = true;
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

		const float t = active ? thick : thin;
		drawBox(r.x, r.y, r.w, r.h, theme::kBlack);
		const Rect inner = inset(r, t);
		const Rect picture = fitAspect(inner, aspect);

		if (isProgram) {
			renderScaled(f, picture, [] { obs_render_main_texture(); });
		} else if (source) {
			renderScaled(f, picture, [source] { obs_source_video_render(source); });
		} else {
			drawBox(inner.x, inner.y, inner.w, inner.h, theme::kPanel);
		}

		drawFrame(r, t, edge);

		if (snap.showLabels) {
			drawTileLabel(label, inner, pad);
			if (previewFallback)
				drawTileNote(snap.studioOffLabel, inner, pad);
		}
	}

	endRegion();

	ViewerState::release(snap);
}

} // namespace obsviewer
