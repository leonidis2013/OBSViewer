#include "metrics/PictureMonitor.hpp"

#include <obs.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>

#include <cmath>

namespace obsviewer {

namespace {
constexpr uint32_t kEveryFrames = 10; // ~3 samples a second at 30 fps
}

PictureMonitor &PictureMonitor::get()
{
	static PictureMonitor monitor;
	return monitor;
}

void PictureMonitor::start()
{
	if (running_)
		return;
	running_ = true;
	obs_add_main_rendered_callback(&PictureMonitor::onRendered, this);
}

void PictureMonitor::stop()
{
	if (!running_)
		return;
	obs_remove_main_rendered_callback(&PictureMonitor::onRendered, this);
	running_ = false;
	obs_enter_graphics();
	if (texrender_)
		gs_texrender_destroy(texrender_);
	for (auto &s : stage_)
		if (s)
			gs_stagesurface_destroy(s);
	obs_leave_graphics();
	texrender_ = nullptr;
	stage_[0] = stage_[1] = nullptr;
	staged_ = -1;
	havePrevious_ = false;
	valid_ = false;
}

PictureMonitor::Reading PictureMonitor::read() const
{
	Reading r;
	r.valid = valid_.load(std::memory_order_relaxed);
	r.luma = luma_.load(std::memory_order_relaxed);
	r.motion = motion_.load(std::memory_order_relaxed);
	return r;
}

void PictureMonitor::onRendered(void *param)
{
	auto *self = static_cast<PictureMonitor *>(param);
	if (++self->frame_ % kEveryFrames == 0)
		self->sample();
}

void PictureMonitor::sample()
{
	gs_texture_t *main = obs_get_main_texture();
	if (!main)
		return;
	if (!texrender_)
		texrender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	for (auto &s : stage_)
		if (!s)
			s = gs_stagesurface_create(kW, kH, GS_RGBA);
	if (!texrender_ || !stage_[0] || !stage_[1])
		return;

	// 1. Read back the sample staged last time (it's ready by now).
	if (staged_ >= 0) {
		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (gs_stagesurface_map(stage_[staged_], &data, &linesize)) {
			double sum = 0.0, change = 0.0;
			for (uint32_t y = 0; y < kH; ++y) {
				const uint8_t *row = data + static_cast<size_t>(y) * linesize;
				for (uint32_t x = 0; x < kW; ++x) {
					const uint8_t *px = row + x * 4;
					const float l = (0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2]) / 255.0f;
					sum += l;
					float &prev = previous_[y * kW + x];
					change += std::fabs(l - prev);
					prev = l;
				}
			}
			gs_stagesurface_unmap(stage_[staged_]);
			const double n = static_cast<double>(kW * kH);
			luma_.store(static_cast<float>(sum / n), std::memory_order_relaxed);
			motion_.store(havePrevious_ ? static_cast<float>(change / n) : 1.0f, std::memory_order_relaxed);
			havePrevious_ = true;
			valid_.store(true, std::memory_order_relaxed);
		}
	}

	// 2. Shrink the current Program picture and stage it for next time.
	gs_texrender_reset(texrender_);
	if (!gs_texrender_begin(texrender_, kW, kH))
		return;
	vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, static_cast<float>(kW), 0.0f, static_cast<float>(kH), -100.0f, 100.0f);
	gs_blend_state_push();
	gs_enable_blending(false);
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), main);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(main, 0, kW, kH);
	gs_blend_state_pop();
	gs_texrender_end(texrender_);

	const int next = staged_ == 0 ? 1 : 0;
	gs_stage_texture(stage_[next], gs_texrender_get_texture(texrender_));
	staged_ = next;
}

} // namespace obsviewer
