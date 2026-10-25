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
#include <QScrollArea>
#include <QTimeEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace obsviewer {

namespace {

QComboBox *makeLayoutBox(QWidget *parent)
{
	auto *box = new QComboBox(parent);
	for (const LayoutInfo &layout : allLayouts())
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
	auto *viewGroup = new QGroupBox(tr("Multiview"), this);
	auto *viewForm = new QFormLayout(viewGroup);
	windowLayout_ = makeLayoutBox(viewGroup);
	dockLayout_ = makeLayoutBox(viewGroup);
	layoutHelp_ = new QLabel(viewGroup);
	layoutHelp_->setWordWrap(true);
	layoutHelp_->setProperty("dim", true);
	clickAction_ = new QComboBox(viewGroup);
	clickAction_->addItem(tr("Does nothing"), Settings::ClickOff);
	clickAction_->addItem(tr("Sends the scene to Preview"), Settings::ClickPreview);
	clickAction_->addItem(tr("Cuts the scene to Program"), Settings::ClickProgram);
	clickAction_->setToolTip(tr("Outside Studio Mode both choices switch the scene. "
				    "Double-click the Preview tile to take it to Program."));
	showLabels_ = new QCheckBox(tr("Show names below the views"), viewGroup);
	tileMeters_ = new QCheckBox(tr("Show audio meters on the views"), viewGroup);
	showSidebar_ = new QCheckBox(tr("Show the side bar (health, stream, network, record, audio)"), viewGroup);
	alarms_ = new QCheckBox(tr("Flash the health light when something is wrong"), viewGroup);
	viewForm->addRow(tr("Window layout"), windowLayout_);
	viewForm->addRow(tr("Dock layout"), dockLayout_);
	viewForm->addRow(QString(), layoutHelp_);
	viewForm->addRow(tr("Clicking a view"), clickAction_);
	viewForm->addRow(QString(), showLabels_);
	viewForm->addRow(QString(), tileMeters_);
	viewForm->addRow(QString(), showSidebar_);
	viewForm->addRow(QString(), alarms_);
	showTransitions_ = new QCheckBox(tr("Show the transition controls (CUT / AUTO between Preview and Program)"), viewGroup);
	showTransitions_->setToolTip(tr("Also shows or hides them in the OBSViewer Controls dock. They work in Studio Mode."));
	viewForm->addRow(QString(), showTransitions_);
	busEmbedded_ = new QCheckBox(tr("Open the master bus next to the side bar (instead of its own window)"), viewGroup);
	viewForm->addRow(QString(), busEmbedded_);
	safeAreas_ = new QCheckBox(tr("Show safe-area guides on Program and Preview"), viewGroup);
	viewForm->addRow(QString(), safeAreas_);
	hideObsPreview_ = new QCheckBox(tr("Turn off OBS's own preview while the OBSViewer window is open"), viewGroup);
	hideObsPreview_->setToolTip(tr("Saves graphics power: the picture isn't drawn twice. It comes back when the window "
				       "closes. In Studio Mode OBS's Program view stays on."));
	viewForm->addRow(QString(), hideObsPreview_);
	warnFrozen_ = new QCheckBox(tr("Warn when Program hasn't changed for a minute while live"), viewGroup);
	warnFrozen_->setToolTip(tr("A black Program picture is always reported."));
	viewForm->addRow(QString(), warnFrozen_);

	// ---- Clock ----
	auto *clockGroup = new QGroupBox(tr("Clock"), this);
	auto *clockForm = new QFormLayout(clockGroup);
	showClock_ = new QCheckBox(tr("Show the clock in the side bar"), clockGroup);
	countdownOn_ = new QCheckBox(tr("Count down to a time each day"), clockGroup);
	countdownTime_ = new QTimeEdit(clockGroup);
	countdownTime_->setDisplayFormat(QStringLiteral("h:mm AP"));
	countdownLabel_ = new QLineEdit(clockGroup);
	countdownLabel_->setPlaceholderText(tr("Service starts"));
	clockForm->addRow(QString(), showClock_);
	clockForm->addRow(QString(), countdownOn_);
	clockForm->addRow(tr("Count down to"), countdownTime_);
	clockForm->addRow(tr("Label"), countdownLabel_);
	connect(countdownOn_, &QCheckBox::toggled, this, [this](bool on) {
		countdownTime_->setEnabled(on);
		countdownLabel_->setEnabled(on);
	});
	auto describe = [this](QComboBox *box) {
		layoutHelp_->setText(QString::fromStdString(layoutById(box->currentData().toString().toStdString()).description));
	};
	connect(windowLayout_, &QComboBox::currentIndexChanged, this, [this, describe] { describe(windowLayout_); });
	connect(dockLayout_, &QComboBox::currentIndexChanged, this, [this, describe] { describe(dockLayout_); });

	// ---- Audio ----
	auto *audioGroup = new QGroupBox(tr("Programme audio"), this);
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
	meterTrack_ = new QComboBox(audioGroup);
	meterTrack_->addItem(tr("Stream output (automatic)"), 0);
	for (int t = 1; t <= 6; ++t)
		meterTrack_->addItem(tr("Track %1").arg(t), t);
	auto *audioHelp = new QLabel(tr("The meter only listens; it never changes your audio. \"Stream output\" "
					"measures exactly the track your stream sends."),
				     audioGroup);
	audioHelp->setWordWrap(true);
	audioHelp->setProperty("dim", true);
	audioForm->addRow(tr("Loudness target"), loudnessPreset_);
	audioForm->addRow(tr("Custom target"), customTarget_);
	audioForm->addRow(tr("Measure"), meterTrack_);
	audioForm->addRow(QString(), audioHelp);
	connect(loudnessPreset_, &QComboBox::currentIndexChanged, this,
		[this](int index) { customTarget_->setEnabled(index == bm::kCustomPresetIndex); });

	// ---- Updates ----
	auto *updateGroup = new QGroupBox(tr("Updates"), this);
	auto *updateForm = new QFormLayout(updateGroup);

	auto *version = new QLabel(tr("OBSViewer %1").arg(QString::fromUtf8(PLUGIN_VERSION)), updateGroup);
	autoCheck_ = new QCheckBox(tr("Check for updates when OBS starts"), updateGroup);
	prereleases_ = new QCheckBox(tr("Include beta releases"), updateGroup);
	repo_ = new QLineEdit(updateGroup);
	repo_->setPlaceholderText(QStringLiteral("owner/repository"));

	// The release repository is public, so no GitHub token is needed. A token
	// saved by an older version is still used; it can be removed here.
	tokenStatus_ = new QLabel(updateGroup);
	tokenStatus_->setWordWrap(true);
	tokenStatus_->setProperty("dim", true);
	auto *clearToken = new QPushButton(tr("Remove Saved Token"), updateGroup);
	clearToken->setToolTip(tr("A GitHub token from when the repository was private; it isn't needed any more."));
	tokenRow_ = new QWidget(updateGroup);
	auto *tokenRow = new QHBoxLayout(tokenRow_);
	tokenRow->setContentsMargins(0, 0, 0, 0);
	tokenRow->addWidget(tokenStatus_, 1);
	tokenRow->addWidget(clearToken);

	auto *checkNow = new QPushButton(tr("Check Now"), updateGroup);

	updateForm->addRow(tr("Installed"), version);
	updateForm->addRow(QString(), autoCheck_);
	updateForm->addRow(QString(), prereleases_);
	updateForm->addRow(tr("Repository"), repo_);
	updateForm->addRow(QString(), tokenRow_);
	updateForm->addRow(QString(), checkNow);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

	// Groups scroll if the screen is short; the OK / Cancel buttons stay put.
	auto *content = new QWidget(this);
	content->setObjectName(QStringLiteral("OBSViewerRoot"));
	auto *cl = new QVBoxLayout(content);
	cl->setContentsMargins(0, 0, 6, 0);
	cl->setSpacing(12);
	cl->addWidget(viewGroup);
	cl->addWidget(clockGroup);
	cl->addWidget(audioGroup);
	cl->addWidget(updateGroup);
	cl->addStretch(1);
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->setWidget(content);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(18, 16, 18, 16);
	layout->setSpacing(12);
	layout->addWidget(scroll, 1);
	layout->addWidget(buttons);
	resize(600, 860);

	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		apply();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(clearToken, &QPushButton::clicked, this, [this] {
		keychain::removeToken();
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
	alarms_->setChecked(s.alarms);
	busEmbedded_->setChecked(s.masterBusEmbedded);
	safeAreas_->setChecked(s.safeAreas);
	showTransitions_->setChecked(s.showTransitions);
	hideObsPreview_->setChecked(s.hideObsPreview);
	warnFrozen_->setChecked(s.warnFrozen);
	showClock_->setChecked(s.showClock);
	countdownOn_->setChecked(s.countdownOn);
	countdownTime_->setTime(QTime(0, 0).addSecs(s.countdownMinutes * 60));
	countdownLabel_->setText(QString::fromStdString(s.countdownLabel));
	countdownTime_->setEnabled(s.countdownOn);
	countdownLabel_->setEnabled(s.countdownOn);
	tileMeters_->setChecked(s.tileMeters);
	loudnessPreset_->setCurrentIndex(std::clamp(s.loudnessPreset, 0, bm::kNumLoudnessPresets - 1));
	customTarget_->setValue(s.customTargetLufs);
	customTarget_->setEnabled(loudnessPreset_->currentIndex() == bm::kCustomPresetIndex);
	meterTrack_->setCurrentIndex(std::clamp(s.meterTrack, 0, 6));
	clickAction_->setCurrentIndex(std::max(0, clickAction_->findData(s.clickAction)));
	layoutHelp_->setText(QString::fromStdString(layoutById(s.windowLayout).description));
	autoCheck_->setChecked(s.updateAutoCheck);
	prereleases_->setChecked(s.updateIncludePrereleases);
	repo_->setText(QString::fromStdString(s.updateRepo));
	refreshTokenStatus();
}

void SettingsDialog::refreshTokenStatus()
{
	tokenSaved_ = !keychain::loadToken().empty();
	tokenStatus_->setText(tr("A GitHub token is saved in the Keychain (no longer needed)."));
	tokenRow_->setVisible(tokenSaved_);
}

void SettingsDialog::apply()
{
	Settings &s = Settings::get();
	s.dockLayout = dockLayout_->currentData().toString().toStdString();
	s.windowLayout = windowLayout_->currentData().toString().toStdString();
	s.showLabels = showLabels_->isChecked();
	s.showSidebar = showSidebar_->isChecked();
	s.alarms = alarms_->isChecked();
	s.masterBusEmbedded = busEmbedded_->isChecked();
	s.safeAreas = safeAreas_->isChecked();
	s.showTransitions = showTransitions_->isChecked();
	s.hideObsPreview = hideObsPreview_->isChecked();
	s.warnFrozen = warnFrozen_->isChecked();
	s.showClock = showClock_->isChecked();
	s.countdownOn = countdownOn_->isChecked();
	s.countdownMinutes = countdownTime_->time().hour() * 60 + countdownTime_->time().minute();
	s.countdownLabel = countdownLabel_->text().trimmed().isEmpty() ? std::string("Service starts")
								     : countdownLabel_->text().trimmed().toStdString();
	s.tileMeters = tileMeters_->isChecked();
	s.loudnessPreset = loudnessPreset_->currentIndex();
	s.customTargetLufs = static_cast<float>(customTarget_->value());
	s.meterTrack = meterTrack_->currentData().toInt();
	s.clickAction = clickAction_->currentData().toInt();
	s.updateAutoCheck = autoCheck_->isChecked();
	s.updateIncludePrereleases = prereleases_->isChecked();

	const QString repo = repo_->text().trimmed();
	if (repo.count(QLatin1Char('/')) == 1 && !repo.startsWith(QLatin1Char('/')) && !repo.endsWith(QLatin1Char('/')))
		s.updateRepo = repo.toStdString();

	refreshTokenStatus();

	s.save();
	App::get().applySettings();
}

} // namespace obsviewer
