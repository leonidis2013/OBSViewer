#pragma once

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;

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
	QCheckBox *tileMeters_ = nullptr;

	QComboBox *loudnessPreset_ = nullptr;
	QDoubleSpinBox *customTarget_ = nullptr;
	QComboBox *meterTrack_ = nullptr;
	QComboBox *clickAction_ = nullptr;
	QLabel *layoutHelp_ = nullptr;

	QCheckBox *autoCheck_ = nullptr;
	QCheckBox *prereleases_ = nullptr;
	QLineEdit *repo_ = nullptr;
	QLineEdit *token_ = nullptr;
	QLabel *tokenStatus_ = nullptr;
	bool tokenSaved_ = false;
};

} // namespace obsviewer
