#include "metrics/Metrics.hpp"
#include "metrics/PictureMonitor.hpp"

#include "audio/meters/LoudnessPresets.h"
#include "bus/MasterBus.hpp"
#include "core/Settings.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/platform.h>

#include <plugin-support.h>

#include <QTimer>

#include <algorithm>
#include <cstring>
#include <string>

namespace obsviewer {

namespace {

constexpr double kSilenceFloorLufs = -50.0;

double pctOverWindow(const History<uint32_t> &part, const History<uint32_t> &total)
{
	if (part.size() < 2 || total.size() < 2)
		return 0.0;
	const uint32_t dp = part.latest() - part.at(0);
	const uint32_t dt = total.latest() - total.at(0);
	return dt > 0 ? 100.0 * static_cast<double>(dp) / static_cast<double>(dt) : 0.0;
}

const bm::LoudnessPreset &currentPreset()
{
	const int index = std::clamp(Settings::get().loudnessPreset, 0, bm::kNumLoudnessPresets - 1);
	return bm::kLoudnessPresets[index];
}

// "Apple VT H264 Hardware · 1920×1080 · 6000 kbps"
std::string describeEncoder(obs_output_t *out)
{
	if (!out || !obs_output_active(out))
		return {};
	obs_encoder_t *enc = obs_output_get_video_encoder(out);
	if (!enc)
		return {};
	std::string d;
	const char *name = obs_encoder_get_display_name(obs_encoder_get_id(enc));
	d = name ? name : obs_encoder_get_id(enc);
	const uint32_t w = obs_encoder_get_width(enc), h = obs_encoder_get_height(enc);
	if (w && h)
		d += " · " + std::to_string(w) + "×" + std::to_string(h);
	if (obs_data_t *settings = obs_encoder_get_settings(enc)) {
		const long long kbps = obs_data_get_int(settings, "bitrate");
		const char *rc = obs_data_get_string(settings, "rate_control");
		const bool constantQuality =
			rc && (std::strcmp(rc, "CRF") == 0 || std::strcmp(rc, "CQP") == 0 || std::strcmp(rc, "CQ") == 0);
		if (kbps > 0 && !constantQuality)
			d += " · " + std::to_string(kbps) + " kbps";
		else if (rc && *rc)
			d += std::string(" · ") + rc;
		obs_data_release(settings);
	}
	return d;
}

} // namespace

int Metrics::streamTrackIndex()
{
	// The stream's audio encoder knows exactly which mix it encodes.
	if (obs_output_t *out = obs_frontend_get_streaming_output()) {
		obs_encoder_t *aenc = obs_output_get_audio_encoder(out, 0);
		const int idx = aenc ? static_cast<int>(obs_encoder_get_mixer_index(aenc)) : -1;
		obs_output_release(out);
		if (idx >= 0 && idx < MAX_AUDIO_MIXES)
			return idx;
	}
	// Otherwise read the profile: Advanced output mode has a stream track setting;
	// Simple mode always streams Track 1.
	if (config_t *cfg = obs_frontend_get_profile_config()) {
		const char *mode = config_get_string(cfg, "Output", "Mode");
		if (mode && std::strcmp(mode, "Advanced") == 0) {
			const int t = static_cast<int>(config_get_int(cfg, "AdvOut", "TrackIndex"));
			if (t >= 1 && t <= MAX_AUDIO_MIXES)
				return t - 1;
		}
	}
	return 0;
}

size_t Metrics::meterTrackIndex() const
{
	const int manual = Settings::get().meterTrack;
	return static_cast<size_t>(manual >= 1 && manual <= 6 ? manual - 1 : streamTrackIndex());
}

Metrics &Metrics::get()
{
	static Metrics metrics;
	return metrics;
}

float Metrics::targetLufs() const
{
	const int index = std::clamp(Settings::get().loudnessPreset, 0, bm::kNumLoudnessPresets - 1);
	return index == bm::kCustomPresetIndex ? Settings::get().customTargetLufs : currentPreset().targetLufs;
}

float Metrics::toleranceLu() const
{
	return currentPreset().tolerance;
}

float Metrics::ceilingDbtp() const
{
	return currentPreset().ceilingDbtp;
}

QString Metrics::targetName() const
{
	return QString::fromUtf8(currentPreset().name);
}

void Metrics::start()
{
	if (!timer_) {
		timer_ = new QTimer(this);
		timer_->setInterval(1000);
		connect(timer_, &QTimer::timeout, this, &Metrics::sample);
	}
	PictureMonitor::get().start();
	if (!cpu_)
		cpu_ = os_cpu_usage_info_start();
	ProgramLoudness::get().start(meterTrackIndex());
	lastSampleNs_ = os_gettime_ns();
	timer_->start();
}

void Metrics::stop()
{
	if (timer_)
		timer_->stop();
	PictureMonitor::get().stop();
	ProgramLoudness::get().stop();
	if (cpu_) {
		os_cpu_usage_info_destroy(cpu_);
		cpu_ = nullptr;
	}
}

void Metrics::refreshAudio()
{
	audio = ProgramLoudness::get().snapshot();
}

void Metrics::sample()
{
	const uint64_t now = os_gettime_ns();
	const double dt = std::max(0.2, static_cast<double>(now - lastSampleNs_) / 1e9);
	lastSampleNs_ = now;

	// Keep measuring the stream's track (or the one chosen in Settings).
	const size_t track = meterTrackIndex();
	measuredTrack = static_cast<int>(track) + 1;
	ProgramLoudness::get().start(track);

	// ---------------- stream ----------------
	BitrateSample bs;
	stream.active = obs_frontend_streaming_active();
	if (stream.active && !wasStreaming_) {
		streamStartNs_ = now;
		stream.reconnects = 0;
		lastStreamBytes_ = 0;
		streamRate_.reset();
		lastDropped_ = 0;
		dropWindow_.clear();
		wasReconnecting_ = false;
	}
	wasStreaming_ = stream.active;

	if (obs_output_t *out = obs_frontend_get_streaming_output()) {
		if (stream.active) {
			const uint64_t bytes = obs_output_get_total_bytes(out);
			// Averaged over 10 s: the stream goes out in bursts (keyframes), so a
			// one-second figure swings far above and below the real rate.
			streamRate_.push(now, bytes);
			stream.kbps = streamRate_.kbps();
			lastStreamBytes_ = bytes;
			stream.totalBytes = bytes;

			stream.framesDropped = obs_output_get_frames_dropped(out);
			stream.framesTotal = obs_output_get_total_frames(out);
			stream.dropPct = stream.framesTotal > 0 ? 100.0 * stream.framesDropped / stream.framesTotal : 0.0;
			const int newDrops = std::max(0, stream.framesDropped - lastDropped_);
			lastDropped_ = stream.framesDropped;
			dropWindow_.push(newDrops);

			stream.reconnecting = obs_output_reconnecting(out);
			if (stream.reconnecting && !wasReconnecting_)
				++stream.reconnects;
			wasReconnecting_ = stream.reconnecting;

			stream.connectMs = obs_output_get_connect_time_ms(out);
			stream.congestion = obs_output_get_congestion(out);
			stream.uptimeSec = static_cast<double>(now - streamStartNs_) / 1e9;

			bs.kbps = stream.kbps;
			bs.dropped = newDrops;
			bs.reconnecting = stream.reconnecting;
			bs.active = true;
		}
		obs_output_release(out);
	}
	if (!stream.active) {
		stream.kbps = 0.0;
		stream.reconnecting = false;
		stream.congestion = 0.0f;
		dropWindow_.push(0);
	}
	stream.droppedRecent = 0;
	for (size_t i = 0; i < dropWindow_.size(); ++i)
		stream.droppedRecent += dropWindow_.at(i);
	bitrateHistory.push(bs);

	// ---------------- record ----------------
	record.active = obs_frontend_recording_active();
	record.paused = record.active && obs_frontend_recording_paused();
	if (obs_output_t *rec = obs_frontend_get_recording_output()) {
		if (record.active) {
			obs_video_info ovi = {};
			double fps = 30.0;
			if (obs_get_video_info(&ovi) && ovi.fps_den > 0)
				fps = static_cast<double>(ovi.fps_num) / ovi.fps_den;
			record.durationSec = obs_output_get_total_frames(rec) / std::max(1.0, fps);

			const uint64_t bytes = obs_output_get_total_bytes(rec);
			record.bytes = bytes;
			if (obs_data_t *os = obs_output_get_settings(rec)) {
				std::string path = obs_data_get_string(os, "path");
				if (path.empty())
					path = obs_data_get_string(os, "url");
				const size_t slash = path.find_last_of('/');
				record.fileName = slash == std::string::npos ? path : path.substr(slash + 1);
				obs_data_release(os);
			}
			if (bytes >= lastRecordBytes_ && lastRecordBytes_ > 0 && !record.paused)
				record.kbps = 0.8 * record.kbps +
					      0.2 * (static_cast<double>(bytes - lastRecordBytes_) * 8.0 / 1000.0 / dt);
			lastRecordBytes_ = bytes;
		}
		obs_output_release(rec);
	}
	if (!record.active) {
		record.durationSec = 0.0;
		record.kbps = 0.0;
		record.bytes = 0;
		record.fileName.clear();
		lastRecordBytes_ = 0;
	}

	if (recordDirCheck_-- <= 0) {
		recordDirCheck_ = 10;
		if (char *dir = obs_frontend_get_current_record_output_path()) {
			recordDir_ = dir;
			bfree(dir);
		}
		record.diskFreeGB = recordDir_.empty() || !os_file_exists(recordDir_.c_str())
					    ? -1.0
					    : static_cast<double>(os_get_free_disk_space(recordDir_.c_str())) / 1e9;
	}
	record.hoursLeft = (record.active && record.kbps > 1.0 && record.diskFreeGB >= 0.0)
				   ? record.diskFreeGB * 1e9 / (record.kbps * 1000.0 / 8.0) / 3600.0
				   : -1.0;

	// ---------------- system ----------------
	system.cpuPct = cpu_ ? os_cpu_usage_info_query(cpu_) : -1.0;
	system.fps = obs_get_active_fps();
	system.memoryMB = static_cast<double>(os_get_proc_resident_size()) / (1024.0 * 1024.0);
	system.renderLaggedTotal = obs_get_lagged_frames();
	lagWindow_.push(system.renderLaggedTotal);
	renderTotalWindow_.push(obs_get_total_frames());
	system.renderLagPct = pctOverWindow(lagWindow_, renderTotalWindow_);
	if (video_t *video = obs_get_video()) {
		system.encoderSkippedTotal = video_output_get_skipped_frames(video);
		skipWindow_.push(system.encoderSkippedTotal);
		encodeTotalWindow_.push(video_output_get_total_frames(video));
		system.encoderSkipPct = pctOverWindow(skipWindow_, encodeTotalWindow_);
	}

	// ---------------- audio ----------------
	refreshAudio();
	loudnessHistory.push(audio.active ? audio.shortTerm : -120.0f);
	if (audio.active && audio.shortTerm < kSilenceFloorLufs)
		silentSeconds += dt;
	else
		silentSeconds = 0.0;

	// ---------------- picture ----------------
	{
		const PictureMonitor::Reading pic = PictureMonitor::get().read();
		pictureKnown = pic.valid;
		if (pic.valid) {
			blackSeconds = pic.luma < 0.035f ? blackSeconds + dt : 0.0;
			stillSeconds = pic.motion < 0.0008f ? stillSeconds + dt : 0.0;
		} else {
			blackSeconds = stillSeconds = 0.0;
		}
	}

	// ---------------- performance ----------------
	{
		obs_video_info ovi = {};
		if (obs_get_video_info(&ovi) && ovi.fps_den > 0)
			perf.fpsTarget = static_cast<double>(ovi.fps_num) / ovi.fps_den;
		perf.fpsActive = system.fps;
		perf.renderMs = static_cast<double>(obs_get_average_frame_time_ns()) / 1e6;

		PerfInputs pi;
		pi.fpsTarget = perf.fpsTarget;
		pi.fpsActive = perf.fpsActive;
		pi.renderMs = perf.renderMs;
		pi.renderLagPct = system.renderLagPct;
		pi.encoderSkipPct = system.encoderSkipPct;
		pi.encoding = stream.active || record.active;
		pi.cpuPct = system.cpuPct;
		pi.memoryMB = system.memoryMB;
		if (obs_output_t *out = obs_frontend_get_streaming_output()) {
			pi.streamEncoder = describeEncoder(out);
			obs_output_release(out);
		}
		if (obs_output_t *rec = obs_frontend_get_recording_output()) {
			pi.recordEncoder = describeEncoder(rec);
			obs_output_release(rec);
		}
		performance = evaluatePerformance(pi);
	}

	// ---------------- health ----------------
	HealthInputs in;
	in.streaming = stream.active;
	in.reconnecting = stream.reconnecting;
	in.reconnects = stream.reconnects;
	in.networkDropPct = stream.dropPct;
	in.networkDropsRecent = stream.droppedRecent;
	in.encoderSkipPct = system.encoderSkipPct;
	in.renderLagPct = system.renderLagPct;
	in.cpuPct = system.cpuPct;
	in.recording = record.active;
	in.diskFreeGB = record.diskFreeGB;
	in.recordHoursLeft = record.hoursLeft;
	in.audioActive = audio.active;
	in.shortTermLufs = audio.shortTerm;
	in.integratedLufs = audio.integrated;
	in.truePeakRecentDb = audio.truePeak3s;
	in.silentSeconds = silentSeconds;
	in.pictureKnown = pictureKnown;
	in.blackSeconds = blackSeconds;
	in.stillSeconds = stillSeconds;
	in.warnFrozen = Settings::get().warnFrozen;
	in.targetLufs = targetLufs();
	in.toleranceLu = toleranceLu();
	in.ceilingDbtp = ceilingDbtp();
	{
		const MasterBus &bus = MasterBus::get();
		in.busActive = bus.running() && bus.routedCount() > 0;
		in.busSendTrack = bus.config().sendTrack;
		in.busOutputMask = bus.config().outputMask;
		in.streamTrack = streamTrackIndex() + 1;
	}
	health = evaluateHealth(in);

	emit updated();
}

} // namespace obsviewer
