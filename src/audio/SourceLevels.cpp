#include "audio/SourceLevels.hpp"

#include <obs-audio-controls.h>
#include <util/platform.h>

namespace obsviewer {

levels::SourceReading LevelEntry::read(uint64_t nowNs) const
{
	levels::SourceReading r;
	const uint64_t last = lastNs.load(std::memory_order_relaxed);
	// Let the meter fall naturally if the source stopped sending audio.
	const double idle = last && nowNs > last ? static_cast<double>(nowNs - last) / 1e9 : 0.0;
	for (int c = 0; c < 2; ++c) {
		r.peak[c] = levels::ballistic(peak[c].load(std::memory_order_relaxed), levels::kFloorDb, idle);
		r.rms[c] = levels::ballistic(rms[c].load(std::memory_order_relaxed), levels::kFloorDb, idle);
	}
	const uint64_t clip = clipNs.load(std::memory_order_relaxed);
	r.clipped = clip && nowNs >= clip && static_cast<double>(nowNs - clip) / 1e9 < levels::kClipHoldSec;
	r.muted = muted.load(std::memory_order_relaxed);
	return r;
}

SourceLevels &SourceLevels::get()
{
	static SourceLevels instance;
	return instance;
}

void SourceLevels::onLevels(void *param, const float magnitude[MAX_AUDIO_CHANNELS],
			    const float peak[MAX_AUDIO_CHANNELS], const float[MAX_AUDIO_CHANNELS])
{
	auto *e = static_cast<LevelEntry *>(param);
	const uint64_t now = os_gettime_ns();
	const uint64_t last = e->lastNs.exchange(now, std::memory_order_relaxed);
	const double dt = last && now > last ? static_cast<double>(now - last) / 1e9 : 0.05;

	// Mono sources only fill channel 0: show it on both bars.
	float p[2] = {levels::clean(peak[0]), levels::clean(peak[1])};
	float m[2] = {levels::clean(magnitude[0]), levels::clean(magnitude[1])};
	if (p[1] <= levels::kFloorDb && p[0] > levels::kFloorDb) {
		p[1] = p[0];
		m[1] = m[0];
	}

	for (int c = 0; c < 2; ++c) {
		e->peak[c].store(levels::ballistic(e->peak[c].load(std::memory_order_relaxed), p[c], dt),
				 std::memory_order_relaxed);
		e->rms[c].store(levels::ballistic(e->rms[c].load(std::memory_order_relaxed), m[c], dt),
				std::memory_order_relaxed);
		if (p[c] >= levels::kClipDb)
			e->clipNs.store(now, std::memory_order_relaxed);
	}
}

void SourceLevels::destroyMeter(Meter &m)
{
	if (!m.volmeter)
		return;
	obs_volmeter_remove_callback(m.volmeter, &SourceLevels::onLevels, m.entry.get());
	obs_volmeter_destroy(m.volmeter);
	m.volmeter = nullptr;
}

void SourceLevels::beginSync()
{
	for (auto &kv : meters_)
		kv.second.seen = false;
}

std::shared_ptr<LevelEntry> SourceLevels::touch(obs_source_t *source)
{
	for (auto &kv : meters_) {
		if (obs_weak_source_references_source(kv.first, source)) {
			kv.second.seen = true;
			kv.second.entry->muted.store(obs_source_muted(source), std::memory_order_relaxed);
			return kv.second.entry;
		}
	}

	Meter m;
	m.entry = std::make_shared<LevelEntry>();
	m.entry->muted.store(obs_source_muted(source), std::memory_order_relaxed);
	m.volmeter = obs_volmeter_create(OBS_FADER_LOG);
	if (m.volmeter) {
		obs_volmeter_add_callback(m.volmeter, &SourceLevels::onLevels, m.entry.get());
		if (!obs_volmeter_attach_source(m.volmeter, source))
			destroyMeter(m);
	}
	m.seen = true;
	std::shared_ptr<LevelEntry> entry = m.entry;
	meters_.emplace(obs_source_get_weak_source(source), std::move(m));
	return entry;
}

void SourceLevels::endSync()
{
	for (auto it = meters_.begin(); it != meters_.end();) {
		obs_source_t *alive = obs_weak_source_get_source(it->first);
		const bool keep = it->second.seen && alive;
		obs_source_release(alive);
		if (keep) {
			++it;
			continue;
		}
		destroyMeter(it->second);
		obs_weak_source_release(it->first);
		it = meters_.erase(it);
	}
}

void SourceLevels::refreshMuted()
{
	for (auto &kv : meters_) {
		if (obs_source_t *src = obs_weak_source_get_source(kv.first)) {
			kv.second.entry->muted.store(obs_source_muted(src), std::memory_order_relaxed);
			obs_source_release(src);
		}
	}
}

void SourceLevels::clear()
{
	for (auto &kv : meters_) {
		destroyMeter(kv.second);
		obs_weak_source_release(kv.first);
	}
	meters_.clear();
}

} // namespace obsviewer
