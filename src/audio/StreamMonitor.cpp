#include "audio/StreamMonitor.hpp"

#include "metrics/Metrics.hpp"

#include <plugin-support.h>

#include <algorithm>

namespace obsviewer {

namespace {

constexpr const char *kSourceId = "obsviewer_stream_monitor";
// A global output slot the frontend never uses (the master bus has the last one).
// Being an output keeps the source active, which OBS's monitor requires.
constexpr uint32_t kChannel = MAX_CHANNELS - 2;

const char *getName(void *)
{
	return "OBSViewer Stream Monitor";
}

void *create(obs_data_t *, obs_source_t *source)
{
	return source;
}

void destroy(void *) {}

} // namespace

StreamMonitor &StreamMonitor::get()
{
	static StreamMonitor monitor;
	return monitor;
}

void StreamMonitor::registerSource()
{
	static obs_source_info info = {};
	info.id = kSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_CAP_DISABLED;
	info.get_name = getName;
	info.create = create;
	info.destroy = destroy;
	obs_register_source(&info);
}

void StreamMonitor::setListening(bool on)
{
	if (on == listening())
		return;
	if (on)
		start(static_cast<size_t>(Metrics::streamTrackIndex()));
	else
		stop();
}

void StreamMonitor::poll()
{
	if (!listening())
		return;
	const size_t track = static_cast<size_t>(Metrics::streamTrackIndex());
	if (track != track_) {
		stop();
		start(track);
	}
}

void StreamMonitor::shutdown()
{
	stop();
}

void StreamMonitor::start(size_t track)
{
	if (source_)
		return;
	obs_audio_info oai = {};
	if (obs_get_audio_info(&oai) && oai.samples_per_sec > 0)
		sampleRate_ = oai.samples_per_sec;

	source_ = obs_source_create_private(kSourceId, "OBSViewer Stream Monitor", nullptr);
	if (!source_) {
		obs_log(LOG_WARNING, "stream monitor: could not create the source");
		return;
	}
	obs_source_set_audio_mixers(source_, 0); // on no track: heard, never mixed
	obs_source_set_monitoring_type(source_, OBS_MONITORING_TYPE_MONITOR_ONLY);
	obs_set_output_source(kChannel, source_);

	track_ = std::min<size_t>(track, MAX_AUDIO_MIXES - 1);
	audio_convert_info conv = {};
	conv.samples_per_sec = sampleRate_;
	conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
	conv.speakers = SPEAKERS_STEREO;
	conv.allow_clipping = true;
	obs_add_raw_audio_callback(track_, &conv, &StreamMonitor::onMix, this);
	obs_log(LOG_INFO, "stream monitor: listening to Track %zu", track_ + 1);
}

void StreamMonitor::stop()
{
	if (!source_)
		return;
	// Removing the callback waits for the audio thread, so onMix never sees a
	// released source.
	obs_remove_raw_audio_callback(track_, &StreamMonitor::onMix, this);
	obs_set_output_source(kChannel, nullptr);
	obs_source_release(source_);
	source_ = nullptr;
	obs_log(LOG_INFO, "stream monitor: off");
}

void StreamMonitor::onMix(void *param, size_t, struct audio_data *data)
{
	auto *self = static_cast<StreamMonitor *>(param);
	obs_source_t *src = self->source_;
	if (!src || !data || !data->data[0] || data->frames == 0)
		return;
	obs_source_audio out = {};
	out.data[0] = data->data[0];
	out.data[1] = data->data[1] ? data->data[1] : data->data[0];
	out.frames = data->frames;
	out.speakers = SPEAKERS_STEREO;
	out.format = AUDIO_FORMAT_FLOAT_PLANAR;
	out.samples_per_sec = self->sampleRate_;
	out.timestamp = data->timestamp;
	obs_source_output_audio(src, &out); // copies the data
}

} // namespace obsviewer
