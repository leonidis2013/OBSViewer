#pragma once

#include <obs.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace obsviewer {

// The master bus inside OBS.
//
// Routing model (all with OBS's own audio tracks):
//   sources you route  ──►  send track (default Track 6, nothing else uses it)
//   send track mix  ──►  plugin chain  ──►  hidden "Master Bus" source  ──►  output
//   tracks (default Track 1 = what the stream and recording send)
//
// Routing a source moves it from the output tracks to the send track; the original
// track assignment is remembered and put back when you un-route it or turn the bus
// off. Track assignments are saved with the scene collection like any OBS setting.
class MasterBus {
public:
	static MasterBus &get();

	static void registerSource(); // obs_module_load

	struct Config {
		bool enabled = false;
		int sendTrack = 6;      // 1-6
		uint32_t outputMask = 1; // bit per track, default Track 1
	};

	// Main thread.
	void load();     // read config, start if enabled (after OBS finished loading)
	void unload();   // OBS exiting: stop and drop references (routing stays saved)
	bool setEnabled(bool enabled, std::string &error);
	bool enabled() const { return config_.enabled; }
	const Config &config() const { return config_; }
	void setSendTrack(int track);
	void setOutputMask(uint32_t mask);

	struct SourceRow {
		std::string uuid;
		std::string name;
		bool routed = false;
		uint32_t mixers = 0;
	};
	std::vector<SourceRow> audioSources() const;
	void setRouted(const std::string &uuid, bool routed);
	void routeAll(bool routed);
	int routedCount() const;

	// Save the plugin chain and routing memory now.
	void save() const;
	// After adding / removing / reordering plugins: update latency sync and save.
	void chainChanged();
	// Scene collection switching (routing memory is per collection).
	void collectionChanging();
	void collectionChanged();
	bool running() const { return source_ != nullptr; }

private:
	MasterBus() = default;
	void start();
	void stop();
	void restoreAllRouting();
	void loadRoutes(obs_data_t *data);
	void applyOutputMixers();
	static void onMix(void *param, size_t mixIdx, struct audio_data *data);

	Config config_;
	std::map<std::string, uint32_t> originalMixers_; // uuid → mixers before routing
	obs_source_t *source_ = nullptr;
	size_t attachedTrack_ = SIZE_MAX;
	uint32_t sampleRate_ = 48000;
	std::vector<float> left_, right_;
	bool loaded_ = false;
};

} // namespace obsviewer
