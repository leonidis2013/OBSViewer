#include "ui/ViewerDock.hpp"

#include "App.hpp"
#include "core/Settings.hpp"
#include "ui/Menu.hpp"
#include "ui/MultiviewPanel.hpp"
#include "ui/SidePanel.hpp"
#include "ui/Theme.hpp"
#include "ui/ViewerDisplay.hpp"
#include "viewer/Layouts.hpp"

#include <QMenu>
#include <QTimer>
#include <QVBoxLayout>

namespace obsviewer {

ViewerDock::ViewerDock(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("OBSViewerRoot"));
	setStyleSheet(theme::styleSheet());

	panel_ = new MultiviewPanel(true, this);
	display_ = panel_->display();
	connect(display_, &ViewerDisplay::contextMenuRequested, this, &ViewerDock::showContextMenu);
	connect(display_, &ViewerDisplay::tileClicked, this,
		[](int kind, int scene) { App::get().handleTileClick(kind, scene, false); });
	connect(display_, &ViewerDisplay::tileDoubleClicked, this,
		[](int kind, int scene) { App::get().handleTileClick(kind, scene, true); });

	// Header icons. Each action runs once the click has finished.
	SidePanel *side = panel_->sidePanel();
	side->addHeaderButton(IconButton::Icon::Settings, QStringLiteral("Settings"),
			      [] { QTimer::singleShot(0, &App::get(), [] { App::get().openSettings(); }); });
	side->addHeaderButton(IconButton::Icon::PopOut, QStringLiteral("Open in a window"),
			      [] { QTimer::singleShot(0, &App::get(), [] { App::get().openWindowed(); }); });
	side->addHeaderButton(IconButton::Icon::Fullscreen, QStringLiteral("Open fullscreen"),
			      [] { QTimer::singleShot(0, &App::get(), [] { App::get().openFullscreen(nullptr); }); });

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(panel_, 1);

	setMinimumSize(420, 260);
	setLayoutId(Settings::get().dockLayout);
}

void ViewerDock::setLayoutId(const std::string &id)
{
	display_->setLayoutId(layoutById(id).id);
}

void ViewerDock::applySettings()
{
	panel_->applySettings();
}

void ViewerDock::pickLayout(const std::string &id)
{
	setLayoutId(id);
	Settings &s = Settings::get();
	if (s.dockLayout != id) {
		s.dockLayout = id;
		s.save();
	}
}

void ViewerDock::showContextMenu(const QPoint &globalPos)
{
	QMenu menu(this);
	menu.setStyleSheet(theme::styleSheet());
	ui::buildViewerMenu(menu, display_->layoutId(), [this](const std::string &id) { pickLayout(id); },
			    ui::MenuContext::Dock);
	menu.exec(globalPos);
}

} // namespace obsviewer
