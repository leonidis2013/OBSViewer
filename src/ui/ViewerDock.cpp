#include "ui/ViewerDock.hpp"

#include "App.hpp"
#include "core/Settings.hpp"
#include "ui/Menu.hpp"
#include "ui/MultiviewPanel.hpp"
#include "ui/Theme.hpp"
#include "ui/ViewerDisplay.hpp"
#include "ui/ViewerToolbar.hpp"
#include "viewer/Layouts.hpp"

#include <QMenu>
#include <QVBoxLayout>

namespace obsviewer {

ViewerDock::ViewerDock(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("OBSViewerRoot"));
	setStyleSheet(theme::styleSheet());

	toolbar_ = new ViewerToolbar(false, [this](const std::string &id) { pickLayout(id); }, this);

	panel_ = new MultiviewPanel(true, this);
	display_ = panel_->display();
	connect(display_, &ViewerDisplay::contextMenuRequested, this, &ViewerDock::showContextMenu);
	connect(display_, &ViewerDisplay::tileClicked, this,
		[](int kind, int scene) { App::get().handleTileClick(kind, scene, false); });
	connect(display_, &ViewerDisplay::tileDoubleClicked, this,
		[](int kind, int scene) { App::get().handleTileClick(kind, scene, true); });

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(toolbar_);
	layout->addWidget(panel_, 1);

	setMinimumSize(420, 260);
	setLayoutId(Settings::get().dockLayout);
}

void ViewerDock::setLayoutId(const std::string &id)
{
	const Layout &layout = layoutById(id);
	display_->setLayoutId(layout.id);
	toolbar_->setLayoutId(layout.id);
}

void ViewerDock::applySettings()
{
	panel_->applySettings();
	toolbar_->syncFromSettings();
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
	ui::buildViewerMenu(menu, display_->layoutId(), [this](const std::string &id) { pickLayout(id); }, false);
	menu.exec(globalPos);
}

} // namespace obsviewer
