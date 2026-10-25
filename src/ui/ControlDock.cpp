#include "ui/ControlDock.hpp"

#include "App.hpp"
#include "bus/Engine.hpp"
#include "bus/MasterBus.hpp"
#include "core/Settings.hpp"
#include "stream/StreamTitle.hpp"
#include "ui/Menu.hpp"
#include "ui/Theme.hpp"
#include "ui/TransitionControls.hpp"
#include "viewer/Layouts.hpp"

#include <obs-frontend-api.h>

#include <QComboBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <string>

namespace obsviewer {

namespace {

// Runs after the click has finished (macOS ignores fullscreen requests made
// while a button or menu is still tracking the mouse).
void later(std::function<void()> fn)
{
	QTimer::singleShot(0, &App::get(), std::move(fn));
}

QLabel *caption(const QString &text, QWidget *parent)
{
	auto *l = new QLabel(text, parent);
	l->setStyleSheet(QStringLiteral("color: #7C8794; font-size: 11px; font-weight: 600;"));
	return l;
}

} // namespace

ControlDock::ControlDock(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("OBSViewerRoot"));
	setStyleSheet(theme::styleSheet() + QStringLiteral(R"(
QPushButton, QToolButton { padding: 6px 8px; }
QPushButton:checked, QToolButton:checked { background-color: #1F3A2A; border-color: #3ECF6E; color: #FFFFFF; }
QPushButton#busPower:checked { background-color: #3ECF6E; border-color: #3ECF6E; color: #0D1015; }
)"));

	// ---- viewer ----
	dot_ = new QLabel(this);
	dot_->setFixedSize(10, 10);
	status_ = new QLabel(this);
	status_->setStyleSheet(QStringLiteral("color: #D8DEE6; font-weight: 600;"));
	auto *statusRow = new QHBoxLayout();
	statusRow->setSpacing(8);
	statusRow->addWidget(dot_);
	statusRow->addWidget(status_, 1);

	streamTitle_ = new QLabel(this);
	streamTitle_->setWordWrap(true);
	streamTitle_->setTextFormat(Qt::RichText);
	streamTitle_->setToolTip(QStringLiteral("The YouTube stream's title (Settings ▸ YouTube)"));
	streamTitle_->hide();

	window_ = new QPushButton(QStringLiteral("Window"), this);
	window_->setCheckable(true);
	window_->setToolTip(QStringLiteral("Show the multiview in a window"));

	fullscreen_ = new QToolButton(this);
	fullscreen_->setText(QStringLiteral("Fullscreen"));
	fullscreen_->setCheckable(true);
	fullscreen_->setToolTip(QStringLiteral("Fullscreen on the last screen used (click again for a window); the arrow picks another screen"));
	fullscreen_->setPopupMode(QToolButton::MenuButtonPopup);
	fullscreen_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	fullscreen_->setMenu(
		new ui::ScreenMenu(QStringLiteral("Screen"), [](QScreen *s) { App::get().openFullscreen(s); }, fullscreen_));

	close_ = new QPushButton(QStringLiteral("Close"), this);
	close_->setToolTip(QStringLiteral("Close the OBSViewer window"));

	auto *viewerRow = new QHBoxLayout();
	viewerRow->setSpacing(4);
	viewerRow->addWidget(window_, 1);
	viewerRow->addWidget(fullscreen_, 1);
	viewerRow->addWidget(close_, 1);

	layout_ = new QComboBox(this);
	for (const LayoutInfo &l : allLayouts())
		layout_->addItem(QString::fromStdString(l.name), QString::fromStdString(l.id));
	layout_->setToolTip(QStringLiteral("Layout of the OBSViewer window"));

	// ---- master bus ----
	busPower_ = new QPushButton(QStringLiteral("Bus OFF"), this);
	busPower_->setObjectName(QStringLiteral("busPower"));
	busPower_->setCheckable(true);
	busPower_->setToolTip(QStringLiteral("Master bus on / off"));
	busOpen_ = new QPushButton(QStringLiteral("Open Bus"), this);
	busOpen_->setToolTip(QStringLiteral("Open the master bus in its own window"));
	busStatus_ = new QLabel(this);
	busStatus_->setProperty("dim", true);
	auto *busRow = new QHBoxLayout();
	busRow->setSpacing(4);
	busRow->addWidget(busPower_, 1);
	busRow->addWidget(busOpen_, 1);

	auto *settings = new QPushButton(QStringLiteral("Settings…"), this);

	// Contents scroll, so the dock has almost no minimum size and fits any OBS
	// dock area (a tall minimum stops Qt from docking it in the short bottom row).
	auto *content = new QWidget(this);
	content->setObjectName(QStringLiteral("OBSViewerRoot"));
	auto *scroll = new QScrollArea(this);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidgetResizable(true);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	scroll->setWidget(content);
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->addWidget(scroll);
	setMinimumSize(120, 60);

	transitionCaption_ = caption(QStringLiteral("TRANSITION"), content);
	transitions_ = new TransitionControls(false, content);

	auto *layout = new QVBoxLayout(content);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);
	layout->addLayout(statusRow);
	layout->addWidget(streamTitle_);
	layout->addLayout(viewerRow);
	layout->addWidget(transitionCaption_);
	layout->addWidget(transitions_);
	layout->addWidget(caption(QStringLiteral("LAYOUT"), content));
	layout->addWidget(layout_);
	layout->addWidget(caption(QStringLiteral("MASTER BUS"), content));
	layout->addLayout(busRow);
	layout->addWidget(busStatus_);
	layout->addWidget(settings);
	layout->addStretch(1);

	// ---- behaviour ----
	connect(window_, &QPushButton::clicked, this, [this] {
		later([] { App::get().openWindowed(); });
		refresh();
	});
	connect(fullscreen_, &QToolButton::clicked, this, [this] {
		// Fullscreen ↔ window; from closed, straight to fullscreen.
		later([] {
			if (!App::get().fullscreenScreenName().empty())
				App::get().openWindowed();
			else
				App::get().openFullscreen(nullptr);
		});
		refresh();
	});
	connect(close_, &QPushButton::clicked, this, [] { later([] { App::get().closeViewer(); }); });
	connect(layout_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (updating_ || index < 0)
			return;
		Settings &s = Settings::get();
		s.windowLayout = layout_->itemData(index).toString().toStdString();
		s.save();
		App::get().applySettings();
	});
	connect(busPower_, &QPushButton::clicked, this, [this](bool on) {
		std::string error;
		if (!MasterBus::get().setEnabled(on, error))
			QMessageBox::warning(window(), QStringLiteral("Master bus"), QString::fromStdString(error));
		refresh();
	});
	connect(busOpen_, &QPushButton::clicked, this, [] { later([] { App::get().openMasterBus(nullptr, true); }); });
	connect(settings, &QPushButton::clicked, this, [] { later([] { App::get().openSettings(); }); });

	auto *timer = new QTimer(this);
	timer->setInterval(400);
	connect(timer, &QTimer::timeout, this, [this] {
		if (isVisible())
			refresh();
	});
	timer->start();
	refresh();
}

void ControlDock::refresh()
{
	updating_ = true;
	const App &app = App::get();
	const bool open = app.viewerOpen();
	const std::string fsScreen = app.fullscreenScreenName();
	const bool fullscreen = !fsScreen.empty();

	QString text;
	uint32_t color = theme::kTextFaint;
	if (!open) {
		text = QStringLiteral("Viewer closed");
	} else if (fullscreen) {
		text = QStringLiteral("Fullscreen · %1").arg(QString::fromStdString(fsScreen));
		color = theme::kGood;
	} else {
		text = QStringLiteral("Windowed");
		color = theme::kAccentBlue;
	}
	status_->setText(text);
	const QString title = QString::fromStdString(StreamTitle::get().shownTitle());
	streamTitle_->setVisible(!title.isEmpty());
	if (!title.isEmpty())
		streamTitle_->setText(
			QStringLiteral("<span style='color:%1; font-weight:600; font-size:10px;'>YOUTUBE</span>&nbsp; "
				       "<span style='color:#D8DEE6;'>%2</span>")
				.arg(obs_frontend_streaming_active() ? QStringLiteral("#EF4C4C") : QStringLiteral("#7C8794"),
				     title.toHtmlEscaped()));
	dot_->setStyleSheet(QStringLiteral("background: #%1; border-radius: 5px;").arg(color & 0xFFFFFF, 6, 16, QLatin1Char('0')));
	window_->setChecked(open && !fullscreen);
	fullscreen_->setChecked(fullscreen);
	close_->setEnabled(open);
	const bool showTransitions = Settings::get().showTransitions;
	transitionCaption_->setVisible(showTransitions);
	transitions_->setVisible(showTransitions);

	const int li = layout_->findData(QString::fromStdString(layoutById(Settings::get().windowLayout).id));
	if (li >= 0 && li != layout_->currentIndex())
		layout_->setCurrentIndex(li);

	const MasterBus &bus = MasterBus::get();
	busPower_->setChecked(bus.enabled());
	busPower_->setText(bus.enabled() ? QStringLiteral("Bus ON") : QStringLiteral("Bus OFF"));
	if (!bus.enabled()) {
		busStatus_->setText(QStringLiteral("Off · audio goes out as OBS mixes it"));
	} else if (!bus.running()) {
		busStatus_->setText(QStringLiteral("On, but not running (see the OBS log)"));
	} else {
		const float gr = bus::Engine::get().totalReduction();
		const int plugins = bus::Engine::get().slotCount();
		busStatus_->setText(QStringLiteral("%1 · %2")
					    .arg(plugins == 1 ? QStringLiteral("1 plugin") : QStringLiteral("%1 plugins").arg(plugins))
					    .arg(gr >= 0.1f ? QStringLiteral("reducing %1 dB").arg(gr, 0, 'f', 1)
							    : QStringLiteral("no reduction")));
	}
	updating_ = false;
}

} // namespace obsviewer
