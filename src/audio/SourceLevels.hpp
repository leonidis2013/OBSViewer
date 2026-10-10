#pragma once

#include "core/LevelMath.hpp"

#include <obs.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

namespace obsviewer {

// Live level of one audio source, written by its OBS volume meter (audio thread)
// and read by the multiview renderer (graphics thread).
struct LevelEntry {
	std::atomic<float> peak[2];
	std::atomic<float> rms[2];
	std::atomic<uint64_t> lastNs{0};
	std::atomic<uint64_t> clipNs{0};
	std::atomic<bool> muted{false};

	LevelEntry()
	{
		for (int c = 0; c < 2; ++c) {
			peak[c] = levels::kFloorDb;
			rms[c] = levels::kFloorDb;
		}
	}

	levels::SourceReading read(uint64_t nowNs) const;
};

// Keeps one OBS volume meter per audio source that appears in any scene.
// Main thread: beginSync / touch / endSync during each refresh.
class SourceLevels {
public:
	static SourceLevels &get();

	void beginSync();
	// Returns the entry for this audio source, creating its meter if needed.
	std::shared_ptr<LevelEntry> touch(obs_source_t *source);
	// Destroys meters for sources not touched since beginSync().
	void endSync();
	void clear();

private:
	SourceLevels() = default;

	struct Meter {
		obs_volmeter_t *volmeter = nullptr;
		std::shared_ptr<LevelEntry> entry;
		bool seen = false;
	};
	static void onLevels(void *param, const float magnitude[MAX_AUDIO_CHANNELS],
			     const float peak[MAX_AUDIO_CHANNELS], const float inputPeak[MAX_AUDIO_CHANNELS]);
	static void destroyMeter(Meter &m);

	std::map<obs_weak_source_t *, Meter> meters_; // key holds a weak reference
};

} // namespace obsviewer
