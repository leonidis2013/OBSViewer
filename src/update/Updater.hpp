#pragma once

#include "core/Version.hpp"
#include "update/ReleaseInfo.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace obsviewer {

class UpdateDialog;

// Checks GitHub Releases for a newer OBSViewer, downloads and verifies the .pkg,
// and installs it when OBS restarts (see scheduleInstallOnRestart). Network work runs on a worker thread; results
// come back to the Qt main thread.
class Updater : public QObject {
	Q_OBJECT

public:
	explicit Updater(QObject *parent = nullptr);
	~Updater() override;

	static Version currentVersion();

	// Automatic check: only if enabled in Settings and the last check was > 12 h ago.
	void checkIfDue();
	// Explicit check; `interactive` shows "up to date" and error messages.
	void checkNow(bool interactive);
	// Download, verify and open the installer for `release`.
	void download(const Release &release);
	// Stop any network work and wait for the worker to finish.
	void cancel();

	// "Restart OBS": starts a small background script that waits for OBS to quit,
	// installs the downloaded package into the user's OBS plugins folder (no admin
	// password; falls back to macOS Installer if that fails) and opens OBS again.
	// Returns the script's process id (0 on failure, with `error` set). The script
	// gives up if OBS is still running after 15 minutes.
	static qint64 scheduleInstallOnRestart(const QString &pkgPath, QString &error);
	// Stops a scheduled install (e.g. the user cancelled quitting OBS).
	static void cancelScheduledInstall(qint64 pid);
	// Where the restart script writes what it did.
	static QString installLogPath();

	bool busy() const { return busy_.load(); }

signals:
	void downloadProgress(qint64 received, qint64 total);
	void downloadFinished(const QString &path);
	void downloadFailed(const QString &message);

private:
	struct CheckResult {
		bool ok = false;
		QString error;
		std::vector<Release> releases;
	};

	bool startWorker(std::function<void()> job);
	CheckResult fetchReleases(const std::string &repo, const std::string &token);
	void handleCheckResult(bool interactive, const CheckResult &result);
	void runDownload(Release release, std::string repo, std::string token);

	std::thread worker_;
	std::atomic<bool> busy_{false};
	std::atomic<bool> cancel_{false};
	QPointer<UpdateDialog> dialog_;
};

} // namespace obsviewer
