#include "ui/FullscreenWindow.hpp"

#include "App.hpp"
#include "core/Settings.hpp"
#include "ui/Menu.hpp"
#include "ui/Theme.hpp"
#include "ui/ViewerDisplay.hpp"

#include <QKeyEvent>
#include <QMenu>
#include <QPointer>
#include <QTimer>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>

namespace obsviewer {

FullscreenWindow::FullscreenWindow(QWidget *parent) : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
{
	setObjectName(QStringLiteral("OBSViewerRoot"));
	setWindowTitle(QStringLiteral("OBSViewer"));
	setStyleSheet(theme::styleSheet());
	setFocusPolicy(Qt::StrongFocus);

	display_ = new ViewerDisplay(this);
	connect(display_, &ViewerDisplay::doubleClicked, this, &QWidget::close);
	connect(display_, &ViewerDisplay::contextMenuRequested, this, &FullscreenWindow::showContextMenu);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(display_);
}

void FullscreenWindow::setLayoutId(const std::string &id)
{
	display_->setLayoutId(id);
}

void FullscreenWindow::showOnScreen(QScreen *screen)
{
	if (!screen)
		return;

	winId();
	if (QWindow *window = windowHandle())
		window->setScreen(screen);
	setGeometry(screen->geometry());
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

	QMenu *layouts = menu.addMenu(tr("Layout"));
	ui::addLayoutActions(layouts, display_->layoutId(), [this](const std::string &id) {
		display_->setLayoutId(id);
		Settings &s = Settings::get();
		s.windowLayout = id;
		s.save();
	});

	QMenu *screens = menu.addMenu(tr("Move to Screen"));
	// Defer: these actions delete this window, which must not happen inside menu.exec().
	ui::addScreenActions(screens, Settings::get().windowScreen, [](QScreen *screen) {
		QPointer<QScreen> guarded(screen);
		QTimer::singleShot(0, &App::get(), [guarded] {
			if (guarded)
				App::get().openFullscreen(guarded.data());
		});
	});

	menu.addSeparator();
	connect(menu.addAction(tr("Settings…")), &QAction::triggered, this, [] { App::get().openSettings(); });
	connect(menu.addAction(tr("Close Fullscreen")), &QAction::triggered, this,
		[] { QTimer::singleShot(0, &App::get(), [] { App::get().closeFullscreen(); }); });
	menu.exec(globalPos);
}

} // namespace obsviewer
