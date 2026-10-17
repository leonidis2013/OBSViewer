#pragma once

#include <atomic>
#include <cstdint>

struct gs_texture_render;
struct gs_stage_surface;

namespace obsviewer {

// Watches the Program picture for black or frozen output. Every few frames,
// after OBS renders its main view, the picture is shrunk to 32 × 18 on the GPU
// and read back (one sample behind, so the GPU never stalls); the average
// brightness and the change since the previous sample are kept.
class PictureMonitor {
public:
	static PictureMonitor &get();

	void start(); // main thread
	void stop();  // main thread

	struct Reading {
		bool valid = false;
		float luma = 0.0f;   // average brightness 0…1
		float motion = 0.0f; // mean absolute change per pixel since the last sample, 0…1
	};
	Reading read() const;

private:
	PictureMonitor() = default;
	static void onRendered(void *param);
	void sample(); // graphics thread

	static constexpr uint32_t kW = 32, kH = 18;
	bool running_ = false;
	uint32_t frame_ = 0;
	gs_texture_render *texrender_ = nullptr;
	gs_stage_surface *stage_[2] = {nullptr, nullptr};
	int staged_ = -1; // which stage surface holds the pending sample
	float previous_[kW * kH] = {};
	bool havePrevious_ = false;

	std::atomic<bool> valid_{false};
	std::atomic<float> luma_{0.0f};
	std::atomic<float> motion_{0.0f};
};

} // namespace obsviewer
