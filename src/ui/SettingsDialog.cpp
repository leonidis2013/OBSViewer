#include "ui/SettingsDialog.hpp"

#include "App.hpp"
#include "audio/meters/LoudnessPresets.h"
#include "core/Settings.hpp"
#include "ui/Theme.hpp"
#include "update/Keychain.hpp"
#include "update/Updater.hpp"
#include "viewer/Layouts.hpp"

#include <plugin-support.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace obsviewer {

namespace {

QComboBox *makeLayoutBox(QWidget *parent)
{
	auto *box = new QComboBox(parent);
	for (const Layout &layout : allLayouts())
		box->addItem(QString::fromStdString(layout.name), QString::fromStdString(layout.id));
	return box;
}

void selectData(QComboBox *box, const std::string &id)
{
	const int index = box->findData(QString::fromStdString(id));
	box->setCurrentIndex(index >= 0 ? index : 0);
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("OBSViewerDialog"));
	setStyleSheet(theme::styleSheet());
	setWindowTitle(tr("OBSViewer Settings"));
	setMinimumWidth(520);

	// ---- Multiview ----
	auto *viewGroup = new QGroupBox(tr("MULTIVIEW"), this);
	auto *viewForm = new QFormLayout(viewGroup);
	dockLayout_ = makeLayoutBox(viewGroup);
	windowLayout_ = makeLayoutBox(viewGroup);
	showLabels_ = new QCheckBox(tr("Show tile labels"), viewGroup);
	viewForm->addRow(tr("Dock layout"), dockLayout_);
	viewForm->addRow(tr("Fullscreen layout"), windowLayout_);
	viewForm->addRow(QString(), showLabels_);
	showSidebar_ = new QCheckBox(tr("Show the side bar (stream, record, audio, health)"), viewGroup);
	showNetbar_ = new QCheckBox(tr("Show the network bar"), viewGroup);
	alarms_ = new QCheckBox(tr("Flash sections that need attention"), viewGroup);
	viewForm->addRow(QString(), showSidebar_);
	viewForm->addRow(QString(), showNetbar_);
	viewForm->addRow(QString(), alarms_);

	// ---- Audio ----
	auto *audioGroup = new QGroupBox(tr("AUDIO"), this);
	auto *audioForm = new QFormLayout(audioGroup);
	loudnessPreset_ = new QComboBox(audioGroup);
	for (int i = 0; i < bm::kNumLoudnessPresets; ++i) {
		const auto &preset = bm::kLoudnessPresets[i];
		loudnessPreset_->addItem(i == bm::kCustomPresetIndex
						 ? QString::fromUtf8(preset.name)
						 : QStringLiteral("%1  (%2 LUFS, %3 dBTP)")
							   .arg(QString::fromUtf8(preset.name))
							   .arg(preset.targetLufs, 0, 'f', 0)
							   .arg(preset.ceilingDbtp, 0, 'f', 0));
	}
	customTarget_ = new QDoubleSpinBox(audioGroup);
	customTarget_->setRange(-36.0, -6.0);
	customTarget_->setSingleStep(0.5);
	customTarget_->setDecimals(1);
	customTarget_->setSuffix(QStringLiteral(" LUFS"));
	audioTrack_ = new QComboBox(audioGroup);
	for (int t = 1; t <= 6; ++t)
		audioTrack_->addItem(tr("Track %1").arg(t), t);
	auto *audioHelp = new QLabel(tr("The meter measures this OBS audio track. Use the track your stream sends "
					"(Settings ▸ Output ▸ Streaming ▸ Audio Track; Track 1 by default)."),
				     audioGroup);
	audioHelp->setWordWrap(true);
	audioHelp->setProperty("dim", true);
	audioForm->addRow(tr("Loudness target"), loudnessPreset_);
	audioForm->addRow(tr("Custom target"), customTarget_);
	audioForm->addRow(tr("Measure"), audioTrack_);
	audioForm->addRow(QString(), audioHelp);
	connect(loudnessPreset_, &QComboBox::currentIndexChanged, this,
		[this](int index) { customTarget_->setEnabled(index == bm::kCustomPresetIndex); });

	// ---- Updates ----
	auto *updateGroup = new QGroupBox(tr("UPDATES"), this);
	auto *updateForm = new QFormLayout(updateGroup);

	auto *version = new QLabel(tr("OBSViewer %1").arg(QString::fromUtf8(PLUGIN_VERSION)), updateGroup);
	autoCheck_ = new QCheckBox(tr("Check for updates when OBS starts"), updateGroup);
	prereleases_ = new QCheckBox(tr("Include beta releases"), updateGroup);
	repo_ = new QLineEdit(updateGroup);
	repo_->setPlaceholderText(QStringLiteral("owner/repository"));

	token_ = new QLineEdit(updateGroup);
	token_->setEchoMode(QLineEdit::Password);
	token_->setPlaceholderText(tr("Paste a token to save it"));
	auto *clearToken = new QPushButton(tr("Remove"), updateGroup);
	auto *tokenRow = new QHBoxLayout();
	tokenRow->addWidget(token_, 1);
	tokenRow->addWidget(clearToken);

	tokenStatus_ = new QLabel(updateGroup);
	tokenStatus_->setWordWrap(true);
	tokenStatus_->setProperty("dim", true);

	auto *tokenHelp = new QLabel(
		tr("Needed only while the release repository is private. Create a fine-grained personal access "
		   "token on GitHub with read-only <i>Contents</i> access to that repository. It is stored in your "
		   "macOS Keychain, never in a file."),
		updateGroup);
	tokenHelp->setWordWrap(true);
	tokenHelp->setProperty("dim", true);

	auto *checkNow = new QPushButton(tr("Check Now"), updateGroup);

	updateForm->addRow(tr("Installed"), version);
	updateForm->addRow(QString(), autoCheck_);
	updateForm->addRow(QString(), prereleases_);
	updateForm->addRow(tr("Repository"), repo_);
	updateForm->addRow(tr("GitHub token"), tokenRow);
	updateForm->addRow(QString(), tokenStatus_);
	updateForm->addRow(QString(), tokenHelp);
	updateForm->addRow(QString(), checkNow);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(18, 16, 18, 16);
	layout->setSpacing(12);
	layout->addWidget(viewGroup);
	layout->addWidget(audioGroup);
	layout->addWidget(updateGroup);
	layout->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		apply();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(clearToken, &QPushButton::clicked, this, [this] {
		keychain::removeToken();
		token_->clear();
		refreshTokenStatus();
	});
	connect(checkNow, &QPushButton::clicked, this, [this] {
		apply();
		App::get().checkForUpdates(true);
	});

	load();
}

void SettingsDialog::load()
{
	const Settings &s = Settings::get();
	selectData(dockLayout_, s.dockLayout);
	selectData(windowLayout_, s.windowLayout);
	showLabels_->setChecked(s.showLabels);
	showSidebar_->setChecked(s.showSidebar);
	showNetbar_->setChecked(s.showNetbar);
	alarms_->setChecked(s.alarms);
	loudnessPreset_->setCurrentIndex(std::clamp(s.loudnessPreset, 0, bm::kNumLoudnessPresets - 1));
	customTarget_->setValue(s.customTargetLufs);
	customTarget_->setEnabled(loudnessPreset_->currentIndex() == bm::kCustomPresetIndex);
	audioTrack_->setCurrentIndex(std::clamp(s.audioTrack, 1, 6) - 1);
	autoCheck_->setChecked(s.updateAutoCheck);
	prereleases_->setChecked(s.updateIncludePrereleases);
	repo_->setText(QString::fromStdString(s.updateRepo));
	token_->clear();
	refreshTokenStatus();
}

void SettingsDialog::refreshTokenStatus()
{
	tokenSaved_ = !keychain::loadToken().empty();
	tokenStatus_->setText(tokenSaved_ ? tr("A token is saved in the Keychain.") : tr("No token saved."));
}

void SettingsDialog::apply()
{
	Settings &s = Settings::get();
	s.dockLayout = dockLayout_->currentData().toString().toStdString();
	s.windowLayout = windowLayout_->currentData().toString().toStdString();
	s.showLabels = showLabels_->isChecked();
	s.showSidebar = showSidebar_->isChecked();
	s.showNetbar = showNetbar_->isChecked();
	s.alarms = alarms_->isChecked();
	s.loudnessPreset = loudnessPreset_->currentIndex();
	s.customTargetLufs = static_cast<float>(customTarget_->value());
	s.audioTrack = audioTrack_->currentData().toInt();
	s.updateAutoCheck = autoCheck_->isChecked();
	s.updateIncludePrereleases = prereleases_->isChecked();

	const QString repo = repo_->text().trimmed();
	if (repo.count(QLatin1Char('/')) == 1 && !repo.startsWith(QLatin1Char('/')) && !repo.endsWith(QLatin1Char('/')))
		s.updateRepo = repo.toStdString();

	const QString token = token_->text().trimmed();
	if (!token.isEmpty()) {
		keychain::saveToken(token.toStdString());
		token_->clear();
	}
	refreshTokenStatus();

	s.save();
	App::get().applySettings();
}

} // namespace obsviewer
