#pragma once

#include <QWidget>

#include <functional>
#include <string>

class QScreen;

namespace obsviewer {

class MultiviewPanel;
class ViewerDisplay;
class ViewerToolbar;

// Fullscreen multiview on a chosen screen, with a toolbar on top. Esc closes it.
class FullscreenWindow : public QWidget {
	Q_OBJECT

public:
	explicit FullscreenWindow(QWidget *parent = nullptr);

	void setLayoutId(const std::string &id);
	void showOnScreen(QScreen *screen);
	void applySettings();

protected:
	void keyPressEvent(QKeyEvent *event) override;

private:
	void showContextMenu(const QPoint &globalPos);
	void pickLayout(const std::string &id);

	ViewerToolbar *toolbar_ = nullptr;
	MultiviewPanel *panel_ = nullptr;
	ViewerDisplay *display_ = nullptr;
};

} // namespace obsviewer
