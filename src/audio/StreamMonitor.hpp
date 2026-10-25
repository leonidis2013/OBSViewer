#pragma once

#include <obs.h>

#include <cstddef>
#include <vector>

namespace obsviewer {

// "Listen to stream": plays the track the stream sends (final mix, master bus
// included) through OBS's monitoring device (Settings ▸ Audio ▸ Advanced ▸
// Monitoring Device).
//
// The stream track's mix is tapped with a raw audio callback and fed to a hidden
// source that is set to "Monitor Only" and is on no track, so it is heard but
// never mixed back into anything. It runs a little behind the live sound (one or
// two audio blocks plus the monitoring device's buffer): fine for checking the
// mix, not for performers' in-ear monitors.
class StreamMonitor {
public:
	static StreamMonitor &get();
	static void registerSource(); // obs_module_load

	// Main thread.
	void setListening(bool on);
	bool listening() const { return source_ != nullptr; }
	int track() const { return static_cast<int>(track_) + 1; } // 1-6 while listening
	void poll();     // follow the stream track if it changes
	void shutdown(); // OBS exiting

private:
	StreamMonitor() = default;
	void start(size_t track);
	void stop();
	static void onMix(void *param, size_t mixIdx, struct audio_data *data);

	obs_source_t *source_ = nullptr;
	size_t track_ = 0;
	uint32_t sampleRate_ = 48000;
};

} // namespace obsviewer
