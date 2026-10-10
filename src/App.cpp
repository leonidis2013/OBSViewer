#include "App.hpp"

#include "core/Settings.hpp"
#include "ui/FullscreenWindow.hpp"
#include "ui/Menu.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/ViewerDock.hpp"
#include "update/Updater.hpp"
#include "viewer/ViewerState.hpp"

#include <plugin-support.h>

#include <QAction>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMenu>
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

	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	updater_ = new Updater(this);

	dock_ = new ViewerDock(mainWindow);
	if (!obs_frontend_add_dock_by_id(kDockId, "OBSViewer", dock_)) {
		obs_log(LOG_WARNING, "could not add the OBSViewer dock");
	}

	buildToolsMenu();

	pollTimer_ = new QTimer(this);
	pollTimer_->setInterval(kPollIntervalMs);
	connect(pollTimer_, &QTimer::timeout, this, [] { ViewerState::get().poll(); });

	obs_frontend_add_event_callback(&App::onFrontendEvent, this);
}

void App::shutdown()
{
	if (!initialised_)
		return;

	obs_frontend_remove_event_callback(&App::onFrontendEvent, this);
	if (pollTimer_)
		pollTimer_->stop();
	if (updater_)
		updater_->cancel();
	closeFullscreen();
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
		if (!exiting_)
			ViewerState::get().refresh();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		ViewerState::get().clear();
		break;

	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
	case OBS_FRONTEND_EVENT_EXIT:
		exiting_ = true;
		if (pollTimer_)
			pollTimer_->stop();
		if (updater_)
			updater_->cancel();
		closeFullscreen();
		ViewerState::get().clear();
		break;

	default:
		break;
	}
}

void App::buildToolsMenu()
{
	auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("OBSViewer"));
	if (!action)
		return;

	toolsMenu_ = new QMenu(static_cast<QWidget *>(obs_frontend_get_main_window()));
	action->setMenu(toolsMenu_);
	connect(toolsMenu_, &QMenu::aboutToShow, this, &App::rebuildToolsMenu);
	rebuildToolsMenu();
}

void App::rebuildToolsMenu()
{
	if (!toolsMenu_)
		return;
	toolsMenu_->clear();

	QMenu *fullscreen = toolsMenu_->addMenu(tr("Open Fullscreen"));
	ui::addScreenActions(fullscreen, Settings::get().windowScreen, [this](QScreen *screen) {
		openFullscreen(screen);
	});

	QAction *close = toolsMenu_->addAction(tr("Close Fullscreen"));
	close->setEnabled(fullscreenOpen());
	connect(close, &QAction::triggered, this, &App::closeFullscreen);

	toolsMenu_->addSeparator();
	connect(toolsMenu_->addAction(tr("Settings…")), &QAction::triggered, this, &App::openSettings);
	connect(toolsMenu_->addAction(tr("Check for Updates…")), &QAction::triggered, this,
		[this] { checkForUpdates(true); });
}

bool App::fullscreenOpen() const
{
	return !fullscreen_.isNull();
}

void App::openFullscreen(QScreen *screen)
{
	if (!screen)
		screen = QGuiApplication::primaryScreen();

	Settings &s = Settings::get();
	s.windowScreen = screen->name().toStdString();
	s.save();

	// Recreate rather than move: macOS fullscreen Spaces don't move cleanly between screens.
	if (fullscreen_) {
		fullscreen_->close();
		fullscreen_ = nullptr;
	}
	fullscreen_ = new FullscreenWindow();
	fullscreen_->setAttribute(Qt::WA_DeleteOnClose);
	fullscreen_->setLayoutId(s.windowLayout);
	fullscreen_->showOnScreen(screen);
}

void App::closeFullscreen()
{
	if (fullscreen_)
		fullscreen_->close();
}

void App::openSettings()
{
	if (!settings_) {
		settings_ = new SettingsDialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
		settings_->setAttribute(Qt::WA_DeleteOnClose);
	}
	settings_->show();
	settings_->raise();
	settings_->activateWindow();
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
	if (dock_)
		dock_->setLayoutId(s.dockLayout);
	if (fullscreen_)
		fullscreen_->setLayoutId(s.windowLayout);
}

} // namespace obsviewer
