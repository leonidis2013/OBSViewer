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
// and opens it in macOS Installer. Network work runs on a worker thread; results
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
