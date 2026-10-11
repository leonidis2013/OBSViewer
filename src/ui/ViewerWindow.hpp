#pragma once

#include <QWidget>

#include <string>

class QScreen;

namespace obsviewer {

class IconButton;
class MultiviewPanel;
class ViewerDisplay;

// The OBSViewer window. One window that is either windowed or fullscreen on a
// chosen screen, never both: the header button (or Esc) switches between them.
class ViewerWindow : public QWidget {
	Q_OBJECT

public:
	explicit ViewerWindow(QWidget *parent = nullptr);

	void setLayoutId(const std::string &id);
	void applySettings();
	MultiviewPanel *panel() const { return panel_; }

	// Windowed at the last saved size and position.
	void showWindowed();
	// Fullscreen on `screen`.
	void showFullscreenOn(QScreen *screen);
	bool fullscreen() const { return fullscreen_; }

protected:
	void closeEvent(QCloseEvent *event) override;
	void changeEvent(QEvent *event) override;

private:
	void showContextMenu(const QPoint &globalPos);
	void pickLayout(const std::string &id);
	void saveWindowGeometry();
	void restoreWindowGeometry();
	void updateModeButton();

	MultiviewPanel *panel_ = nullptr;
	ViewerDisplay *display_ = nullptr;
	IconButton *modeButton_ = nullptr;
	bool fullscreen_ = false;
};

} // namespace obsviewer
