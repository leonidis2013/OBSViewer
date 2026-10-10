#pragma once

#include <QWidget>

#include <string>

class QComboBox;
class QToolButton;

namespace obsviewer {

class MultiviewPanel;
class ViewerDisplay;

// Contents of the "OBSViewer" dock: a slim toolbar above a multiview display.
class ViewerDock : public QWidget {
	Q_OBJECT

public:
	explicit ViewerDock(QWidget *parent = nullptr);

	void setLayoutId(const std::string &id);
	void applySettings();

private:
	void onLayoutPicked(int index);
	void showContextMenu(const QPoint &globalPos);

	MultiviewPanel *panel_ = nullptr;
	ViewerDisplay *display_ = nullptr;
	QComboBox *layoutBox_ = nullptr;
	QToolButton *fullscreenButton_ = nullptr;
	QToolButton *settingsButton_ = nullptr;
};

} // namespace obsviewer
