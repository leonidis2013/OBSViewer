#pragma once

#include <cstdint>
#include <string>

namespace obsviewer {

// Plugin settings, stored as JSON in OBS's plugin config folder.
// Read and written on the Qt main thread only.
struct Settings {
	std::string dockLayout = "auto";
	std::string windowLayout = "auto"; // the viewer window (windowed or fullscreen)
	std::string windowScreen;          // QScreen::name() for fullscreen; empty = primary screen
	std::string windowGeometry;        // QWidget::saveGeometry() of the windowed viewer, base64
	bool showLabels = true;
	int clickAction = 1; // tile click: 0 = off, 1 = to Preview, 2 = to Program

	bool showSidebar = true; // health, stream, network, record and audio on the right
	bool alarms = true;      // flash the health light when something is wrong
	bool tileMeters = true;  // audio meters on each tile

	// Audio
	int loudnessPreset = 0;          // index into bm::kLoudnessPresets (0 = YouTube, -14 LUFS)
	float customTargetLufs = -16.0f; // used when the preset is "Custom"
	int meterTrack = 0;              // 0 = follow the stream output, else OBS track 1-6

	bool updateAutoCheck = true;
	bool updateIncludePrereleases = false;
	std::string updateRepo = "leonidis2013/OBSViewer";
	std::string updateSkippedVersion;
	int64_t updateLastCheck = 0; // unix seconds

	enum ClickAction { ClickOff = 0, ClickPreview = 1, ClickProgram = 2 };

	static Settings &get();

	void load();
	void save() const;
};

} // namespace obsviewer
