#pragma once

#include <string>
#include <vector>

// Multiview layouts. Tiles are placed in window pixels for the actual window
// shape, so the views always use the space they have, with each view's label in
// a band just below its picture.
namespace obsviewer {

enum class TileKind { Program, Preview, Scene };

struct Box {
	float x = 0, y = 0, w = 0, h = 0;
	bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct PlacedTile {
	TileKind kind = TileKind::Scene;
	int sceneIndex = -1; // for scenes: index into the OBS scene list
	Box cell;            // picture + label band (what a click hits)
	Box picture;         // the video, at the canvas aspect ratio
	Box label;           // band below the picture (height 0 when labels are off)
};

struct LayoutInfo {
	std::string id;
	std::string name;
	std::string description;
	int fixedScenes = -1; // scene tiles the preset has; -1 = every scene
};

struct PlaceParams {
	float width = 0;        // view size (any unit, usually logical pixels)
	float height = 0;
	float aspect = 16.0f / 9.0f; // canvas width / height
	float labelHeight = 0;  // band under each picture; 0 = no labels
	float gap = 4;          // space between tiles
	int sceneCount = 0;     // scenes in the current collection
};

const std::vector<LayoutInfo> &allLayouts();

// The layout with this id, or the default ("auto") layout.
const LayoutInfo &layoutById(const std::string &id);

// Where every tile goes for a view of the given size. Scene tiles past the end of
// the scene list are still placed (they show as empty), except in "auto", which
// shows exactly the scenes there are.
std::vector<PlacedTile> placeTiles(const std::string &layoutId, const PlaceParams &params);

// Index of the tile under (x, y), or -1.
int tileAt(const std::vector<PlacedTile> &tiles, float x, float y);

// Label band height for a view this tall (same units as PlaceParams).
float labelBandHeight(float viewHeight);

constexpr const char *kAutoLayoutId = "auto";
constexpr const char *kDefaultLayoutId = "auto";

} // namespace obsviewer
