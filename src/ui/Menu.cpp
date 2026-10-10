#include "ui/Menu.hpp"

#include "viewer/Layouts.hpp"

#include <QActionGroup>
#include <QGuiApplication>
#include <QMenu>
#include <QPointer>
#include <QScreen>

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

} // namespace obsviewer::ui
