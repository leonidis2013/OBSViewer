#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace obsviewer {

enum class Level { Off = 0, Good = 1, Warn = 2, Bad = 3 };

// Everything the health checks look at, gathered once per second.
struct HealthInputs {
	bool streaming = false;
	bool reconnecting = false;
	int reconnects = 0;            // since the stream started
	double networkDropPct = 0.0;   // whole stream
	int networkDropsRecent = 0;    // frames dropped in the last 10 s
	double encoderSkipPct = 0.0;   // recent
	double renderLagPct = 0.0;     // recent
	double cpuPct = -1.0;          // < 0 = unknown
	bool recording = false;
	double diskFreeGB = -1.0;      // < 0 = unknown
	double recordHoursLeft = -1.0; // < 0 = unknown
	bool audioActive = false;      // meter is receiving audio
	float shortTermLufs = -120.0f;
	float integratedLufs = -120.0f;
	float truePeakRecentDb = -120.0f; // last few seconds
	double silentSeconds = 0.0;       // short-term below the silence floor
	float targetLufs = -14.0f;
	float toleranceLu = 1.0f;
	float ceilingDbtp = -1.0f;
	bool pictureKnown = false;    // Program picture is being sampled
	double blackSeconds = 0.0;    // Program has been (near) black this long
	double stillSeconds = 0.0;    // Program hasn't changed this long
	bool warnFrozen = true;       // Settings: warn about a frozen picture
	// Master bus: does its output reach the track the stream sends?
	bool busActive = false;       // running with at least one source routed
	int busSendTrack = 6;         // 1-6
	uint32_t busOutputMask = 1;   // bit per track
	int streamTrack = 1;          // 1-6, the track the stream encodes
};

// True if the bus's output lands on `track` (1-6) and that track isn't the bus's
// own (unprocessed) send track.
bool busReachesTrack(int sendTrack, uint32_t outputMask, int track);

// Bus settings that put its output on the stream track, keeping the tracks it
// already feeds. If the stream sends the bus's send track, the send moves to
// the highest free track.
struct BusTracks {
	int sendTrack = 6;
	uint32_t outputMask = 1;
};
BusTracks busTracksFor(int sendTrack, uint32_t outputMask, int streamTrack);

// Picture thresholds (also used by the sampler).
constexpr double kBlackWarnSec = 3.0;   // black this long → warning
constexpr double kBlackBadSec = 5.0;    // … while live → problem
constexpr double kFrozenWarnSec = 60.0; // no change this long while live → warning

struct HealthCheck {
	std::string name;   // "Network"
	Level level = Level::Off;
	std::string detail; // "12 frames dropped (0.4%)"
};

struct HealthReport {
	Level overall = Level::Off;
	std::vector<HealthCheck> checks;

	// Worst level among checks of these names (Off if none present).
	Level levelOf(const std::string &name) const;
};

HealthReport evaluateHealth(const HealthInputs &in);

// Frame rate and encoding, shown behind the second (Performance) button.
struct PerfInputs {
	double fpsTarget = 30.0;
	double fpsActive = 0.0;
	double renderMs = 0.0;        // average time to render a frame
	double renderLagPct = 0.0;    // last 10 s
	double encoderSkipPct = 0.0;  // last 10 s
	bool encoding = false;        // stream or recording active
	double cpuPct = -1.0;
	double memoryMB = 0.0;
	std::string streamEncoder;    // "Apple VT H264 Hardware · 1920×1080 · 6000 kbps", empty when idle
	std::string recordEncoder;
};

HealthReport evaluatePerformance(const PerfInputs &in);

const char *levelName(Level level);

} // namespace obsviewer
