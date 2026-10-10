#pragma once

#include <QWidget>

#include <string>

namespace obsviewer {

class MultiviewPanel;
class ViewerDisplay;
class ViewerToolbar;

// Contents of the "OBSViewer" dock: a slim toolbar above the multiview panel.
class ViewerDock : public QWidget {
	Q_OBJECT

public:
	explicit ViewerDock(QWidget *parent = nullptr);

	void setLayoutId(const std::string &id);
	void applySettings();

private:
	void pickLayout(const std::string &id);
	void showContextMenu(const QPoint &globalPos);

	ViewerToolbar *toolbar_ = nullptr;
	MultiviewPanel *panel_ = nullptr;
	ViewerDisplay *display_ = nullptr;
};

} // namespace obsviewer
