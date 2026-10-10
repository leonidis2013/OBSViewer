#pragma once

#include <cstdint>

namespace obsviewer {

struct Layout;

// Draws a multiview layout into the current obs_display. Graphics thread only.
class Renderer {
public:
	static void render(const Layout &layout, uint32_t cx, uint32_t cy);
};

} // namespace obsviewer
