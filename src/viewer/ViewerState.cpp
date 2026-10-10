#include "viewer/ViewerState.hpp"

#include "audio/SourceLevels.hpp"

#include <obs-frontend-api.h>

#include <algorithm>
#include <utility>

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

void releaseAll(std::vector<obs_source_t *> &sources)
{
	for (obs_source_t *s : sources)
		obs_source_release(s);
	sources.clear();
}

void releaseAll(std::vector<obs_weak_source_t *> &weak)
{
	for (obs_weak_source_t *w : weak)
		obs_weak_source_release(w);
	weak.clear();
}

// Audio sources a scene plays: visible items, including inside groups and nested
// scenes. Collected without holding any scene lock while recursing.
std::vector<obs_source_t *> collectAudio(obs_source_t *root)
{
	std::vector<obs_source_t *> audio;   // strong refs, caller releases
	std::vector<obs_source_t *> pending; // scenes still to enumerate (strong refs)
	std::vector<obs_source_t *> visited;
	if (root)
		pending.push_back(obs_source_get_ref(root));

	struct Ctx {
		std::vector<obs_source_t *> *audio;
		std::vector<obs_source_t *> *pending;
	};

	while (!pending.empty()) {
		obs_source_t *src = pending.back();
		pending.pop_back();
		if (std::find(visited.begin(), visited.end(), src) != visited.end()) {
			obs_source_release(src);
			continue;
		}
		visited.push_back(src);

		obs_scene_t *scene = obs_scene_from_source(src);
		if (!scene)
			scene = obs_group_from_source(src);
		if (!scene)
			continue;

		Ctx ctx{&audio, &pending};
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				auto *c = static_cast<Ctx *>(param);
				if (!obs_sceneitem_visible(item))
					return true;
				obs_source_t *child = obs_sceneitem_get_source(item);
				if (!child)
					return true;
				if (obs_sceneitem_is_group(item) || obs_scene_from_source(child)) {
					c->pending->push_back(obs_source_get_ref(child));
					return true;
				}
				if ((obs_source_get_output_flags(child) & OBS_SOURCE_AUDIO) &&
				    std::find(c->audio->begin(), c->audio->end(), child) == c->audio->end())
					c->audio->push_back(obs_source_get_ref(child));
				return true;
			},
			&ctx);
	}
	for (obs_source_t *v : visited)
		obs_source_release(v);
	return audio;
}

AudioList touchAll(const std::vector<obs_source_t *> &sources)
{
	AudioList list;
	for (obs_source_t *s : sources)
		if (auto e = SourceLevels::get().touch(s))
			list.push_back(std::move(e));
	return list;
}

} // namespace

ViewerState &ViewerState::get()
{
	static ViewerState state;
	return state;
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

void ViewerState::applyShowing(bool showing)
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

	for (obs_weak_source_t *weak : sceneWeak_) {
		obs_source_t *source = obs_weak_source_get_source(weak);
		if (!source)
			continue;
		obs_source_inc_showing(source);
		shown_.push_back(obs_source_get_weak_source(source));
		obs_source_release(source);
	}
}

void ViewerState::refresh()
{
	// 1. Read the frontend (no lock held).
	obs_video_info ovi = {};
	uint32_t width = baseWidth_, height = baseHeight_;
	if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
		width = ovi.base_width;
		height = ovi.base_height;
	}

	obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	std::vector<std::string> names;
	names.reserve(list.sources.num);
	for (size_t i = 0; i < list.sources.num; ++i)
		names.push_back(sourceName(list.sources.array[i]));

	const bool studio = obs_frontend_preview_program_mode_active();
	obs_source_t *program = obs_frontend_get_current_scene();
	obs_source_t *preview = studio ? obs_frontend_get_current_preview_scene() : obs_source_get_ref(program);
	const std::string programName = sourceName(program);
	const std::string previewName = sourceName(preview);

	const bool streaming = obs_frontend_streaming_active();
	const bool recording = obs_frontend_recording_active();
	const bool paused = recording && obs_frontend_recording_paused();

	// 2. Work out what changed (main-thread copies, no lock needed).
	bool listChanged = names.size() != sceneNames_.size();
	for (size_t i = 0; !listChanged && i < names.size(); ++i)
		listChanged = names[i] != sceneNames_[i] ||
			      !obs_weak_source_references_source(sceneWeak_[i], list.sources.array[i]);
	const bool heightChanged = height != labelHeight_;
	const bool namesChanged = !haveProgramNames_ || programName != programName_ || previewName != previewName_;

	// 3. Build replacements (no lock held: creating sources may enter graphics).
	std::vector<RenderScene> newScenes;
	if (listChanged || heightChanged) {
		newScenes.reserve(names.size());
		for (size_t i = 0; i < names.size(); ++i)
			newScenes.push_back(RenderScene{obs_source_get_weak_source(list.sources.array[i]),
						       createLabel(std::to_string(i + 1) + "   " + names[i], height,
								   kTileLabelScale)});
	}

	Labels newLabels;
	const bool rebuildNamed = namesChanged || heightChanged;
	const bool rebuildStatic = heightChanged;
	if (rebuildNamed) {
		newLabels.program = createLabel("PROGRAM" + (programName.empty() ? "" : "  ·  " + programName), height,
						kTileLabelScale);
		newLabels.preview = createLabel("PREVIEW" + (previewName.empty() ? "" : "  ·  " + previewName), height,
						kTileLabelScale);
		newLabels.programName = createLabel(
			"PGM   " + (programName.empty() ? std::string("—") : programName), height, kStatusLabelScale);
	}
	if (rebuildStatic) {
		newLabels.onAir = createLabel("ON AIR", height, kStatusLabelScale);
		newLabels.rec = createLabel("REC", height, kStatusLabelScale);
		newLabels.studioOff = createLabel("STUDIO MODE OFF", height, kTileLabelScale * 0.8f);
	}

	// Per-scene audio for the tile meters (volume meters are created / destroyed
	// here, never under the lock).
	std::vector<AudioList> newSceneAudio;
	AudioList newPreviewAudio;
	if (showTileMeters_) {
		SourceLevels::get().beginSync();
		newSceneAudio.reserve(list.sources.num);
		for (size_t i = 0; i < list.sources.num; ++i) {
			std::vector<obs_source_t *> srcs = collectAudio(list.sources.array[i]);
			newSceneAudio.push_back(touchAll(srcs));
			for (obs_source_t *a : srcs)
				obs_source_release(a);
		}
		std::vector<obs_source_t *> pv = collectAudio(preview);
		newPreviewAudio = touchAll(pv);
		for (obs_source_t *a : pv)
			obs_source_release(a);
		SourceLevels::get().endSync();
	} else {
		SourceLevels::get().clear();
	}
	if (rebuildStatic)
		newLabels.muted = createLabel("MUTED", height, kTileLabelScale * 0.7f);

	obs_weak_source_t *newProgram = program ? obs_source_get_weak_source(program) : nullptr;
	obs_weak_source_t *newPreview = preview ? obs_source_get_weak_source(preview) : nullptr;

	// 4. Swap under the lock; collect what to release afterwards.
	std::vector<obs_source_t *> oldSources;
	std::vector<obs_weak_source_t *> oldWeak;
	{
		std::lock_guard<std::mutex> lock(mutex_);

		baseWidth_ = width;
		baseHeight_ = height;
		studioMode_ = studio;
		streaming_ = streaming;
		recording_ = recording;
		recordingPaused_ = paused;

		std::swap(sceneAudio_, newSceneAudio);
		std::swap(previewAudio_, newPreviewAudio);

		oldWeak.push_back(program_);
		oldWeak.push_back(preview_);
		program_ = newProgram;
		preview_ = newPreview;

		if (listChanged || heightChanged) {
			for (auto &rs : renderScenes_) {
				oldWeak.push_back(rs.weak);
				oldSources.push_back(rs.label);
			}
			renderScenes_ = std::move(newScenes);
		}
		if (rebuildNamed) {
			oldSources.push_back(std::exchange(labels_.program, newLabels.program));
			oldSources.push_back(std::exchange(labels_.preview, newLabels.preview));
			oldSources.push_back(std::exchange(labels_.programName, newLabels.programName));
		}
		if (rebuildStatic) {
			oldSources.push_back(std::exchange(labels_.onAir, newLabels.onAir));
			oldSources.push_back(std::exchange(labels_.rec, newLabels.rec));
			oldSources.push_back(std::exchange(labels_.studioOff, newLabels.studioOff));
			oldSources.push_back(std::exchange(labels_.muted, newLabels.muted));
		}
	}

	// 5. Update main-thread bookkeeping and release old objects (no lock held).
	if (listChanged) {
		releaseAll(sceneWeak_);
		for (size_t i = 0; i < list.sources.num; ++i)
			sceneWeak_.push_back(obs_source_get_weak_source(list.sources.array[i]));
		sceneNames_ = names;
	}
	programName_ = programName;
	previewName_ = previewName;
	haveProgramNames_ = true;
	labelHeight_ = height;
	baseWidthMain_ = width;

	releaseAll(oldSources);
	releaseAll(oldWeak);

	if (listChanged && showing_)
		applyShowing(true);

	obs_source_release(program);
	obs_source_release(preview);
	obs_frontend_source_list_free(&list);
}

void ViewerState::addView()
{
	if (++views_ == 1)
		applyShowing(true);
}

void ViewerState::removeView()
{
	if (views_ > 0 && --views_ == 0)
		applyShowing(false);
}

std::string ViewerState::sceneName(int index) const
{
	return index >= 0 && static_cast<size_t>(index) < sceneNames_.size() ? sceneNames_[static_cast<size_t>(index)]
									  : std::string();
}

obs_source_t *ViewerState::sceneAt(int index) const
{
	if (index < 0 || static_cast<size_t>(index) >= sceneWeak_.size())
		return nullptr;
	return obs_weak_source_get_source(sceneWeak_[static_cast<size_t>(index)]);
}

void ViewerState::setShowTileMeters(bool show)
{
	std::lock_guard<std::mutex> lock(mutex_);
	showTileMeters_ = show;
}

void ViewerState::setShowLabels(bool show)
{
	std::lock_guard<std::mutex> lock(mutex_);
	showLabels_ = show;
}

void ViewerState::clear()
{
	applyShowing(false);

	std::vector<obs_source_t *> oldSources;
	std::vector<obs_weak_source_t *> oldWeak;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (auto &rs : renderScenes_) {
			oldWeak.push_back(rs.weak);
			oldSources.push_back(rs.label);
		}
		renderScenes_.clear();
		oldWeak.push_back(std::exchange(program_, nullptr));
		oldWeak.push_back(std::exchange(preview_, nullptr));
		for (obs_source_t **l : {&labels_.program, &labels_.preview, &labels_.onAir, &labels_.rec,
					 &labels_.programName, &labels_.studioOff, &labels_.muted})
			oldSources.push_back(std::exchange(*l, nullptr));
		sceneAudio_.clear();
		previewAudio_.clear();
	}
	SourceLevels::get().clear();

	releaseAll(oldSources);
	releaseAll(oldWeak);
	releaseAll(sceneWeak_);
	sceneNames_.clear();
	programName_.clear();
	previewName_.clear();
	haveProgramNames_ = false;
	labelHeight_ = 0;
	// views_ is kept, so scenes are marked showing again when the next collection loads.
}

ViewerState::Snapshot ViewerState::acquire()
{
	Snapshot snap;
	std::lock_guard<std::mutex> lock(mutex_);

	snap.program = obs_weak_source_get_source(program_);
	snap.preview = obs_weak_source_get_source(preview_);
	snap.scenes.reserve(renderScenes_.size());
	snap.sceneLabels.reserve(renderScenes_.size());
	for (const auto &rs : renderScenes_) {
		snap.scenes.push_back(obs_weak_source_get_source(rs.weak));
		snap.sceneLabels.push_back(obs_source_get_ref(rs.label));
	}
	snap.programLabel = obs_source_get_ref(labels_.program);
	snap.previewLabel = obs_source_get_ref(labels_.preview);
	snap.onAirLabel = obs_source_get_ref(labels_.onAir);
	snap.recLabel = obs_source_get_ref(labels_.rec);
	snap.programNameLabel = obs_source_get_ref(labels_.programName);
	snap.studioOffLabel = obs_source_get_ref(labels_.studioOff);
	snap.mutedLabel = obs_source_get_ref(labels_.muted);
	snap.showTileMeters = showTileMeters_;
	if (showTileMeters_) {
		snap.sceneAudio = sceneAudio_;
		snap.previewAudio = previewAudio_;
	}
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
				snap.programNameLabel, snap.studioOffLabel, snap.mutedLabel})
		obs_source_release(s);
	snap = Snapshot{};
}

} // namespace obsviewer
