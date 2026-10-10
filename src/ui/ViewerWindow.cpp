#include "ui/ViewerWindow.hpp"

#include "App.hpp"
#include "core/Settings.hpp"
#include "ui/Menu.hpp"
#include "ui/MultiviewPanel.hpp"
#include "ui/SidePanel.hpp"
#include "ui/Theme.hpp"
#include "ui/ViewerDisplay.hpp"

#include <plugin-support.h>

#include <QAction>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace obsviewer {

// A normal titled window, switched to fullscreen when asked (macOS refuses to
// make a borderless window fullscreen), like OBS's own projectors.
ViewerWindow::ViewerWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
	setObjectName(QStringLiteral("OBSViewerRoot"));
	setWindowTitle(QStringLiteral("OBSViewer"));
	setStyleSheet(theme::styleSheet());
	setAttribute(Qt::WA_QuitOnClose, false); // closing it must never quit OBS
	setFocusPolicy(Qt::StrongFocus);
	setMinimumSize(640, 360);

	panel_ = new MultiviewPanel(false, this);
	display_ = panel_->display();
	connect(display_, &ViewerDisplay::contextMenuRequested, this, &ViewerWindow::showContextMenu);
	connect(display_, &ViewerDisplay::tileClicked, this,
		[](int kind, int scene) { App::get().handleTileClick(kind, scene, false); });
	connect(display_, &ViewerDisplay::tileDoubleClicked, this,
		[](int kind, int scene) { App::get().handleTileClick(kind, scene, true); });

	QPointer<ViewerWindow> self(this);
	SidePanel *side = panel_->sidePanel();
	side->addHeaderButton(IconButton::Icon::Settings, QStringLiteral("Settings"), [self] {
		if (self)
			QTimer::singleShot(0, self, [self] { App::get().openSettings(self); });
	});
	modeButton_ = side->addHeaderButton(IconButton::Icon::Fullscreen, QStringLiteral("Fullscreen"), [self] {
		if (self)
			QTimer::singleShot(0, &App::get(), [] { App::get().toggleViewerMode(); });
	});
	side->addHeaderButton(IconButton::Icon::Close, QStringLiteral("Close OBSViewer"), [self] {
		if (self)
			QTimer::singleShot(0, self, [self] {
				if (self)
					self->close();
			});
	});

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(panel_, 1);

	// Esc leaves fullscreen (the window stays open); Cmd+W closes. Window-wide
	// shortcuts, so they work even while the native video view has focus.
	auto *escape = new QAction(this);
	escape->setShortcut(Qt::Key_Escape);
	escape->setShortcutContext(Qt::WindowShortcut);
	addAction(escape);
	connect(escape, &QAction::triggered, this, [this] {
		if (fullscreen_)
			QTimer::singleShot(0, &App::get(), [] { App::get().openWindowed(); });
	});
	auto *closeAction = new QAction(this);
	closeAction->setShortcut(QKeySequence::Close);
	closeAction->setShortcutContext(Qt::WindowShortcut);
	addAction(closeAction);
	connect(closeAction, &QAction::triggered, this, &QWidget::close);

	applySettings();
}

void ViewerWindow::setLayoutId(const std::string &id)
{
	display_->setLayoutId(id);
}

void ViewerWindow::pickLayout(const std::string &id)
{
	setLayoutId(id);
	Settings &s = Settings::get();
	if (s.windowLayout != id) {
		s.windowLayout = id;
		s.save();
	}
}

void ViewerWindow::applySettings()
{
	panel_->applySettings();
	setLayoutId(Settings::get().windowLayout);
}

void ViewerWindow::updateModeButton()
{
	modeButton_->setIcon(fullscreen_ ? IconButton::Icon::Windowed : IconButton::Icon::Fullscreen);
	modeButton_->setToolTip(fullscreen_ ? QStringLiteral("Exit fullscreen (Esc)") : QStringLiteral("Fullscreen"));
}

void ViewerWindow::saveWindowGeometry()
{
	if (fullscreen_ || isFullScreen() || !isVisible())
		return;
	Settings &s = Settings::get();
	s.windowGeometry = saveGeometry().toBase64().toStdString();
	s.save();
}

void ViewerWindow::restoreWindowGeometry()
{
	const std::string &saved = Settings::get().windowGeometry;
	if (!saved.empty() && restoreGeometry(QByteArray::fromBase64(QByteArray::fromStdString(saved))))
		return;
	// First time: 1280 × 760, centred on the main screen.
	QScreen *screen = QGuiApplication::primaryScreen();
	const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1440, 900);
	const QSize size(std::min(1280, avail.width() - 80), std::min(760, avail.height() - 80));
	setGeometry(QRect(avail.center() - QPoint(size.width() / 2, size.height() / 2), size));
}

void ViewerWindow::showWindowed()
{
	const bool wasFullscreen = fullscreen_ || isFullScreen();
	if (!wasFullscreen && isVisible()) {
		raise();
		activateWindow();
		return;
	}
	fullscreen_ = false;
	updateModeButton();
	if (wasFullscreen) {
		showNormal();
		// macOS animates out of the fullscreen Space first; put the window back
		// where the user had it once that has finished.
		QPointer<ViewerWindow> self(this);
		QTimer::singleShot(900, this, [self] {
			if (self && !self->fullscreen_ && !self->isFullScreen())
				self->restoreWindowGeometry();
		});
	} else {
		restoreWindowGeometry();
		show();
	}
	raise();
	activateWindow();
	obs_log(LOG_INFO, "viewer: windowed");
}

void ViewerWindow::showFullscreenOn(QScreen *screen)
{
	if (!screen)
		return;
	if (!fullscreen_ && isVisible())
		saveWindowGeometry();
	fullscreen_ = true;
	updateModeButton();

	if (!isVisible() || this->screen() != screen) {
		// Place the window on the target screen first, then go fullscreen there.
		setGeometry(screen->geometry());
		winId();
		if (QWindow *window = windowHandle())
			window->setScreen(screen);
	}
	showFullScreen();
	raise();
	activateWindow();
	setFocus();
	obs_log(LOG_INFO, "viewer: fullscreen on '%s'", screen->name().toUtf8().constData());
}

void ViewerWindow::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::WindowStateChange) {
		// The green title-bar button also enters / leaves fullscreen.
		const bool fs = isFullScreen();
		if (fs != fullscreen_) {
			fullscreen_ = fs;
			updateModeButton();
		}
	}
}

void ViewerWindow::closeEvent(QCloseEvent *event)
{
	saveWindowGeometry();
	obs_log(LOG_INFO, "viewer: closed");
	QWidget::closeEvent(event);
}

void ViewerWindow::showContextMenu(const QPoint &globalPos)
{
	QMenu menu(this);
	menu.setStyleSheet(theme::styleSheet());
	QPointer<ViewerWindow> self(this);
	ui::buildViewerMenu(
		menu, display_->layoutId(),
		[self](const std::string &id) {
			if (self)
				self->pickLayout(id);
		},
		fullscreen_ ? ui::MenuContext::Fullscreen : ui::MenuContext::Window);
	menu.exec(globalPos);
}

} // namespace obsviewer
