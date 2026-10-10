#pragma once

#include <QWidget>

#include <string>

class QComboBox;
class QToolButton;

namespace obsviewer {

class ViewerDisplay;

// Contents of the "OBSViewer" dock: a slim toolbar above a multiview display.
class ViewerDock : public QWidget {
	Q_OBJECT

public:
	explicit ViewerDock(QWidget *parent = nullptr);

	void setLayoutId(const std::string &id);

private:
	void onLayoutPicked(int index);
	void showContextMenu(const QPoint &globalPos);

	ViewerDisplay *display_ = nullptr;
	QComboBox *layoutBox_ = nullptr;
	QToolButton *fullscreenButton_ = nullptr;
	QToolButton *settingsButton_ = nullptr;
};

} // namespace obsviewer
