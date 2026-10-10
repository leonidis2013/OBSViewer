#include "ui/ViewerDock.hpp"

#include "App.hpp"
#include "core/Settings.hpp"
#include "ui/Menu.hpp"
#include "ui/MultiviewPanel.hpp"
#include "ui/Theme.hpp"
#include "ui/ViewerDisplay.hpp"
#include "viewer/Layouts.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

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
	auto *screenMenu = new ui::ScreenMenu(tr("Open Fullscreen"),
					      [](QScreen *screen) { App::get().openFullscreen(screen); },
					      fullscreenButton_);
	screenMenu->setExtraAction(
		tr("Close Fullscreen"), [] { App::get().closeFullscreen(); }, [] { return App::get().fullscreenOpen(); });
	fullscreenButton_->setMenu(screenMenu);

	settingsButton_ = new QToolButton(toolbar);
	settingsButton_->setText(tr("Settings"));
	settingsButton_->setToolTip(tr("OBSViewer settings and updates"));
	connect(settingsButton_, &QToolButton::clicked, this, [] { App::get().openSettings(); });

	bar->addWidget(layoutBox_, 1);
	bar->addWidget(fullscreenButton_);
	bar->addWidget(settingsButton_);

	panel_ = new MultiviewPanel(true, this);
	display_ = panel_->display();
	connect(display_, &ViewerDisplay::contextMenuRequested, this, &ViewerDock::showContextMenu);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(toolbar);
	layout->addWidget(panel_, 1);

	setMinimumSize(420, 260);
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

void ViewerDock::applySettings()
{
	panel_->applySettings();
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
	// Actions run after the menu closes: on macOS, opening a fullscreen window or a
	// dialog while a menu is still tracking the mouse can be ignored.
	auto later = [this](std::function<void()> fn) { QTimer::singleShot(0, this, std::move(fn)); };

	QMenu menu(this);
	QMenu *layouts = menu.addMenu(tr("Layout"));
	ui::addLayoutActions(layouts, display_->layoutId(), [this, later](const std::string &id) {
		later([this, id] {
			setLayoutId(id);
			onLayoutPicked(layoutBox_->currentIndex());
		});
	});
	QMenu *screens = menu.addMenu(tr("Open Fullscreen"));
	ui::addScreenActions(screens, Settings::get().windowScreen, [later](QScreen *screen) {
		QPointer<QScreen> guarded(screen);
		later([guarded] {
			if (guarded)
				App::get().openFullscreen(guarded.data());
		});
	});
	menu.addSeparator();
	connect(menu.addAction(tr("Settings…")), &QAction::triggered, this,
		[later] { later([] { App::get().openSettings(); }); });
	menu.exec(globalPos);
}

} // namespace obsviewer
