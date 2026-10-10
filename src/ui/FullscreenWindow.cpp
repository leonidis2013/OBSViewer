#include "ui/FullscreenWindow.hpp"

#include "App.hpp"
#include "core/Settings.hpp"
#include "ui/Menu.hpp"
#include "ui/Theme.hpp"
#include "ui/ViewerDisplay.hpp"

#include <QAction>
#include <QKeyEvent>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace obsviewer {

// Set up like OBS's own fullscreen projectors: a normal titled window that is
// switched to fullscreen. macOS refuses to put a frameless (borderless) window
// into fullscreen, which is why the first version never went fullscreen.
FullscreenWindow::FullscreenWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
	setObjectName(QStringLiteral("OBSViewerRoot"));
	setWindowTitle(QStringLiteral("OBSViewer Multiview"));
	setStyleSheet(theme::styleSheet());
	setAttribute(Qt::WA_QuitOnClose, false); // closing it must never quit OBS
	setFocusPolicy(Qt::StrongFocus);

	display_ = new ViewerDisplay(this);
	connect(display_, &ViewerDisplay::doubleClicked, this, &QWidget::close);
	connect(display_, &ViewerDisplay::contextMenuRequested, this, &FullscreenWindow::showContextMenu);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(display_);

	// Esc closes, even when the native video view has keyboard focus.
	auto *escape = new QAction(this);
	escape->setShortcut(Qt::Key_Escape);
	escape->setShortcutContext(Qt::WindowShortcut);
	addAction(escape);
	connect(escape, &QAction::triggered, this, &QWidget::close);
}

void FullscreenWindow::setLayoutId(const std::string &id)
{
	display_->setLayoutId(id);
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
	// Everything runs after the menu closes: some of these actions delete this
	// window, and macOS ignores fullscreen changes made while a menu is open.
	auto later = [](std::function<void()> fn) { QTimer::singleShot(0, &App::get(), std::move(fn)); };
	QPointer<FullscreenWindow> self(this);

	QMenu menu(this);

	QMenu *layouts = menu.addMenu(tr("Layout"));
	ui::addLayoutActions(layouts, display_->layoutId(), [self, later](const std::string &id) {
		later([self, id] {
			if (!self)
				return;
			self->display_->setLayoutId(id);
			Settings &s = Settings::get();
			s.windowLayout = id;
			s.save();
		});
	});

	QMenu *screens = menu.addMenu(tr("Move to Screen"));
	ui::addScreenActions(screens, Settings::get().windowScreen, [later](QScreen *screen) {
		QPointer<QScreen> guarded(screen);
		later([guarded] {
			if (guarded)
				App::get().openFullscreen(guarded.data());
		});
	});

	menu.addSeparator();
	connect(menu.addAction(tr("Settings…")), &QAction::triggered, this,
		[later] { later([] { App::get().openSettings(); }); });
	connect(menu.addAction(tr("Close Fullscreen")), &QAction::triggered, this,
		[later] { later([] { App::get().closeFullscreen(); }); });
	menu.exec(globalPos);
}

} // namespace obsviewer
