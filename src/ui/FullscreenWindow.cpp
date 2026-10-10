#include "ui/FullscreenWindow.hpp"

#include "App.hpp"
#include "core/Settings.hpp"
#include "ui/Menu.hpp"
#include "ui/MultiviewPanel.hpp"
#include "ui/Theme.hpp"
#include "ui/ViewerDisplay.hpp"
#include "ui/ViewerToolbar.hpp"

#include <QAction>
#include <QKeyEvent>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>

namespace obsviewer {

// Set up like OBS's own fullscreen projectors: a normal titled window that is
// switched to fullscreen (macOS refuses to make a borderless window fullscreen).
FullscreenWindow::FullscreenWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
	setObjectName(QStringLiteral("OBSViewerRoot"));
	setWindowTitle(QStringLiteral("OBSViewer Multiview"));
	setStyleSheet(theme::styleSheet());
	setAttribute(Qt::WA_QuitOnClose, false); // closing it must never quit OBS
	setFocusPolicy(Qt::StrongFocus);

	toolbar_ = new ViewerToolbar(true, [this](const std::string &id) { pickLayout(id); }, this);

	panel_ = new MultiviewPanel(false, this);
	display_ = panel_->display();
	connect(display_, &ViewerDisplay::contextMenuRequested, this, &FullscreenWindow::showContextMenu);
	connect(display_, &ViewerDisplay::tileClicked, this,
		[](int kind, int scene) { App::get().handleTileClick(kind, scene, false); });
	connect(display_, &ViewerDisplay::tileDoubleClicked, this,
		[](int kind, int scene) { App::get().handleTileClick(kind, scene, true); });

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(toolbar_);
	layout->addWidget(panel_, 1);

	// Esc closes, even when the native video view has keyboard focus.
	auto *escape = new QAction(this);
	escape->setShortcut(Qt::Key_Escape);
	escape->setShortcutContext(Qt::WindowShortcut);
	addAction(escape);
	connect(escape, &QAction::triggered, this, &QWidget::close);

	applySettings();
}

void FullscreenWindow::setLayoutId(const std::string &id)
{
	display_->setLayoutId(id);
	toolbar_->setLayoutId(id);
}

void FullscreenWindow::pickLayout(const std::string &id)
{
	setLayoutId(id);
	Settings &s = Settings::get();
	if (s.windowLayout != id) {
		s.windowLayout = id;
		s.save();
	}
}

void FullscreenWindow::applySettings()
{
	panel_->applySettings();
	toolbar_->syncFromSettings();
	toolbar_->setVisible(Settings::get().fullscreenToolbar);
}

void FullscreenWindow::showOnScreen(QScreen *screen)
{
	if (!screen)
		return;

	// Place the window on the target screen first, then go fullscreen there.
	setGeometry(screen->geometry());
	winId();
	if (QWindow *window = windowHandle())
		window->setScreen(screen);
	showFullScreen();
	raise();
	activateWindow();
	setFocus();
}

void FullscreenWindow::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape) {
		close();
		return;
	}
	QWidget::keyPressEvent(event);
}

void FullscreenWindow::showContextMenu(const QPoint &globalPos)
{
	QMenu menu(this);
	menu.setStyleSheet(theme::styleSheet());
	QPointer<FullscreenWindow> self(this);
	ui::buildViewerMenu(
		menu, display_->layoutId(),
		[self](const std::string &id) {
			if (self)
				self->pickLayout(id);
		},
		true);
	menu.exec(globalPos);
}

} // namespace obsviewer
