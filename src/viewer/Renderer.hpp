#pragma once

#include <cstdint>
#include <string>

namespace obsviewer {

// Draws a multiview layout into the current obs_display. Graphics thread only.
class Renderer {
public:
	// cx × cy is the display size in device pixels; dpr = device pixels per
	// logical pixel. Tiles are placed in logical pixels (see placeTiles), the
	// same coordinates ViewerDisplay uses for clicks.
	static void render(const std::string &layoutId, uint32_t cx, uint32_t cy, float dpr);
};

} // namespace obsviewer
