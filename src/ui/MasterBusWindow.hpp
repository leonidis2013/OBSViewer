#pragma once

#include <QDialog>

#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QTreeWidget;
class QVBoxLayout;

namespace obsviewer {

class DbKnob;
class FaderMeter;
class PowerButton;
class SlotWidget;
class StripButton;

// Master bus as a vertical channel strip (LiveProfessor / Waves style):
// power and inputs on top, insert slots, input trim, then the output fader with
// stereo and gain-reduction meters. A plugin browser sits to the right.
class MasterBusWindow : public QDialog {
public:
	explicit MasterBusWindow(QWidget *parent = nullptr);

private:
	void refreshAll();
	void refreshSlots();
	void refreshStatus();
	void refreshBrowser();
	void selectSlot(int index);
	void insertPlugin(const std::string &id, int index); // index == slotCount → append
	void replacePlugin(const std::string &id, int index);
	void showSlotMenu(int index, const QPoint &globalPos);
	void showInsertMenu(int index, const QPoint &globalPos);
	void showInputs();
	void startScan(bool rescanAll);
	void scanTick();
	void finishScan();
	void insertFromBrowser();

	// strip
	PowerButton *power_ = nullptr;
	QLabel *status_ = nullptr;
	StripButton *inputs_ = nullptr;
	QVBoxLayout *slotsLayout_ = nullptr;
	QWidget *slotsBox_ = nullptr;
	std::vector<SlotWidget *> slots_;
	DbKnob *trim_ = nullptr;
	FaderMeter *fader_ = nullptr;
	QLabel *footer_ = nullptr;
	int selected_ = -1;

	// browser
	QLineEdit *search_ = nullptr;
	QTreeWidget *tree_ = nullptr;
	QPushButton *insertButton_ = nullptr;
	QPushButton *scanButton_ = nullptr;
	QPushButton *rescanButton_ = nullptr;
	QLabel *scanStatus_ = nullptr;
	QLabel *failed_ = nullptr;

	QTimer *meterTimer_ = nullptr;
	QTimer *scanTimer_ = nullptr;
	bool updating_ = false;
};

} // namespace obsviewer
