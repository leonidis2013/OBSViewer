#pragma once

#include <string>
#include <vector>

namespace obsviewer {

// Rectangle in normalised canvas units: (0,0) is top-left, (1,1) bottom-right.
// On the (usually 16:9) canvas, a rect with w == h has the canvas aspect ratio.
struct NRect {
	float x = 0, y = 0, w = 0, h = 0;
};

enum class TileKind { Program, Preview, Scene, Status };

struct Tile {
	TileKind kind = TileKind::Scene;
	int sceneIndex = -1; // for TileKind::Scene: index into the OBS scene list
	NRect rect;
};

struct Layout {
	std::string id;
	std::string name;
	std::vector<Tile> tiles;

	int sceneTileCount() const;
	// Area the tiles actually cover (from 0,0), so short layouts don't waste space.
	NRect bounds() const;
};

const std::vector<Layout> &allLayouts();

// Returns the layout with this id, or the default (ATEM) layout.
const Layout &layoutById(const std::string &id);

constexpr const char *kDefaultLayoutId = "atem";

} // namespace obsviewer
