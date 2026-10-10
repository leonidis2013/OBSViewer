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

	for (const auto &c : r.checks)
		if (c.level > r.overall)
			r.overall = c.level;
	return r;
}

} // namespace obsviewer
