#include "ui/MultiviewPanel.hpp"

#include "core/Settings.hpp"
#include "App.hpp"
#include "ui/MasterBusWindow.hpp"
#include "ui/Paint.hpp"
#include "ui/SidePanel.hpp"
#include "ui/ViewerDisplay.hpp"

#include <QGridLayout>
#include <QPainter>
#include <QPointer>
#include <QTimer>

namespace obsviewer {

MultiviewPanel::MultiviewPanel(bool compact, QWidget *parent) : QWidget(parent)
{
	display_ = new ViewerDisplay(this);
	side_ = new SidePanel(this);
	side_->setCompact(compact);

	QPointer<MultiviewPanel> self(this);
	side_->addHeaderButton(IconButton::Icon::Mixer, QStringLiteral("Master bus"), [self] {
		QTimer::singleShot(0, self, [self] {
			if (self)
				self->masterBusButton();
		});
	});

	auto *grid = new QGridLayout(this);
	grid_ = grid;
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

void MultiviewPanel::masterBusButton()
{
	if (Settings::get().masterBusEmbedded)
		setMasterBusVisible(!masterBusVisible());
	else
		App::get().openMasterBus(window());
}

bool MultiviewPanel::masterBusVisible() const
{
	return bus_ && bus_->isVisible();
}

void MultiviewPanel::setMasterBusVisible(bool visible)
{
	if (!visible) {
		if (bus_)
			bus_->hide();
		return;
	}
	if (!bus_) {
		bus_ = new MasterBusPanel(true, this);
		QPointer<MultiviewPanel> self(this);
		bus_->onHide = [self] {
			if (self)
				self->setMasterBusVisible(false);
		};
		bus_->onPopOut = [self] {
			Settings::get().masterBusEmbedded = false;
			Settings::get().save();
			App::get().applySettings(); // hides every embedded strip
			App::get().openMasterBus(self ? self->window() : nullptr);
		};
		grid_->addWidget(bus_, 0, 2);
	}
	bus_->show();
}

void MultiviewPanel::applySettings()
{
	const Settings &s = Settings::get();
	side_->setVisible(s.showSidebar);
	if (!s.masterBusEmbedded)
		setMasterBusVisible(false);
}

void MultiviewPanel::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), paint::col(theme::kBg));
}

} // namespace obsviewer
