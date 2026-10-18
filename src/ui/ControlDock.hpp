#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;

namespace obsviewer {

// "OBSViewer Controls" dock: a small remote for the viewer so nothing needs the
// Tools menu — open as a window, fullscreen (pick the screen), close, choose the
// layout, master bus on / off and open, Settings. Main thread.
class ControlDock : public QWidget {
public:
	explicit ControlDock(QWidget *parent = nullptr);

private:
	void refresh();

	QLabel *dot_ = nullptr;
	QLabel *status_ = nullptr;
	QPushButton *window_ = nullptr;
	QToolButton *fullscreen_ = nullptr;
	QPushButton *close_ = nullptr;
	QComboBox *layout_ = nullptr;
	QPushButton *busPower_ = nullptr;
	QPushButton *busOpen_ = nullptr;
	QLabel *busStatus_ = nullptr;
	bool updating_ = false;
};

} // namespace obsviewer
