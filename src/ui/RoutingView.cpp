#include "ui/RoutingView.hpp"

#include "bus/Engine.hpp"
#include "bus/MasterBus.hpp"
#include "core/Health.hpp"
#include "ui/Paint.hpp"
#include "ui/Theme.hpp"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

namespace obsviewer {

namespace {

constexpr qreal kMargin = 16.0;
constexpr qreal kTop = 40.0;   // below the column headings
constexpr qreal kRowH = 28.0;  // a source
constexpr qreal kRowGap = 6.0;
constexpr qreal kTrackH = 40.0;
constexpr qreal kTrackGapMin = 10.0;
constexpr qreal kLegendH = 34.0;

// Tracks the recording writes (bit per track). Simple output mode records Track 1.
uint32_t recordTracks()
{
	if (config_t *cfg = obs_frontend_get_profile_config()) {
		const char *mode = config_get_string(cfg, "Output", "Mode");
		if (mode && std::strcmp(mode, "Advanced") == 0) {
			const auto mask = static_cast<uint32_t>(config_get_int(cfg, "AdvOut", "RecTracks")) & 0x3Fu;
			return mask ? mask : 1u;
		}
	}
	return 1u;
}

QString trackList(uint32_t mask)
{
	QString s;
	for (int t = 0; t < 6; ++t)
		if (mask & (1u << t))
			s += (s.isEmpty() ? QString() : QStringLiteral(", ")) + QString::number(t + 1);
	return s.isEmpty() ? QStringLiteral("none") : s;
}

void wire(QPainter &p, QPointF a, QPointF b, const QColor &c, qreal width, bool dashed = false)
{
	QPainterPath path(a);
	const qreal dx = std::max(30.0, (b.x() - a.x()) * 0.5);
	path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
	QPen pen(c, width, dashed ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);
}

void port(QPainter &p, QPointF at, const QColor &c)
{
	p.setPen(Qt::NoPen);
	p.setBrush(c);
	p.drawEllipse(at, 3.5, 3.5);
}

// Small rounded tag ("STREAM", "REC") right-aligned at `right`; returns its left edge.
qreal tag(QPainter &p, qreal right, qreal cy, const QString &text, uint32_t color)
{
	const QFont f = paint::font(9.5, true);
	p.setFont(f);
	const qreal w = p.fontMetrics().horizontalAdvance(text) + 10.0;
	const QRectF r(right - w, cy - 8.0, w, 16.0);
	QColor fill = paint::col(color);
	fill.setAlphaF(0.18f);
	p.setPen(QPen(paint::col(color), 1.0));
	p.setBrush(fill);
	p.drawRoundedRect(r, 4.0, 4.0);
	p.setPen(paint::col(color));
	p.drawText(r, Qt::AlignCenter, text);
	return r.left();
}

} // namespace

// ============================================================ diagram

RoutingDiagram::RoutingDiagram(QWidget *parent) : QWidget(parent)
{
	setMouseTracking(true);
	setMinimumWidth(620);
	refresh();
}

QSize RoutingDiagram::sizeHint() const
{
	const qreal srcH = static_cast<qreal>(std::max<size_t>(1, sources_.size())) * (kRowH + kRowGap);
	const qreal busH = 96.0 + static_cast<qreal>(std::max<size_t>(1, plugins_.size())) * 22.0;
	const qreal trackH = 6.0 * kTrackH + 5.0 * kTrackGapMin;
	const qreal h = kTop + std::max({srcH, busH, trackH}) + kLegendH;
	return QSize(780, static_cast<int>(std::ceil(h)));
}

void RoutingDiagram::refresh()
{
	MasterBus &bus = MasterBus::get();
	const auto rows = bus.audioSources();
	const auto &cfg = bus.config();
	bus::Engine &engine = bus::Engine::get();

	std::string sig = std::to_string(bus.enabled()) + std::to_string(bus.running()) + "|" +
			  std::to_string(cfg.sendTrack) + "|" + std::to_string(cfg.outputMask) + "|" +
			  std::to_string(MasterBus::streamTrack()) + "|" + std::to_string(recordTracks()) + "|" +
			  std::to_string(engine.revision()) + "|";
	for (const auto &r : rows)
		sig += r.uuid + (r.routed ? "+" : "-") + std::to_string(r.mixers) + r.name + ";";
	if (sig == signature_)
		return;
	signature_ = sig;

	sources_.clear();
	for (const auto &r : rows)
		sources_.push_back(Source{r.uuid, QString::fromStdString(r.name), r.routed, r.mixers, {}});
	plugins_.clear();
	bypassed_.clear();
	for (int i = 0; i < engine.slotCount(); ++i) {
		const bus::SlotInfo s = engine.slot(i);
		plugins_.push_back(QString::fromStdString(s.name));
		bypassed_.push_back(s.bypass);
	}
	busOn_ = bus.enabled();
	busRunning_ = bus.running();
	sendTrack_ = cfg.sendTrack;
	outputMask_ = cfg.outputMask;
	streamTrack_ = MasterBus::streamTrack();
	recordMask_ = recordTracks();

	setMinimumHeight(sizeHint().height()); // scrolls when there are many sources
	updateGeometry();
	layoutBoxes();
	update();
}

void RoutingDiagram::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	layoutBoxes();
}

void RoutingDiagram::layoutBoxes()
{
	const qreal W = std::max<qreal>(width(), 620.0);
	const qreal srcW = std::clamp(W * 0.28, 150.0, 240.0);
	const qreal trackW = std::clamp(W * 0.22, 140.0, 190.0);
	const qreal busW = std::clamp(W * 0.26, 170.0, 230.0);
	const qreal srcX = kMargin;
	const qreal trackX = W - kMargin - trackW;
	const qreal busX = (srcX + srcW + trackX) / 2.0 - busW / 2.0;

	const qreal srcH = static_cast<qreal>(sources_.size()) * (kRowH + kRowGap) - kRowGap;
	const qreal busH = 96.0 + static_cast<qreal>(std::max<size_t>(1, plugins_.size())) * 22.0;
	const qreal minTrackH = 6.0 * kTrackH + 5.0 * kTrackGapMin;
	const qreal content = std::max({srcH, busH, minTrackH});

	qreal y = kTop + std::max(0.0, (content - srcH) / 2.0);
	for (auto &s : sources_) {
		s.box = QRectF(srcX, y, srcW, kRowH);
		y += kRowH + kRowGap;
	}
	const qreal gap = (content - 6.0 * kTrackH) / 5.0;
	for (int t = 0; t < 6; ++t)
		tracks_[t].box = QRectF(trackX, kTop + t * (kTrackH + gap), trackW, kTrackH);
	bus_ = QRectF(busX, kTop + (content - busH) / 2.0, busW, busH);
}

int RoutingDiagram::hitTest(const QPointF &pos, int &index) const
{
	for (size_t i = 0; i < sources_.size(); ++i)
		if (sources_[i].box.contains(pos)) {
			index = static_cast<int>(i);
			return 1;
		}
	for (int t = 0; t < 6; ++t)
		if (tracks_[t].box.contains(pos)) {
			index = t;
			return 2;
		}
	index = -1;
	return 0;
}

void RoutingDiagram::mouseMoveEvent(QMouseEvent *event)
{
	int index = -1;
	const int kind = hitTest(event->position(), index);
	if (kind != hoverKind_ || index != hoverIndex_) {
		hoverKind_ = kind;
		hoverIndex_ = index;
		const bool clickable = (kind == 1 && busOn_) || (kind == 2 && busOn_ && index + 1 != sendTrack_);
		setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
		if (kind == 1)
			setToolTip(busOn_ ? (sources_[static_cast<size_t>(index)].routed
						     ? QStringLiteral("Click to take this source out of the bus (back to its own tracks)")
						     : QStringLiteral("Click to send this source through the bus"))
					  : QStringLiteral("Turn the bus on to route sources through it"));
		else if (kind == 2)
			setToolTip(index + 1 == sendTrack_
					   ? QStringLiteral("The bus input: sources you route are mixed here, then processed")
				   : busOn_ ? QStringLiteral("Click to add or remove this track as a bus output")
					    : QString());
		else
			setToolTip(QString());
		update();
	}
}

void RoutingDiagram::leaveEvent(QEvent *event)
{
	hoverKind_ = 0;
	hoverIndex_ = -1;
	update();
	QWidget::leaveEvent(event);
}

void RoutingDiagram::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || !busOn_)
		return;
	int index = -1;
	const int kind = hitTest(event->position(), index);
	MasterBus &bus = MasterBus::get();
	if (kind == 1) {
		const Source &s = sources_[static_cast<size_t>(index)];
		bus.setRouted(s.uuid, !s.routed);
	} else if (kind == 2 && index + 1 != sendTrack_) {
		bus.setOutputMask(outputMask_ ^ (1u << index)); // the last output can't be removed
	} else {
		return;
	}
	signature_.clear();
	refresh();
}

void RoutingDiagram::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.fillRect(rect(), paint::col(theme::kBg));

	const QColor blue = paint::col(theme::kAccentBlue);
	const QColor grey = paint::col(theme::kTextFaint);
	const QColor dim = paint::col(theme::kTextDim);
	const QColor text = paint::col(theme::kText);
	const bool anyRouted = std::any_of(sources_.begin(), sources_.end(), [](const Source &s) { return s.routed; });
	const bool streamMissing = busRunning_ && anyRouted && !busReachesTrack(sendTrack_, outputMask_, streamTrack_);

	// ---- column headings ----
	auto heading = [&](qreal x, qreal w, const QString &s, uint32_t accent) {
		p.setPen(Qt::NoPen);
		p.setBrush(paint::col(accent));
		p.drawRoundedRect(QRectF(x, 14, 3, 12), 1.5, 1.5);
		paint::text(p, QRectF(x + 9, 8, w - 9, 24), s, text, paint::font(12, true));
	};
	if (!sources_.empty())
		heading(sources_.front().box.left(), sources_.front().box.width(), QStringLiteral("Sources"),
			theme::kAccentSlate);
	else
		heading(kMargin, 200, QStringLiteral("Sources"), theme::kAccentSlate);
	heading(bus_.left(), bus_.width(), QStringLiteral("Master bus"), theme::kAccentBlue);
	heading(tracks_[0].box.left(), tracks_[0].box.width(), QStringLiteral("OBS tracks"), theme::kAccentTeal);

	auto hovered = [&](int kind, int index) { return hoverKind_ == kind && hoverIndex_ == index; };
	const QPointF busIn(bus_.left(), bus_.center().y());
	const QPointF busOut(bus_.right(), bus_.center().y());

	// ---- wires: straight to tracks (grey) first, then through the bus (blue) ----
	for (size_t i = 0; i < sources_.size(); ++i) {
		const Source &s = sources_[i];
		if (s.routed)
			continue;
		const bool hi = hovered(1, static_cast<int>(i));
		for (int t = 0; t < 6; ++t)
			if (s.mixers & (1u << t)) {
				const QRectF &tb = tracks_[t].box;
				wire(p, QPointF(s.box.right(), s.box.center().y()), QPointF(tb.left(), tb.center().y()),
				     hi ? dim : grey, hi ? 2.0 : 1.2);
			}
	}
	for (size_t i = 0; i < sources_.size(); ++i) {
		const Source &s = sources_[i];
		if (!s.routed)
			continue;
		const bool hi = hovered(1, static_cast<int>(i));
		wire(p, QPointF(s.box.right(), s.box.center().y()), busIn, hi ? blue.lighter(130) : blue, hi ? 3.0 : 2.0,
		     !busRunning_);
	}
	if (busOn_ && anyRouted)
		for (int t = 0; t < 6; ++t)
			if (outputMask_ & (1u << t)) {
				const QRectF &tb = tracks_[t].box;
				wire(p, busOut, QPointF(tb.left(), tb.center().y()), blue, 2.4, !busRunning_);
			}

	// ---- sources ----
	for (size_t i = 0; i < sources_.size(); ++i) {
		const Source &s = sources_[i];
		const bool hi = hovered(1, static_cast<int>(i));
		paint::panel(p, s.box, 6.0, hi ? theme::kPanelRaised : theme::kPanel, s.routed ? theme::kAccentBlue : theme::kBorder);
		const QString right = s.routed ? QStringLiteral("bus") : trackList(s.mixers);
		p.setFont(paint::font(10.5));
		const qreal rw = p.fontMetrics().horizontalAdvance(right) + 4;
		paint::text(p, s.box.adjusted(10, 0, -(rw + 14), 0), s.name, text, paint::font(11.5, true));
		paint::text(p, s.box.adjusted(0, 0, -10, 0), right, s.routed ? blue : dim, paint::font(10.5),
			    Qt::AlignRight | Qt::AlignVCenter);
		port(p, QPointF(s.box.right(), s.box.center().y()), s.routed ? blue : grey);
	}
	if (sources_.empty())
		paint::text(p, QRectF(kMargin, kTop, 220, kRowH), QStringLiteral("No audio sources"), dim, paint::font(11.5));

	// ---- bus ----
	paint::panel(p, bus_, 8.0, theme::kPanel, busOn_ ? theme::kAccentBlue : theme::kBorder);
	{
		qreal y = bus_.top() + 8;
		const QRectF titleRow(bus_.left() + 10, y, bus_.width() - 20, 20);
		paint::text(p, titleRow, QStringLiteral("In: Track %1").arg(sendTrack_), dim, paint::font(10.5));
		tag(p, titleRow.right(), titleRow.center().y(), !busOn_ ? QStringLiteral("OFF")
							       : busRunning_ ? QStringLiteral("ON")
									     : QStringLiteral("ERROR"),
		    !busOn_ ? theme::kTextFaint : busRunning_ ? theme::kGood : theme::kBad);
		y += 26;
		p.setPen(QPen(paint::col(theme::kBorder), 1.0));
		p.drawLine(QPointF(bus_.left() + 8, y), QPointF(bus_.right() - 8, y));
		y += 6;
		if (plugins_.empty()) {
			paint::text(p, QRectF(bus_.left() + 10, y, bus_.width() - 20, 22), QStringLiteral("No plugins"), dim,
				    paint::font(11));
			y += 22;
		}
		for (size_t i = 0; i < plugins_.size(); ++i) {
			const QRectF row(bus_.left() + 10, y, bus_.width() - 20, 22);
			const bool off = bypassed_[i];
			p.setPen(Qt::NoPen);
			p.setBrush(off ? grey : paint::col(theme::kGood));
			p.drawEllipse(QPointF(row.left() + 4, row.center().y()), 3, 3);
			paint::text(p, row.adjusted(14, 0, 0, 0),
				    off ? QStringLiteral("%1 (bypassed)").arg(plugins_[i]) : plugins_[i], off ? dim : text,
				    paint::font(11));
			y += 22;
		}
		y += 4;
		p.setPen(QPen(paint::col(theme::kBorder), 1.0));
		p.drawLine(QPointF(bus_.left() + 8, y), QPointF(bus_.right() - 8, y));
		y += 6;
		paint::text(p, QRectF(bus_.left() + 10, y, bus_.width() - 20, 20),
			    QStringLiteral("Out: Track %1").arg(trackList(outputMask_)), streamMissing ? paint::col(theme::kBad) : dim,
			    paint::font(10.5));
	}
	port(p, busIn, busOn_ ? blue : grey);
	port(p, busOut, busOn_ ? blue : grey);

	// ---- tracks ----
	for (int t = 0; t < 6; ++t) {
		const QRectF &b = tracks_[t].box;
		const bool isSend = t + 1 == sendTrack_;
		const bool isOut = busOn_ && (outputMask_ & (1u << t));
		const bool hi = hovered(2, t) && busOn_ && !isSend;
		paint::panel(p, b, 6.0, hi ? theme::kPanelRaised : theme::kPanel, isOut ? theme::kAccentBlue : theme::kBorder);
		paint::text(p, b.adjusted(10, 0, 0, 0), QStringLiteral("Track %1").arg(t + 1), isSend ? dim : text,
			    paint::font(11.5, true));
		qreal right = b.right() - 8;
		if (t + 1 == streamTrack_)
			right = tag(p, right, b.center().y(), QStringLiteral("STREAM"),
				    streamMissing ? theme::kBad : theme::kAccentTeal) - 4;
		if (recordMask_ & (1u << t))
			right = tag(p, right, b.center().y(), QStringLiteral("REC"), theme::kAccentOrange) - 4;
		if (isSend && busOn_)
			tag(p, right, b.center().y(), QStringLiteral("BUS IN"), theme::kAccentSlate);
		port(p, QPointF(b.left(), b.center().y()), isOut ? blue : grey);
	}

	// ---- legend ----
	const qreal ly = height() - kLegendH + 10;
	wire(p, QPointF(kMargin, ly + 7), QPointF(kMargin + 30, ly + 7), blue, 2.0);
	paint::text(p, QRectF(kMargin + 38, ly, 200, 14), QStringLiteral("through the master bus"), dim, paint::font(10.5));
	wire(p, QPointF(kMargin + 200, ly + 7), QPointF(kMargin + 230, ly + 7), grey, 1.2);
	paint::text(p, QRectF(kMargin + 238, ly, 220, 14), QStringLiteral("straight to a track (unprocessed)"), dim,
		    paint::font(10.5));
}

// ============================================================ dialog

RoutingDialog::RoutingDialog(QWidget *parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("OBSViewerDialog"));
	setStyleSheet(theme::styleSheet());
	setWindowTitle(QStringLiteral("Master Bus Routing"));
	setAttribute(Qt::WA_DeleteOnClose);
	resize(820, 560);

	auto *help = new QLabel(QStringLiteral("Click a source to send it through the master bus or back to its own "
					       "tracks. Click a track to add or remove it as a bus output. The stream "
					       "only sends the track marked STREAM."),
				this);
	help->setWordWrap(true);
	help->setProperty("dim", true);
	summary_ = new QLabel(this);
	summary_->setWordWrap(true);

	diagram_ = new RoutingDiagram(this);
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(diagram_);

	auto *all = all_ = new QPushButton(QStringLiteral("All Through Bus"), this);
	auto *none = none_ = new QPushButton(QStringLiteral("None"), this);
	sendTrack_ = new QComboBox(this);
	for (int t = 1; t <= 6; ++t)
		sendTrack_->addItem(QStringLiteral("Track %1").arg(t), t);
	sendTrack_->setToolTip(QStringLiteral("A spare OBS track that carries the bus input. Don't use it for anything else."));
	auto *row = new QHBoxLayout();
	row->setSpacing(8);
	row->addWidget(all);
	row->addWidget(none);
	row->addStretch(1);
	row->addWidget(new QLabel(QStringLiteral("Bus input on"), this));
	row->addWidget(sendTrack_);
	row->addSpacing(12);
	row->addWidget(new QLabel(QStringLiteral("Output to"), this));
	for (int t = 1; t <= 6; ++t) {
		auto *cb = new QCheckBox(QString::number(t), this);
		cb->setToolTip(QStringLiteral("Send the processed bus to Track %1").arg(t));
		outputs_.push_back(cb);
		row->addWidget(cb);
	}

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(14, 12, 14, 12);
	layout->setSpacing(8);
	layout->addWidget(help);
	layout->addWidget(summary_);
	layout->addWidget(scroll, 1);
	layout->addLayout(row);

	connect(all, &QPushButton::clicked, this, [this] {
		MasterBus::get().routeAll(true);
		refresh();
	});
	connect(none, &QPushButton::clicked, this, [this] {
		MasterBus::get().routeAll(false);
		refresh();
	});
	connect(sendTrack_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (updating_ || index < 0)
			return;
		MasterBus::get().setSendTrack(sendTrack_->itemData(index).toInt());
		refresh();
	});
	for (QCheckBox *cb : outputs_)
		connect(cb, &QCheckBox::toggled, this, [this] {
			if (updating_)
				return;
			uint32_t mask = 0;
			for (size_t i = 0; i < outputs_.size(); ++i)
				if (outputs_[i]->isChecked())
					mask |= 1u << i;
			MasterBus::get().setOutputMask(mask);
			refresh();
		});

	timer_ = new QTimer(this);
	timer_->setInterval(500);
	connect(timer_, &QTimer::timeout, this, [this] { refresh(); });
	refresh();
}

void RoutingDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	refresh();
	timer_->start();
}

void RoutingDialog::hideEvent(QHideEvent *event)
{
	QDialog::hideEvent(event);
	timer_->stop();
}

void RoutingDialog::refresh()
{
	updating_ = true;
	MasterBus &bus = MasterBus::get();
	const auto &cfg = bus.config();
	diagram_->refresh();

	const auto rows = bus.audioSources();
	const auto routed = std::count_if(rows.begin(), rows.end(), [](const auto &r) { return r.routed; });
	QString s;
	if (!bus.enabled())
		s = QStringLiteral("The bus is <b>off</b>: every source goes straight to its own tracks.");
	else
		s = QStringLiteral("<b>%1 of %2</b> sources go through the bus. The stream sends <b>Track %3</b>.")
			    .arg(routed)
			    .arg(rows.size())
			    .arg(MasterBus::streamTrack());
	if (bus.running() && routed > 0 && !bus.reachesStream())
		s += QStringLiteral(" <span style='color:#EF4C4C'>The bus doesn't output to the stream's track, so "
				    "routed sources are missing from the stream.</span>");
	summary_->setText(s);

	const bool on = bus.enabled();
	sendTrack_->setCurrentIndex(cfg.sendTrack - 1);
	for (int t = 0; t < 6; ++t) {
		QCheckBox *cb = outputs_[static_cast<size_t>(t)];
		cb->setChecked(cfg.outputMask & (1u << t));
		cb->setEnabled(on && t != cfg.sendTrack - 1);
	}
	sendTrack_->setEnabled(on);
	all_->setEnabled(on);
	none_->setEnabled(on);
	updating_ = false;
}

} // namespace obsviewer
