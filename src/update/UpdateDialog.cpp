#include "update/UpdateDialog.hpp"

#include "core/Settings.hpp"
#include "ui/Theme.hpp"
#include "update/Updater.hpp"

#include <obs-frontend-api.h>

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace obsviewer {

namespace {

QString formatSize(qint64 bytes)
{
	if (bytes <= 0)
		return QStringLiteral("—");
	if (bytes < 1024 * 1024)
		return QStringLiteral("%1 KB").arg(bytes / 1024);
	return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
}

} // namespace

UpdateDialog::UpdateDialog(const Release &release, Updater *updater, QWidget *parent)
	: QDialog(parent),
	  release_(release),
	  updater_(updater)
{
	setObjectName(QStringLiteral("OBSViewerDialog"));
	setStyleSheet(theme::styleSheet());
	setWindowTitle(tr("OBSViewer Update"));
	setMinimumSize(520, 440);

	const QString newVersion = QString::fromStdString(release.version.toString());
	const QString current = QString::fromStdString(Updater::currentVersion().toString());

	auto *title = new QLabel(tr("<b>OBSViewer %1 is available</b>").arg(newVersion), this);
	title->setStyleSheet(QStringLiteral("font-size: 16px;"));

	QString meta = tr("You have %1.").arg(current);
	if (!release.publishedAt.empty())
		meta += QStringLiteral("  ·  ") +
			tr("Released %1").arg(QString::fromStdString(release.publishedAt.substr(0, 10)));
	if (const ReleaseAsset *pkg = release.installer())
		meta += QStringLiteral("  ·  ") + formatSize(pkg->size);
	if (release.prerelease || release.version.isPrerelease())
		meta += QStringLiteral("  ·  ") + tr("pre-release");
	auto *metaLabel = new QLabel(meta, this);
	metaLabel->setProperty("dim", true);

	auto *notes = new QTextBrowser(this);
	notes->setOpenExternalLinks(true);
	if (release.body.empty())
		notes->setPlainText(tr("No release notes were published for this version."));
	else
		notes->setMarkdown(QString::fromStdString(release.body));

	status_ = new QLabel(this);
	status_->setWordWrap(true);
	status_->hide();

	progress_ = new QProgressBar(this);
	progress_->setRange(0, 0);
	progress_->hide();

	installButton_ = new QPushButton(tr("Download && Install"), this);
	installButton_->setProperty("accent", true);
	installButton_->setDefault(true);
	skipButton_ = new QPushButton(tr("Skip This Version"), this);
	laterButton_ = new QPushButton(tr("Later"), this);
	quitButton_ = new QPushButton(tr("Quit OBS"), this);
	quitButton_->hide();
	auto *githubButton = new QPushButton(tr("View on GitHub"), this);
	githubButton->setEnabled(!release.htmlUrl.empty());

	auto *buttons = new QHBoxLayout();
	buttons->addWidget(githubButton);
	buttons->addStretch(1);
	buttons->addWidget(skipButton_);
	buttons->addWidget(laterButton_);
	buttons->addWidget(quitButton_);
	buttons->addWidget(installButton_);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(18, 16, 18, 16);
	layout->setSpacing(10);
	layout->addWidget(title);
	layout->addWidget(metaLabel);
	layout->addWidget(notes, 1);
	layout->addWidget(status_);
	layout->addWidget(progress_);
	layout->addLayout(buttons);

	connect(installButton_, &QPushButton::clicked, this, &UpdateDialog::startDownload);
	connect(laterButton_, &QPushButton::clicked, this, &QDialog::close);
	connect(skipButton_, &QPushButton::clicked, this, [this] {
		Settings &s = Settings::get();
		s.updateSkippedVersion = release_.version.toString();
		s.save();
		close();
	});
	connect(githubButton, &QPushButton::clicked, this,
		[this] { QDesktopServices::openUrl(QUrl(QString::fromStdString(release_.htmlUrl))); });
	connect(quitButton_, &QPushButton::clicked, this, [] {
		// Closing OBS's main window quits OBS (it asks first if you're live).
		if (auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window()))
			QTimer::singleShot(0, main, [main] { main->close(); });
	});

	connect(updater_, &Updater::downloadProgress, this, &UpdateDialog::onProgress);
	connect(updater_, &Updater::downloadFinished, this, &UpdateDialog::onFinished);
	connect(updater_, &Updater::downloadFailed, this, &UpdateDialog::onFailed);
}

void UpdateDialog::startDownload()
{
	installButton_->setEnabled(false);
	skipButton_->setEnabled(false);
	status_->setText(tr("Downloading the installer…"));
	status_->show();
	progress_->setRange(0, 0);
	progress_->show();
	updater_->download(release_);
}

void UpdateDialog::onProgress(qint64 received, qint64 total)
{
	if (total > 0) {
		progress_->setRange(0, 1000);
		progress_->setValue(static_cast<int>(received * 1000 / total));
		status_->setText(tr("Downloading the installer… %1 of %2").arg(formatSize(received), formatSize(total)));
	}
}

void UpdateDialog::onFinished(const QString &path)
{
	progress_->setRange(0, 1000);
	progress_->setValue(1000);
	status_->setText(tr("Verified and saved to %1.\n\nThe installer is opening. Quit OBS before you click "
			    "Install, then reopen OBS to load OBSViewer %2.")
				 .arg(path, QString::fromStdString(release_.version.toString())));
	installButton_->hide();
	skipButton_->hide();
	laterButton_->setText(tr("Close"));
	quitButton_->show();
	quitButton_->setProperty("accent", true);
	quitButton_->style()->unpolish(quitButton_);
	quitButton_->style()->polish(quitButton_);

	QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void UpdateDialog::onFailed(const QString &message)
{
	progress_->hide();
	status_->setText(message);
	status_->show();
	installButton_->setEnabled(true);
	installButton_->setText(tr("Try Again"));
	skipButton_->setEnabled(true);
}

} // namespace obsviewer
