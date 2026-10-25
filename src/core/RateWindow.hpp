#pragma once

#include <cstdint>
#include <deque>
#include <utility>

namespace obsviewer {

// Average data rate over a sliding time window. Streams send in bursts (each
// keyframe is a big one, every 2 s or so), so a rate from one-second samples
// jumps around; averaged over a few seconds it shows the real upload speed.
class RateWindow {
public:
	explicit RateWindow(double windowSec = 5.0) : windowNs_(static_cast<uint64_t>(windowSec * 1e9)) {}

	void reset() { samples_.clear(); }

	// A running byte total at time t (ns). A total that goes backwards (the
	// output restarted) starts the window over.
	void push(uint64_t tNs, uint64_t totalBytes)
	{
		if (!samples_.empty() && (totalBytes < samples_.back().second || tNs <= samples_.back().first))
			samples_.clear();
		samples_.emplace_back(tNs, totalBytes);
		// Keep one sample at or before the window start, so the span covers it.
		while (samples_.size() > 2 && samples_[1].first + windowNs_ <= tNs)
			samples_.pop_front();
	}

	// kbit/s over the window (0 until there are two samples).
	double kbps() const
	{
		if (samples_.size() < 2)
			return 0.0;
		const auto &a = samples_.front();
		const auto &b = samples_.back();
		const double sec = static_cast<double>(b.first - a.first) / 1e9;
		return sec > 0.0 ? static_cast<double>(b.second - a.second) * 8.0 / 1000.0 / sec : 0.0;
	}

private:
	uint64_t windowNs_;
	std::deque<std::pair<uint64_t, uint64_t>> samples_;
};

} // namespace obsviewer
