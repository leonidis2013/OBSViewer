#include "ui/Menu.hpp"

#include "App.hpp"
#include "core/Settings.hpp"
#include "viewer/ViewerState.hpp"
#include "viewer/Layouts.hpp"

#include <QActionGroup>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QTimer>

#include <obs-frontend-api.h>

namespace obsviewer::ui {

void addLayoutActions(QMenu *menu, const std::string &currentId, const std::function<void(const std::string &)> &onPick)
{
	auto *group = new QActionGroup(menu);
	group->setExclusive(true);
	for (const Layout &layout : allLayouts()) {
		QAction *action = menu->addAction(QString::fromStdString(layout.name));
		action->setCheckable(true);
		action->setChecked(layout.id == currentId);
		group->addAction(action);
		const std::string id = layout.id;
		QObject::connect(action, &QAction::triggered, menu, [onPick, id] { onPick(id); });
	}
}

QString screenTitle(QScreen *screen)
{
	if (!screen)
		return QString();
	const QSize size = screen->size();
	QString name = screen->name();
	if (name.isEmpty())
		name = QStringLiteral("Screen");
	QString title = QStringLiteral("%1 — %2 × %3").arg(name).arg(size.width()).arg(size.height());
	if (screen == QGuiApplication::primaryScreen())
		title += QStringLiteral(" (main)");
	return title;
}

void addScreenActions(QMenu *menu, const std::string &currentScreen, const std::function<void(QScreen *)> &onPick)
{
	const auto screens = QGuiApplication::screens();
	for (QScreen *screen : screens) {
		QAction *action = menu->addAction(screenTitle(screen));
		action->setCheckable(true);
		action->setChecked(!currentScreen.empty() && screen->name().toStdString() == currentScreen);
		QPointer<QScreen> guarded(screen);
		QObject::connect(action, &QAction::triggered, menu, [onPick, guarded] {
			if (guarded)
				onPick(guarded.data());
		});
	}
}

namespace {

void later(std::function<void()> fn)
{
	QTimer::singleShot(0, &App::get(), std::move(fn));
}

void addSceneActions(QMenu *menu, int action)
{
	obs_source_t *current = action == Settings::ClickPreview && obs_frontend_preview_program_mode_active()
					? obs_frontend_get_current_preview_scene()
					: obs_frontend_get_current_scene();
	const ViewerState &state = ViewerState::get();
	for (int i = 0; i < state.sceneCount(); ++i) {
		QAction *a = menu->addAction(QStringLiteral("%1   %2").arg(i + 1).arg(QString::fromStdString(state.sceneName(i))));
		a->setCheckable(true);
		obs_source_t *scene = state.sceneAt(i);
		a->setChecked(scene && scene == current);
		obs_source_release(scene);
		QObject::connect(a, &QAction::triggered, menu, [i, action] { later([i, action] { App::get().switchScene(i, action); }); });
	}
	obs_source_release(current);
	if (state.sceneCount() == 0)
		menu->addAction(QStringLiteral("No scenes"))->setEnabled(false);
}

} // namespace

void buildViewerMenu(QMenu &menu, const std::string &layoutId, const std::function<void(const std::string &)> &onLayout,
		     bool fullscreen)
{
	QMenu *layouts = menu.addMenu(QStringLiteral("Layout"));
	addLayoutActions(layouts, layoutId, [onLayout](const std::string &id) { later([onLayout, id] { onLayout(id); }); });

	if (obs_frontend_preview_program_mode_active()) {
		addSceneActions(menu.addMenu(QStringLiteral("Send to Preview")), Settings::ClickPreview);
		addSceneActions(menu.addMenu(QStringLiteral("Cut to Program")), Settings::ClickProgram);
	} else {
		addSceneActions(menu.addMenu(QStringLiteral("Switch Scene")), Settings::ClickProgram);
	}

	QMenu *click = menu.addMenu(QStringLiteral("Clicking a Tile"));
	auto *group = new QActionGroup(click);
	const std::pair<int, const char *> options[] = {{Settings::ClickOff, "Does Nothing"},
							{Settings::ClickPreview, "Sends It to Preview"},
							{Settings::ClickProgram, "Cuts It to Program"}};
	for (const auto &[value, text] : options) {
		QAction *a = click->addAction(QString::fromUtf8(text));
		a->setCheckable(true);
		a->setChecked(Settings::get().clickAction == value);
		group->addAction(a);
		const int v = value;
		QObject::connect(a, &QAction::triggered, click, [v] {
			later([v] {
				Settings::get().clickAction = v;
				Settings::get().save();
				App::get().applySettings();
			});
		});
	}

	menu.addSeparator();
	if (fullscreen) {
		QMenu *screens = menu.addMenu(QStringLiteral("Move to Screen"));
		addScreenActions(screens, Settings::get().windowScreen, [](QScreen *screen) {
			QPointer<QScreen> guarded(screen);
			later([guarded] {
				if (guarded)
					App::get().openFullscreen(guarded.data());
			});
		});
		QAction *toolbar = menu.addAction(QStringLiteral("Show Toolbar"));
		toolbar->setCheckable(true);
		toolbar->setChecked(Settings::get().fullscreenToolbar);
		QObject::connect(toolbar, &QAction::triggered, &menu, [](bool on) {
			later([on] {
				Settings::get().fullscreenToolbar = on;
				Settings::get().save();
				App::get().applySettings();
			});
		});
	} else {
		QMenu *screens = menu.addMenu(QStringLiteral("Open Fullscreen"));
		addScreenActions(screens, Settings::get().windowScreen, [](QScreen *screen) {
			QPointer<QScreen> guarded(screen);
			later([guarded] {
				if (guarded)
					App::get().openFullscreen(guarded.data());
			});
		});
	}

	QWidget *owner = menu.parentWidget() ? menu.parentWidget()->window() : nullptr;
	QPointer<QWidget> guardedOwner(owner);
	QObject::connect(menu.addAction(QStringLiteral("Settings…")), &QAction::triggered, &menu,
			 [guardedOwner] { later([guardedOwner] { App::get().openSettings(guardedOwner.data()); }); });
	if (fullscreen)
		QObject::connect(menu.addAction(QStringLiteral("Exit Fullscreen")), &QAction::triggered, &menu,
				 [] { later([] { App::get().closeFullscreen(); }); });
}

ScreenMenu::ScreenMenu(const QString &title, std::function<void(QScreen *)> onPick, QWidget *parent)
	: QMenu(title, parent),
	  onPick_(std::move(onPick))
{
	auto *app = qobject_cast<QGuiApplication *>(QGuiApplication::instance());
	auto schedule = [this] { QTimer::singleShot(0, this, [this] { rebuild(); }); };
	connect(app, &QGuiApplication::screenAdded, this, schedule);
	connect(app, &QGuiApplication::screenRemoved, this, schedule);
	connect(app, &QGuiApplication::primaryScreenChanged, this, schedule);
	// Only toggles check marks / enabled state; never adds or removes items.
	connect(this, &QMenu::aboutToShow, this, &ScreenMenu::refreshState);
	rebuild();
}

void ScreenMenu::setExtraAction(const QString &text, std::function<void()> onTrigger, std::function<bool()> enabled)
{
	extraText_ = text;
	extraTrigger_ = std::move(onTrigger);
	extraEnabled_ = std::move(enabled);
	rebuild();
}

void ScreenMenu::rebuild()
{
	clear();
	extraAction_ = nullptr;

	for (QScreen *screen : QGuiApplication::screens()) {
		QAction *action = addAction(screenTitle(screen));
		action->setCheckable(true);
		action->setData(screen->name());
		QPointer<QScreen> guarded(screen);
		connect(action, &QAction::triggered, this, [this, guarded] {
			// Run after the menu has fully closed; macOS ignores fullscreen
			// requests made while a menu is still tracking the mouse.
			QTimer::singleShot(0, this, [this, guarded] {
				if (guarded && onPick_)
					onPick_(guarded.data());
			});
		});
	}

	if (extraTrigger_) {
		addSeparator();
		extraAction_ = addAction(extraText_);
		connect(extraAction_, &QAction::triggered, this,
			[this] { QTimer::singleShot(0, this, [this] { extraTrigger_(); }); });
	}
	refreshState();
}

void ScreenMenu::refreshState()
{
	const QString current = QString::fromStdString(Settings::get().windowScreen);
	for (QAction *action : actions()) {
		if (action->isCheckable())
			action->setChecked(!current.isEmpty() && action->data().toString() == current);
	}
	if (extraAction_ && extraEnabled_)
		extraAction_->setEnabled(extraEnabled_());
}

} // namespace obsviewer::ui
