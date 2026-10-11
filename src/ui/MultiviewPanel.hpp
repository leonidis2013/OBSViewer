#pragma once

#include <QWidget>

#include <string>

class QGridLayout;

namespace obsviewer {

class MasterBusPanel;
class SidePanel;
class ViewerDisplay;

// The full OBSViewer view: multiview tiles and the right-hand control bar.
// Used by both the dock and the viewer window.
class MultiviewPanel : public QWidget {
public:
	explicit MultiviewPanel(bool compact, QWidget *parent = nullptr);

	ViewerDisplay *display() const { return display_; }
	SidePanel *sidePanel() const { return side_; }
	void setLayoutId(const std::string &id);
	std::string layoutId() const;

	// Show / hide the side bar from Settings.
	void applySettings();

	// Master bus strip next to the side bar.
	void setMasterBusVisible(bool visible);
	bool masterBusVisible() const;
	// Header button: embedded strip on / off, or the master bus window.
	void masterBusButton();

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	ViewerDisplay *display_ = nullptr;
	SidePanel *side_ = nullptr;
	MasterBusPanel *bus_ = nullptr;
	QGridLayout *grid_ = nullptr;
};

} // namespace obsviewer
