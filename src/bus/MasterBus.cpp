#include "bus/MasterBus.hpp"

#include "bus/Engine.hpp"
#include "core/Health.hpp"
#include "metrics/Metrics.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>
#include <util/util_uint64.h>

#include <plugin-support.h>

#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace obsviewer {

namespace {

constexpr const char *kSourceId = "obsviewer_master_bus";
constexpr uint32_t kChannel = MAX_CHANNELS - 1; // a global output slot the frontend never uses

std::string configPath(const char *file)
{
	char *p = obs_module_config_path(file);
	std::string s = p ? p : "";
	bfree(p);
	return s;
}

std::string collectionName()
{
	char *name = obs_frontend_get_current_scene_collection();
	std::string s = name ? name : "";
	bfree(name);
	return s;
}

const char *busGetName(void *)
{
	return "OBSViewer Master Bus";
}

void *busCreate(obs_data_t *, obs_source_t *source)
{
	return source;
}

void busDestroy(void *) {}

} // namespace

MasterBus &MasterBus::get()
{
	static MasterBus bus;
	return bus;
}

void MasterBus::registerSource()
{
	static obs_source_info info = {};
	info.id = kSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	// Audio only; hidden from OBS's "Add Source" menu.
	info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_CAP_DISABLED;
	info.get_name = busGetName;
	info.create = busCreate;
	info.destroy = busDestroy;
	obs_register_source(&info);
}

// ------------------------------------------------------------------ config

namespace {
// obsviewer.plugin/Contents/MacOS/obsviewer -> obsviewer.plugin/Contents/Helpers/obsviewer-scan
std::string scanHelperPath()
{
	const char *binary = obs_get_module_binary_path(obs_current_module());
	if (!binary)
		return {};
	std::string path(binary);
	const size_t slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return {};
	path.resize(slash); // .../Contents/MacOS
	const size_t up = path.find_last_of('/');
	if (up == std::string::npos)
		return {};
	path.resize(up); // .../Contents
	return path + "/Helpers/obsviewer-scan";
}
} // namespace

void MasterBus::load()
{
	if (loaded_)
		return;
	loaded_ = true;

	char *dir = obs_module_config_path("bus");
	if (dir) {
		os_mkdirs(dir);
		bus::Engine::get().init(dir, scanHelperPath());
		bfree(dir);
	}

	const std::string path = configPath("masterbus.json");
	obs_data_t *data = os_file_exists(path.c_str()) ? obs_data_create_from_json_file_safe(path.c_str(), "bak") : nullptr;
	std::string chain;
	if (data) {
		config_.enabled = obs_data_get_bool(data, "enabled");
		config_.sendTrack = static_cast<int>(obs_data_get_int(data, "send_track"));
		config_.outputMask = static_cast<uint32_t>(obs_data_get_int(data, "output_mask"));
		chain = obs_data_get_string(data, "chain");

		loadRoutes(data);
		obs_data_release(data);
	}
	if (config_.sendTrack < 1 || config_.sendTrack > 6)
		config_.sendTrack = 6;
	config_.outputMask &= 0x3F;
	config_.outputMask &= ~(1u << (config_.sendTrack - 1));
	if (!config_.outputMask)
		config_.outputMask = config_.sendTrack == 1 ? 2 : 1;

	// Plugins load after OBS has finished opening, one at a time, so they don't
	// hold up OBS's start. Until then the bus passes audio through unprocessed.
	if (!chain.empty()) {
		QTimer::singleShot(2000, [this, chain] {
			if (!loaded_ || !bus::Engine::get().ready())
				return;
			obs_log(LOG_INFO, "master bus: restoring the plugin chain");
			bus::Engine::get().loadChain(chain, [this](const std::string &errors) {
				if (!errors.empty())
					obs_log(LOG_WARNING, "master bus: some plugins could not be restored:\n%s",
						errors.c_str());
				obs_log(LOG_INFO, "master bus: %d plugin(s) restored", bus::Engine::get().slotCount());
				applyOutputMixers(); // latency of the restored chain
			});
		});
	}

	if (config_.enabled) {
		start();
	} else if (!originalMixers_.empty()) {
		// Bus off but sources still routed (e.g. OBS quit unexpectedly): put them back
		// so nothing goes silent.
		restoreAllRouting();
		save();
	}
}

void MasterBus::loadRoutes(obs_data_t *data)
{
	originalMixers_.clear();
	obs_data_t *routes = data ? obs_data_get_obj(data, "routes") : nullptr;
	obs_data_t *mine = routes ? obs_data_get_obj(routes, collectionName().c_str()) : nullptr;
	if (mine) {
		for (obs_data_item_t *item = obs_data_first(mine); item; obs_data_item_next(&item))
			originalMixers_[obs_data_item_get_name(item)] = static_cast<uint32_t>(obs_data_item_get_int(item));
		obs_data_release(mine);
	}
	obs_data_release(routes);
}

void MasterBus::collectionChanging()
{
	if (loaded_)
		save(); // file routing memory under the collection we're leaving
}

void MasterBus::collectionChanged()
{
	if (!loaded_)
		return;
	const std::string path = configPath("masterbus.json");
	obs_data_t *data = os_file_exists(path.c_str()) ? obs_data_create_from_json_file_safe(path.c_str(), "bak") : nullptr;
	loadRoutes(data);
	obs_data_release(data);
	if (!config_.enabled && !originalMixers_.empty()) {
		restoreAllRouting();
		save();
	}
}

void MasterBus::chainChanged()
{
	applyOutputMixers();
	save();
}

void MasterBus::save() const
{
	const std::string path = configPath("masterbus.json");
	if (path.empty())
		return;

	// Keep routing memory of other scene collections.
	obs_data_t *old = os_file_exists(path.c_str()) ? obs_data_create_from_json_file_safe(path.c_str(), "bak") : nullptr;
	obs_data_t *routes = old ? obs_data_get_obj(old, "routes") : nullptr;
	if (!routes)
		routes = obs_data_create();

	obs_data_t *mine = obs_data_create();
	for (const auto &[uuid, mixers] : originalMixers_)
		obs_data_set_int(mine, uuid.c_str(), mixers);
	obs_data_set_obj(routes, collectionName().c_str(), mine);
	obs_data_release(mine);

	obs_data_t *data = obs_data_create();
	obs_data_set_bool(data, "enabled", config_.enabled);
	obs_data_set_int(data, "send_track", config_.sendTrack);
	obs_data_set_int(data, "output_mask", config_.outputMask);
	if (bus::Engine::get().ready())
		obs_data_set_string(data, "chain", bus::Engine::get().saveChain().c_str());
	else if (old)
		obs_data_set_string(data, "chain", obs_data_get_string(old, "chain"));
	obs_data_set_obj(data, "routes", routes);

	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	if (!obs_data_save_json_safe(data, path.c_str(), "tmp", "bak"))
		obs_log(LOG_WARNING, "master bus: could not save %s", path.c_str());

	obs_data_release(data);
	obs_data_release(routes);
	obs_data_release(old);
}

void MasterBus::saveSoon()
{
	// Controls like the fader change many times a second; write once they settle.
	if (!saveTimer_) {
		saveTimer_ = new QTimer();
		saveTimer_->setSingleShot(true);
		saveTimer_->setInterval(800);
		QObject::connect(saveTimer_, &QTimer::timeout, [this] { save(); });
	}
	saveTimer_->start();
}

void MasterBus::checkLatency()
{
	if (!source_)
		return;
	const int latency = bus::Engine::get().latencySamples();
	if (latency == lastLatency_)
		return;
	obs_log(LOG_INFO, "master bus: chain latency now %d samples; resyncing", latency);
	lastLatency_ = latency;
	applyOutputMixers();
}

void MasterBus::unload()
{
	if (!loaded_)
		return;
	if (saveTimer_) {
		saveTimer_->stop();
		delete saveTimer_;
		saveTimer_ = nullptr;
	}
	save();
	stop();
	bus::Engine::get().shutdown();
	loaded_ = false;
}

// ------------------------------------------------------------------ running

void MasterBus::start()
{
	if (source_)
		return;
	obs_audio_info oai = {};
	if (obs_get_audio_info(&oai) && oai.samples_per_sec > 0)
		sampleRate_ = oai.samples_per_sec;
	left_.assign(AUDIO_OUTPUT_FRAMES * 4, 0.0f);
	right_.assign(AUDIO_OUTPUT_FRAMES * 4, 0.0f);
	bus::Engine::get().prepare(static_cast<double>(sampleRate_), AUDIO_OUTPUT_FRAMES);

	source_ = obs_source_create_private(kSourceId, "OBSViewer Master Bus", nullptr);
	if (!source_) {
		obs_log(LOG_WARNING, "master bus: could not create the bus source");
		return;
	}
	obs_source_set_monitoring_type(source_, OBS_MONITORING_TYPE_NONE);
	applyOutputMixers();
	obs_set_output_source(kChannel, source_);

	audio_convert_info conv = {};
	conv.samples_per_sec = sampleRate_;
	conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
	conv.speakers = SPEAKERS_STEREO;
	conv.allow_clipping = true;
	attachedTrack_ = static_cast<size_t>(config_.sendTrack - 1);
	obs_add_raw_audio_callback(attachedTrack_, &conv, &MasterBus::onMix, this);

	obs_log(LOG_INFO, "master bus: running (send Track %d, output mask 0x%x, %u Hz)", config_.sendTrack,
		config_.outputMask, sampleRate_);
}

void MasterBus::stop()
{
	if (attachedTrack_ != SIZE_MAX) {
		obs_remove_raw_audio_callback(attachedTrack_, &MasterBus::onMix, this);
		attachedTrack_ = SIZE_MAX;
	}
	if (source_) {
		obs_set_output_source(kChannel, nullptr);
		obs_source_release(source_);
		source_ = nullptr;
		obs_log(LOG_INFO, "master bus: stopped");
	}
}

void MasterBus::applyOutputMixers()
{
	if (source_)
		obs_source_set_audio_mixers(source_, config_.outputMask);
	// No negative sync offset: the bus's audio already arrives one mix block after
	// the block it came from, and anything stamped earlier than the block OBS is
	// mixing is discarded (once OBS's audio buffering is at its limit), which
	// silenced the bus. Plugin latency is shown in the strip instead.
	if (source_) {
		obs_source_set_sync_offset(source_, 0);
		lastLatency_ = bus::Engine::get().latencySamples();
	}
}

void MasterBus::onMix(void *param, size_t, struct audio_data *data)
{
	auto *self = static_cast<MasterBus *>(param);
	obs_source_t *src = self->source_;
	if (!src || !data || !data->data[0] || data->frames == 0)
		return;
	const uint32_t frames = std::min<uint32_t>(data->frames, static_cast<uint32_t>(self->left_.size()));
	std::memcpy(self->left_.data(), data->data[0], frames * sizeof(float));
	std::memcpy(self->right_.data(), data->data[1] ? data->data[1] : data->data[0], frames * sizeof(float));

	bus::Engine::get().process(self->left_.data(), self->right_.data(), static_cast<int>(frames));

	obs_source_audio out = {};
	out.data[0] = reinterpret_cast<const uint8_t *>(self->left_.data());
	out.data[1] = reinterpret_cast<const uint8_t *>(self->right_.data());
	out.frames = frames;
	out.speakers = SPEAKERS_STEREO;
	out.format = AUDIO_FORMAT_FLOAT_PLANAR;
	out.samples_per_sec = self->sampleRate_;
	// The raw callback runs after OBS has mixed this block, so the block's own
	// timestamp is already in the past: stamped with it, the audio is "late",
	// OBS keeps adding audio buffering until it hits the maximum, then drops it.
	// Stamp it for the next block instead, which OBS mixes on its next tick: the
	// bus is one block (1024 samples, ~21 ms at 48 kHz) behind, every time.
	out.timestamp = data->timestamp +
			util_mul_div64(frames, 1000000000ULL, self->sampleRate_ ? self->sampleRate_ : 48000);
	obs_source_output_audio(src, &out);
}

bool MasterBus::setEnabled(bool enabled, std::string &error)
{
	if (enabled == config_.enabled && enabled == running())
		return true;
	if (enabled && !bus::Engine::get().ready()) {
		error = "The plugin host didn't start.";
		return false;
	}
	config_.enabled = enabled;
	if (enabled) {
		start();
	} else {
		// Turning the bus off puts every source back where it was.
		restoreAllRouting();
		stop();
	}
	save();
	return true;
}

void MasterBus::setSendTrack(int track)
{
	track = std::clamp(track, 1, 6);
	if (track == config_.sendTrack)
		return;
	const uint32_t newBit = 1u << (track - 1);
	config_.sendTrack = track;
	config_.outputMask &= ~newBit;
	if (!config_.outputMask)
		config_.outputMask = track == 1 ? 2 : 1;
	// Re-route existing sources onto the new send track.
	reapplyRouting();
	if (running()) {
		stop();
		start();
	}
	save();
}

void MasterBus::setOutputMask(uint32_t mask)
{
	mask &= 0x3F;
	mask &= ~(1u << (config_.sendTrack - 1)); // never feed the bus back into itself
	if (!mask)
		return;
	config_.outputMask = mask;
	applyOutputMixers();
	// Routed sources come off every output track (a track added here would
	// otherwise get them twice: dry and through the bus).
	reapplyRouting();
	save();
}

void MasterBus::reapplyRouting()
{
	const uint32_t sendBit = 1u << (config_.sendTrack - 1);
	for (const auto &[uuid, original] : originalMixers_) {
		obs_source_t *s = obs_get_source_by_uuid(uuid.c_str());
		if (!s)
			continue;
		obs_source_set_audio_mixers(s, (original & ~config_.outputMask) | sendBit);
		obs_source_release(s);
	}
}

int MasterBus::streamTrack()
{
	return Metrics::streamTrackIndex() + 1;
}

bool MasterBus::reachesStream() const
{
	return busReachesTrack(config_.sendTrack, config_.outputMask, streamTrack());
}

void MasterBus::matchStreamTrack()
{
	const int stream = streamTrack();
	const BusTracks t = busTracksFor(config_.sendTrack, config_.outputMask, stream);
	obs_log(LOG_INFO, "master bus: matching the stream's Track %d (send Track %d, outputs 0x%x)", stream,
		t.sendTrack, t.outputMask);
	if (t.sendTrack != config_.sendTrack)
		setSendTrack(t.sendTrack);
	setOutputMask(t.outputMask);
}

// ------------------------------------------------------------------ routing

std::vector<MasterBus::SourceRow> MasterBus::audioSources() const
{
	struct Ctx {
		const MasterBus *bus;
		std::vector<SourceRow> rows;
	} ctx{this, {}};
	obs_enum_sources(
		[](void *param, obs_source_t *s) {
			auto *c = static_cast<Ctx *>(param);
			if (!(obs_source_get_output_flags(s) & OBS_SOURCE_AUDIO))
				return true;
			SourceRow row;
			row.uuid = obs_source_get_uuid(s);
			row.name = obs_source_get_name(s);
			row.mixers = obs_source_get_audio_mixers(s);
			row.routed = c->bus->originalMixers_.count(row.uuid) > 0;
			c->rows.push_back(std::move(row));
			return true;
		},
		&ctx);
	std::sort(ctx.rows.begin(), ctx.rows.end(), [](const auto &a, const auto &b) { return a.name < b.name; });
	return ctx.rows;
}

void MasterBus::setRouted(const std::string &uuid, bool routed)
{
	obs_source_t *s = obs_get_source_by_uuid(uuid.c_str());
	if (!s)
		return;
	const uint32_t sendBit = 1u << (config_.sendTrack - 1);
	const auto it = originalMixers_.find(uuid);
	if (routed && it == originalMixers_.end()) {
		const uint32_t original = obs_source_get_audio_mixers(s);
		originalMixers_[uuid] = original;
		// Off the output tracks, onto the send track: the bus now carries it.
		obs_source_set_audio_mixers(s, (original & ~config_.outputMask) | sendBit);
		obs_log(LOG_INFO, "master bus: routing '%s'", obs_source_get_name(s));
	} else if (!routed && it != originalMixers_.end()) {
		obs_source_set_audio_mixers(s, it->second);
		originalMixers_.erase(it);
		obs_log(LOG_INFO, "master bus: un-routing '%s'", obs_source_get_name(s));
	}
	obs_source_release(s);
	save();
}

void MasterBus::routeAll(bool routed)
{
	for (const auto &row : audioSources())
		setRouted(row.uuid, routed);
}

int MasterBus::routedCount() const
{
	return static_cast<int>(originalMixers_.size());
}

void MasterBus::restoreAllRouting()
{
	for (const auto &[uuid, mixers] : originalMixers_) {
		obs_source_t *s = obs_get_source_by_uuid(uuid.c_str());
		if (!s)
			continue;
		obs_source_set_audio_mixers(s, mixers);
		obs_source_release(s);
	}
	originalMixers_.clear();
}

} // namespace obsviewer
