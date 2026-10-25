#include "viewer/Renderer.hpp"

#include "audio/ProgramLoudness.hpp"
#include "audio/SourceLevels.hpp"
#include "core/LevelMath.hpp"

#include "viewer/Layouts.hpp"
#include "viewer/ViewerState.hpp"
#include "ui/Theme.hpp"

#include <obs.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace obsviewer {

namespace {

struct Rect {
	float x, y, w, h;
};

// Maps layout coordinates to device pixels.
struct Frame {
	float fw = 1920, fh = 1080; // canvas size (OBS base resolution)
	float ox = 0, oy = 0;       // device-pixel offset
	float scale = 1;            // layout units → device pixels
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

// Top-left note inside the picture ("STUDIO MODE OFF").
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

// Labels are text_ft2 sources: every label made with the same font has its
// baseline at the same distance from the top (the font's tallest glyph), and only
// gets taller when the text has descenders. So every label is scaled from one
// reference height (the all-caps "PROGRAM" title) and top-aligned: same size and
// same baseline everywhere, whatever the text.
struct LabelMetrics {
	float refH = 0; // height of an all-caps label
	float scale = 1;
	float top = 0; // offset from the band top
};

LabelMetrics labelMetrics(obs_source_t *reference, const Rect &band)
{
	LabelMetrics m;
	m.refH = reference ? static_cast<float>(obs_source_get_height(reference)) : 0.0f;
	if (m.refH <= 0.0f)
		m.refH = 40.0f;
	m.scale = band.h * 0.5f / m.refH;
	m.top = (band.h - m.refH * m.scale) / 2.0f;
	return m;
}

// Draws `label` with its left edge at x (or centred in [x, x + w] when centre is
// set), shrunk only if it doesn't fit in w.
void drawLabelText(obs_source_t *label, const LabelMetrics &m, const Rect &band, float x, float w, bool centre)
{
	if (!label || w <= 4.0f)
		return;
	const float lw = static_cast<float>(obs_source_get_width(label));
	if (lw <= 0.0f)
		return;
	float s = m.scale;
	if (lw * s > w)
		s = w / lw;
	const float y = band.y + m.top + (m.refH * (m.scale - s)) / 2.0f;
	const float lx = centre ? x + (w - lw * s) / 2.0f : x;
	drawLabelAt(label, lx, y, s, 0, 0);
}

// Scene band: tinted when the scene is on Program / in Preview, name centred.
void drawSceneBand(obs_source_t *label, obs_source_t *reference, const Rect &band, uint32_t fill)
{
	if (band.h <= 0.0f)
		return;
	drawBox(band.x, band.y, band.w, band.h, fill);
	const LabelMetrics m = labelMetrics(reference, band);
	drawLabelText(label, m, band, band.x + 8.0f, band.w - 16.0f, true);
}

// Program / Preview band: accent tab, a fixed title ("PROGRAM" / "PREVIEW") that
// never moves, and the scene name in its own field to the right of it.
void drawTitleBand(obs_source_t *title, obs_source_t *name, obs_source_t *reference, float titleW,
		   const Rect &band, uint32_t tab)
{
	if (band.h <= 0.0f)
		return;
	drawBox(band.x, band.y, band.w, band.h, theme::kPanel);
	const float tabH = std::min(band.h * 0.5f, 12.0f);
	drawBox(band.x + 6.0f, band.y + (band.h - tabH) / 2.0f, 3.0f, tabH, tab);
	const LabelMetrics m = labelMetrics(reference, band);
	const float titleX = band.x + 16.0f;
	const float titleWidth = titleW * m.scale;
	drawLabelText(title, m, band, titleX, titleWidth, false);

	const float fx = titleX + titleWidth + 10.0f;
	const float fw = band.x + band.w - 4.0f - fx;
	if (fw <= 20.0f)
		return;
	drawBox(fx, band.y + 3.0f, fw, band.h - 6.0f, theme::kBg);
	drawLabelText(name, m, band, fx + 8.0f, fw - 16.0f, false);
}

// Rounded rectangle (radius r) built from horizontal slices: the corners step
// one device pixel at a time, which reads as round at these sizes.
void drawRoundBox(float x, float y, float w, float h, float r, float px, uint32_t argb)
{
	r = std::min({r, w / 2.0f, h / 2.0f});
	if (r < px * 1.5f) {
		drawBox(x, y, w, h, argb);
		return;
	}
	drawBox(x, y + r, w, h - 2.0f * r, argb);
	for (float dy = 0.0f; dy < r; dy += px) {
		const float yc = r - dy - px / 2.0f; // distance from the corner centre
		const float inset = r - std::sqrt(std::max(0.0f, r * r - yc * yc));
		drawBox(x + inset, y + dy, w - 2.0f * inset, px, argb);
		drawBox(x + inset, y + h - dy - px, w - 2.0f * inset, px, argb);
	}
}

// LiveVoice-style button: raised fill, 1-px border, 5-px corners, label centred.
void drawButton(const Box &b, obs_source_t *label, obs_source_t *reference, float px, uint32_t fill,
		uint32_t border, bool dim)
{
	drawRoundBox(b.x, b.y, b.w, b.h, 5.0f, px, border);
	drawRoundBox(b.x + px, b.y + px, b.w - 2.0f * px, b.h - 2.0f * px, 5.0f - px, px, fill);
	const Rect band{b.x, b.y, b.w, b.h};
	LabelMetrics m = labelMetrics(reference, Rect{b.x, b.y, b.w, std::min(b.h, 30.0f)});
	m.top = (b.h - m.refH * m.scale) / 2.0f;
	drawLabelText(label, m, band, b.x + 4.0f, b.w - 8.0f, true);
	if (dim)
		drawRoundBox(b.x, b.y, b.w, b.h, 5.0f, px, 0xB00D1015u);
}

// The transition column between Preview and Program: CUT, AUTO (lit red while a
// transition runs), a progress bar, and the transition's name and length.
void drawTransitionStrip(const TransitionStrip &st, const ViewerState::Snapshot &snap, float px, int hover,
			 int pressed)
{
	const bool live = snap.studioMode;
	float t = snap.transition ? obs_transition_get_time(snap.transition) : 1.0f;
	const bool running = live && t > 0.0f && t < 1.0f;

	auto fillFor = [&](int id) -> uint32_t {
		if (!live)
			return theme::kPanelRaised;
		if (pressed == id)
			return theme::kBorder;
		return hover == id ? 0xFF222933u : theme::kPanelRaised;
	};
	auto borderFor = [&](int id) -> uint32_t {
		return live && (hover == id || pressed == id) ? theme::kTextFaint : theme::kBorder;
	};

	drawButton(st.cut, snap.cutLabel, snap.programLabel, px, fillFor(Renderer::kCut), borderFor(Renderer::kCut),
		   !live);
	if (running)
		drawButton(st.autoTake, snap.autoLabel, snap.programLabel, px, theme::kBad, theme::kBad, false);
	else
		drawButton(st.autoTake, snap.autoLabel, snap.programLabel, px, fillFor(Renderer::kAuto),
			   borderFor(Renderer::kAuto), !live);

	if (st.progress.h > 0.0f) {
		const Box &p = st.progress;
		drawRoundBox(p.x, p.y, p.w, p.h, p.w / 2.0f, px, theme::kPanel);
		if (running) {
			const float fh = std::max(p.w, p.h * t);
			drawRoundBox(p.x, p.y, p.w, fh, p.w / 2.0f, px, theme::kBad);
		}
	}

	if (st.nameLine.h > 0.0f) {
		for (const auto &[box, label, dimText] :
		     {std::tuple{st.nameLine, snap.transitionNameLabel, false},
		      std::tuple{st.timeLine, snap.transitionTimeLabel, true}}) {
			const Rect band{box.x, box.y, box.w, box.h};
			LabelMetrics m = labelMetrics(snap.programLabel, band);
			m.scale = box.h * 0.62f / m.refH;
			m.top = (box.h - m.refH * m.scale) / 2.0f;
			drawLabelText(label, m, band, box.x, box.w, true);
			if (dimText || !live)
				drawBox(box.x, box.y, box.w, box.h, 0x800D1015u);
		}
	}
}

uint32_t levelColor(float db)
{
	if (db >= -3.0f)
		return theme::kBad;
	if (db >= -9.0f)
		return theme::kWarn;
	return theme::kGood;
}

levels::TileLevels readTile(const AudioList &list, uint64_t now)
{
	std::vector<levels::SourceReading> readings;
	readings.reserve(list.size());
	for (const auto &e : list)
		if (e)
			readings.push_back(e->read(now));
	return levels::combine(readings);
}

// Two thin bars (L / R) at the right edge of the picture: RMS as a solid bar,
// peak as a tick, red cap when clipping, red tint and a MUTED tag when muted.
// Always drawn; greyed out (empty grey tracks) when the view has no audio.
void drawTileMeter(const levels::TileLevels &t, const Rect &pic, float px, float pad, obs_source_t *mutedLabel,
		   float maxLabel)
{
	const float barW = std::max(3.0f * px, pic.w * 0.011f);
	const float gap = 1.0f * px;
	const float h = pic.h - 2 * pad;
	const float x0 = pic.x + pic.w - pad - 2 * barW - gap;
	const float y0 = pic.y + pad;
	if (h <= 0 || x0 <= pic.x)
		return;

	drawBox(x0 - gap, y0 - gap, 2 * barW + 3 * gap, h + 2 * gap,
		t.hasAudio && t.allMuted ? 0xCC4A1416 : theme::kLabelPlate);
	// tracks: dark when the view has audio, grey when it has none
	for (int c = 0; c < 2; ++c)
		drawBox(x0 + c * (barW + gap), y0, barW, h, t.hasAudio ? theme::kGrid : 0xFF353C46);
	if (!t.hasAudio)
		return;

	for (int c = 0; c < 2; ++c) {
		const float x = x0 + c * (barW + gap);
		if (!t.allMuted) {
			const float rmsH = h * levels::meterPos(t.rms[c]);
			if (rmsH > 0)
				drawBox(x, y0 + h - rmsH, barW, rmsH, levelColor(t.rms[c]));
			if (t.peak[c] > -60.0f) {
				const float py = y0 + h - h * levels::meterPos(t.peak[c]);
				drawBox(x, py, barW, std::max(1.0f * px, 1.5f * px), levelColor(t.peak[c]));
			}
		}
	}
	if (t.clipped && !t.allMuted)
		drawBox(x0, y0, 2 * barW + gap, 3.0f * px, theme::kBad);

	if (t.allMuted && mutedLabel) {
		const float lw = static_cast<float>(obs_source_get_width(mutedLabel));
		const float lh = static_cast<float>(obs_source_get_height(mutedLabel));
		if (lw > 0 && lh > 0) {
			const float s = std::min({1.0f, (maxLabel * 0.8f) / lh, (pic.w * 0.3f) / lw});
			const float x = x0 - gap - pad * 0.5f - lw * s;
			drawBox(x - pad * 0.4f, y0, lw * s + pad * 0.8f, lh * s, 0xE0A32D2D);
			gs_matrix_push();
			gs_matrix_translate3f(x, y0, 0.0f);
			gs_matrix_scale3f(s, s, 1.0f);
			obs_source_video_render(mutedLabel);
			gs_matrix_pop();
		}
	}
}

} // namespace

void RenderCache::destroy()
{
	for (auto &e : scenes)
		if (e.tex)
			gs_texrender_destroy(e.tex);
	scenes.clear();
}

namespace {

// Scene views in the Dynamic layout redraw every other frame (half the views on
// even frames, half on odd), and show the cached picture in between. Preview and
// Program always redraw.
void renderSceneHalfRate(RenderCache &cache, int sceneIndex, obs_source_t *source, const Rect &pic, float dpr,
			 float fw, float fh)
{
	if (sceneIndex < 0)
		return;
	if (cache.scenes.size() <= static_cast<size_t>(sceneIndex))
		cache.scenes.resize(static_cast<size_t>(sceneIndex) + 1);
	RenderCache::Entry &e = cache.scenes[static_cast<size_t>(sceneIndex)];
	const uint32_t w = static_cast<uint32_t>(std::max(1L, std::lround(pic.w * dpr)));
	const uint32_t h = static_cast<uint32_t>(std::max(1L, std::lround(pic.h * dpr)));
	if (!e.tex)
		e.tex = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	if (!e.tex)
		return;

	const bool due = ((cache.frame + static_cast<uint64_t>(sceneIndex)) & 1u) == 0;
	if (!e.valid || e.cx != w || e.cy != h || due) {
		gs_texrender_reset(e.tex);
		if (gs_texrender_begin(e.tex, w, h)) {
			vec4 clear;
			vec4_zero(&clear);
			gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
			gs_ortho(0.0f, fw, 0.0f, fh, -100.0f, 100.0f);
			gs_blend_state_push();
			gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
			obs_source_video_render(source);
			gs_blend_state_pop();
			gs_texrender_end(e.tex);
			e.cx = w;
			e.cy = h;
			e.valid = true;
		}
	}

	gs_texture_t *tex = gs_texrender_get_texture(e.tex);
	if (!tex || !e.valid)
		return;
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
	gs_matrix_push();
	gs_matrix_translate3f(pic.x, pic.y, 0.0f);
	gs_matrix_scale3f(pic.w / static_cast<float>(e.cx), pic.h / static_cast<float>(e.cy), 1.0f);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(tex, 0, e.cx, e.cy);
	gs_matrix_pop();
}

} // namespace

void Renderer::render(const std::string &layoutId, uint32_t cx, uint32_t cy, float dpr, RenderCache &cache, int hover,
		      int pressed)
{
	if (cx == 0 || cy == 0)
		return;
	dpr = std::max(0.5f, dpr);

	ViewerState::Snapshot snap = ViewerState::get().acquire();

	// Everything below is laid out in logical (Qt) pixels; the viewport maps
	// them to the display's device pixels.
	const float W = static_cast<float>(cx) / dpr;
	const float H = static_cast<float>(cy) / dpr;
	const float aspect = static_cast<float>(snap.baseWidth) / static_cast<float>(std::max<uint32_t>(1, snap.baseHeight));

	PlaceParams params;
	params.width = W;
	params.height = H;
	params.aspect = aspect;
	params.labelHeight = snap.showLabels ? labelBandHeight(H) : 0.0f;
	params.gap = 4.0f;
	params.centerStrip = snap.showTransitions ? transitionStripWidth(layoutId, W) : 0.0f;
	// Which scene each view shows (Settings / right-click ▸ This View Shows).
	const std::string &resolvedId = layoutById(layoutId).id;
	const std::vector<int> *viewMap = nullptr;
	if (snap.viewMaps) {
		const auto it = snap.viewMaps->find(resolvedId);
		if (it != snap.viewMaps->end())
			viewMap = &it->second;
	}
	params.sceneCount = viewMap && resolvedId == kAutoLayoutId ? static_cast<int>(viewMap->size())
								      : static_cast<int>(snap.scenes.size());
	const std::vector<PlacedTile> tiles = placeTiles(layoutId, params);
	const bool halfRate = resolvedId == kAutoLayoutId;
	++cache.frame;

	Frame f;
	f.fw = static_cast<float>(snap.baseWidth);
	f.fh = static_cast<float>(snap.baseHeight);
	f.scale = dpr; // logical → device pixels

	const float px = 1.0f / dpr;   // one device pixel
	const float tally = 2.0f;      // scene on Program / in Preview
	const float pad = 4.0f;

	startRegion(0, 0, static_cast<int>(cx), static_cast<int>(cy), 0.0f, W, 0.0f, H);
	drawBox(0, 0, W, H, theme::kBg);
	const uint64_t now = os_gettime_ns();
	// Width of the wider title, so Program and Preview bands line up the same.
	float titleW = 0.0f;
	for (obs_source_t *t : {snap.programLabel, snap.previewLabel})
		if (t)
			titleW = std::max(titleW, static_cast<float>(obs_source_get_width(t)));

	for (const PlacedTile &tile : tiles) {
		const Rect pic{tile.picture.x, tile.picture.y, tile.picture.w, tile.picture.h};
		const Rect band{tile.label.x, tile.label.y, tile.label.w, tile.label.h};
		// tile.sceneIndex is the view's position; this is the scene it shows.
		const int sceneIdx = tile.kind != TileKind::Scene ? -1
				     : viewMap ? (static_cast<size_t>(tile.sceneIndex) < viewMap->size()
							  ? (*viewMap)[static_cast<size_t>(tile.sceneIndex)]
							  : -1)
					       : tile.sceneIndex;

		obs_source_t *source = nullptr;
		obs_source_t *label = nullptr;
		uint32_t edge = theme::kBorder;
		uint32_t bandFill = theme::kPanel;
		uint32_t tab = 0;
		bool active = false;
		bool isProgram = false;
		bool previewFallback = false;

		switch (tile.kind) {
		case TileKind::Program:
			source = snap.program;
			label = snap.programLabel;
			isProgram = true;
			tab = theme::kBad;
			break;
		case TileKind::Preview:
			source = snap.preview;
			label = snap.previewLabel;
			previewFallback = !snap.studioMode;
			tab = snap.studioMode ? theme::kGood : theme::kTextFaint;
			break;
		case TileKind::Scene:
			if (sceneIdx >= 0 && static_cast<size_t>(sceneIdx) < snap.scenes.size()) {
				source = snap.scenes[static_cast<size_t>(sceneIdx)];
				label = snap.sceneLabels[static_cast<size_t>(sceneIdx)];
				if (source && source == snap.program) {
					edge = theme::kBad;
					bandFill = theme::kTallyProgram;
					active = true;
				} else if (source && snap.studioMode && source == snap.preview) {
					edge = theme::kGood;
					bandFill = theme::kTallyPreview;
					active = true;
				}
			}
			break;
		}

		drawBox(pic.x, pic.y, pic.w, pic.h, (source || isProgram) ? theme::kBlack : theme::kPanel);
		if (isProgram)
			renderScaled(f, pic, [] { obs_render_main_texture(); });
		else if (source && halfRate && tile.kind == TileKind::Scene)
			renderSceneHalfRate(cache, sceneIdx, source, pic, dpr, f.fw, f.fh);
		else if (source)
			renderScaled(f, pic, [source] { obs_source_video_render(source); });

		if (active)
			drawFrame(Rect{pic.x, pic.y, pic.w, pic.h + band.h}, tally, edge);
		else
			drawFrame(Rect{pic.x, pic.y, pic.w, pic.h + band.h}, px, theme::kBorder);

		// Safe-area guides on Program and Preview: action safe 93 %, title safe 90 %,
		// and a small centre mark.
		if (snap.safeAreas && tile.kind != TileKind::Scene) {
			for (float frac : {0.93f, 0.90f}) {
				const float dx = pic.w * (1.0f - frac) / 2.0f, dy = pic.h * (1.0f - frac) / 2.0f;
				drawFrame(Rect{pic.x + dx, pic.y + dy, pic.w - 2 * dx, pic.h - 2 * dy}, px,
					  frac > 0.92f ? 0x80FFFFFFu : 0x80F2C94Cu);
			}
			const float cxp = pic.x + pic.w / 2.0f, cyp = pic.y + pic.h / 2.0f, arm = pic.h * 0.03f;
			drawBox(cxp - arm, cyp - px / 2.0f, 2.0f * arm, px, 0x80FFFFFFu);
			drawBox(cxp - px / 2.0f, cyp - arm, px, 2.0f * arm, 0x80FFFFFFu);
		}

		if (snap.showTileMeters) {
			levels::TileLevels tl;
			if (isProgram) {
				// Program tile shows the real stream mix (the track the loudness meter measures).
				const auto a = ProgramLoudness::get().snapshot();
				tl.hasAudio = a.active;
				tl.peak[0] = tl.rms[0] = a.peakL;
				tl.peak[1] = tl.rms[1] = a.peakR;
				tl.clipped = a.truePeak3s > 0.0f;
			} else if (tile.kind == TileKind::Preview) {
				tl = readTile(snap.previewAudio, now);
			} else if (tile.kind == TileKind::Scene && sceneIdx >= 0 &&
				   static_cast<size_t>(sceneIdx) < snap.sceneAudio.size()) {
				tl = readTile(snap.sceneAudio[static_cast<size_t>(sceneIdx)], now);
			}
			drawTileMeter(tl, pic, px, pad, snap.mutedLabel, std::max(10.0f, pic.h * 0.1f));
		}

		if (snap.showLabels) {
			if (tile.kind == TileKind::Scene)
				drawSceneBand(label, snap.programLabel, band, bandFill);
			else
				drawTitleBand(label, isProgram ? snap.programNameLabel : snap.previewNameLabel, snap.programLabel,
					      titleW, band, tab);
			if (previewFallback)
				drawTileNote(snap.studioOffLabel, pic, pad, std::max(10.0f, pic.h * 0.08f));
		}
	}

	if (snap.showTransitions) {
		const TransitionStrip st = transitionStrip(tiles, params.gap);
		if (st.valid)
			drawTransitionStrip(st, snap, px, hover, pressed);
	}

	endRegion();

	ViewerState::release(snap);
}

} // namespace obsviewer
