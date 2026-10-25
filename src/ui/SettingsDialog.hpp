#pragma once

#include <QDialog>

#include <vector>

class QCheckBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTimeEdit;

namespace obsviewer {

class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit SettingsDialog(QWidget *parent = nullptr);

private:
	void load();
	void apply();
	void refreshTokenStatus();

	QComboBox *dockLayout_ = nullptr;
	QComboBox *windowLayout_ = nullptr;
	QCheckBox *showLabels_ = nullptr;
	QCheckBox *showSidebar_ = nullptr;
	QCheckBox *alarms_ = nullptr;
	QCheckBox *busEmbedded_ = nullptr;
	QCheckBox *safeAreas_ = nullptr;
	QCheckBox *showTransitions_ = nullptr;
	QCheckBox *hideObsPreview_ = nullptr;
	QCheckBox *warnFrozen_ = nullptr;
	QCheckBox *showClock_ = nullptr;
	QCheckBox *countdownOn_ = nullptr;
	QTimeEdit *countdownTime_ = nullptr;
	QLineEdit *countdownLabel_ = nullptr;
	std::vector<QCheckBox *> countdownDays_;
	QCheckBox *tileMeters_ = nullptr;

	QComboBox *loudnessPreset_ = nullptr;
	QDoubleSpinBox *customTarget_ = nullptr;
	QComboBox *meterTrack_ = nullptr;
	QComboBox *clickAction_ = nullptr;
	QLabel *layoutHelp_ = nullptr;

	QCheckBox *autoCheck_ = nullptr;
	QCheckBox *prereleases_ = nullptr;
	QLineEdit *repo_ = nullptr;
	QWidget *tokenRow_ = nullptr;
	QLabel *tokenStatus_ = nullptr;
	bool tokenSaved_ = false;
};

} // namespace obsviewer
