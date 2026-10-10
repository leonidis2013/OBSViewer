#pragma once

#include <QWidget>

#include <functional>
#include <string>

class QComboBox;
class QToolButton;

namespace obsviewer {

// Slim toolbar shown above the multiview in the dock and the fullscreen window:
// layout picker, tile-click action, fullscreen / exit and settings.
class ViewerToolbar : public QWidget {
public:
	ViewerToolbar(bool fullscreen, std::function<void(const std::string &)> onLayout, QWidget *parent = nullptr);

	void setLayoutId(const std::string &id);
	void syncFromSettings(); // click action may change from menus / settings

private:
	QComboBox *layoutBox_ = nullptr;
	QComboBox *clickBox_ = nullptr;
	std::function<void(const std::string &)> onLayout_;
};

} // namespace obsviewer
