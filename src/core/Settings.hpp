#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace obsviewer {

// Plugin settings, stored as JSON in OBS's plugin config folder.
// Read and written on the Qt main thread only.
struct Settings {
	std::string dockLayout = "auto";
	std::string windowLayout = "auto"; // the viewer window (windowed or fullscreen)
	std::string windowScreen;          // QScreen::name() for fullscreen; empty = primary screen
	std::string windowGeometry;        // QWidget::saveGeometry() of the windowed viewer, base64
	bool showLabels = true;
	int clickAction = 0; // tile click: 0 = off (default), 1 = to Preview, 2 = to Program

	bool showSidebar = true; // health, stream, network, record and audio on the right
	bool alarms = true;      // flash the health light when something is wrong
	bool masterBusEmbedded = true; // master bus opens next to the side bar (else in its own window)
	bool warnFrozen = true;        // health warns when Program hasn't changed for a minute while live
	bool safeAreas = false;        // action-safe / title-safe guides on Program and Preview
	bool hideObsPreview = true;    // turn OBS's own preview off while the viewer window is open
	bool showStreamTitle = true;   // YouTube stream title banner on the multiview and in the dock
	std::string youtubeChannel;    // "@handle" (backup way to find the live stream)
	bool showTransitions = true;   // CUT / AUTO strip between Preview and Program (and in the controls dock)

	// Clock and countdown in the side bar.
	bool showClock = true;
	bool countdownOn = false;
	int countdownMinutes = 10 * 60 + 30; // time of day to count down to, minutes after midnight
	int countdownDays = 0x7F;            // days it applies: bit 0 = Monday … bit 6 = Sunday
	std::string countdownLabel = "Service starts";

	// Which scene each view shows. Fixed layouts: layout id → scene name per view
	// ("" = automatic, the view's position in the scene list). Dynamic: scene
	// names left out.
	std::map<std::string, std::vector<std::string>> viewScenes;
	std::vector<std::string> dynamicHidden;
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
