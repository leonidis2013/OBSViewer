#include "ui/SidePanel.hpp"

#include "core/Settings.hpp"
#include "metrics/Metrics.hpp"
#include "ui/Paint.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QAbstractButton>
#include <QDateTime>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace obsviewer {

using paint::col;

// ---------------------------------------------------------------------------
// Overall health light. Click to show / hide every check.
class HealthButton : public QAbstractButton {
public:
	explicit HealthButton(QWidget *parent) : QAbstractButton(parent)
	{
		setCursor(Qt::PointingHandCursor);
		setToolTip(QStringLiteral("Show everything OBSViewer is checking"));
	}

	void setState(const HealthReport &report, bool expanded, bool blink, qreal scale)
	{
		report_ = report;
		expanded_ = expanded;
		blink_ = blink;
		scale_ = scale;
		setFixedHeight(static_cast<int>(30 * scale));
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect());
		paint::panel(p, r, report_.overall, blink_, Settings::get().alarms);

		int warns = 0, bads = 0;
		std::string firstBad;
		for (const auto &c : report_.checks) {
			if (c.level == Level::Warn)
				++warns;
			if (c.level == Level::Bad) {
				if (!bads)
					firstBad = c.name;
				++bads;
			}
		}
		QString label;
		if (bads)
			label = QStringLiteral("Problem: %1").arg(QString::fromStdString(firstBad));
		else if (warns)
			label = warns == 1 ? QStringLiteral("1 warning") : QStringLiteral("%1 warnings").arg(warns);
		else if (report_.overall == Level::Good)
			label = QStringLiteral("All good");
		else
			label = QStringLiteral("Health");

		const qreal d = 10 * scale_;
		QColor dot = paint::levelColor(report_.overall == Level::Off ? Level::Off : report_.overall);
		if (report_.overall == Level::Off)
			dot = col(theme::kTextDim);
		p.setPen(Qt::NoPen);
		p.setBrush(dot);
		p.drawEllipse(QRectF(r.left() + 9 * scale_, r.center().y() - d / 2, d, d));

		const QRectF tr = r.adjusted(26 * scale_, 0, -22 * scale_, 0);
		paint::text(p, tr, label, col(theme::kText), paint::font(12 * scale_, true));
		paint::text(p, r.adjusted(0, 0, -9 * scale_, 0), expanded_ ? QStringLiteral("▾") : QStringLiteral("▸"),
			    col(theme::kTextDim), paint::font(12 * scale_), Qt::AlignRight | Qt::AlignVCenter);
	}

private:
	HealthReport report_;
	bool expanded_ = false;
	bool blink_ = false;
	qreal scale_ = 1.0;
};

// ---------------------------------------------------------------------------
// Expanded health view: one row per check. Click anywhere to close.
class HealthList : public QAbstractButton {
public:
	explicit HealthList(QWidget *parent) : QAbstractButton(parent)
	{
		setCursor(Qt::PointingHandCursor);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	}

	void setState(const HealthReport &report, qreal scale)
	{
		report_ = report;
		scale_ = scale;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect());
		paint::panel(p, r);

		const qreal pad = 9 * scale_;
		paint::text(p, QRectF(r.left() + pad, r.top() + 4 * scale_, r.width() - 2 * pad, 20 * scale_),
			    QStringLiteral("HEALTH CHECKS"), col(theme::kTextDim), paint::font(10 * scale_, true));

		const int n = static_cast<int>(report_.checks.size());
		const qreal top = r.top() + 26 * scale_;
		const qreal rowH = std::min(36 * scale_, (r.height() - 26 * scale_ - 22 * scale_) / std::max(1, n));
		for (int i = 0; i < n; ++i) {
			const auto &c = report_.checks[static_cast<size_t>(i)];
			const QRectF row(r.left() + pad, top + i * rowH, r.width() - 2 * pad, rowH);
			const qreal d = 8 * scale_;
			p.setPen(Qt::NoPen);
			p.setBrush(paint::levelColor(c.level));
			p.drawEllipse(QRectF(row.left(), row.top() + rowH * 0.28 - d / 2, d, d));
			const QRectF textR = row.adjusted(14 * scale_, 0, 0, 0);
			paint::text(p, QRectF(textR.left(), row.top(), textR.width(), rowH * 0.55), QString::fromStdString(c.name),
				    col(theme::kText), paint::font(11.5 * scale_, true), Qt::AlignLeft | Qt::AlignVCenter);
			paint::text(p, QRectF(textR.left(), row.top() + rowH * 0.45, textR.width(), rowH * 0.5),
				    QString::fromStdString(c.detail), col(theme::kTextDim), paint::font(10.5 * scale_),
				    Qt::AlignLeft | Qt::AlignVCenter);
		}
		paint::text(p, QRectF(r.left() + pad, r.bottom() - 20 * scale_, r.width() - 2 * pad, 16 * scale_),
			    QStringLiteral("Click to close"), col(theme::kTextDim), paint::font(10 * scale_),
			    Qt::AlignRight | Qt::AlignVCenter);
	}

private:
	HealthReport report_;
	qreal scale_ = 1.0;
};

// ---------------------------------------------------------------------------
// Big status / action button (Start stream ↔ ON AIR 01:12:45).
class StateButton : public QAbstractButton {
public:
	explicit StateButton(QWidget *parent) : QAbstractButton(parent) { setCursor(Qt::PointingHandCursor); }

	void setLook(const QString &label, uint32_t fill, uint32_t textColor, bool outlined, qreal scale)
	{
		label_ = label;
		fill_ = fill;
		text_ = textColor;
		outlined_ = outlined;
		scale_ = scale;
		setFixedHeight(static_cast<int>(28 * scale));
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		QColor fill = col(fill_);
		if (underMouse())
			fill = fill.lighter(isDown() ? 95 : 115);
		p.setPen(outlined_ ? QPen(col(theme::kBorder), 1.0) : Qt::NoPen);
		p.setBrush(fill);
		p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
		paint::text(p, QRectF(rect()).adjusted(6, 0, -6, 0), label_, col(text_), paint::font(12 * scale_, true),
			    Qt::AlignCenter);
	}
	void enterEvent(QEnterEvent *e) override
	{
		QAbstractButton::enterEvent(e);
		update();
	}
	void leaveEvent(QEvent *e) override
	{
		QAbstractButton::leaveEvent(e);
		update();
	}

private:
	QString label_;
	uint32_t fill_ = theme::kPanelRaised;
	uint32_t text_ = theme::kText;
	bool outlined_ = true;
	qreal scale_ = 1.0;
};

// ---------------------------------------------------------------------------
// Panel with a title on top, an optional child in the middle and status lines below.
class Section : public QWidget {
public:
	Section(const QString &title, QWidget *parent) : QWidget(parent), title_(title)
	{
		layout_ = new QVBoxLayout(this);
		layout_->setSpacing(0);
		relayout();
	}

	void setChild(QWidget *child, int stretch = 0)
	{
		layout_->addWidget(child, stretch);
	}

	void setLines(std::vector<std::pair<QString, uint32_t>> lines)
	{
		if (lines.size() != lines_.size()) {
			lines_ = std::move(lines);
			relayout();
		} else {
			lines_ = std::move(lines);
		}
		update();
	}

	void setAlarm(Level level, bool blink)
	{
		level_ = level;
		blink_ = blink;
		update();
	}

	void setScale(qreal scale)
	{
		scale_ = scale;
		relayout();
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect());
		paint::panel(p, r, level_, blink_, Settings::get().alarms);
		const qreal pad = 9 * scale_;
		paint::text(p, QRectF(r.left() + pad, r.top() + 4 * scale_, r.width() - 2 * pad, titleH()),
			    title_.toUpper(), col(theme::kTextDim), paint::font(10 * scale_, true));
		const qreal lh = lineH();
		qreal y = r.bottom() - pad * 0.6 - lh * static_cast<qreal>(lines_.size());
		for (const auto &line : lines_) {
			paint::text(p, QRectF(r.left() + pad, y, r.width() - 2 * pad, lh), line.first, col(line.second),
				    paint::font(11 * scale_));
			y += lh;
		}
	}

private:
	qreal titleH() const { return 18 * scale_; }
	qreal lineH() const { return 16 * scale_; }
	void relayout()
	{
		const int pad = static_cast<int>(9 * scale_);
		layout_->setContentsMargins(pad, static_cast<int>(4 * scale_ + titleH() + 2 * scale_), pad,
					    static_cast<int>(pad * 0.6 + lineH() * static_cast<qreal>(lines_.size()) +
							     (lines_.empty() ? 2 * scale_ : 4 * scale_)));
	}

	QString title_;
	QVBoxLayout *layout_ = nullptr;
	std::vector<std::pair<QString, uint32_t>> lines_;
	Level level_ = Level::Off;
	bool blink_ = false;
	qreal scale_ = 1.0;
};

// ---------------------------------------------------------------------------
// Programme meter: L / R sample peak and short-term loudness against the target.
class AudioMeterWidget : public QAbstractButton {
public:
	explicit AudioMeterWidget(QWidget *parent) : QAbstractButton(parent)
	{
		setCursor(Qt::PointingHandCursor);
		setToolTip(QStringLiteral("Click to reset the integrated loudness and peak hold"));
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMinimumHeight(70);
		timer_ = new QTimer(this);
		timer_->setInterval(50);
		connect(timer_, &QTimer::timeout, this, [this] {
			if (isVisible()) {
				Metrics::get().refreshAudio();
				update();
			}
		});
		timer_->start();
		connect(this, &QAbstractButton::clicked, this, [] { ProgramLoudness::get().resetIntegrated(); });
	}

	void setScale(qreal scale)
	{
		scale_ = scale;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		const Metrics &m = Metrics::get();
		const auto &a = m.audio;
		const float target = m.targetLufs();
		const float tol = m.toleranceLu();
		const float ceiling = m.ceilingDbtp();

		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect());

		// --- bars ---
		constexpr float kMin = -60.0f;
		const qreal barW = 7 * scale_;
		const qreal gap = 3 * scale_;
		const qreal scaleW = 18 * scale_;
		const QRectF bars(r.left() + scaleW, r.top() + 4 * scale_, barW * 3 + gap * 3, r.height() - 8 * scale_);
		auto yFor = [&](float v) {
			const float t = std::clamp((v - kMin) / (0.0f - kMin), 0.0f, 1.0f);
			return bars.bottom() - t * bars.height();
		};

		// scale labels
		for (float tick : {0.0f, -12.0f, -24.0f, -36.0f, -48.0f}) {
			const qreal y = yFor(tick);
			paint::text(p, QRectF(r.left(), y - 7 * scale_, scaleW - 3 * scale_, 14 * scale_),
				    QString::number(static_cast<int>(-tick)), col(theme::kTextDim), paint::font(9 * scale_),
				    Qt::AlignRight | Qt::AlignVCenter);
		}

		auto drawBar = [&](qreal x, float value, const QColor &c) {
			const QRectF track(x, bars.top(), barW, bars.height());
			p.setPen(Qt::NoPen);
			p.setBrush(col(theme::kPanelRaised));
			p.drawRoundedRect(track, 2, 2);
			if (value > kMin) {
				const qreal y = yFor(value);
				p.setBrush(c);
				p.drawRoundedRect(QRectF(x, y, barW, bars.bottom() - y), 2, 2);
			}
		};
		auto peakColor = [&](float v) {
			return v > ceiling ? col(theme::kBad) : v > -9.0f ? col(theme::kWarn) : col(theme::kGood);
		};
		drawBar(bars.left(), a.active ? a.peakL : -120.0f, peakColor(a.peakL));
		drawBar(bars.left() + barW + gap, a.active ? a.peakR : -120.0f, peakColor(a.peakR));

		// loudness bar, coloured against the target
		const qreal lx = bars.left() + (barW + gap) * 2 + gap;
		QColor lc = col(0xFF6AA8FF); // below target: blue
		if (a.shortTerm >= target - tol && a.shortTerm <= target + tol)
			lc = col(theme::kGood);
		else if (a.shortTerm > target + tol)
			lc = a.shortTerm > target + tol + 3.0f ? col(theme::kBad) : col(theme::kWarn);
		drawBar(lx, a.active ? a.shortTerm : -120.0f, lc);

		// target band across the loudness bar
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(col(theme::kWarn), 1.0));
		const qreal y1 = yFor(target + tol), y2 = yFor(target - tol);
		p.drawRect(QRectF(lx - 2 * scale_, y1, barW + 4 * scale_, std::max(1.5, y2 - y1)));
		// ceiling tick across the peak bars
		p.setPen(QPen(col(theme::kBad), 1.0));
		const qreal yc = yFor(ceiling);
		p.drawLine(QPointF(bars.left() - 2 * scale_, yc), QPointF(bars.left() + barW * 2 + gap + 2 * scale_, yc));

		paint::text(p, QRectF(bars.left(), bars.bottom() - 1, barW * 2 + gap, 10 * scale_), QStringLiteral("L R"),
			    col(theme::kTextDim), paint::font(8 * scale_), Qt::AlignCenter);

		// --- numbers ---
		const qreal tx = lx + barW + 10 * scale_;
		const QRectF nr(tx, r.top(), r.right() - tx, r.height());
		qreal y = nr.top() + 2 * scale_;
		const qreal big = 20 * scale_;
		paint::text(p, QRectF(nr.left(), y, nr.width(), big * 1.2), a.active ? paint::db(a.shortTerm) : QStringLiteral("—"),
			    lc, paint::font(big, true));
		y += big * 1.2;
		paint::text(p, QRectF(nr.left(), y, nr.width(), 14 * scale_), QStringLiteral("LUFS short-term"),
			    col(theme::kTextDim), paint::font(10 * scale_));
		y += 18 * scale_;
		paint::text(p, QRectF(nr.left(), y, nr.width(), 15 * scale_),
			    QStringLiteral("%1 integrated").arg(paint::db(a.integrated)), col(theme::kText),
			    paint::font(11 * scale_));
		y += 15 * scale_;
		paint::text(p, QRectF(nr.left(), y, nr.width(), 15 * scale_),
			    QStringLiteral("%1 dBTP max").arg(paint::db(a.truePeakMax)),
			    a.truePeakMax > ceiling ? col(theme::kWarn) : col(theme::kText), paint::font(11 * scale_));
		y += 15 * scale_;
		paint::text(p, QRectF(nr.left(), y, nr.width(), 15 * scale_),
			    QStringLiteral("Target %1 · %2").arg(paint::db(target, 0), m.targetName()), col(theme::kGood),
			    paint::font(10.5 * scale_));
	}

private:
	QTimer *timer_ = nullptr;
	qreal scale_ = 1.0;
};

// ---------------------------------------------------------------------------

SidePanel::SidePanel(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_StyledBackground, false);

	health_ = new HealthButton(this);
	connect(health_, &QAbstractButton::clicked, this, &SidePanel::toggleDetails);

	stack_ = new QStackedWidget(this);

	auto *controls = new QWidget(stack_);
	auto *cl = new QVBoxLayout(controls);
	cl->setContentsMargins(0, 0, 0, 0);
	cl->setSpacing(6);

	streamSection_ = new Section(QStringLiteral("Stream"), controls);
	streamButton_ = new StateButton(streamSection_);
	streamSection_->setChild(streamButton_);
	connect(streamButton_, &QAbstractButton::clicked, this, &SidePanel::onStreamClicked);

	recordSection_ = new Section(QStringLiteral("Record"), controls);
	recordButton_ = new StateButton(recordSection_);
	recordSection_->setChild(recordButton_);
	connect(recordButton_, &QAbstractButton::clicked, this, &SidePanel::onRecordClicked);

	audioSection_ = new Section(QStringLiteral("Programme audio"), controls);
	meter_ = new AudioMeterWidget(audioSection_);
	audioSection_->setChild(meter_, 1);

	cl->addWidget(streamSection_);
	cl->addWidget(recordSection_);
	cl->addWidget(audioSection_, 1);

	details_ = new HealthList(stack_);
	connect(details_, &QAbstractButton::clicked, this, &SidePanel::toggleDetails);

	stack_->addWidget(controls);
	stack_->addWidget(details_);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);
	layout->addWidget(health_);
	layout->addWidget(stack_, 1);

	blinkTimer_ = new QTimer(this);
	blinkTimer_->setInterval(500);
	connect(blinkTimer_, &QTimer::timeout, this, [this] {
		blink_ = !blink_;
		if (Metrics::get().health.overall >= Level::Warn)
			refresh();
	});
	blinkTimer_->start();

	connect(&Metrics::get(), &Metrics::updated, this, &SidePanel::refresh);
	setCompact(false);
}

void SidePanel::setCompact(bool compact)
{
	compact_ = compact;
	const qreal s = compact ? 0.82 : 1.0;
	setFixedWidth(static_cast<int>((compact ? 168 : 214)));
	streamSection_->setScale(s);
	recordSection_->setScale(s);
	audioSection_->setScale(s);
	meter_->setScale(s);
	refresh();
}

void SidePanel::paintEvent(QPaintEvent *)
{
	// transparent: the parent paints the background
}

void SidePanel::toggleDetails()
{
	stack_->setCurrentIndex(stack_->currentIndex() == 0 ? 1 : 0);
	refresh();
}

void SidePanel::refresh()
{
	const Metrics &m = Metrics::get();
	const qreal s = compact_ ? 0.82 : 1.0;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const HealthReport &h = m.health;

	health_->setState(h, stack_->currentIndex() == 1, blink_, s);
	details_->setState(h, s);

	// ---- stream ----
	if (m.stream.active)
		streamPendingUntil_ = 0;
	if (m.stream.active && m.stream.reconnecting)
		streamButton_->setLook(QStringLiteral("Reconnecting…"), theme::kWarn, theme::kBg, false, s);
	else if (m.stream.active)
		streamButton_->setLook(QStringLiteral("ON AIR   %1").arg(paint::duration(m.stream.uptimeSec)), theme::kBad,
				       0xFFFFFFFF, false, s);
	else if (now < streamPendingUntil_)
		streamButton_->setLook(QStringLiteral("Starting…"), theme::kPanelRaised, theme::kTextDim, true, s);
	else
		streamButton_->setLook(QStringLiteral("Start stream"), theme::kPanelRaised, theme::kText, true, s);

	std::vector<std::pair<QString, uint32_t>> sl;
	if (m.stream.active) {
		sl.push_back({QStringLiteral("%1 Mbps · %2% dropped")
				      .arg(paint::mbps(m.stream.kbps))
				      .arg(m.stream.dropPct, 0, 'f', 1),
			      m.stream.droppedRecent > 0 ? theme::kWarn : theme::kText});
		QString second = m.stream.reconnects == 0   ? QStringLiteral("No reconnects")
				 : m.stream.reconnects == 1 ? QStringLiteral("1 reconnect")
							    : QStringLiteral("%1 reconnects").arg(m.stream.reconnects);
		sl.push_back({second, m.stream.reconnects ? theme::kWarn : theme::kTextDim});
	} else {
		sl.push_back({QStringLiteral("Not streaming"), theme::kTextDim});
	}
	streamSection_->setLines(std::move(sl));
	Level streamAlarm = std::max(h.levelOf("Stream"), h.levelOf("Network"));
	streamSection_->setAlarm(streamAlarm, blink_);

	// ---- record ----
	if (m.record.active)
		recordPendingUntil_ = 0;
	if (m.record.active && m.record.paused)
		recordButton_->setLook(QStringLiteral("PAUSED   %1").arg(paint::duration(m.record.durationSec)), theme::kWarn,
				       theme::kBg, false, s);
	else if (m.record.active)
		recordButton_->setLook(QStringLiteral("REC   %1").arg(paint::duration(m.record.durationSec)), theme::kBad,
				       0xFFFFFFFF, false, s);
	else if (now < recordPendingUntil_)
		recordButton_->setLook(QStringLiteral("Starting…"), theme::kPanelRaised, theme::kTextDim, true, s);
	else
		recordButton_->setLook(QStringLiteral("Start recording"), theme::kPanelRaised, theme::kText, true, s);

	std::vector<std::pair<QString, uint32_t>> rl;
	if (m.record.diskFreeGB >= 0.0) {
		QString line = QStringLiteral("%1 GB free").arg(m.record.diskFreeGB, 0, 'f', 0);
		if (m.record.hoursLeft >= 0.0)
			line += QStringLiteral(" · ~%1 h").arg(m.record.hoursLeft, 0, 'f', 1);
		rl.push_back({line, h.levelOf("Disk") >= Level::Warn ? theme::kWarn : theme::kTextDim});
	} else {
		rl.push_back({QStringLiteral("Recording folder not found"), theme::kTextDim});
	}
	recordSection_->setLines(std::move(rl));
	recordSection_->setAlarm(h.levelOf("Disk"), blink_);

	// ---- audio ----
	Level audioAlarm = std::max({h.levelOf("Loudness"), h.levelOf("Peaks"), h.levelOf("Silence")});
	// Loudness drifting off target is information, not an alarm; only flash for
	// clipping, silence or far-too-loud audio.
	if (audioAlarm == Level::Warn && h.levelOf("Peaks") < Level::Warn && h.levelOf("Silence") < Level::Warn)
		audioAlarm = Level::Off;
	audioSection_->setAlarm(audioAlarm, blink_);
}

void SidePanel::onStreamClicked()
{
	if (obs_frontend_streaming_active()) {
		const auto answer = QMessageBox::question(window(), QStringLiteral("Stop the stream?"),
							  QStringLiteral("Viewers will lose the stream."),
							  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
		if (answer == QMessageBox::Yes) {
			obs_log(LOG_INFO, "stream: stop requested from OBSViewer");
			obs_frontend_streaming_stop();
		}
	} else {
		obs_log(LOG_INFO, "stream: start requested from OBSViewer");
		streamPendingUntil_ = QDateTime::currentMSecsSinceEpoch() + 15000;
		obs_frontend_streaming_start();
	}
	refresh();
}

void SidePanel::onRecordClicked()
{
	if (obs_frontend_recording_active()) {
		const auto answer = QMessageBox::question(window(), QStringLiteral("Stop recording?"),
							  QStringLiteral("The recording file will be finished and closed."),
							  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
		if (answer == QMessageBox::Yes) {
			obs_log(LOG_INFO, "record: stop requested from OBSViewer");
			obs_frontend_recording_stop();
		}
	} else {
		obs_log(LOG_INFO, "record: start requested from OBSViewer");
		recordPendingUntil_ = QDateTime::currentMSecsSinceEpoch() + 10000;
		obs_frontend_recording_start();
	}
	refresh();
}

} // namespace obsviewer
