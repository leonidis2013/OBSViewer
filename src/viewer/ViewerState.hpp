#pragma once

#include <obs.h>

#include <mutex>
#include <string>
#include <vector>

namespace obsviewer {

// Everything the renderer needs, gathered on the Qt main thread and read on the
// graphics thread. The main thread owns all writes; the graphics thread takes a
// Snapshot (strong references) for each frame.
class ViewerState {
public:
	static ViewerState &get();

	// One frame's worth of strong references. Release with ViewerState::release().
	struct Snapshot {
		obs_source_t *program = nullptr;
		obs_source_t *preview = nullptr;
		std::vector<obs_source_t *> scenes; // scene list order
		std::vector<obs_source_t *> sceneLabels; // parallel to scenes, may contain nullptr
		obs_source_t *programLabel = nullptr;
		obs_source_t *previewLabel = nullptr;
		obs_source_t *onAirLabel = nullptr;
		obs_source_t *recLabel = nullptr;
		obs_source_t *programNameLabel = nullptr;
		obs_source_t *studioOffLabel = nullptr;
		bool studioMode = false;
		bool streaming = false;
		bool recording = false;
		bool recordingPaused = false;
		bool showLabels = true;
		uint32_t baseWidth = 1920;
		uint32_t baseHeight = 1080;
	};

	Snapshot acquire();
	static void release(Snapshot &snapshot);

	// ---- main thread only ----

	// Re-read scenes, program/preview, studio mode and outputs from the frontend.
	void refresh();
	// Cheap check run on a timer: canvas size, scene renames, output state.
	void poll();
	// A view (dock or window) became visible / hidden. Scenes are marked as
	// "showing" while at least one view is visible.
	void addView();
	void removeView();
	void setShowLabels(bool show);
	// Drop every reference; called when OBS exits or a scene collection unloads.
	void clear();

private:
	ViewerState() = default;

	struct SceneEntry {
		obs_weak_source_t *weak = nullptr;
		std::string name;
		obs_source_t *label = nullptr; // private text source, owned
	};

	void rebuildLabelsLocked(); // requires mutex_
	void setShowingLocked(bool showing);
	void releaseEntriesLocked();
	void releaseFixedLabelsLocked();
	static obs_source_t *createLabel(const std::string &text, uint32_t canvasHeight, float scale);
	static void setWeak(obs_weak_source_t *&slot, obs_source_t *source);

	std::mutex mutex_;
	std::vector<SceneEntry> scenes_;
	obs_weak_source_t *program_ = nullptr;
	obs_weak_source_t *preview_ = nullptr;
	std::string programName_;
	std::string previewName_;

	obs_source_t *programLabel_ = nullptr;
	obs_source_t *previewLabel_ = nullptr;
	obs_source_t *onAirLabel_ = nullptr;
	obs_source_t *recLabel_ = nullptr;
	obs_source_t *programNameLabel_ = nullptr;
	obs_source_t *studioOffLabel_ = nullptr;
	uint32_t labelHeightBasis_ = 0; // canvas height labels were built for

	bool studioMode_ = false;
	bool streaming_ = false;
	bool recording_ = false;
	bool recordingPaused_ = false;
	bool showLabels_ = true;
	uint32_t baseWidth_ = 1920;
	uint32_t baseHeight_ = 1080;

	int views_ = 0;
	bool showing_ = false;
	std::vector<obs_weak_source_t *> shown_; // scenes we called inc_showing on
};

} // namespace obsviewer
