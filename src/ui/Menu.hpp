#pragma once

#include <QString>

#include <functional>
#include <string>

class QMenu;
class QScreen;

namespace obsviewer::ui {

// Adds one checkable action per layout preset.
void addLayoutActions(QMenu *menu, const std::string &currentId, const std::function<void(const std::string &)> &onPick);

// Adds one action per connected screen ("Built-in Display — 1512 × 982").
void addScreenActions(QMenu *menu, const std::string &currentScreen, const std::function<void(QScreen *)> &onPick);

// Human-readable screen description.
QString screenTitle(QScreen *screen);

} // namespace obsviewer::ui
