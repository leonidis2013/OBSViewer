#pragma once

#include <cstdint>
#include <functional>
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

	// Main thread. dataDir holds the scanned plugin list and crash markers;
	// scanHelper is the obsviewer-scan executable shipped inside the plugin bundle.
	bool init(const std::string &dataDir, const std::string &scanHelper);
	void shutdown();
	bool ready() const;

	// ---- plugin list (main thread) ----
	std::vector<PluginInfo> plugins() const;
	// Plugins are scanned one file at a time in a separate helper process, so a
	// plugin that crashes or hangs while being scanned can't take OBS down; it is
	// skipped and listed as failed instead.
	void startScan(bool rescanAll);
	// Never blocks. Call from a timer; returns false once the scan has finished.
	// progress 0..1, current = the plugin being looked at.
	bool scanStep(std::string &current, float &progress);
	bool scanning() const;
	void cancelScan();
	// Plugins that crashed, hung or failed during scanning (or crashed OBS while
	// loading) and are skipped until "Rescan all".
	std::vector<std::string> failedPlugins() const;
	// Set when the last OBS session ended while a plugin was being loaded; that
	// plugin was skipped. Cleared after reading.
	std::string takeStartupWarning();

	// ---- chain (main thread) ----
	// Loads a plugin and inserts it at insertAt (-1 = end). Asynchronous (out-of-
	// process Audio Units need that); `done` runs on the main thread with the slot
	// index. state / bypass / gains restore a saved slot.
	using AddDone = std::function<void(bool ok, const std::string &error, int index)>;
	void addPlugin(const std::string &id, int insertAt, AddDone done, const std::string &state = {},
		       bool bypass = false, float inDb = 0.0f, float outDb = 0.0f);
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
	// Restores a saved chain one plugin per event-loop turn; `done` gets any errors.
	void loadChain(const std::string &state, std::function<void(const std::string &errors)> done);
	bool loadingChain() const;
	// Changes whenever the chain or the plugin list changes (cheap UI change check).
	uint64_t revision() const;
	// Gain reduction of all active slots added up, dB.
	float totalReduction() const;

	// ---- audio (OBS audio thread) ----
	void prepare(double sampleRate, int maxBlock); // main thread
	void process(float *left, float *right, int frames);

private:
	Engine();
	~Engine();
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// Entry point of the obsviewer-scan helper executable:
//   obsviewer-scan <format name> <file or identifier> <result file>
int runScanHelper(int argc, char **argv);

} // namespace obsviewer::bus
