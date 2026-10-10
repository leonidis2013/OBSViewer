#include "viewer/Layouts.hpp"

#include <algorithm>

namespace obsviewer {

namespace {

Tile program(float x, float y, float s)
{
	return Tile{TileKind::Program, -1, {x, y, s, s}};
}

Tile preview(float x, float y, float s)
{
	return Tile{TileKind::Preview, -1, {x, y, s, s}};
}

Tile scene(int index, float x, float y, float s)
{
	return Tile{TileKind::Scene, index, {x, y, s, s}};
}

Layout makeAtem()
{
	Layout l{"atem", "ATEM (2 + 4)", {}};
	l.tiles.push_back(preview(0.0f, 0.0f, 0.5f));
	l.tiles.push_back(program(0.5f, 0.0f, 0.5f));
	for (int i = 0; i < 4; ++i)
		l.tiles.push_back(scene(i, 0.25f * i, 0.5f, 0.25f));
	return l;
}

Layout makeClassic()
{
	Layout l{"classic8", "Classic (2 + 8)", {}};
	l.tiles.push_back(preview(0.0f, 0.0f, 0.5f));
	l.tiles.push_back(program(0.5f, 0.0f, 0.5f));
	for (int i = 0; i < 8; ++i)
		l.tiles.push_back(scene(i, 0.25f * (i % 4), 0.5f + 0.25f * (i / 4), 0.25f));
	return l;
}

Layout makeBigProgram()
{
	Layout l{"bigprogram", "Big Program (1 + 1 + 6)", {}};
	l.tiles.push_back(program(0.0f, 0.0f, 0.75f));
	l.tiles.push_back(preview(0.75f, 0.0f, 0.25f));
	l.tiles.push_back(scene(0, 0.75f, 0.25f, 0.25f));
	l.tiles.push_back(scene(1, 0.75f, 0.5f, 0.25f));
	for (int i = 0; i < 4; ++i)
		l.tiles.push_back(scene(2 + i, 0.25f * i, 0.75f, 0.25f));
	return l;
}

Layout makeGrid(const char *id, const char *name, int n)
{
	Layout l{id, name, {}};
	const float s = 1.0f / static_cast<float>(n);
	for (int i = 0; i < n * n; ++i)
		l.tiles.push_back(scene(i, s * (i % n), s * (i / n), s));
	return l;
}

} // namespace

NRect Layout::bounds() const
{
	NRect b{0, 0, 0, 0};
	for (const auto &t : tiles) {
		b.w = std::max(b.w, t.rect.x + t.rect.w);
		b.h = std::max(b.h, t.rect.y + t.rect.h);
	}
	if (b.w <= 0 || b.h <= 0)
		b = NRect{0, 0, 1, 1};
	return b;
}

int Layout::sceneTileCount() const
{
	int count = 0;
	for (const auto &t : tiles)
		if (t.kind == TileKind::Scene)
			++count;
	return count;
}

const std::vector<Layout> &allLayouts()
{
	static const std::vector<Layout> layouts = {
		makeAtem(),
		makeClassic(),
		makeBigProgram(),
		makeGrid("grid3", "Scenes 3 × 3", 3),
		makeGrid("grid4", "Scenes 4 × 4", 4),
	};
	return layouts;
}

const Layout &layoutById(const std::string &id)
{
	for (const auto &l : allLayouts())
		if (l.id == id)
			return l;
	return allLayouts().front();
}

} // namespace obsviewer
