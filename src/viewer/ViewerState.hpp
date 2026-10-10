#pragma once

#include <obs.h>

#include <mutex>
#include <string>
#include <vector>

namespace obsviewer {

// Everything the renderer needs, gathered on the Qt main thread and read on the
// graphics thread. The main thread owns all writes; the graphics thread takes a
// Snapshot (strong references) for each frame.
//
// Locking rule: the mutex only guards swapping pointers. No OBS call that can
// create, destroy, show or hide a source runs while it is held, because the
// graphics thread holds the graphics lock while it waits for this mutex.
class ViewerState {
public:
	static ViewerState &get();

	// One frame's worth of strong references. Release with ViewerState::release().
	struct Snapshot {
		obs_source_t *program = nullptr;
		obs_source_t *preview = nullptr;
		std::vector<obs_source_t *> scenes;      // scene list order
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
	// Cheap when nothing changed, so it also runs on a timer.
	void refresh();
	void poll() { refresh(); }
	// A view (dock or window) became visible / hidden. Scenes are marked as
	// "showing" while at least one view is visible, like OBS's own Multiview.
	void addView();
	void removeView();
	void setShowLabels(bool show);
	// Drop every reference; called when OBS exits or a scene collection unloads.
	void clear();

private:
	ViewerState() = default;

	struct Labels {
		obs_source_t *program = nullptr;
		obs_source_t *preview = nullptr;
		obs_source_t *onAir = nullptr;
		obs_source_t *rec = nullptr;
		obs_source_t *programName = nullptr;
		obs_source_t *studioOff = nullptr;
	};
	struct RenderScene {
		obs_weak_source_t *weak = nullptr;
		obs_source_t *label = nullptr;
	};

	void applyShowing(bool showing); // main thread, unlocked
	static obs_source_t *createLabel(const std::string &text, uint32_t canvasHeight, float scale);

	// ---- shared with the graphics thread (guarded by mutex_) ----
	std::mutex mutex_;
	std::vector<RenderScene> renderScenes_;
	obs_weak_source_t *program_ = nullptr;
	obs_weak_source_t *preview_ = nullptr;
	Labels labels_;
	bool studioMode_ = false;
	bool streaming_ = false;
	bool recording_ = false;
	bool recordingPaused_ = false;
	bool showLabels_ = true;
	uint32_t baseWidth_ = 1920;
	uint32_t baseHeight_ = 1080;

	// ---- main thread only ----
	std::vector<std::string> sceneNames_;
	std::vector<obs_weak_source_t *> sceneWeak_;
	std::string programName_;
	std::string previewName_;
	bool haveProgramNames_ = false;
	uint32_t labelHeight_ = 0; // canvas height the labels were built for
	int views_ = 0;
	bool showing_ = false;
	std::vector<obs_weak_source_t *> shown_; // scenes we called inc_showing on
};

} // namespace obsviewer
