#pragma once

#include <QWidget>

#include <functional>
#include <string>

class QScreen;

namespace obsviewer {

class ViewerDisplay;

// Fullscreen multiview on a chosen screen. Esc or double-click closes it.
class FullscreenWindow : public QWidget {
	Q_OBJECT

public:
	explicit FullscreenWindow(QWidget *parent = nullptr);

	void setLayoutId(const std::string &id);
	void showOnScreen(QScreen *screen);

protected:
	void keyPressEvent(QKeyEvent *event) override;

private:
	void showContextMenu(const QPoint &globalPos);

	ViewerDisplay *display_ = nullptr;
};

} // namespace obsviewer
