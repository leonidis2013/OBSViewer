#include "viewer/Layouts.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace obsviewer {

namespace {

using Item = std::pair<TileKind, int>;

struct Builder {
	float aspect;
	float label;
	float gap;
	float pairGap; // between Preview and Program
	std::vector<PlacedTile> tiles;

	float tileHeight(float w) const { return w / aspect + label; }

	void add(TileKind kind, int scene, float x, float y, float w)
	{
		PlacedTile t;
		t.kind = kind;
		t.sceneIndex = scene;
		const float ph = w / aspect;
		t.cell = Box{x, y, w, ph + label};
		t.picture = Box{x, y, w, ph};
		t.label = Box{x, y + ph, w, label};
		tiles.push_back(t);
	}

	// Equal tiles side by side, `width` wide in total. Returns the row height.
	float row(float x, float y, float width, const std::vector<Item> &items)
	{
		const int k = static_cast<int>(items.size());
		if (k == 0)
			return 0.0f;
		const float w = (width - gap * static_cast<float>(k - 1)) / static_cast<float>(k);
		for (int i = 0; i < k; ++i)
			add(items[static_cast<size_t>(i)].first, items[static_cast<size_t>(i)].second,
			    x + static_cast<float>(i) * (w + gap), y, w);
		return tileHeight(w);
	}

	// Preview and Program side by side, pairGap apart. Returns the row height.
	float pair(float x, float y, float width)
	{
		const float w = (width - pairGap) / 2.0f;
		add(TileKind::Preview, -1, x, y, w);
		add(TileKind::Program, -1, x + w + pairGap, y, w);
		return tileHeight(w);
	}
};

float pairGapOf(const PlaceParams &p)
{
	return std::max(p.gap, p.centerStrip);
}

std::vector<Item> scenes(int first, int count)
{
	std::vector<Item> v;
	for (int i = 0; i < count; ++i)
		v.emplace_back(TileKind::Scene, first + i);
	return v;
}

// Fixed presets, built for a total width `s` from (0, 0). Returns the height used.
float buildPreset(const std::string &id, float s, Builder &b)
{
	if (id == "atem") {
		float y = b.pair(0, 0, s) + b.gap;
		return y + b.row(0, y, s, scenes(0, 4));
	}
	if (id == "classic8") {
		float y = b.pair(0, 0, s) + b.gap;
		y += b.row(0, y, s, scenes(0, 4)) + b.gap;
		return y + b.row(0, y, s, scenes(4, 4));
	}
	if (id == "bigprogram") {
		// Program large on the left; Preview and two scenes stacked on the right.
		const float colW = (s - b.gap) / 4.0f;
		const float bigW = s - b.gap - colW;
		const float colTile = b.tileHeight(colW);
		const float colH = 3.0f * colTile + 2.0f * b.gap;
		const float bigH = b.tileHeight(bigW);
		const float top = std::max(colH, bigH);
		b.add(TileKind::Program, -1, 0, (top - bigH) / 2.0f, bigW);
		const float cx = bigW + b.gap;
		float cy = (top - colH) / 2.0f;
		b.add(TileKind::Preview, -1, cx, cy, colW);
		cy += colTile + b.gap;
		b.add(TileKind::Scene, 0, cx, cy, colW);
		cy += colTile + b.gap;
		b.add(TileKind::Scene, 1, cx, cy, colW);
		const float y = top + b.gap;
		return y + b.row(0, y, s, scenes(2, 4));
	}
	if (id == "grid3" || id == "grid4") {
		const int n = id == "grid3" ? 3 : 4;
		float y = 0;
		for (int r = 0; r < n; ++r) {
			y += b.row(0, y, s, scenes(r * n, n));
			if (r + 1 < n)
				y += b.gap;
		}
		return y;
	}
	// "two": just Preview and Program (auto with no scenes)
	return b.pair(0, 0, s);
}

void shift(std::vector<PlacedTile> &tiles, float dx, float dy)
{
	for (auto &t : tiles)
		for (Box *box : {&t.cell, &t.picture, &t.label}) {
			box->x += dx;
			box->y += dy;
		}
}

// Largest width whose layout still fits the view, then centred.
std::vector<PlacedTile> fitPreset(const std::string &id, const PlaceParams &p)
{
	auto build = [&](float s, Builder &b) { return buildPreset(id, s, b); };
	Builder probe{p.aspect, p.labelHeight, p.gap, pairGapOf(p), {}};
	float s = p.width;
	if (build(s, probe) > p.height) {
		float lo = 0.0f, hi = p.width;
		for (int i = 0; i < 32; ++i) {
			const float mid = (lo + hi) / 2.0f;
			Builder b{p.aspect, p.labelHeight, p.gap, pairGapOf(p), {}};
			(build(mid, b) <= p.height ? lo : hi) = mid;
		}
		s = lo;
	}
	Builder b{p.aspect, p.labelHeight, p.gap, pairGapOf(p), {}};
	const float h = build(s, b);
	shift(b.tiles, (p.width - s) / 2.0f, std::max(0.0f, (p.height - h) / 2.0f));
	return std::move(b.tiles);
}

// "Auto": Preview and Program large on top, every scene in a grid below. Tries a
// range of sizes for the top pair and column counts for the scenes, and keeps the
// arrangement that shows the most picture (Preview / Program count extra).
std::vector<PlacedTile> placeAuto(const PlaceParams &p)
{
	const float pg = pairGapOf(p);
	const int n = std::max(0, p.sceneCount);
	if (n == 0)
		return fitPreset("two", p);

	const float W = p.width, H = p.height, a = p.aspect, L = p.labelHeight, g = p.gap;
	float bestScore = -1.0f, bestTop = 0.0f, bestScene = 0.0f;
	int bestCols = 1;
	constexpr int kSteps = 80;
	for (int step = 0; step <= kSteps; ++step) {
		const float rowW = W * (1.0f - 0.75f * static_cast<float>(step) / kSteps); // 100% … 25% of the width
		const float topW = (rowW - pg) / 2.0f;
		if (topW <= 0.0f)
			continue;
		const float topH = topW / a + L;
		const float rest = H - topH - g;
		if (rest <= L + 4.0f)
			continue;
		for (int cols = 1; cols <= n; ++cols) {
			const int rows = (n + cols - 1) / cols;
			const float byWidth = (W - g * static_cast<float>(cols - 1)) / static_cast<float>(cols);
			const float byHeight = ((rest - g * static_cast<float>(rows - 1)) / static_cast<float>(rows) - L) * a;
			float sw = std::min({byWidth, byHeight, topW * 0.75f});
			if (sw <= 0.0f)
				continue;
			const float score = 2.0f * 1.5f * topW * topW + static_cast<float>(n) * sw * sw;
			if (score > bestScore) {
				bestScore = score;
				bestTop = topW;
				bestScene = sw;
				bestCols = cols;
			}
		}
	}
	if (bestScore < 0.0f)
		return fitPreset("two", p); // too small for scenes: just the pair

	Builder b{a, L, g, pg, {}};
	const float topRow = 2.0f * bestTop + pg;
	b.pair((W - topRow) / 2.0f, 0, topRow);
	float y = b.tileHeight(bestTop) + g;
	const int rows = (n + bestCols - 1) / bestCols;
	for (int r = 0; r < rows; ++r) {
		const int inRow = std::min(bestCols, n - r * bestCols);
		const float rowW = static_cast<float>(inRow) * bestScene + g * static_cast<float>(inRow - 1);
		const float x0 = (W - rowW) / 2.0f;
		for (int c = 0; c < inRow; ++c)
			b.add(TileKind::Scene, r * bestCols + c, x0 + static_cast<float>(c) * (bestScene + g), y, bestScene);
		y += b.tileHeight(bestScene) + (r + 1 < rows ? g : 0.0f);
	}
	shift(b.tiles, 0.0f, std::max(0.0f, (H - y) / 2.0f));
	return std::move(b.tiles);
}

} // namespace

const std::vector<LayoutInfo> &allLayouts()
{
	static const std::vector<LayoutInfo> layouts = {
		{"auto", "Dynamic (Preview + Program, all scenes)",
		 "Preview and Program large on top; every scene below, sized to fill the window.", -1},
		{"atem", "ATEM (2 + 4)", "Preview and Program on top, four scenes below.", 4},
		{"classic8", "Classic (2 + 8)", "Preview and Program on top, eight scenes below.", 8},
		{"bigprogram", "Big Program (1 + 1 + 6)", "Large Program, Preview and six scenes around it.", 6},
		{"grid3", "Scenes 3 × 3", "Nine scenes, no Preview or Program.", 9},
		{"grid4", "Scenes 4 × 4", "Sixteen scenes, no Preview or Program.", 16},
	};
	return layouts;
}

const LayoutInfo &layoutById(const std::string &id)
{
	for (const auto &l : allLayouts())
		if (l.id == id)
			return l;
	for (const auto &l : allLayouts())
		if (l.id == kDefaultLayoutId)
			return l;
	return allLayouts().front();
}

std::vector<PlacedTile> placeTiles(const std::string &layoutId, const PlaceParams &params)
{
	if (params.width <= 1.0f || params.height <= 1.0f || params.aspect <= 0.0f)
		return {};
	const std::string &id = layoutById(layoutId).id;
	return id == kAutoLayoutId ? placeAuto(params) : fitPreset(id, params);
}

int tileAt(const std::vector<PlacedTile> &tiles, float x, float y)
{
	for (size_t i = 0; i < tiles.size(); ++i)
		if (tiles[i].cell.contains(x, y))
			return static_cast<int>(i);
	return -1;
}

float labelBandHeight(float viewHeight)
{
	return std::clamp(std::round(viewHeight * 0.028f), 14.0f, 26.0f);
}

float transitionStripWidth(const std::string &layoutId, float viewWidth)
{
	const std::string &id = layoutById(layoutId).id;
	if (id != "auto" && id != "atem" && id != "classic8")
		return 0.0f;
	return std::clamp(std::round(viewWidth * 0.045f), 52.0f, 84.0f);
}

TransitionStrip transitionStrip(const std::vector<PlacedTile> &tiles, float gap)
{
	TransitionStrip st;
	const PlacedTile *pv = nullptr, *pg = nullptr;
	for (const auto &t : tiles) {
		if (t.kind == TileKind::Preview)
			pv = &t;
		else if (t.kind == TileKind::Program)
			pg = &t;
	}
	if (!pv || !pg || std::fabs(pv->cell.y - pg->cell.y) > 0.5f)
		return st;
	const float x0 = pv->cell.x + pv->cell.w + gap;
	const float x1 = pg->cell.x - gap;
	const float w = x1 - x0;
	const float h = pv->cell.h;
	if (w < 28.0f || h < 60.0f)
		return st;

	st.valid = true;
	st.area = Box{x0, pv->cell.y, w, h};
	const float bh = std::clamp(std::round(w * 0.62f), 24.0f, 44.0f);
	const float sp = std::max(4.0f, std::round(bh * 0.2f));
	const float lineH = std::clamp(std::round(bh * 0.5f), 12.0f, 18.0f);
	float y = st.area.y;
	st.cut = Box{x0, y, w, bh};
	y += bh + sp;
	st.autoTake = Box{x0, y, w, bh};
	y += bh + sp;
	// Name and time sit at the bottom, level with the label bands.
	st.timeLine = Box{x0, st.area.y + h - lineH, w, lineH};
	st.nameLine = Box{x0, st.timeLine.y - lineH, w, lineH};
	const float barBottom = st.nameLine.y - sp;
	const float barW = std::clamp(std::round(w * 0.14f), 6.0f, 10.0f);
	st.progress = Box{x0 + (w - barW) / 2.0f, y, barW, std::max(0.0f, barBottom - y)};
	if (st.progress.h < 16.0f)
		st.progress.h = 0.0f;
	if (st.nameLine.y < y) // too short for the text lines: buttons only
		st.nameLine.h = st.timeLine.h = 0.0f;
	return st;
}

} // namespace obsviewer
