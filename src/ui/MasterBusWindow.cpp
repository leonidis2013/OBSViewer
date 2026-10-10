#include "ui/MasterBusWindow.hpp"

#include "bus/Engine.hpp"
#include "bus/MasterBus.hpp"
#include "ui/Paint.hpp"
#include "ui/Theme.hpp"

#include <plugin-support.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <map>

namespace obsviewer {

using paint::col;

// Small horizontal L/R meter: pre (dim) and post (bright) levels.
class MiniMeter : public QWidget {
public:
	explicit MiniMeter(QWidget *parent) : QWidget(parent) { setMinimumSize(90, 14); }

	void setLevels(const bus::Levels &l, bool showReduction)
	{
		levels_ = l;
		showReduction_ = showReduction;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		const QRectF r = QRectF(rect()).adjusted(0, 1, 0, -1);
		const qreal grW = showReduction_ ? 34 : 0;
		const QRectF bars = r.adjusted(0, 0, -grW, 0);
		const qreal h = (bars.height() - 2) / 2;
		auto pos = [](float db) { return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f); };
		auto color = [](float db) {
			return db > -1.0f ? col(theme::kBad) : db > -9.0f ? col(theme::kWarn) : col(theme::kGood);
		};
		for (int c = 0; c < 2; ++c) {
			const QRectF track(bars.left(), bars.top() + c * (h + 2), bars.width(), h);
			p.fillRect(track, col(theme::kPanelRaised));
			QColor pre = color(levels_.pre[c]);
			pre.setAlpha(90);
			p.fillRect(QRectF(track.left(), track.top(), track.width() * pos(levels_.pre[c]), h), pre);
			p.fillRect(QRectF(track.left(), track.top() + h * 0.25, track.width() * pos(levels_.post[c]), h * 0.5),
				   color(levels_.post[c]));
		}
		if (showReduction_) {
			const QRectF t(r.right() - grW + 4, r.top(), grW - 4, r.height());
			const bool active = levels_.reduction >= 0.5f;
			paint::text(p, t, active ? QStringLiteral("−%1").arg(levels_.reduction, 0, 'f', 1) : QStringLiteral("—"),
				    active ? col(theme::kWarn) : col(theme::kTextDim), paint::font(10), Qt::AlignRight | Qt::AlignVCenter);
		}
	}

private:
	bus::Levels levels_;
	bool showReduction_ = false;
};

// One insert slot: bypass, name, meter, in / out gain, editor, move, remove.
class SlotRow : public QWidget {
public:
	SlotRow(int index, QWidget *parent) : QWidget(parent), index_(index)
	{
		setObjectName(QStringLiteral("OBSViewerSlot"));
		setAttribute(Qt::WA_StyledBackground, true);
		setStyleSheet(QStringLiteral("QWidget#OBSViewerSlot { background: #161A20; border: 1px solid #2A323D;"
					     " border-radius: 6px; }"));
		auto *layout = new QHBoxLayout(this);
		layout->setContentsMargins(8, 4, 8, 4);
		layout->setSpacing(6);

		number_ = new QLabel(this);
		number_->setProperty("dim", true);
		number_->setFixedWidth(16);
		on_ = new QCheckBox(this);
		on_->setToolTip(QStringLiteral("On / bypass"));
		name_ = new QLabel(this);
		name_->setMinimumWidth(120);
		meter_ = new MiniMeter(this);
		meter_->setToolTip(QStringLiteral("Input (dim) and output (bright) level, and gain reduction"));
		in_ = makeGain(QStringLiteral("Input trim"));
		out_ = makeGain(QStringLiteral("Output gain"));
		auto *edit = new QToolButton(this);
		edit->setText(QStringLiteral("Edit"));
		edit->setToolTip(QStringLiteral("Open the plugin's window"));
		up_ = new QToolButton(this);
		up_->setText(QStringLiteral("▲"));
		up_->setToolTip(QStringLiteral("Move earlier in the chain"));
		down_ = new QToolButton(this);
		down_->setText(QStringLiteral("▼"));
		down_->setToolTip(QStringLiteral("Move later in the chain"));
		remove_ = new QToolButton(this);
		auto *remove = remove_;
		remove->setText(QStringLiteral("✕"));
		remove->setToolTip(QStringLiteral("Remove from the chain"));

		layout->addWidget(number_);
		layout->addWidget(on_);
		layout->addWidget(name_, 1);
		layout->addWidget(meter_, 1);
		layout->addWidget(new QLabel(QStringLiteral("In"), this));
		layout->addWidget(in_);
		layout->addWidget(new QLabel(QStringLiteral("Out"), this));
		layout->addWidget(out_);
		layout->addWidget(edit);
		layout->addWidget(up_);
		layout->addWidget(down_);
		layout->addWidget(remove);

		connect(on_, &QCheckBox::toggled, this, [this](bool on) {
			if (!updating_)
				bus::Engine::get().setBypass(index_, !on);
			MasterBus::get().save();
		});
		auto gains = [this] {
			if (!updating_) {
				bus::Engine::get().setSlotGains(index_, static_cast<float>(in_->value()),
								static_cast<float>(out_->value()));
				MasterBus::get().save();
			}
		};
		connect(in_, &QDoubleSpinBox::valueChanged, this, gains);
		connect(out_, &QDoubleSpinBox::valueChanged, this, gains);
		connect(edit, &QToolButton::clicked, this, [this] { bus::Engine::get().openEditor(index_); });
	}

	QToolButton *upButton() const { return up_; }
	QToolButton *downButton() const { return down_; }
	QToolButton *removeButton() const { return remove_; }
	int index() const { return index_; }

	void refresh(int count)
	{
		const bus::SlotInfo info = bus::Engine::get().slot(index_);
		updating_ = true;
		number_->setText(QString::number(index_ + 1));
		on_->setChecked(!info.bypass);
		QString label = QStringLiteral("<b>%1</b>").arg(QString::fromStdString(info.name).toHtmlEscaped());
		if (!info.vendor.empty())
			label += QStringLiteral(" <span style='color:#8794A5'>%1 · %2</span>")
					 .arg(QString::fromStdString(info.vendor).toHtmlEscaped(),
					      info.format == "AudioUnit" ? QStringLiteral("AU")
									 : QString::fromStdString(info.format));
		name_->setText(label);
		in_->setValue(info.inGainDb);
		out_->setValue(info.outGainDb);
		up_->setEnabled(index_ > 0);
		down_->setEnabled(index_ < count - 1);
		updating_ = false;
	}

	void refreshMeter() { meter_->setLevels(bus::Engine::get().slotLevels(index_), true); }

private:
	QDoubleSpinBox *makeGain(const QString &tip)
	{
		auto *s = new QDoubleSpinBox(this);
		s->setRange(-12.0, 12.0);
		s->setSingleStep(0.5);
		s->setDecimals(1);
		s->setSuffix(QStringLiteral(" dB"));
		s->setToolTip(tip);
		s->setFixedWidth(82);
		return s;
	}

	int index_;
	bool updating_ = false;
	QLabel *number_ = nullptr;
	QCheckBox *on_ = nullptr;
	QLabel *name_ = nullptr;
	MiniMeter *meter_ = nullptr;
	QDoubleSpinBox *in_ = nullptr;
	QDoubleSpinBox *out_ = nullptr;
	QToolButton *up_ = nullptr;
	QToolButton *down_ = nullptr;
	QToolButton *remove_ = nullptr;
};

// ---------------------------------------------------------------------------

MasterBusWindow::MasterBusWindow(QWidget *parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("OBSViewerDialog"));
	setStyleSheet(theme::styleSheet());
	setWindowTitle(QStringLiteral("OBSViewer Master Bus"));
	resize(860, 680);

	// ---- header ----
	enabled_ = new QCheckBox(QStringLiteral("Master bus on"), this);
	enabled_->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 14px;"));
	status_ = new QLabel(this);
	status_->setProperty("dim", true);
	masterMeter_ = new MiniMeter(this);
	masterMeter_->setMinimumWidth(180);
	masterMeter_->setToolTip(QStringLiteral("Bus input (dim) and output (bright)"));
	auto makeMaster = [this](const QString &tip) {
		auto *s = new QDoubleSpinBox(this);
		s->setRange(-12.0, 12.0);
		s->setSingleStep(0.5);
		s->setDecimals(1);
		s->setSuffix(QStringLiteral(" dB"));
		s->setToolTip(tip);
		return s;
	};
	masterIn_ = makeMaster(QStringLiteral("Bus input trim"));
	masterOut_ = makeMaster(QStringLiteral("Bus output gain"));

	auto *header = new QHBoxLayout();
	header->addWidget(enabled_);
	header->addWidget(status_, 1);
	header->addWidget(new QLabel(QStringLiteral("In"), this));
	header->addWidget(masterIn_);
	header->addWidget(masterMeter_);
	header->addWidget(new QLabel(QStringLiteral("Out"), this));
	header->addWidget(masterOut_);

	// ---- routing ----
	auto *routing = new QGroupBox(QStringLiteral("SOURCES THROUGH THE BUS"), this);
	auto *rl = new QVBoxLayout(routing);
	auto *help = new QLabel(QStringLiteral(
		"Ticked sources are taken off the output tracks and fed through the bus instead, so the stream and "
		"recording hear them processed. Unticking (or turning the bus off) puts their tracks back exactly as "
		"they were."),
		routing);
	help->setWordWrap(true);
	help->setProperty("dim", true);
	sources_ = new QListWidget(routing);
	sources_->setMinimumHeight(110);
	auto *routeAll = new QPushButton(QStringLiteral("Route all"), routing);
	auto *unrouteAll = new QPushButton(QStringLiteral("Un-route all"), routing);
	sendTrack_ = new QComboBox(routing);
	for (int t = 1; t <= 6; ++t)
		sendTrack_->addItem(QStringLiteral("Track %1").arg(t), t);
	sendTrack_->setToolTip(QStringLiteral("A spare OBS track that carries the bus input. Don't use it for anything else."));
	auto *tracksRow = new QHBoxLayout();
	tracksRow->addWidget(routeAll);
	tracksRow->addWidget(unrouteAll);
	tracksRow->addStretch(1);
	tracksRow->addWidget(new QLabel(QStringLiteral("Bus input on"), routing));
	tracksRow->addWidget(sendTrack_);
	tracksRow->addSpacing(12);
	tracksRow->addWidget(new QLabel(QStringLiteral("Bus output to"), routing));
	for (int t = 1; t <= 6; ++t) {
		auto *cb = new QCheckBox(QString::number(t), routing);
		cb->setToolTip(QStringLiteral("Send the processed bus to Track %1").arg(t));
		outputTracks_.push_back(cb);
		tracksRow->addWidget(cb);
	}
	rl->addWidget(help);
	rl->addWidget(sources_);
	rl->addLayout(tracksRow);

	// ---- chain ----
	auto *chain = new QGroupBox(QStringLiteral("PLUGIN CHAIN"), this);
	auto *cl = new QVBoxLayout(chain);
	chainBox_ = new QWidget(chain);
	chainLayout_ = new QVBoxLayout(chainBox_);
	chainLayout_->setContentsMargins(0, 0, 0, 0);
	chainLayout_->setSpacing(4);
	auto *scroll = new QScrollArea(chain);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(chainBox_);
	scroll->setMinimumHeight(180);

	addButton_ = new QToolButton(chain);
	addButton_->setText(QStringLiteral("Add plugin"));
	addButton_->setPopupMode(QToolButton::InstantPopup);
	addButton_->setMenu(new QMenu(addButton_));
	scanButton_ = new QPushButton(QStringLiteral("Scan for plugins"), chain);
	scanButton_->setToolTip(QStringLiteral("Find VST3 and Audio Unit plugins installed on this Mac"));
	scanStatus_ = new QLabel(chain);
	scanStatus_->setProperty("dim", true);
	latency_ = new QLabel(chain);
	latency_->setProperty("dim", true);
	auto *chainButtons = new QHBoxLayout();
	chainButtons->addWidget(addButton_);
	chainButtons->addWidget(scanButton_);
	chainButtons->addWidget(scanStatus_, 1);
	chainButtons->addWidget(latency_);
	cl->addWidget(scroll, 1);
	cl->addLayout(chainButtons);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 14, 16, 14);
	layout->setSpacing(10);
	layout->addLayout(header);
	layout->addWidget(routing);
	layout->addWidget(chain, 1);

	// ---- behaviour ----
	connect(enabled_, &QCheckBox::toggled, this, [this](bool on) {
		if (updating_)
			return;
		std::string error;
		if (!MasterBus::get().setEnabled(on, error))
			QMessageBox::warning(this, QStringLiteral("Master bus"), QString::fromStdString(error));
		refreshAll();
	});
	auto masterGains = [this] {
		if (updating_)
			return;
		bus::Engine::get().setMasterGains(static_cast<float>(masterIn_->value()),
						  static_cast<float>(masterOut_->value()));
		MasterBus::get().save();
	};
	connect(masterIn_, &QDoubleSpinBox::valueChanged, this, masterGains);
	connect(masterOut_, &QDoubleSpinBox::valueChanged, this, masterGains);
	connect(sources_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
		if (updating_)
			return;
		MasterBus::get().setRouted(item->data(Qt::UserRole).toString().toStdString(),
					   item->checkState() == Qt::Checked);
		refreshStatus();
	});
	connect(routeAll, &QPushButton::clicked, this, [this] {
		MasterBus::get().routeAll(true);
		refreshSources();
		refreshStatus();
	});
	connect(unrouteAll, &QPushButton::clicked, this, [this] {
		MasterBus::get().routeAll(false);
		refreshSources();
		refreshStatus();
	});
	connect(sendTrack_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (updating_ || index < 0)
			return;
		MasterBus::get().setSendTrack(sendTrack_->itemData(index).toInt());
		refreshAll();
	});
	for (int t = 0; t < 6; ++t)
		connect(outputTracks_[static_cast<size_t>(t)], &QCheckBox::toggled, this, [this] {
			if (updating_)
				return;
			uint32_t mask = 0;
			for (int i = 0; i < 6; ++i)
				if (outputTracks_[static_cast<size_t>(i)]->isChecked())
					mask |= 1u << i;
			MasterBus::get().setOutputMask(mask);
			refreshAll();
		});
	connect(scanButton_, &QPushButton::clicked, this, [this] { startScan(false); });

	meterTimer_ = new QTimer(this);
	meterTimer_->setInterval(50);
	connect(meterTimer_, &QTimer::timeout, this, [this] {
		masterMeter_->setLevels(bus::Engine::get().masterLevels(), false);
		for (SlotRow *row : rows_)
			row->refreshMeter();
	});
	meterTimer_->start();

	scanTimer_ = new QTimer(this);
	scanTimer_->setInterval(0);
	connect(scanTimer_, &QTimer::timeout, this, &MasterBusWindow::scanTick);

	refreshAll();
	if (bus::Engine::get().plugins().empty())
		scanStatus_->setText(QStringLiteral("No plugins found yet. Click “Scan for plugins”."));
}

void MasterBusWindow::refreshAll()
{
	updating_ = true;
	const auto &cfg = MasterBus::get().config();
	enabled_->setChecked(MasterBus::get().enabled());
	masterIn_->setValue(bus::Engine::get().masterInDb());
	masterOut_->setValue(bus::Engine::get().masterOutDb());
	sendTrack_->setCurrentIndex(cfg.sendTrack - 1);
	for (int t = 0; t < 6; ++t) {
		QCheckBox *cb = outputTracks_[static_cast<size_t>(t)];
		cb->setChecked(cfg.outputMask & (1u << t));
		cb->setEnabled(t != cfg.sendTrack - 1);
	}
	updating_ = false;
	refreshSources();
	refreshChain();
	refreshStatus();
	rebuildAddMenu();
}

void MasterBusWindow::refreshSources()
{
	updating_ = true;
	sources_->clear();
	for (const auto &row : MasterBus::get().audioSources()) {
		QString tracks;
		for (int t = 0; t < 6; ++t)
			if (row.mixers & (1u << t))
				tracks += QString::number(t + 1);
		auto *item = new QListWidgetItem(
			QStringLiteral("%1      tracks %2")
				.arg(QString::fromStdString(row.name), tracks.isEmpty() ? QStringLiteral("—") : tracks),
			sources_);
		item->setData(Qt::UserRole, QString::fromStdString(row.uuid));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(row.routed ? Qt::Checked : Qt::Unchecked);
	}
	sources_->setEnabled(MasterBus::get().enabled());
	updating_ = false;
}

void MasterBusWindow::refreshChain()
{
	for (SlotRow *row : rows_)
		row->deleteLater();
	rows_.clear();
	QLayoutItem *child;
	while ((child = chainLayout_->takeAt(0)) != nullptr)
		delete child;

	const int count = bus::Engine::get().slotCount();
	for (int i = 0; i < count; ++i) {
		auto *row = new SlotRow(i, chainBox_);
		row->refresh(count);
		connect(row->upButton(), &QToolButton::clicked, this, [this, i] {
			bus::Engine::get().moveSlot(i, i - 1);
			MasterBus::get().chainChanged();
			refreshChain();
		});
		connect(row->downButton(), &QToolButton::clicked, this, [this, i] {
			bus::Engine::get().moveSlot(i, i + 1);
			MasterBus::get().chainChanged();
			refreshChain();
		});
		connect(row->removeButton(), &QToolButton::clicked, this, [this, i] {
			bus::Engine::get().removeSlot(i);
			MasterBus::get().chainChanged();
			refreshChain();
		});
		chainLayout_->addWidget(row);
		rows_.push_back(row);
	}
	if (count == 0) {
		auto *empty = new QLabel(QStringLiteral("No plugins yet. Use “Add plugin” to build your mastering chain; "
							"audio runs through the plugins top to bottom."),
					 chainBox_);
		empty->setWordWrap(true);
		empty->setProperty("dim", true);
		chainLayout_->addWidget(empty);
	}
	chainLayout_->addStretch(1);
	refreshStatus();
}

void MasterBusWindow::refreshStatus()
{
	const MasterBus &bus = MasterBus::get();
	const int routed = bus.routedCount();
	const int plugins = bus::Engine::get().slotCount();
	if (!bus.enabled())
		status_->setText(QStringLiteral("Off — audio goes out exactly as OBS mixes it"));
	else if (!bus.running())
		status_->setText(QStringLiteral("On, but the bus couldn't start (see the OBS log)"));
	else
		status_->setText(QStringLiteral("Running · %1 · %2")
					 .arg(routed == 1 ? QStringLiteral("1 source routed")
							  : QStringLiteral("%1 sources routed").arg(routed))
					 .arg(plugins == 1 ? QStringLiteral("1 plugin") : QStringLiteral("%1 plugins").arg(plugins)));
	const int lat = bus::Engine::get().latencySamples();
	const double ms = 1000.0 * lat / std::max(1.0, bus::Engine::get().sampleRate());
	latency_->setText(lat > 0 ? QStringLiteral("Latency %1 samples (%2 ms), compensated").arg(lat).arg(ms, 0, 'f', 1)
				  : QStringLiteral("No added latency"));
}

void MasterBusWindow::rebuildAddMenu()
{
	QMenu *menu = addButton_->menu();
	menu->clear();
	const auto plugins = bus::Engine::get().plugins();
	if (plugins.empty()) {
		menu->addAction(QStringLiteral("No plugins scanned yet"))->setEnabled(false);
		return;
	}
	std::map<QString, QMenu *> vendors;
	for (const auto &p : plugins) {
		const QString vendor = p.vendor.empty() ? QStringLiteral("Other") : QString::fromStdString(p.vendor);
		QMenu *&sub = vendors[vendor];
		if (!sub)
			sub = menu->addMenu(vendor);
		const QString label = QStringLiteral("%1   (%2)").arg(QString::fromStdString(p.name),
								  p.format == "AudioUnit" ? QStringLiteral("AU")
											  : QString::fromStdString(p.format));
		const std::string id = p.id;
		connect(sub->addAction(label), &QAction::triggered, this,
			[this, id] { QTimer::singleShot(0, this, [this, id] { addPlugin(id); }); });
	}
	menu->addSeparator();
	connect(menu->addAction(QStringLiteral("Rescan everything")), &QAction::triggered, this,
		[this] { QTimer::singleShot(0, this, [this] { startScan(true); }); });
}

void MasterBusWindow::addPlugin(const std::string &id)
{
	std::string error;
	scanStatus_->setText(QStringLiteral("Loading plugin…"));
	if (!bus::Engine::get().addPlugin(id, error)) {
		scanStatus_->clear();
		QMessageBox::warning(this, QStringLiteral("Add plugin"),
				     QStringLiteral("The plugin couldn't be added.\n\n%1").arg(QString::fromStdString(error)));
		return;
	}
	scanStatus_->clear();
	MasterBus::get().chainChanged();
	refreshChain();
	bus::Engine::get().openEditor(bus::Engine::get().slotCount() - 1);
}

void MasterBusWindow::startScan(bool rescanAll)
{
	if (scanTimer_->isActive())
		return;
	bus::Engine::get().startScan(rescanAll);
	scanButton_->setEnabled(false);
	addButton_->setEnabled(false);
	scanStatus_->setText(QStringLiteral("Scanning…"));
	scanTimer_->start();
}

void MasterBusWindow::scanTick()
{
	std::string current;
	float progress = 0.0f;
	if (bus::Engine::get().scanStep(current, progress)) {
		scanStatus_->setText(QStringLiteral("Scanning %1%  ·  %2")
					     .arg(static_cast<int>(progress * 100.0f))
					     .arg(QString::fromStdString(current)));
		return;
	}
	scanTimer_->stop();
	scanButton_->setEnabled(true);
	addButton_->setEnabled(true);
	const size_t n = bus::Engine::get().plugins().size();
	scanStatus_->setText(n == 1 ? QStringLiteral("Found 1 effect plugin")
				    : QStringLiteral("Found %1 effect plugins").arg(n));
	obs_log(LOG_INFO, "master bus: scan finished, %zu effect plugins", n);
	rebuildAddMenu();
}

} // namespace obsviewer
