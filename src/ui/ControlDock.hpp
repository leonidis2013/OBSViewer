#pragma once

#include <QWidget>

class QBoxLayout;
class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;

namespace obsviewer {

class TransitionControls;

// "OBSViewer Controls" dock: a small remote for the viewer so nothing needs the
// Tools menu — open as a window, fullscreen (pick the screen), close, choose the
// layout, master bus on / off and open, Settings. Main thread.
class ControlDock : public QWidget {
public:
	explicit ControlDock(QWidget *parent = nullptr);

protected:
	void resizeEvent(QResizeEvent *event) override;

private:
	void refresh();
	void applyShape(); // one row in a wide, short dock area; a column otherwise

	QLabel *dot_ = nullptr;
	QLabel *status_ = nullptr;
	QLabel *streamTitle_ = nullptr;
	QPushButton *window_ = nullptr;
	QToolButton *fullscreen_ = nullptr;
	QPushButton *close_ = nullptr;
	QComboBox *layout_ = nullptr;
	QPushButton *busPower_ = nullptr;
	QPushButton *busOpen_ = nullptr;
	QLabel *busStatus_ = nullptr;
	QLabel *transitionCaption_ = nullptr;
	TransitionControls *transitions_ = nullptr;
	QLabel *layoutCaption_ = nullptr;
	QLabel *busCaption_ = nullptr;
	QWidget *statusGroup_ = nullptr;
	QWidget *viewerGroup_ = nullptr;
	QWidget *transitionGroup_ = nullptr;
	QWidget *layoutGroup_ = nullptr;
	QWidget *busGroup_ = nullptr;
	QPushButton *settings_ = nullptr;
	QBoxLayout *main_ = nullptr;
	bool strip_ = false;
	bool updating_ = false;
};

} // namespace obsviewer
