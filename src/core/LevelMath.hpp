#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Meter math shared by the per-tile audio meters. Pure functions, unit-tested.
namespace obsviewer::levels {

constexpr float kFloorDb = -120.0f;
constexpr float kFallDbPerSec = 20.0f;  // peak / RMS fall-back speed
constexpr float kClipDb = -0.5f;        // sample peak at or above this counts as a clip
constexpr double kClipHoldSec = 2.0;    // clip light stays on this long

inline float clean(float db)
{
	return std::isfinite(db) ? std::max(db, kFloorDb) : kFloorDb;
}

// Instant rise, steady fall: held value after `dt` seconds given a new reading.
inline float ballistic(float held, float incoming, double dt)
{
	const float fallen = held - kFallDbPerSec * static_cast<float>(std::max(0.0, dt));
	return std::max(clean(incoming), std::max(fallen, kFloorDb));
}

// One audio source's current reading.
struct SourceReading {
	float peak[2] = {kFloorDb, kFloorDb};
	float rms[2] = {kFloorDb, kFloorDb};
	bool muted = false;
	bool clipped = false; // clipped within the hold time
};

// What a tile shows: the loudest unmuted source per channel.
struct TileLevels {
	bool hasAudio = false; // the scene contains at least one audio source
	bool allMuted = false; // ...and every one of them is muted
	bool clipped = false;  // an unmuted source clipped recently
	float peak[2] = {kFloorDb, kFloorDb};
	float rms[2] = {kFloorDb, kFloorDb};
};

inline TileLevels combine(const std::vector<SourceReading> &sources)
{
	TileLevels t;
	t.hasAudio = !sources.empty();
	if (!t.hasAudio)
		return t;
	t.allMuted = true;
	for (const SourceReading &s : sources) {
		if (s.muted)
			continue;
		t.allMuted = false;
		t.clipped = t.clipped || s.clipped;
		for (int c = 0; c < 2; ++c) {
			t.peak[c] = std::max(t.peak[c], clean(s.peak[c]));
			t.rms[c] = std::max(t.rms[c], clean(s.rms[c]));
		}
	}
	return t;
}

// Position (0 = bottom, 1 = top) on a meter from `minDb` to 0 dBFS.
inline float meterPos(float db, float minDb = -60.0f)
{
	return std::clamp((clean(db) - minDb) / (0.0f - minDb), 0.0f, 1.0f);
}

} // namespace obsviewer::levels
