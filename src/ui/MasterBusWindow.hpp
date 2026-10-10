#pragma once

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace obsviewer {

class MiniMeter;
class SlotRow;

// Master bus: on/off, which sources go through it, and the plugin chain
// (VST3 / Audio Unit inserts with gain staging and meters).
class MasterBusWindow : public QDialog {
public:
	explicit MasterBusWindow(QWidget *parent = nullptr);

private:
	void refreshAll();
	void refreshSources();
	void refreshChain();
	void refreshStatus();
	void rebuildAddMenu();
	void addPlugin(const std::string &id);
	void startScan(bool rescanAll);
	void scanTick();

	QCheckBox *enabled_ = nullptr;
	QLabel *status_ = nullptr;
	MiniMeter *masterMeter_ = nullptr;
	QDoubleSpinBox *masterIn_ = nullptr;
	QDoubleSpinBox *masterOut_ = nullptr;

	QListWidget *sources_ = nullptr;
	QComboBox *sendTrack_ = nullptr;
	std::vector<QCheckBox *> outputTracks_;

	QVBoxLayout *chainLayout_ = nullptr;
	QWidget *chainBox_ = nullptr;
	std::vector<SlotRow *> rows_;
	QToolButton *addButton_ = nullptr;
	QPushButton *scanButton_ = nullptr;
	QLabel *scanStatus_ = nullptr;
	QLabel *latency_ = nullptr;

	QTimer *meterTimer_ = nullptr;
	QTimer *scanTimer_ = nullptr;
	bool updating_ = false;
};

} // namespace obsviewer
