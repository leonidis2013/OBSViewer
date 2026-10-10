#include "audio/ProgramLoudness.hpp"

#include <obs.h>
#include <util/platform.h>

#include <plugin-support.h>

#include <algorithm>
#include <cmath>

namespace obsviewer {

namespace {

float toDb(float lin)
{
	return lin > 1.0e-6f ? 20.0f * std::log10(lin) : -120.0f;
}

// Sample-peak ballistics: instant attack, ~20 dB/s fall at 48 kHz blocks.
void updatePeak(std::atomic<float> &slot, const float *x, uint32_t frames, double sampleRate)
{
	float block = 0.0f;
	for (uint32_t i = 0; i < frames; ++i)
		block = std::max(block, std::fabs(x[i]));
	const float decay =
		static_cast<float>(std::pow(10.0, -20.0 / 20.0 * static_cast<double>(frames) / std::max(1.0, sampleRate)));
	const float held = slot.load(std::memory_order_relaxed) * decay;
	slot.store(std::max(block, held), std::memory_order_relaxed);
}

} // namespace

ProgramLoudness &ProgramLoudness::get()
{
	static ProgramLoudness instance;
	return instance;
}

void ProgramLoudness::start(size_t track)
{
	std::lock_guard<std::mutex> lock(lifecycle_);
	if (running_ && track == track_)
		return;
	if (running_)
		obs_remove_raw_audio_callback(track_, &ProgramLoudness::onAudio, this);

	obs_audio_info oai = {};
	if (obs_get_audio_info(&oai) && oai.samples_per_sec > 0)
		sampleRate_ = static_cast<double>(oai.samples_per_sec);

	loudness_.prepare(sampleRate_);
	truePeak_.prepare(sampleRate_);
	peakL_ = 0.0f;
	peakR_ = 0.0f;

	audio_convert_info conv = {};
	conv.samples_per_sec = static_cast<uint32_t>(sampleRate_);
	conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
	conv.speakers = SPEAKERS_STEREO;
	conv.allow_clipping = true; // measure real overs instead of hiding them

	track_ = std::min<size_t>(track, MAX_AUDIO_MIXES - 1);
	obs_add_raw_audio_callback(track_, &conv, &ProgramLoudness::onAudio, this);
	running_ = true;
	obs_log(LOG_INFO, "loudness: measuring Track %zu at %.0f Hz", track_ + 1, sampleRate_);
}

void ProgramLoudness::stop()
{
	std::lock_guard<std::mutex> lock(lifecycle_);
	if (!running_)
		return;
	obs_remove_raw_audio_callback(track_, &ProgramLoudness::onAudio, this);
	running_ = false;
}

void ProgramLoudness::resetIntegrated()
{
	loudness_.requestReset();
	truePeak_.requestReset();
}

void ProgramLoudness::onAudio(void *param, size_t, struct audio_data *data)
{
	if (!data || !data->data[0] || data->frames == 0)
		return;
	auto *self = static_cast<ProgramLoudness *>(param);
	const auto *l = reinterpret_cast<const float *>(data->data[0]);
	const auto *r = data->data[1] ? reinterpret_cast<const float *>(data->data[1]) : l;
	self->process(l, r, data->frames);
}

void ProgramLoudness::process(const float *l, const float *r, uint32_t frames)
{
	loudness_.process(l, r, static_cast<int>(frames));
	truePeak_.process(l, r, static_cast<int>(frames));
	updatePeak(peakL_, l, frames, sampleRate_);
	updatePeak(peakR_, r, frames, sampleRate_);
	lastAudioNs_.store(os_gettime_ns(), std::memory_order_relaxed);
}

ProgramLoudness::Snapshot ProgramLoudness::snapshot() const
{
	Snapshot s;
	const uint64_t last = lastAudioNs_.load(std::memory_order_relaxed);
	s.active = last != 0 && os_gettime_ns() - last < 1000000000ULL;
	s.momentary = loudness_.momentary();
	s.shortTerm = loudness_.shortTerm();
	s.integrated = loudness_.integrated();
	s.range = loudness_.range();
	s.truePeakRecent = truePeak_.recentDb();
	s.truePeakMax = truePeak_.holdDb();
	s.truePeak3s = truePeak_.window3sDb();
	s.peakL = toDb(peakL_.load(std::memory_order_relaxed));
	s.peakR = toDb(peakR_.load(std::memory_order_relaxed));
	return s;
}

} // namespace obsviewer
