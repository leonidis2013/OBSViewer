#include "ui/Menu.hpp"

#include "core/Settings.hpp"
#include "viewer/Layouts.hpp"

#include <QActionGroup>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QTimer>

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
