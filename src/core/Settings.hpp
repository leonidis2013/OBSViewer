#pragma once

#include <cstdint>
#include <string>

namespace obsviewer {

// Plugin settings, stored as JSON in OBS's plugin config folder.
// Read and written on the Qt main thread only.
struct Settings {
	std::string dockLayout = "atem";
	std::string windowLayout = "atem";
	std::string windowScreen; // QScreen::name(); empty = primary screen
	bool showLabels = true;
	bool clickToSwitch = false;

	bool updateAutoCheck = true;
	bool updateIncludePrereleases = false;
	std::string updateRepo = "leonidis2013/OBSViewer";
	std::string updateSkippedVersion;
	int64_t updateLastCheck = 0; // unix seconds

	static Settings &get();

	void load();
	void save() const;
};

} // namespace obsviewer
