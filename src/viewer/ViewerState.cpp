#include "viewer/ViewerState.hpp"

#include <obs-frontend-api.h>

#include <algorithm>

namespace obsviewer {

namespace {

// Label sizes as a fraction of the canvas height.
constexpr float kTileLabelScale = 0.034f;
constexpr float kStatusLabelScale = 0.045f;

// text_ft2_source colours are 0xAABBGGRR. Theme "text" #E9EDF2.
constexpr long long kLabelColor = 0xFFF2EDE9;

std::string sourceName(obs_source_t *source)
{
	const char *name = source ? obs_source_get_name(source) : nullptr;
	return name ? std::string(name) : std::string();
}

} // namespace

ViewerState &ViewerState::get()
{
	static ViewerState state;
	return state;
}

void ViewerState::setWeak(obs_weak_source_t *&slot, obs_source_t *source)
{
	obs_weak_source_release(slot);
	slot = source ? obs_source_get_weak_source(source) : nullptr;
}

obs_source_t *ViewerState::createLabel(const std::string &text, uint32_t canvasHeight, float scale)
{
	obs_data_t *settings = obs_data_create();
	obs_data_t *font = obs_data_create();

	obs_data_set_string(font, "face", "Helvetica");
	obs_data_set_string(font, "style", "Bold");
	obs_data_set_int(font, "flags", 1); // bold
	obs_data_set_int(font, "size", std::max(12, static_cast<int>(static_cast<float>(canvasHeight) * scale)));

	obs_data_set_obj(settings, "font", font);
	obs_data_set_string(settings, "text", (" " + text + " ").c_str());
	obs_data_set_int(settings, "color1", kLabelColor);
	obs_data_set_int(settings, "color2", kLabelColor);
	obs_data_set_bool(settings, "outline", false);
	obs_data_set_bool(settings, "drop_shadow", false);
	obs_data_set_bool(settings, "antialiasing", true);

	obs_source_t *label = obs_source_create_private("text_ft2_source", "obsviewer-label", settings);

	obs_data_release(font);
	obs_data_release(settings);
	return label;
}

void ViewerState::releaseEntriesLocked()
{
	for (auto &entry : scenes_) {
		obs_weak_source_release(entry.weak);
		obs_source_release(entry.label);
	}
	scenes_.clear();
}

void ViewerState::releaseFixedLabelsLocked()
{
	for (obs_source_t **label : {&programLabel_, &previewLabel_, &onAirLabel_, &recLabel_, &programNameLabel_,
				     &studioOffLabel_}) {
		obs_source_release(*label);
		*label = nullptr;
	}
	labelHeightBasis_ = 0;
}

void ViewerState::rebuildLabelsLocked()
{
	const uint32_t h = baseHeight_;

	for (size_t i = 0; i < scenes_.size(); ++i) {
		obs_source_release(scenes_[i].label);
		scenes_[i].label = createLabel(std::to_string(i + 1) + "   " + scenes_[i].name, h, kTileLabelScale);
	}

	releaseFixedLabelsLocked();
	programLabel_ = createLabel("PROGRAM" + (programName_.empty() ? "" : "  ·  " + programName_), h,
				    kTileLabelScale);
	previewLabel_ = createLabel("PREVIEW" + (previewName_.empty() ? "" : "  ·  " + previewName_), h,
				    kTileLabelScale);
	onAirLabel_ = createLabel("ON AIR", h, kStatusLabelScale);
	recLabel_ = createLabel("REC", h, kStatusLabelScale);
	programNameLabel_ = createLabel("PGM   " + (programName_.empty() ? std::string("—") : programName_), h,
					kStatusLabelScale);
	studioOffLabel_ = createLabel("STUDIO MODE OFF", h, kTileLabelScale * 0.8f);
	labelHeightBasis_ = h;
}

void ViewerState::setShowingLocked(bool showing)
{
	// Undo previous marks first; then (re)apply to the current scene list.
	for (obs_weak_source_t *weak : shown_) {
		obs_source_t *source = obs_weak_source_get_source(weak);
		if (source) {
			obs_source_dec_showing(source);
			obs_source_release(source);
		}
		obs_weak_source_release(weak);
	}
	shown_.clear();

	showing_ = showing;
	if (!showing)
		return;

	for (const auto &entry : scenes_) {
		obs_source_t *source = obs_weak_source_get_source(entry.weak);
		if (!source)
			continue;
		obs_source_inc_showing(source);
		shown_.push_back(obs_source_get_weak_source(source));
		obs_source_release(source);
	}
}

void ViewerState::refresh()
{
	obs_video_info ovi = {};
	const bool haveVideo = obs_get_video_info(&ovi);

	obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);

	std::vector<obs_source_t *> scenes;
	std::vector<std::string> names;
	for (size_t i = 0; i < list.sources.num; ++i) {
		obs_source_t *s = list.sources.array[i];
		scenes.push_back(s);
		names.push_back(sourceName(s));
	}

	const bool studio = obs_frontend_preview_program_mode_active();
	obs_source_t *program = obs_frontend_get_current_scene();
	obs_source_t *preview = studio ? obs_frontend_get_current_preview_scene() : obs_source_get_ref(program);

	const bool streaming = obs_frontend_streaming_active();
	const bool recording = obs_frontend_recording_active();
	const bool paused = recording && obs_frontend_recording_paused();

	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (haveVideo && ovi.base_width > 0 && ovi.base_height > 0) {
			baseWidth_ = ovi.base_width;
			baseHeight_ = ovi.base_height;
		}

		// Did the scene list change (membership, order or names)?
		bool listChanged = scenes.size() != scenes_.size();
		for (size_t i = 0; !listChanged && i < scenes.size(); ++i) {
			if (names[i] != scenes_[i].name || !obs_weak_source_references_source(scenes_[i].weak, scenes[i]))
				listChanged = true;
		}

		if (listChanged) {
			releaseEntriesLocked();
			for (size_t i = 0; i < scenes.size(); ++i)
				scenes_.push_back(SceneEntry{obs_source_get_weak_source(scenes[i]), names[i], nullptr});
		}

		const std::string programName = sourceName(program);
		const std::string previewName = sourceName(preview);
		const bool namesChanged = programName != programName_ || previewName != previewName_;
		programName_ = programName;
		previewName_ = previewName;

		setWeak(program_, program);
		setWeak(preview_, preview);

		if (listChanged || namesChanged || labelHeightBasis_ != baseHeight_ || !programLabel_)
			rebuildLabelsLocked();

		if (listChanged && showing_)
			setShowingLocked(true);

		studioMode_ = studio;
		streaming_ = streaming;
		recording_ = recording;
		recordingPaused_ = paused;
	}

	obs_source_release(program);
	obs_source_release(preview);
	obs_frontend_source_list_free(&list);
}

void ViewerState::poll()
{
	refresh();
}

void ViewerState::addView()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (++views_ == 1)
		setShowingLocked(true);
}

void ViewerState::removeView()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (views_ > 0 && --views_ == 0)
		setShowingLocked(false);
}

void ViewerState::setShowLabels(bool show)
{
	std::lock_guard<std::mutex> lock(mutex_);
	showLabels_ = show;
}

void ViewerState::clear()
{
	std::lock_guard<std::mutex> lock(mutex_);
	setShowingLocked(false);
	releaseEntriesLocked();
	releaseFixedLabelsLocked();
	obs_weak_source_release(program_);
	obs_weak_source_release(preview_);
	program_ = preview_ = nullptr;
	programName_.clear();
	previewName_.clear();
	// Keep views_ so showing resumes when the next scene collection loads.
	showing_ = false;
}

ViewerState::Snapshot ViewerState::acquire()
{
	Snapshot snap;
	std::lock_guard<std::mutex> lock(mutex_);

	snap.program = obs_weak_source_get_source(program_);
	snap.preview = obs_weak_source_get_source(preview_);
	snap.scenes.reserve(scenes_.size());
	snap.sceneLabels.reserve(scenes_.size());
	for (const auto &entry : scenes_) {
		snap.scenes.push_back(obs_weak_source_get_source(entry.weak));
		snap.sceneLabels.push_back(obs_source_get_ref(entry.label));
	}
	snap.programLabel = obs_source_get_ref(programLabel_);
	snap.previewLabel = obs_source_get_ref(previewLabel_);
	snap.onAirLabel = obs_source_get_ref(onAirLabel_);
	snap.recLabel = obs_source_get_ref(recLabel_);
	snap.programNameLabel = obs_source_get_ref(programNameLabel_);
	snap.studioOffLabel = obs_source_get_ref(studioOffLabel_);
	snap.studioMode = studioMode_;
	snap.streaming = streaming_;
	snap.recording = recording_;
	snap.recordingPaused = recordingPaused_;
	snap.showLabels = showLabels_;
	snap.baseWidth = baseWidth_;
	snap.baseHeight = baseHeight_;
	return snap;
}

void ViewerState::release(Snapshot &snap)
{
	obs_source_release(snap.program);
	obs_source_release(snap.preview);
	for (obs_source_t *s : snap.scenes)
		obs_source_release(s);
	for (obs_source_t *s : snap.sceneLabels)
		obs_source_release(s);
	for (obs_source_t *s : {snap.programLabel, snap.previewLabel, snap.onAirLabel, snap.recLabel,
				snap.programNameLabel, snap.studioOffLabel})
		obs_source_release(s);
	snap = Snapshot{};
}

} // namespace obsviewer
