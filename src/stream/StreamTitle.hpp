#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <QtGlobal>

class QTimer;

namespace obsviewer {

// Finds the title of the YouTube live stream, on a background thread:
//   1. OBS connected to a YouTube account (Settings ▸ Stream ▸ Connect Account,
//      then Manage Broadcast): the broadcast id OBS put in the stream settings →
//      YouTube's public oEmbed lookup.
//   2. Otherwise the channel from Settings ▸ YouTube ("@handle"): its /live page
//      names the current or next live stream.
//   3. Otherwise the title OBS remembered in Manage Broadcast.
// Checked every minute while streaming, every 3 minutes otherwise, and right
// away when the stream starts. Public and unlisted streams work; private ones
// only through 3.
class StreamTitle {
public:
	static StreamTitle &get();

	// Main thread.
	void start();
	void stop();
	void refreshNow();
	// The title to show ("" = none, or turned off in Settings).
	std::string shownTitle() const;
	// Where the title came from, or why there is none (Settings).
	std::string status() const;

private:
	StreamTitle() = default;
	void tick();
	void launch();

	struct Inputs {
		std::string broadcastId;
		std::string profileTitle;
		std::string liveUrl;
	};
	static void fetch(const Inputs &in, std::string &title, std::string &status, const std::atomic<bool> &cancel);

	QTimer *timer_ = nullptr;
	std::thread worker_;
	std::atomic<bool> busy_{false};
	std::atomic<bool> cancel_{false};
	mutable std::mutex mutex_;
	std::string title_;  // guarded by mutex_
	std::string status_; // guarded by mutex_
	qint64 nextFetchMs_ = 0;
	bool wasStreaming_ = false;
};

} // namespace obsviewer
