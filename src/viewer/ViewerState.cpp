#include "viewer/ViewerState.hpp"

#include "audio/SourceLevels.hpp"
#include "core/Settings.hpp"
#include "viewer/Layouts.hpp"

#include <obs-frontend-api.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace obsviewer {

namespace {

// Label sizes as a fraction of the canvas height.
constexpr float kTileLabelScale = 0.034f;
constexpr float kStatusLabelScale = 0.045f;

// text_ft2_source colours are 0xAABBGGRR. Theme "text" #D8DEE6.
constexpr long long kLabelColor = 0xFFE6DED8;

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

std::vector<int> ViewerState::wantedShown() const
{
	std::vector<int> scenes;
	for (const auto &kv : views_) {
		const auto it = viewMapsMain_.find(layoutById(kv.second).id);
		if (it == viewMapsMain_.end())
			continue;
		for (int i : it->second)
			if (i >= 0 && std::find(scenes.begin(), scenes.end(), i) == scenes.end())
				scenes.push_back(i);
	}
	std::sort(scenes.begin(), scenes.end());
	return scenes;
}

void ViewerState::applyShowing(const std::vector<int> &scenes)
{
	// Mark the new set first, then unmark the old one, so a scene that stays on
	// screen never drops to "not showing" for a moment (that would restart media
	// sources set to restart when shown).
	std::vector<obs_weak_source_t *> next;
	for (int i : scenes) {
		if (i < 0 || static_cast<size_t>(i) >= sceneWeak_.size())
			continue;
		obs_source_t *source = obs_weak_source_get_source(sceneWeak_[static_cast<size_t>(i)]);
		if (!source)
			continue;
		obs_source_inc_showing(source);
		next.push_back(obs_source_get_weak_source(source));
		obs_source_release(source);
	}
	for (obs_weak_source_t *weak : shown_) {
		obs_source_t *source = obs_weak_source_get_source(weak);
		if (source) {
			obs_source_dec_showing(source);
			obs_source_release(source);
		}
		obs_weak_source_release(weak);
	}
	shown_ = std::move(next);
	shownScenes_ = scenes;
}

void ViewerState::rebuildViewMaps()
{
	const Settings &st = Settings::get();
	const int n = static_cast<int>(sceneNames_.size());
	auto indexOf = [this](const std::string &name) {
		for (size_t i = 0; i < sceneNames_.size(); ++i)
			if (sceneNames_[i] == name)
				return static_cast<int>(i);
		return -1;
	};
	std::map<std::string, std::vector<int>> maps;
	for (const LayoutInfo &l : allLayouts()) {
		std::vector<int> &m = maps[l.id];
		if (l.fixedScenes < 0) {
			for (int i = 0; i < n; ++i)
				if (std::find(st.dynamicHidden.begin(), st.dynamicHidden.end(), sceneNames_[static_cast<size_t>(i)]) ==
				    st.dynamicHidden.end())
					m.push_back(i);
			continue;
		}
		const auto it = st.viewScenes.find(l.id);
		for (int slot = 0; slot < l.fixedScenes; ++slot) {
			std::string name;
			if (it != st.viewScenes.end() && static_cast<size_t>(slot) < it->second.size())
				name = it->second[static_cast<size_t>(slot)];
			m.push_back(name.empty() ? (slot < n ? slot : -1) : indexOf(name));
		}
	}
	viewMapsMain_ = maps;
	auto shared = std::make_shared<const std::map<std::string, std::vector<int>>>(std::move(maps));
	{
		std::lock_guard<std::mutex> lock(mutex_);
		viewMaps_ = std::move(shared);
	}
}

std::vector<int> ViewerState::viewMap(const std::string &layoutId) const
{
	const auto it = viewMapsMain_.find(layoutById(layoutId).id);
	return it == viewMapsMain_.end() ? std::vector<int>() : it->second;
}

void ViewerState::updateViewScenes()
{
	rebuildViewMaps();
	const std::vector<int> wanted = wantedShown();
	if (wanted != shownScenes_)
		applyShowing(wanted);
}

void ViewerState::setSafeAreas(bool on)
{
	std::lock_guard<std::mutex> lock(mutex_);
	safeAreas_ = on;
}

void ViewerState::setShowTransitions(bool on)
{
	std::lock_guard<std::mutex> lock(mutex_);
	showTransitions_ = on;
}

namespace {

// "300 ms", "1.5 s"; empty for transitions with their own fixed length.
std::string durationText(obs_source_t *transition, int ms)
{
	if (!transition || obs_transition_fixed(transition))
		return std::string();
	if (ms < 1000)
		return std::to_string(ms) + " ms";
	char buf[32];
	snprintf(buf, sizeof(buf), "%.1f s", static_cast<double>(ms) / 1000.0);
	return buf;
}

} // namespace

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
	obs_source_t *transition = obs_frontend_get_current_transition();
	const std::string transitionName = sourceName(transition);
	const std::string transitionTime = durationText(transition, obs_frontend_get_transition_duration());
	const std::string transitionText = transitionName + "\n" + transitionTime;

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
	const bool rebuildTransition = heightChanged || transitionText != transitionText_;
	if (rebuildNamed) {
		// Fixed titles plus the scene names on their own, so the titles never move.
		newLabels.program = createLabel("PROGRAM", height, kTileLabelScale);
		newLabels.preview = createLabel("PREVIEW", height, kTileLabelScale);
		newLabels.programName = createLabel(programName.empty() ? std::string("—") : programName, height,
						    kTileLabelScale);
		newLabels.previewName = createLabel(previewName.empty() ? std::string("—") : previewName, height,
						    kTileLabelScale);
	}
	if (rebuildStatic) {
		newLabels.rec = createLabel("REC", height, kStatusLabelScale);
		newLabels.studioOff = createLabel("STUDIO MODE OFF", height, kTileLabelScale * 0.8f);
		newLabels.cut = createLabel("CUT", height, kTileLabelScale);
		newLabels.autoTake = createLabel("AUTO", height, kTileLabelScale);
	}
	if (rebuildTransition) {
		newLabels.transitionName =
			createLabel(transitionName.empty() ? std::string("—") : transitionName, height, kTileLabelScale);
		newLabels.transitionTime = createLabel(transitionTime.empty() ? std::string("—") : transitionTime,
						       height, kTileLabelScale);
	}

	// Per-scene audio for the tile meters (volume meters are created / destroyed
	// here, never under the lock).
	// Rebuilt only when something changed: the scene list, Program / Preview, a
	// scene's items (signals set audioDirty_), or every 5 s as a safety net for
	// changes inside groups. Otherwise only mute states are refreshed.
	std::vector<AudioList> newSceneAudio;
	AudioList newPreviewAudio;
	const bool safetyRebuild = ++pollsSinceAudio_ >= 10;
	const bool rebuildAudio = listChanged || namesChanged || heightChanged || safetyRebuild ||
				  audioDirty_.exchange(false);
	if (rebuildAudio)
		pollsSinceAudio_ = 0;
	if (!rebuildAudio) {
		if (showTileMeters_)
			SourceLevels::get().refreshMuted();
	} else if (showTileMeters_) {
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
	obs_weak_source_t *newTransition = transition ? obs_source_get_weak_source(transition) : nullptr;

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

		if (rebuildAudio) {
			std::swap(sceneAudio_, newSceneAudio);
			std::swap(previewAudio_, newPreviewAudio);
		}

		oldWeak.push_back(program_);
		oldWeak.push_back(preview_);
		program_ = newProgram;
		preview_ = newPreview;
		oldWeak.push_back(std::exchange(transition_, newTransition));

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
			oldSources.push_back(std::exchange(labels_.previewName, newLabels.previewName));
		}
		if (rebuildStatic) {
			oldSources.push_back(std::exchange(labels_.rec, newLabels.rec));
			oldSources.push_back(std::exchange(labels_.studioOff, newLabels.studioOff));
			oldSources.push_back(std::exchange(labels_.muted, newLabels.muted));
			oldSources.push_back(std::exchange(labels_.cut, newLabels.cut));
			oldSources.push_back(std::exchange(labels_.autoTake, newLabels.autoTake));
		}
		if (rebuildTransition) {
			oldSources.push_back(std::exchange(labels_.transitionName, newLabels.transitionName));
			oldSources.push_back(std::exchange(labels_.transitionTime, newLabels.transitionTime));
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
	transitionText_ = transitionText;
	haveProgramNames_ = true;
	labelHeight_ = height;
	baseWidthMain_ = width;

	releaseAll(oldSources);
	releaseAll(oldWeak);

	if (listChanged) {
		rebuildViewMaps();
		applyShowing(wantedShown());
		watchScenes();
	}

	obs_source_release(program);
	obs_source_release(preview);
	obs_source_release(transition);
	obs_frontend_source_list_free(&list);
}

void ViewerState::setView(const void *view, const std::string &layoutId)
{
	views_[view] = layoutId;
	const std::vector<int> wanted = wantedShown();
	if (wanted != shownScenes_)
		applyShowing(wanted);
}

void ViewerState::removeView(const void *view)
{
	views_.erase(view);
	const std::vector<int> wanted = wantedShown();
	if (wanted != shownScenes_)
		applyShowing(wanted);
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
	{
		std::lock_guard<std::mutex> lock(mutex_);
		showTileMeters_ = show;
	}
	audioDirty_ = true;
}

void ViewerState::onSceneItemsChanged(void *param, calldata_t *)
{
	// Any thread. The next poll rebuilds the audio lists.
	static_cast<ViewerState *>(param)->audioDirty_ = true;
}

void ViewerState::unwatchScenes()
{
	for (obs_weak_source_t *weak : watched_) {
		// A scene that's already gone took its signals with it.
		if (obs_source_t *src = obs_weak_source_get_source(weak)) {
			signal_handler_t *sh = obs_source_get_signal_handler(src);
			for (const char *sig : {"item_add", "item_remove", "item_visible", "refresh"})
				signal_handler_disconnect(sh, sig, &ViewerState::onSceneItemsChanged, this);
			obs_source_release(src);
		}
		obs_weak_source_release(weak);
	}
	watched_.clear();
}

void ViewerState::watchScenes()
{
	unwatchScenes();
	for (obs_weak_source_t *weak : sceneWeak_) {
		obs_source_t *src = obs_weak_source_get_source(weak);
		if (!src)
			continue;
		signal_handler_t *sh = obs_source_get_signal_handler(src);
		for (const char *sig : {"item_add", "item_remove", "item_visible", "refresh"})
			signal_handler_connect(sh, sig, &ViewerState::onSceneItemsChanged, this);
		watched_.push_back(obs_source_get_weak_source(src));
		obs_source_release(src);
	}
}

void ViewerState::setShowLabels(bool show)
{
	std::lock_guard<std::mutex> lock(mutex_);
	showLabels_ = show;
}

void ViewerState::clear()
{
	applyShowing({});
	unwatchScenes();
	audioDirty_ = true;

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
		oldWeak.push_back(std::exchange(transition_, nullptr));
		for (obs_source_t **l : {&labels_.program, &labels_.preview, &labels_.previewName, &labels_.rec,
					 &labels_.programName, &labels_.studioOff, &labels_.muted, &labels_.cut,
					 &labels_.autoTake, &labels_.transitionName, &labels_.transitionTime})
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
	transitionText_.clear();
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
	snap.previewNameLabel = obs_source_get_ref(labels_.previewName);
	snap.recLabel = obs_source_get_ref(labels_.rec);
	snap.programNameLabel = obs_source_get_ref(labels_.programName);
	snap.studioOffLabel = obs_source_get_ref(labels_.studioOff);
	snap.mutedLabel = obs_source_get_ref(labels_.muted);
	snap.transition = obs_weak_source_get_source(transition_);
	snap.cutLabel = obs_source_get_ref(labels_.cut);
	snap.autoLabel = obs_source_get_ref(labels_.autoTake);
	snap.transitionNameLabel = obs_source_get_ref(labels_.transitionName);
	snap.transitionTimeLabel = obs_source_get_ref(labels_.transitionTime);
	snap.showTransitions = showTransitions_;
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
	snap.safeAreas = safeAreas_;
	snap.viewMaps = viewMaps_;
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
	for (obs_source_t *s : {snap.programLabel, snap.previewLabel, snap.previewNameLabel, snap.recLabel,
				snap.programNameLabel, snap.studioOffLabel, snap.mutedLabel, snap.transition, snap.cutLabel,
				snap.autoLabel, snap.transitionNameLabel, snap.transitionTimeLabel})
		obs_source_release(s);
	snap = Snapshot{};
}

} // namespace obsviewer
