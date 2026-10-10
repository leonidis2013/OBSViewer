#pragma once

#include "audio/meters/LoudnessMeter.h"
#include "audio/meters/TruePeakMeter.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

struct audio_data;

namespace obsviewer {

// Measures the loudness of one OBS mix track (what the stream hears) using the
// same BS.1770 meter as BroadcastMaster. Audio arrives on OBS's audio thread;
// the getters are safe from the UI thread.
class ProgramLoudness {
public:
	static ProgramLoudness &get();

	struct Snapshot {
		bool active = false; // audio arrived in the last second
		float momentary = -120.0f;
		float shortTerm = -120.0f;
		float integrated = -120.0f;
		float range = 0.0f;
		float truePeakRecent = -120.0f; // decaying true peak, dBTP
		float truePeakMax = -120.0f;    // since reset
		float truePeak3s = -120.0f;     // max over the last ~3 s
		float peakL = -120.0f;          // decaying sample peak per channel, dBFS
		float peakR = -120.0f;
	};

	// track: 0-based mix index (0 = Track 1). Re-attaches if it changed.
	void start(size_t track);
	void stop();
	void resetIntegrated();

	Snapshot snapshot() const;

private:
	ProgramLoudness() = default;
	static void onAudio(void *param, size_t mixIdx, struct audio_data *data);
	void process(const float *l, const float *r, uint32_t frames);

	std::mutex lifecycle_;
	bool running_ = false;
	size_t track_ = 0;
	double sampleRate_ = 48000.0;

	bm::LoudnessMeter loudness_;
	bm::TruePeakMeter truePeak_;
	std::atomic<float> peakL_{0.0f};
	std::atomic<float> peakR_{0.0f};
	std::atomic<uint64_t> lastAudioNs_{0};
};

} // namespace obsviewer
