#include "ui/ViewerDock.hpp"

#include "App.hpp"
#include "core/Settings.hpp"
#include "ui/Menu.hpp"
#include "ui/Theme.hpp"
#include "ui/ViewerDisplay.hpp"
#include "viewer/Layouts.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

namespace obsviewer {

ViewerDock::ViewerDock(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("OBSViewerRoot"));
	setStyleSheet(theme::styleSheet());

	auto *toolbar = new QWidget(this);
	toolbar->setObjectName(QStringLiteral("OBSViewerToolbar"));
	auto *bar = new QHBoxLayout(toolbar);
	bar->setContentsMargins(6, 4, 6, 4);
	bar->setSpacing(6);

	layoutBox_ = new QComboBox(toolbar);
	for (const Layout &layout : allLayouts())
		layoutBox_->addItem(QString::fromStdString(layout.name), QString::fromStdString(layout.id));
	layoutBox_->setToolTip(tr("Multiview layout"));
	connect(layoutBox_, &QComboBox::currentIndexChanged, this, &ViewerDock::onLayoutPicked);

	fullscreenButton_ = new QToolButton(toolbar);
	fullscreenButton_->setText(tr("Fullscreen"));
	fullscreenButton_->setToolTip(tr("Open the multiview fullscreen on a screen"));
	fullscreenButton_->setPopupMode(QToolButton::InstantPopup);
	auto *screenMenu = new QMenu(fullscreenButton_);
	connect(screenMenu, &QMenu::aboutToShow, this, [screenMenu] {
		screenMenu->clear();
		ui::addScreenActions(screenMenu, Settings::get().windowScreen,
				     [](QScreen *screen) { App::get().openFullscreen(screen); });
		if (App::get().fullscreenOpen()) {
			screenMenu->addSeparator();
			QObject::connect(screenMenu->addAction(tr("Close Fullscreen")), &QAction::triggered, screenMenu,
					 [] { App::get().closeFullscreen(); });
		}
	});
	fullscreenButton_->setMenu(screenMenu);

	settingsButton_ = new QToolButton(toolbar);
	settingsButton_->setText(tr("Settings"));
	settingsButton_->setToolTip(tr("OBSViewer settings and updates"));
	connect(settingsButton_, &QToolButton::clicked, this, [] { App::get().openSettings(); });

	bar->addWidget(layoutBox_, 1);
	bar->addWidget(fullscreenButton_);
	bar->addWidget(settingsButton_);

	display_ = new ViewerDisplay(this);
	connect(display_, &ViewerDisplay::contextMenuRequested, this, &ViewerDock::showContextMenu);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(toolbar);
	layout->addWidget(display_, 1);

	setMinimumSize(240, 170);
	setLayoutId(Settings::get().dockLayout);
}

void ViewerDock::setLayoutId(const std::string &id)
{
	const Layout &layout = layoutById(id);
	display_->setLayoutId(layout.id);
	const int index = layoutBox_->findData(QString::fromStdString(layout.id));
	if (index >= 0 && index != layoutBox_->currentIndex()) {
		const QSignalBlocker block(layoutBox_);
		layoutBox_->setCurrentIndex(index);
	}
}

void ViewerDock::onLayoutPicked(int index)
{
	if (index < 0)
		return;
	const std::string id = layoutBox_->itemData(index).toString().toStdString();
	display_->setLayoutId(id);
	Settings &s = Settings::get();
	if (s.dockLayout != id) {
		s.dockLayout = id;
		s.save();
	}
}

void ViewerDock::showContextMenu(const QPoint &globalPos)
{
	QMenu menu(this);
	QMenu *layouts = menu.addMenu(tr("Layout"));
	ui::addLayoutActions(layouts, display_->layoutId(), [this](const std::string &id) { setLayoutId(id); onLayoutPicked(layoutBox_->currentIndex()); });
	QMenu *screens = menu.addMenu(tr("Open Fullscreen"));
	ui::addScreenActions(screens, Settings::get().windowScreen, [](QScreen *screen) { App::get().openFullscreen(screen); });
	menu.addSeparator();
	connect(menu.addAction(tr("Settings…")), &QAction::triggered, this, [] { App::get().openSettings(); });
	menu.exec(globalPos);
}

} // namespace obsviewer
