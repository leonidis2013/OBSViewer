#pragma once

#include <QWidget>

#include <string>

namespace obsviewer {

class MultiviewPanel;
class ViewerDisplay;

// Contents of the "OBSViewer" dock: the multiview panel in its compact size.
class ViewerDock : public QWidget {
	Q_OBJECT

public:
	explicit ViewerDock(QWidget *parent = nullptr);

	void setLayoutId(const std::string &id);
	void applySettings();

private:
	void pickLayout(const std::string &id);
	void showContextMenu(const QPoint &globalPos);

	MultiviewPanel *panel_ = nullptr;
	ViewerDisplay *display_ = nullptr;
};

} // namespace obsviewer
