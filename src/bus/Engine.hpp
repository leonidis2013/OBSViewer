#pragma once

#include <memory>
#include <string>
#include <vector>

// Plugin host for the master bus. Implemented with JUCE in Engine.cpp; this header
// stays JUCE-free so the Qt / OBS code never includes JUCE.
namespace obsviewer::bus {

struct PluginInfo {
	std::string id; // JUCE identifier string, stable across scans
	std::string name;
	std::string vendor;
	std::string format; // "VST3" or "AudioUnit"
};

struct SlotInfo {
	std::string name;
	std::string vendor;
	std::string format;
	bool bypass = false;
	float inGainDb = 0.0f;
	float outGainDb = 0.0f;
	int latency = 0; // samples
};

struct Levels {
	float pre[2] = {-120.0f, -120.0f};  // dBFS, decaying peak
	float post[2] = {-120.0f, -120.0f};
	float reduction = 0.0f;             // dB the slot took off (post vs pre, ≥ 0)
};

class Engine {
public:
	static Engine &get();

	// Main thread. dataDir holds the scanned plugin list and the crash marker.
	bool init(const std::string &dataDir);
	void shutdown();
	bool ready() const;

	// ---- plugin list (main thread) ----
	std::vector<PluginInfo> plugins() const;
	void startScan(bool rescanAll);
	// One file per call; returns false when finished. progress 0..1.
	bool scanStep(std::string &current, float &progress);
	bool scanning() const;

	// ---- chain (main thread) ----
	bool addPlugin(const std::string &id, std::string &error, int insertAt = -1);
	void removeSlot(int index);
	void moveSlot(int from, int to);
	void setBypass(int index, bool bypass);
	void setSlotGains(int index, float inDb, float outDb);
	void openEditor(int index);
	int slotCount() const;
	SlotInfo slot(int index) const;
	Levels slotLevels(int index) const;
	void setMasterGains(float inDb, float outDb);
	float masterInDb() const;
	float masterOutDb() const;
	Levels masterLevels() const;
	int latencySamples() const;
	double sampleRate() const;
	void clearChain();
	// Whole chain (plugins, their settings, gains) as text, and back.
	std::string saveChain() const;
	bool loadChain(const std::string &state, std::string &errors);

	// ---- audio (OBS audio thread) ----
	void prepare(double sampleRate, int maxBlock); // main thread
	void process(float *left, float *right, int frames);

private:
	Engine();
	~Engine();
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace obsviewer::bus
