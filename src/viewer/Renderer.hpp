#pragma once

#include <cstdint>

namespace obsviewer {

struct Layout;
enum class TileKind;

struct TileHit {
	int tile = -1;       // index into the resolved layout, -1 = none
	TileKind kind{};
	int sceneIndex = -1; // for scene tiles
};

// Draws a multiview layout into the current obs_display. Graphics thread only.
class Renderer {
public:
	static void render(const Layout &layout, uint32_t cx, uint32_t cy);

	// Which tile is under window point (x, y) for a display of size w × h.
	// `layout` must already be resolved (see resolveLayout). Any thread.
	static TileHit hitTest(const Layout &layout, uint32_t baseWidth, uint32_t baseHeight, double w, double h,
			       double x, double y);
};

} // namespace obsviewer
