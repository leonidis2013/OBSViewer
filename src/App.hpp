#pragma once

#include <QObject>
#include <QPointer>

#include <string>

#include <obs-frontend-api.h>

class QMenu;
class QWidget;
class QScreen;
class QTimer;

namespace obsviewer {

class ViewerDock;
class ViewerWindow;
class SettingsDialog;
class MasterBusWindow;
class MultiviewPanel;
class Updater;

// Owns OBSViewer's UI objects and wires them to OBS. Qt main thread only.
class App : public QObject {
	Q_OBJECT

public:
	static App &get();

	void init();
	void shutdown();

	// The viewer window is either windowed or fullscreen, never both: opening one
	// mode switches the existing window to it. screen = null: the last screen used.
	void openWindowed();
	void openFullscreen(QScreen *screen);
	void toggleViewerMode();
	void closeViewer();
	bool viewerOpen() const;
	// Dock placeholder's "Show Here Too": draw in the dock as well, this session.
	void showDockAnyway();
	// Name of the screen the viewer is fullscreen on; empty when it isn't fullscreen.
	std::string fullscreenScreenName() const;
	// parent: the window asking (so the dialog opens on top of fullscreen), or null for OBS.
	void openSettings(QWidget *parent = nullptr);
	// Next to the side bar of the asking view (Settings ▸ embedded, the default), or
	// in its own window.
	void openMasterBus(QWidget *parent = nullptr);
	// action: Settings::ClickPreview or Settings::ClickProgram.
	void switchScene(int sceneIndex, int action);
	// A tile in a multiview was clicked (kind = TileKind as int).
	void handleTileClick(int kind, int sceneIndex, bool doubleClick);
	void checkForUpdates(bool interactive);

	// Called after settings change so every view picks up new values.
	void applySettings();

	Updater *updater() const { return updater_; }

private:
	App() = default;

	static void onFrontendEvent(enum obs_frontend_event event, void *param);
	void handleFrontendEvent(enum obs_frontend_event event);
	void buildToolsMenu();
	ViewerWindow *ensureViewer();
	// Where an embedded master bus goes for a request from `from`.
	MultiviewPanel *masterBusHost(QWidget *from);
	// Pauses the dock's multiview while the viewer window is open, and turns
	// OBS's own preview off then (Settings), so the picture isn't drawn twice.
	void updateDockPause();
	void updateObsPreview();
	void restoreObsPreview();
	bool obsPreviewHidden_ = false;   // we turned OBS's preview off
	bool obsPreviewStudio_ = false;   // … while in Studio Mode (only the display was hidden)
	bool dockAnyway_ = false;

	QPointer<ViewerDock> dock_;
	QPointer<ViewerWindow> viewer_;
	QPointer<SettingsDialog> settings_;
	QPointer<MasterBusWindow> masterBus_;
	QPointer<QMenu> toolsMenu_;
	QTimer *pollTimer_ = nullptr;
	Updater *updater_ = nullptr;
	bool initialised_ = false;
	bool exiting_ = false;
};

} // namespace obsviewer
