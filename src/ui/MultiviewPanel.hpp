#pragma once

#include <QWidget>

#include <string>

namespace obsviewer {

class NetworkBar;
class SidePanel;
class ViewerDisplay;

// The full OBSViewer view: multiview tiles, the right-hand control bar and the
// bottom network bar. Used by both the dock and the fullscreen window.
class MultiviewPanel : public QWidget {
public:
	explicit MultiviewPanel(bool compact, QWidget *parent = nullptr);

	ViewerDisplay *display() const { return display_; }
	void setLayoutId(const std::string &id);
	std::string layoutId() const;

	// Show / hide the bars from Settings.
	void applySettings();

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	ViewerDisplay *display_ = nullptr;
	SidePanel *side_ = nullptr;
	NetworkBar *net_ = nullptr;
};

} // namespace obsviewer
