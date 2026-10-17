#include "App.hpp"

#include "Hotkeys.hpp"

#include "bus/MasterBus.hpp"
#include "core/Settings.hpp"
#include "ui/MasterBusWindow.hpp"
#include "metrics/Metrics.hpp"
#include "ui/Menu.hpp"
#include "ui/MultiviewPanel.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/ViewerDock.hpp"
#include "ui/ViewerWindow.hpp"
#include "update/Updater.hpp"
#include "viewer/Layouts.hpp"
#include "viewer/ViewerState.hpp"

#include <plugin-support.h>

#include <QAction>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QTimer>

namespace obsviewer {

namespace {
constexpr const char *kDockId = "obsviewer-multiview";
constexpr int kPollIntervalMs = 500;
} // namespace

App &App::get()
{
	static App app;
	return app;
}

void App::init()
{
	if (initialised_)
		return;
	initialised_ = true;

	Settings::get().load();
	ViewerState::get().setShowLabels(Settings::get().showLabels);
	ViewerState::get().setShowTileMeters(Settings::get().tileMeters);
	ViewerState::get().setSafeAreas(Settings::get().safeAreas);

	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	updater_ = new Updater(this);

	dock_ = new ViewerDock(mainWindow);
	if (!obs_frontend_add_dock_by_id(kDockId, "OBSViewer", dock_)) {
		obs_log(LOG_WARNING, "could not add the OBSViewer dock");
	}

	buildToolsMenu();
	Hotkeys::get().registerAll();

	pollTimer_ = new QTimer(this);
	pollTimer_->setInterval(kPollIntervalMs);
	connect(pollTimer_, &QTimer::timeout, this, [] {
		ViewerState::get().poll();
		MasterBus::get().checkLatency();
	});

	obs_frontend_add_event_callback(&App::onFrontendEvent, this);
}

void App::shutdown()
{
	if (!initialised_)
		return;

	obs_frontend_remove_event_callback(&App::onFrontendEvent, this);
	Metrics::get().stop();
	MasterBus::get().unload();
	if (pollTimer_)
		pollTimer_->stop();
	if (updater_)
		updater_->cancel();
	closeViewer();
	if (settings_)
		settings_->close();
	ViewerState::get().clear();
	initialised_ = false;
}

void App::onFrontendEvent(enum obs_frontend_event event, void *param)
{
	static_cast<App *>(param)->handleFrontendEvent(event);
}

void App::handleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		ViewerState::get().refresh();
		pollTimer_->start();
		Metrics::get().start();
		MasterBus::get().load();
		// Give OBS a moment to settle before talking to the network.
		QTimer::singleShot(8000, this, [this] {
			if (!exiting_)
				checkForUpdates(false);
		});
		break;

	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		if (!exiting_) {
			ViewerState::get().refresh();
			MasterBus::get().collectionChanged();
		}
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		ViewerState::get().clear();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		MasterBus::get().collectionChanging();
		break;

	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
	case OBS_FRONTEND_EVENT_EXIT:
		Hotkeys::get().save();
		exiting_ = true;
		Metrics::get().stop();
		if (masterBus_)
			masterBus_->close();
		MasterBus::get().unload();
		if (pollTimer_)
			pollTimer_->stop();
		if (updater_)
			updater_->cancel();
		closeViewer();
		ViewerState::get().clear();
		break;

	default:
		break;
	}
}

void App::buildToolsMenu()
{
	auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("OBSViewer"));
	if (!action) {
		obs_log(LOG_WARNING, "could not add the Tools menu entry");
		return;
	}

	// Built once. On macOS the Tools menu is a native menu, and adding or removing
	// items while it opens makes clicks get lost, so nothing here is rebuilt on show.
	toolsMenu_ = new QMenu(static_cast<QWidget *>(obs_frontend_get_main_window()));
	action->setMenu(toolsMenu_);

	// Every action runs after the menu has closed (see ScreenMenu for why).
	auto later = [this](auto fn) { return [this, fn] { QTimer::singleShot(0, this, fn); }; };

	connect(toolsMenu_->addAction(tr("Open Window")), &QAction::triggered, this, later([this] { openWindowed(); }));
	auto *screens = new ui::ScreenMenu(tr("Open Fullscreen"), [this](QScreen *screen) { openFullscreen(screen); },
					   toolsMenu_);
	toolsMenu_->addMenu(screens);
	connect(toolsMenu_->addAction(tr("Close Window")), &QAction::triggered, this, later([this] { closeViewer(); }));
	toolsMenu_->addSeparator();
	connect(toolsMenu_->addAction(tr("Master Bus…")), &QAction::triggered, this,
		later([this] { openMasterBus(); }));
	connect(toolsMenu_->addAction(tr("Settings…")), &QAction::triggered, this, later([this] { openSettings(); }));
	connect(toolsMenu_->addAction(tr("Check for Updates…")), &QAction::triggered, this,
		later([this] { checkForUpdates(true); }));
}

bool App::viewerOpen() const
{
	return !viewer_.isNull();
}

std::string App::fullscreenScreenName() const
{
	if (!viewer_ || !viewer_->fullscreen() || !viewer_->isVisible() || !viewer_->screen())
		return {};
	return viewer_->screen()->name().toStdString();
}

ViewerWindow *App::ensureViewer()
{
	if (!viewer_) {
		viewer_ = new ViewerWindow();
		viewer_->setAttribute(Qt::WA_DeleteOnClose);
		connect(viewer_, &QObject::destroyed, this,
			[this] { QTimer::singleShot(0, this, [this] { updateDockPause(); }); });
	}
	return viewer_;
}

void App::updateDockPause()
{
	if (!dock_)
		return;
	const bool viewerShowing = viewer_ && viewer_->isVisible();
	if (!viewerShowing)
		dockAnyway_ = false;
	dock_->panel()->setPaused(viewerShowing && !dockAnyway_);
}

void App::showDockAnyway()
{
	dockAnyway_ = true;
	updateDockPause();
}

void App::openWindowed()
{
	ensureViewer()->showWindowed();
	updateDockPause();
}

void App::openFullscreen(QScreen *screen)
{
	if (!screen) {
		const QString saved = QString::fromStdString(Settings::get().windowScreen);
		for (QScreen *s : QGuiApplication::screens())
			if (s->name() == saved)
				screen = s;
	}
	if (!screen)
		screen = QGuiApplication::primaryScreen();
	if (!screen) {
		obs_log(LOG_WARNING, "fullscreen: no screen available");
		return;
	}
	const QRect g = screen->geometry();
	obs_log(LOG_INFO, "fullscreen: opening on '%s' (%d,%d %dx%d, scale %.1f)",
		screen->name().toUtf8().constData(), g.x(), g.y(), g.width(), g.height(), screen->devicePixelRatio());

	Settings &s = Settings::get();
	s.windowScreen = screen->name().toStdString();
	s.save();

	// Already fullscreen on another screen: recreate rather than move, because
	// macOS fullscreen Spaces don't move cleanly between screens.
	if (viewer_ && viewer_->fullscreen() && viewer_->screen() != screen) {
		viewer_->close();
		viewer_ = nullptr;
	}
	ensureViewer()->showFullscreenOn(screen);
	updateDockPause();

	// Report what macOS actually did, so a log shows whether fullscreen took effect.
	QPointer<ViewerWindow> window = viewer_;
	QTimer::singleShot(2000, this, [window] {
		if (!window)
			return;
		const QRect wg = window->geometry();
		obs_log(LOG_INFO, "fullscreen: visible=%d fullscreen=%d geometry=%d,%d %dx%d screen='%s'",
			window->isVisible(), window->isFullScreen(), wg.x(), wg.y(), wg.width(), wg.height(),
			window->screen() ? window->screen()->name().toUtf8().constData() : "?");
	});
}

void App::toggleViewerMode()
{
	if (viewer_ && viewer_->fullscreen())
		openWindowed();
	else
		openFullscreen(viewer_ ? viewer_->screen() : nullptr);
}

void App::closeViewer()
{
	if (viewer_)
		viewer_->close();
}

void App::openSettings(QWidget *parent)
{
	obs_log(LOG_INFO, "settings: opening");
	if (!parent)
		parent = static_cast<QWidget *>(obs_frontend_get_main_window());
	// A dialog belongs to its parent's window / fullscreen Space on macOS, so
	// reopen it on the window that asked if that changed.
	if (settings_ && settings_->parentWidget() != parent) {
		settings_->close();
		settings_ = nullptr;
	}
	if (!settings_) {
		settings_ = new SettingsDialog(parent);
		settings_->setAttribute(Qt::WA_DeleteOnClose);
	}
	settings_->show();
	settings_->raise();
	settings_->activateWindow();
	obs_log(LOG_INFO, "settings: visible=%d", settings_->isVisible());
}

MultiviewPanel *App::masterBusHost(QWidget *from)
{
	QWidget *w = from ? from->window() : nullptr;
	if (viewer_ && w == viewer_.data())
		return viewer_->panel();
	if (dock_ && from && (from == dock_.data() || dock_->isAncestorOf(from)))
		return dock_->panel();
	if (viewer_ && viewer_->isVisible())
		return viewer_->panel();
	if (dock_ && dock_->isVisible())
		return dock_->panel();
	// Nothing open to put it in: open the viewer window.
	openWindowed();
	return viewer_ ? viewer_->panel() : nullptr;
}

void App::openMasterBus(QWidget *parent)
{
	if (Settings::get().masterBusEmbedded) {
		if (MultiviewPanel *host = masterBusHost(parent)) {
			obs_log(LOG_INFO, "master bus: showing next to the side bar");
			host->setMasterBusVisible(true);
			return;
		}
	}
	obs_log(LOG_INFO, "master bus: opening window");
	if (!parent)
		parent = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (masterBus_ && masterBus_->parentWidget() != parent) {
		masterBus_->close();
		masterBus_ = nullptr;
	}
	if (!masterBus_) {
		masterBus_ = new MasterBusWindow(parent);
		masterBus_->setAttribute(Qt::WA_DeleteOnClose);
		QPointer<MasterBusWindow> window = masterBus_;
		masterBus_->panel()->onDockIn = [this, window] {
			Settings::get().masterBusEmbedded = true;
			Settings::get().save();
			if (window)
				window->close();
			openMasterBus(nullptr);
		};
	}
	masterBus_->show();
	masterBus_->raise();
	masterBus_->activateWindow();
}

void App::switchScene(int sceneIndex, int action)
{
	obs_source_t *scene = ViewerState::get().sceneAt(sceneIndex);
	if (!scene)
		return;
	const bool studio = obs_frontend_preview_program_mode_active();
	if (action == Settings::ClickPreview && studio) {
		obs_log(LOG_INFO, "scene: '%s' to preview", obs_source_get_name(scene));
		obs_frontend_set_current_preview_scene(scene);
	} else if (action == Settings::ClickPreview || action == Settings::ClickProgram) {
		// Outside Studio Mode there is no preview, so either action switches the scene.
		obs_log(LOG_INFO, "scene: '%s' to program", obs_source_get_name(scene));
		obs_frontend_set_current_scene(scene);
	}
	obs_source_release(scene);
	ViewerState::get().refresh();
}

void App::handleTileClick(int kind, int sceneIndex, bool doubleClick)
{
	const int action = Settings::get().clickAction;
	if (action == Settings::ClickOff)
		return;
	const auto tile = static_cast<TileKind>(kind);
	if (tile == TileKind::Scene && !doubleClick) {
		switchScene(sceneIndex, action);
	} else if (tile == TileKind::Preview && doubleClick && obs_frontend_preview_program_mode_active()) {
		// Double-click Preview = take it to Program with the current transition.
		obs_log(LOG_INFO, "scene: transition preview to program");
		obs_frontend_preview_program_trigger_transition();
	}
}

void App::checkForUpdates(bool interactive)
{
	if (!updater_)
		return;
	if (interactive) {
		updater_->checkNow(true);
	} else {
		updater_->checkIfDue();
	}
}

void App::applySettings()
{
	const Settings &s = Settings::get();
	ViewerState::get().setShowLabels(s.showLabels);
	ViewerState::get().setShowTileMeters(s.tileMeters);
	ViewerState::get().setSafeAreas(s.safeAreas);
	ViewerState::get().updateViewScenes();
	if (dock_) {
		dock_->setLayoutId(s.dockLayout);
		dock_->applySettings();
	}
	if (viewer_)
		viewer_->applySettings();
}

} // namespace obsviewer
