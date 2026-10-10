#include "ui/MultiviewPanel.hpp"

#include "core/Settings.hpp"
#include "ui/Paint.hpp"
#include "ui/SidePanel.hpp"
#include "ui/ViewerDisplay.hpp"

#include <QGridLayout>
#include <QPainter>

namespace obsviewer {

MultiviewPanel::MultiviewPanel(bool compact, QWidget *parent) : QWidget(parent)
{
	display_ = new ViewerDisplay(this);
	side_ = new SidePanel(this);
	side_->setCompact(compact);

	auto *grid = new QGridLayout(this);
	const int m = compact ? 4 : 8;
	grid->setContentsMargins(m, m, m, m);
	grid->setSpacing(compact ? 4 : 6);
	grid->addWidget(display_, 0, 0);
	grid->addWidget(side_, 0, 1);
	grid->setColumnStretch(0, 1);
	grid->setRowStretch(0, 1);

	applySettings();
}

void MultiviewPanel::setLayoutId(const std::string &id)
{
	display_->setLayoutId(id);
}

std::string MultiviewPanel::layoutId() const
{
	return display_->layoutId();
}

void MultiviewPanel::applySettings()
{
	const Settings &s = Settings::get();
	side_->setVisible(s.showSidebar);
}

void MultiviewPanel::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), paint::col(theme::kBg));
}

} // namespace obsviewer
