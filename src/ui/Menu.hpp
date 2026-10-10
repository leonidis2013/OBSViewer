#pragma once

#include <QMenu>
#include <QString>

#include <functional>
#include <string>

class QScreen;

namespace obsviewer::ui {

// Adds one checkable action per layout preset. For menus built fresh each time
// they are shown (context menus run with exec()).
void addLayoutActions(QMenu *menu, const std::string &currentId, const std::function<void(const std::string &)> &onPick);

// Adds one action per connected screen. For menus built fresh each time.
void addScreenActions(QMenu *menu, const std::string &currentScreen, const std::function<void(QScreen *)> &onPick);

// Human-readable screen description ("Built-in Retina Display — 1512 × 982 (main)").
QString screenTitle(QScreen *screen);

enum class MenuContext { Dock, Window, Fullscreen };

// The right-click menu for the dock and the viewer window. Every action runs
// after the menu closes. `onLayout` is called with the picked layout id.
void buildViewerMenu(QMenu &menu, const std::string &layoutId, const std::function<void(const std::string &)> &onLayout,
		     MenuContext context);

// A long-lived "pick a screen" menu for the Tools menu and the dock toolbar.
// On macOS, adding or deleting items while a native menu is opening makes clicks
// get lost, so this menu rebuilds only when screens are added or removed, never
// in aboutToShow. Picks are delivered after the menu has closed.
class ScreenMenu : public QMenu {
public:
	ScreenMenu(const QString &title, std::function<void(QScreen *)> onPick, QWidget *parent = nullptr);

	// Optional trailing item (e.g. "Close Viewer"); `enabled` is re-checked
	// each time the menu is shown.
	void setExtraAction(const QString &text, std::function<void()> onTrigger, std::function<bool()> enabled);

private:
	void rebuild();
	void refreshState();

	std::function<void(QScreen *)> onPick_;
	QString extraText_;
	std::function<void()> extraTrigger_;
	std::function<bool()> extraEnabled_;
	QAction *extraAction_ = nullptr;
};

} // namespace obsviewer::ui
