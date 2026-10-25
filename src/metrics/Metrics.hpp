#pragma once

#include "audio/ProgramLoudness.hpp"
#include "core/Health.hpp"
#include "core/History.hpp"

#include <QObject>

#include <cstdint>
#include <string>

class QTimer;
struct os_cpu_usage_info;

namespace obsviewer {

// One second of stream history.
struct BitrateSample {
	double kbps = 0.0;
	int dropped = 0;        // network frames dropped during this second
	bool reconnecting = false;
	bool active = false;    // stream was live
};

// Stream, recording, system and audio metrics, sampled once per second on the
// Qt main thread. Views read it and repaint on updated().
class Metrics : public QObject {
	Q_OBJECT

public:
	static Metrics &get();

	void start();
	void stop();

	// ---- latest values ----
	struct Stream {
		bool active = false;
		bool reconnecting = false;
		double kbps = 0.0;
		uint64_t totalBytes = 0;
		int framesDropped = 0;
		int framesTotal = 0;
		double dropPct = 0.0;
		int droppedRecent = 0; // last 10 s
		int reconnects = 0;
		int connectMs = -1;
		float congestion = 0.0f;
		double uptimeSec = 0.0;
	} stream;

	struct Record {
		bool active = false;
		bool paused = false;
		double durationSec = 0.0;
		double kbps = 0.0;
		double diskFreeGB = -1.0;
		double hoursLeft = -1.0;
		uint64_t bytes = 0;    // size of the recording so far
		std::string fileName; // file being written (name only), empty if unknown
	} record;

	struct System {
		double cpuPct = -1.0;
		double renderLagPct = 0.0;   // last 10 s
		double encoderSkipPct = 0.0; // last 10 s
		uint32_t renderLaggedTotal = 0;
		uint32_t encoderSkippedTotal = 0;
		double fps = 0.0;
		double memoryMB = 0.0;
	} system;

	ProgramLoudness::Snapshot audio; // refreshed by refreshAudio(), ~20 Hz
	double silentSeconds = 0.0;
	// Program picture (PictureMonitor), seconds black / unchanged.
	bool pictureKnown = false;
	double blackSeconds = 0.0;
	double stillSeconds = 0.0;

	History<BitrateSample> bitrateHistory{300};
	History<float> loudnessHistory{300}; // short-term LUFS once per second

	HealthReport health;

	struct Perf {
		double fpsTarget = 30.0;
		double fpsActive = 0.0;
		double renderMs = 0.0;
	} perf;
	HealthReport performance;

	int measuredTrack = 1; // 1-based OBS track the loudness meter is on

	// Loudness target from Settings.
	float targetLufs() const;
	float toleranceLu() const;
	float ceilingDbtp() const;
	QString targetName() const;

	// Cheap; called by the meter widget on its own fast timer.
	void refreshAudio();

	// 0-based mix index the stream sends (its audio encoder, else the profile).
	static int streamTrackIndex();

signals:
	void updated();

private:
	Metrics() = default;
	void sample();
	size_t meterTrackIndex() const;

	QTimer *timer_ = nullptr;
	os_cpu_usage_info *cpu_ = nullptr;

	// previous counters
	uint64_t lastStreamBytes_ = 0;
	uint64_t lastRecordBytes_ = 0;
	uint64_t lastSampleNs_ = 0;
	uint64_t streamStartNs_ = 0;
	bool wasReconnecting_ = false;
	bool wasStreaming_ = false;
	int lastDropped_ = 0;
	History<int> dropWindow_{10};
	History<uint32_t> lagWindow_{11};
	History<uint32_t> renderTotalWindow_{11};
	History<uint32_t> skipWindow_{11};
	History<uint32_t> encodeTotalWindow_{11};
	std::string recordDir_;
	int recordDirCheck_ = 0;
};

} // namespace obsviewer
