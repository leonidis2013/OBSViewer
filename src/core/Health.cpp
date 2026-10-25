#include "core/Health.hpp"

#include <cmath>
#include <cstdio>

namespace obsviewer {

namespace {

std::string fmt(const char *format, double a)
{
	char buf[128];
	std::snprintf(buf, sizeof(buf), format, a);
	return buf;
}

std::string fmt2(const char *format, double a, double b)
{
	char buf[128];
	std::snprintf(buf, sizeof(buf), format, a, b);
	return buf;
}

} // namespace

const char *levelName(Level level)
{
	switch (level) {
	case Level::Good:
		return "Good";
	case Level::Warn:
		return "Warning";
	case Level::Bad:
		return "Problem";
	default:
		return "Off";
	}
}

Level HealthReport::levelOf(const std::string &name) const
{
	Level worst = Level::Off;
	for (const auto &c : checks)
		if (c.name == name && c.level > worst)
			worst = c.level;
	return worst;
}

HealthReport evaluateHealth(const HealthInputs &in)
{
	HealthReport r;
	auto add = [&r](const char *name, Level level, std::string detail) {
		r.checks.push_back(HealthCheck{name, level, std::move(detail)});
	};

	// ---- Stream connection ----
	if (!in.streaming)
		add("Stream", Level::Off, "Not streaming");
	else if (in.reconnecting)
		add("Stream", Level::Bad, "Reconnecting…");
	else if (in.reconnects > 0)
		add("Stream", Level::Warn,
		    in.reconnects == 1 ? std::string("Connected · 1 reconnect") : fmt("Connected · %.0f reconnects", in.reconnects));
	else
		add("Stream", Level::Good, "Connected");

	// ---- Network drops ----
	if (!in.streaming) {
		add("Network", Level::Off, "Not streaming");
	} else {
		Level l = Level::Good;
		if (in.networkDropPct >= 2.0 || in.networkDropsRecent >= 30)
			l = Level::Bad;
		else if (in.networkDropsRecent > 0 || in.networkDropPct >= 0.5)
			l = Level::Warn;
		std::string d = in.networkDropPct > 0.0 ? fmt("%.1f%% of frames dropped", in.networkDropPct)
							: std::string("No dropped frames");
		if (in.networkDropsRecent > 0)
			d += fmt(" · %.0f in last 10 s", in.networkDropsRecent);
		add("Network", l, d);
	}

	// ---- Encoder ----
	if (!in.streaming && !in.recording) {
		add("Encoder", Level::Off, "Idle");
	} else {
		const Level l = in.encoderSkipPct >= 5.0 ? Level::Bad : in.encoderSkipPct >= 0.5 ? Level::Warn : Level::Good;
		add("Encoder", l,
		    in.encoderSkipPct > 0.0 ? fmt("%.1f%% frames skipped (overloaded)", in.encoderSkipPct)
					    : std::string("Keeping up"));
	}

	// ---- Rendering ----
	{
		const Level l = in.renderLagPct >= 5.0 ? Level::Bad : in.renderLagPct >= 1.0 ? Level::Warn : Level::Good;
		add("Rendering", l,
		    in.renderLagPct > 0.0 ? fmt("%.1f%% frames missed (GPU busy)", in.renderLagPct)
					  : std::string("Keeping up"));
	}

	// ---- CPU ----
	if (in.cpuPct < 0.0) {
		add("CPU", Level::Off, "Unknown");
	} else {
		const Level l = in.cpuPct >= 90.0 ? Level::Bad : in.cpuPct >= 75.0 ? Level::Warn : Level::Good;
		add("CPU", l, fmt("OBS using %.0f%%", in.cpuPct));
	}

	// ---- Disk ----
	if (in.diskFreeGB < 0.0) {
		add("Disk", Level::Off, "Recording folder not found");
	} else {
		Level l = in.diskFreeGB < 5.0 ? Level::Bad : in.diskFreeGB < 20.0 ? Level::Warn : Level::Good;
		std::string d = fmt("%.0f GB free", in.diskFreeGB);
		if (in.recording && in.recordHoursLeft >= 0.0) {
			d += fmt(" · about %.1f h of recording left", in.recordHoursLeft);
			if (in.recordHoursLeft < 1.0)
				l = Level::Bad;
			else if (in.recordHoursLeft < 3.0 && l < Level::Warn)
				l = Level::Warn;
		}
		add("Disk", l, d);
	}

	// ---- Audio loudness ----
	if (!in.audioActive) {
		add("Loudness", Level::Off, "No audio");
	} else {
		const bool haveIntegrated = in.integratedLufs > -70.0f;
		const float value = haveIntegrated ? in.integratedLufs : in.shortTermLufs;
		if (value <= -70.0f) {
			add("Loudness", Level::Off, "Waiting for programme audio");
		} else {
			const float diff = value - in.targetLufs;
			Level l = std::fabs(diff) <= in.toleranceLu ? Level::Good : Level::Warn;
			if (diff > in.toleranceLu + 3.0f)
				l = Level::Bad;
			add("Loudness", l,
			    fmt2(haveIntegrated ? "%.1f LUFS integrated · target %.0f" : "%.1f LUFS short-term · target %.0f",
				 value, in.targetLufs));
		}
	}

	// ---- Peaks ----
	if (!in.audioActive) {
		add("Peaks", Level::Off, "No audio");
	} else if (in.truePeakRecentDb > 0.0f) {
		add("Peaks", Level::Bad, fmt("Clipping (%+.1f dBTP)", in.truePeakRecentDb));
	} else if (in.truePeakRecentDb > in.ceilingDbtp) {
		add("Peaks", Level::Warn, fmt2("%.1f dBTP · above the %.0f ceiling", in.truePeakRecentDb, in.ceilingDbtp));
	} else {
		add("Peaks", Level::Good,
		    in.truePeakRecentDb <= -119.0f ? std::string("Under the ceiling")
						   : fmt("%.1f dBTP · under the ceiling", in.truePeakRecentDb));
	}

	// ---- Silence ----
	if (!in.audioActive) {
		add("Silence", Level::Off, "No audio");
	} else if (in.silentSeconds >= 10.0) {
		add("Silence", in.streaming ? Level::Bad : Level::Warn, fmt("Silent for %.0f s", in.silentSeconds));
	} else {
		add("Silence", Level::Good, "Audio present");
	}

	// ---- Picture (Program black or frozen) ----
	const bool live = in.streaming || in.recording;
	if (!in.pictureKnown) {
		add("Picture", Level::Off, "Not checked yet");
	} else if (in.blackSeconds >= kBlackWarnSec) {
		const Level l = live && in.blackSeconds >= kBlackBadSec ? Level::Bad : Level::Warn;
		add("Picture", l, fmt("Program is black (%.0f s)", in.blackSeconds));
	} else if (in.warnFrozen && live && in.stillSeconds >= kFrozenWarnSec) {
		add("Picture", Level::Warn, fmt("Program hasn't changed for %.0f s (frozen?)", in.stillSeconds));
	} else {
		add("Picture", Level::Good, "Program picture OK");
	}

	// ---- Master bus reaches the stream ----
	if (in.busActive) {
		if (busReachesTrack(in.busSendTrack, in.busOutputMask, in.streamTrack))
			add("Master bus", Level::Good, fmt("Reaches the stream (Track %.0f)", in.streamTrack));
		else
			add("Master bus", in.streaming ? Level::Bad : Level::Warn,
			    fmt("Not on the stream's Track %.0f: routed sources are missing (fix in the bus)", in.streamTrack));
	}

	for (const auto &c : r.checks)
		if (c.level > r.overall)
			r.overall = c.level;
	return r;
}

bool busReachesTrack(int sendTrack, uint32_t outputMask, int track)
{
	if (track < 1 || track > 6 || track == sendTrack)
		return false;
	return (outputMask & (1u << (track - 1))) != 0;
}

BusTracks busTracksFor(int sendTrack, uint32_t outputMask, int streamTrack)
{
	BusTracks t{sendTrack, outputMask & 0x3Fu};
	if (streamTrack < 1 || streamTrack > 6)
		return t;
	if (t.sendTrack == streamTrack) {
		// Highest track not already used; if every track is, the highest other one
		// (it stops being an output).
		t.sendTrack = streamTrack == 6 ? 5 : 6;
		for (int c = 6; c >= 1; --c)
			if (c != streamTrack && !(t.outputMask & (1u << (c - 1)))) {
				t.sendTrack = c;
				break;
			}
	}
	t.outputMask |= 1u << (streamTrack - 1);
	t.outputMask &= ~(1u << (t.sendTrack - 1));
	return t;
}

HealthReport evaluatePerformance(const PerfInputs &in)
{
	HealthReport r;
	auto add = [&r](const char *name, Level level, std::string detail) {
		r.checks.push_back(HealthCheck{name, level, std::move(detail)});
	};

	// ---- frame rate ----
	if (in.fpsTarget <= 0.0) {
		add("Frame rate", Level::Off, "Unknown");
	} else {
		const double ratio = in.fpsActive / in.fpsTarget;
		const Level l = ratio >= 0.97 ? Level::Good : ratio >= 0.90 ? Level::Warn : Level::Bad;
		add("Frame rate", l, fmt2("%.2f of %.2f fps", in.fpsActive, in.fpsTarget));
	}

	// ---- render time ----
	if (in.fpsTarget > 0.0) {
		const double budget = 1000.0 / in.fpsTarget;
		const double share = in.renderMs / budget;
		const Level l = share < 0.6 ? Level::Good : share < 0.9 ? Level::Warn : Level::Bad;
		add("Render time", l, fmt2("%.1f ms of %.1f ms per frame", in.renderMs, budget));
	}

	// ---- missed / skipped frames ----
	{
		const Level l = in.renderLagPct >= 5.0 ? Level::Bad : in.renderLagPct >= 1.0 ? Level::Warn : Level::Good;
		add("Rendering", l,
		    in.renderLagPct > 0.0 ? fmt("%.1f%% frames missed (GPU busy)", in.renderLagPct)
					  : std::string("No missed frames"));
	}
	if (!in.encoding) {
		add("Encoding", Level::Off, "Idle");
	} else {
		const Level l = in.encoderSkipPct >= 5.0 ? Level::Bad : in.encoderSkipPct >= 0.5 ? Level::Warn : Level::Good;
		add("Encoding", l,
		    in.encoderSkipPct > 0.0 ? fmt("%.1f%% frames skipped (encoder overloaded)", in.encoderSkipPct)
					    : std::string("No skipped frames"));
	}

	// ---- encoders (information) ----
	add("Stream encoder", in.streamEncoder.empty() ? Level::Off : Level::Good,
	    in.streamEncoder.empty() ? std::string("Not streaming") : in.streamEncoder);
	add("Record encoder", in.recordEncoder.empty() ? Level::Off : Level::Good,
	    in.recordEncoder.empty() ? std::string("Not recording") : in.recordEncoder);

	// ---- system ----
	if (in.cpuPct < 0.0) {
		add("CPU", Level::Off, "Unknown");
	} else {
		const Level l = in.cpuPct >= 90.0 ? Level::Bad : in.cpuPct >= 75.0 ? Level::Warn : Level::Good;
		add("CPU", l, fmt("OBS using %.0f%%", in.cpuPct));
	}
	add("Memory", in.memoryMB > 0.0 ? Level::Good : Level::Off,
	    in.memoryMB >= 1024.0 ? fmt("%.1f GB", in.memoryMB / 1024.0) : fmt("%.0f MB", in.memoryMB));

	for (const auto &c : r.checks)
		if (c.level > r.overall)
			r.overall = c.level;
	return r;
}

} // namespace obsviewer
