#include "ui/MultiviewPanel.hpp"

#include "core/Settings.hpp"
#include "App.hpp"
#include "bus/Engine.hpp"
#include "bus/MasterBus.hpp"
#include "ui/MasterBusWindow.hpp"
#include "ui/Paint.hpp"
#include "ui/SidePanel.hpp"
#include "ui/ViewerDisplay.hpp"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPointer>
#include <QTimer>

namespace obsviewer {

MultiviewPanel::MultiviewPanel(bool compact, QWidget *parent) : QWidget(parent)
{
	display_ = new ViewerDisplay(this);
	side_ = new SidePanel(this);
	side_->setCompact(compact);

	QPointer<MultiviewPanel> self(this);
	busButton_ = side_->addHeaderButton(IconButton::Icon::Mixer, QStringLiteral("Master bus"), [self] {
		QTimer::singleShot(0, self, [self] {
			if (self)
				self->masterBusButton();
		});
	});
	// Bus status on the icon: green dot = running, amber = reducing ≥ 1 dB.
	auto *busStatus = new QTimer(this);
	busStatus->setInterval(250);
	connect(busStatus, &QTimer::timeout, this, [this] {
		if (!isVisible() || !busButton_)
			return;
		const MasterBus &bus = MasterBus::get();
		const float gr = bus.running() ? bus::Engine::get().totalReduction() : 0.0f;
		busButton_->setBadge(!bus.running() ? 0u : gr >= 1.0f ? theme::kAccentAmber : theme::kGood);
		const QString tip = !bus.enabled()   ? QStringLiteral("Master bus · off")
				    : !bus.running() ? QStringLiteral("Master bus · on, not running (see the OBS log)")
				    : gr >= 0.1f     ? QStringLiteral("Master bus · on · reducing %1 dB").arg(gr, 0, 'f', 1)
						     : QStringLiteral("Master bus · on");
		if (busButton_->toolTip() != tip)
			busButton_->setToolTip(tip);
	});
	busStatus->start();

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

void MultiviewPanel::setPaused(bool paused)
{
	if (paused && !pausedNote_) {
		pausedNote_ = new QWidget(this);
		auto *l = new QVBoxLayout(pausedNote_);
		l->addStretch(1);
		auto *text = new QLabel(QStringLiteral("The multiview is showing in the OBSViewer window."), pausedNote_);
		text->setAlignment(Qt::AlignCenter);
		text->setWordWrap(true);
		text->setProperty("dim", true);
		auto *button = new QPushButton(QStringLiteral("Show Here Too"), pausedNote_);
		button->setToolTip(QStringLiteral("Draw it in the dock as well (uses more graphics power)"));
		connect(button, &QPushButton::clicked, this, [] {
			QTimer::singleShot(0, &App::get(), [] { App::get().showDockAnyway(); });
		});
		l->addWidget(text);
		l->addWidget(button, 0, Qt::AlignHCenter);
		l->addStretch(1);
		grid_->addWidget(pausedNote_, 0, 0);
	}
	if (pausedNote_)
		pausedNote_->setVisible(paused);
	display_->setVisible(!paused); // hiding the display stops its drawing
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
