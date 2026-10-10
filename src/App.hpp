#pragma once

#include <QObject>
#include <QPointer>

#include <obs-frontend-api.h>

class QMenu;
class QScreen;
class QTimer;

namespace obsviewer {

class ViewerDock;
class FullscreenWindow;
class SettingsDialog;
class Updater;

// Owns OBSViewer's UI objects and wires them to OBS. Qt main thread only.
class App : public QObject {
	Q_OBJECT

public:
	static App &get();

	void init();
	void shutdown();

	void openFullscreen(QScreen *screen);
	void closeFullscreen();
	bool fullscreenOpen() const;
	void openSettings();
	void checkForUpdates(bool interactive);

	// Called after settings change so every view picks up new values.
	void applySettings();

	Updater *updater() const { return updater_; }

private:
	App() = default;

	static void onFrontendEvent(enum obs_frontend_event event, void *param);
	void handleFrontendEvent(enum obs_frontend_event event);
	void buildToolsMenu();
	void rebuildToolsMenu();

	QPointer<ViewerDock> dock_;
	QPointer<FullscreenWindow> fullscreen_;
	QPointer<SettingsDialog> settings_;
	QPointer<QMenu> toolsMenu_;
	QTimer *pollTimer_ = nullptr;
	Updater *updater_ = nullptr;
	bool initialised_ = false;
	bool exiting_ = false;
};

} // namespace obsviewer
