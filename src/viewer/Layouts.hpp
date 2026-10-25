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
	// Space between Preview and Program when they sit side by side, for the
	// transition strip (0 = the normal gap).
	float centerStrip = 0;
};

// Transition strip between Preview and Program: CUT and AUTO on top, a
// progress bar, and the transition's name and length at the bottom.
struct TransitionStrip {
	bool valid = false;
	Box area;       // the whole column
	Box cut;
	Box autoTake;
	Box progress;   // h may be 0 when there is no room
	Box nameLine;   // transition name
	Box timeLine;   // duration
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

// Width to reserve (PlaceParams::centerStrip) for the transition strip in a
// view this wide, or 0 for layouts without Preview and Program side by side.
float transitionStripWidth(const std::string &layoutId, float viewWidth);

// Where the strip and its buttons go, from placed tiles. valid = false when the
// layout has no Preview / Program pair side by side, or the space is too small.
TransitionStrip transitionStrip(const std::vector<PlacedTile> &tiles, float gap);

constexpr const char *kAutoLayoutId = "auto";
constexpr const char *kDefaultLayoutId = "auto";

} // namespace obsviewer
