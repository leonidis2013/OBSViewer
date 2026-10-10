#pragma once

#include "update/ReleaseInfo.hpp"

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

namespace obsviewer {

class Updater;

// Shows an available update with its release notes; downloads and opens the installer.
class UpdateDialog : public QDialog {
	Q_OBJECT

public:
	UpdateDialog(const Release &release, Updater *updater, QWidget *parent = nullptr);

private:
	void startDownload();
	void onProgress(qint64 received, qint64 total);
	void onFinished(const QString &path);
	void onFailed(const QString &message);

	Release release_;
	Updater *updater_;
	QLabel *status_ = nullptr;
	QProgressBar *progress_ = nullptr;
	QPushButton *installButton_ = nullptr;
	QPushButton *skipButton_ = nullptr;
	QPushButton *laterButton_ = nullptr;
	QPushButton *quitButton_ = nullptr;
};

} // namespace obsviewer
