#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct gs_texture_render;

namespace obsviewer {

// Per-display render state: cached scene pictures for views that redraw at half
// rate. Graphics thread only; destroy() inside obs_enter_graphics().
struct RenderCache {
	struct Entry {
		gs_texture_render *tex = nullptr;
		uint32_t cx = 0, cy = 0;
		bool valid = false;
	};
	std::vector<Entry> scenes; // by scene index
	uint64_t frame = 0;
	void destroy();
};

// Draws a multiview layout into the current obs_display. Graphics thread only.
class Renderer {
public:
	// cx × cy is the display size in device pixels; dpr = device pixels per
	// logical pixel. Tiles are placed in logical pixels (see placeTiles), the
	// same coordinates ViewerDisplay uses for clicks.
	// hover / pressed: which transition button the mouse is over / holding down.
	enum StripButton { kNone = 0, kCut = 1, kAuto = 2 };
	static void render(const std::string &layoutId, uint32_t cx, uint32_t cy, float dpr, RenderCache &cache,
			   int hover = kNone, int pressed = kNone);
};

} // namespace obsviewer
