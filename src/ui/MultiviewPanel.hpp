#pragma once

#include <QWidget>

#include <string>

namespace obsviewer {

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

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	ViewerDisplay *display_ = nullptr;
	SidePanel *side_ = nullptr;
};

} // namespace obsviewer
