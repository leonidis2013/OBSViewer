#include "ui/SidePanel.hpp"

#include "core/Settings.hpp"
#include "metrics/Metrics.hpp"
#include "ui/Paint.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace obsviewer {

using paint::col;

// ---------------------------------------------------------------------------
// Header icon button.

IconButton::IconButton(Icon icon, const QString &tip, QWidget *parent) : QAbstractButton(parent), icon_(icon)
{
	setCursor(Qt::PointingHandCursor);
	setToolTip(tip);
	setFixedSize(24, 24);
	setFocusPolicy(Qt::NoFocus);
}

void IconButton::setIcon(Icon icon)
{
	icon_ = icon;
	update();
}

void IconButton::enterEvent(QEnterEvent *e)
{
	QAbstractButton::enterEvent(e);
	update();
}

void IconButton::leaveEvent(QEvent *e)
{
	QAbstractButton::leaveEvent(e);
	update();
}

void IconButton::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	const bool hot = underMouse();
	if (hot) {
		p.setPen(QPen(col(theme::kBorder), 1.0));
		p.setBrush(col(isDown() ? theme::kPanel : theme::kPanelRaised));
		p.drawRoundedRect(r, 5, 5);
	}
	const QColor ink = col(hot ? theme::kText : theme::kTextDim);
	p.setPen(QPen(ink, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	const QPointF c = r.center();
	const qreal u = 5.5; // half size of the glyph

	switch (icon_) {
	case Icon::Settings: {
		// gear: ring with eight teeth
		p.drawEllipse(c, u * 0.62, u * 0.62);
		p.drawEllipse(c, u * 0.22, u * 0.22);
		for (int i = 0; i < 8; ++i) {
			const qreal a = i * M_PI / 4.0;
			const QPointF d(std::cos(a), std::sin(a));
			p.drawLine(c + d * (u * 0.66), c + d * (u * 1.0));
		}
		break;
	}
	case Icon::Fullscreen: {
		// four outward corners
		const qreal k = u * 0.45;
		for (int sx : {-1, 1})
			for (int sy : {-1, 1}) {
				const QPointF corner(c.x() + sx * u, c.y() + sy * u);
				p.drawLine(corner, corner - QPointF(sx * k, 0));
				p.drawLine(corner, corner - QPointF(0, sy * k));
			}
		break;
	}
	case Icon::Windowed: {
		// four inward corners (exit fullscreen)
		const qreal k = u * 0.45;
		for (int sx : {-1, 1})
			for (int sy : {-1, 1}) {
				const QPointF corner(c.x() + sx * u * 0.35, c.y() + sy * u * 0.35);
				p.drawLine(corner, corner + QPointF(sx * k, 0));
				p.drawLine(corner, corner + QPointF(0, sy * k));
			}
		break;
	}
	case Icon::Close:
		p.drawLine(c + QPointF(-u * 0.7, -u * 0.7), c + QPointF(u * 0.7, u * 0.7));
		p.drawLine(c + QPointF(u * 0.7, -u * 0.7), c + QPointF(-u * 0.7, u * 0.7));
		break;
	case Icon::Mixer: {
		// three faders with their caps at different heights
		const qreal xs[3] = {c.x() - u * 0.75, c.x(), c.x() + u * 0.75};
		const qreal caps[3] = {0.35, -0.45, 0.1};
		for (int i = 0; i < 3; ++i) {
			p.drawLine(QPointF(xs[i], c.y() - u), QPointF(xs[i], c.y() + u));
			p.setBrush(ink);
			p.drawRoundedRect(QRectF(xs[i] - 2.2, c.y() + caps[i] * u - 1.6, 4.4, 3.2), 1, 1);
			p.setBrush(Qt::NoBrush);
		}
		break;
	}
	case Icon::DockIn: {
		// window with an arrow coming into its bottom-left corner
		const QRectF box(c.x() - u * 0.45, c.y() - u, u * 1.45, u * 1.55);
		p.drawRoundedRect(box, 1.5, 1.5);
		const QPointF tip(c.x() - u * 0.15, c.y() + u * 0.25);
		const QPointF tail(c.x() - u, c.y() + u);
		p.drawLine(tail, tip);
		p.drawLine(tip, tip + QPointF(-u * 0.6, 0));
		p.drawLine(tip, tip + QPointF(0, u * 0.6));
		break;
	}
	case Icon::PopOut: {
		// window with an arrow leaving its top-right corner
		const QRectF box(c.x() - u, c.y() - u * 0.55, u * 1.45, u * 1.55);
		p.drawRoundedRect(box, 1.5, 1.5);
		const QPointF tip(c.x() + u, c.y() - u);
		p.drawLine(tip, tip + QPointF(-u * 0.9, u * 0.9));
		p.drawLine(tip, tip + QPointF(-u * 0.6, 0));
		p.drawLine(tip, tip + QPointF(0, u * 0.6));
		break;
	}
	}
}

// ---------------------------------------------------------------------------
// Overall health light. Click to show / hide every check.
class HealthButton : public QAbstractButton {
public:
	explicit HealthButton(QWidget *parent) : QAbstractButton(parent)
	{
		setCursor(Qt::PointingHandCursor);
		setToolTip(QStringLiteral("Show everything OBSViewer is checking"));
	}

	void setState(const HealthReport &report, bool expanded, bool blink, qreal scale, const QString &label = {})
	{
		report_ = report;
		expanded_ = expanded;
		blink_ = blink;
		scale_ = scale;
		customLabel_ = label;
		setFixedHeight(static_cast<int>(28 * scale));
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect());
		uint32_t border = theme::kBorder;
		// Only a real problem lights the outline, and only while alarms are on.
		if (Settings::get().alarms && report_.overall == Level::Bad && blink_)
			border = theme::kBad;
		paint::panel(p, r, 6.0, underMouse() ? theme::kPanelRaised : theme::kPanel, border);

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
		if (!customLabel_.isEmpty())
			label = bads ? QStringLiteral("%1 · %2").arg(customLabel_, QString::fromStdString(firstBad))
				     : customLabel_;
		else if (bads)
			label = QStringLiteral("Problem: %1").arg(QString::fromStdString(firstBad));
		else if (warns)
			label = warns == 1 ? QStringLiteral("1 warning") : QStringLiteral("%1 warnings").arg(warns);
		else if (report_.overall == Level::Good)
			label = QStringLiteral("All good");
		else
			label = QStringLiteral("Health");

		const qreal d = 8 * scale_;
		const QColor dot = report_.overall == Level::Off ? col(theme::kTextFaint) : paint::levelColor(report_.overall);
		p.setPen(Qt::NoPen);
		p.setBrush(dot);
		p.drawEllipse(QRectF(r.left() + 10 * scale_, r.center().y() - d / 2, d, d));

		const QRectF tr = r.adjusted(25 * scale_, 0, -20 * scale_, 0);
		paint::text(p, tr, label, col(theme::kText), paint::font(11.5 * scale_, true));
		paint::text(p, r.adjusted(0, 0, -9 * scale_, 0), expanded_ ? QStringLiteral("▾") : QStringLiteral("▸"),
			    col(theme::kTextDim), paint::font(11 * scale_), Qt::AlignRight | Qt::AlignVCenter);
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
	HealthReport report_;
	bool expanded_ = false;
	bool blink_ = false;
	qreal scale_ = 1.0;
	QString customLabel_;
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

	void setState(const HealthReport &report, qreal scale, const QString &title, uint32_t accent)
	{
		report_ = report;
		scale_ = scale;
		title_ = title;
		accent_ = accent;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect());
		paint::panel(p, r);
		paint::sectionHeader(p, r, accent_, title_, scale_);

		const qreal pad = 10 * scale_;
		const int n = static_cast<int>(report_.checks.size());
		const qreal top = r.top() + 32 * scale_;
		const qreal rowH = std::min(36 * scale_, (r.height() - 32 * scale_ - 22 * scale_) / std::max(1, n));
		for (int i = 0; i < n; ++i) {
			const auto &c = report_.checks[static_cast<size_t>(i)];
			const QRectF row(r.left() + pad, top + i * rowH, r.width() - 2 * pad, rowH);
			const qreal d = 7 * scale_;
			p.setPen(Qt::NoPen);
			p.setBrush(c.level == Level::Off ? col(theme::kTextFaint) : paint::levelColor(c.level));
			p.drawEllipse(QRectF(row.left(), row.top() + rowH * 0.28 - d / 2, d, d));
			const QRectF textR = row.adjusted(14 * scale_, 0, 0, 0);
			paint::text(p, QRectF(textR.left(), row.top(), textR.width(), rowH * 0.55), QString::fromStdString(c.name),
				    col(theme::kText), paint::font(11.5 * scale_, true), Qt::AlignLeft | Qt::AlignVCenter);
			paint::text(p, QRectF(textR.left(), row.top() + rowH * 0.45, textR.width(), rowH * 0.5),
				    QString::fromStdString(c.detail), col(theme::kTextDim), paint::font(10.5 * scale_),
				    Qt::AlignLeft | Qt::AlignVCenter);
		}
		paint::text(p, QRectF(r.left() + pad, r.bottom() - 20 * scale_, r.width() - 2 * pad, 16 * scale_),
			    QStringLiteral("Click to close"), col(theme::kTextFaint), paint::font(10 * scale_),
			    Qt::AlignRight | Qt::AlignVCenter);
	}

private:
	HealthReport report_;
	qreal scale_ = 1.0;
	QString title_;
	uint32_t accent_ = theme::kAccentGreen;
};

// ---------------------------------------------------------------------------
// Big status / action button (Start stream ↔ ON AIR 01:12:45).
class StateButton : public QAbstractButton {
public:
	explicit StateButton(QWidget *parent) : QAbstractButton(parent) { setCursor(Qt::PointingHandCursor); }

	void setLook(const QString &label, uint32_t fill, uint32_t textColor, bool outlined, qreal scale,
		     bool enabled = true, const QString &tip = QString())
	{
		setEnabled(enabled);
		setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
		setToolTip(tip);
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
		if (underMouse() && isEnabled())
			fill = fill.lighter(isDown() ? 95 : 118);
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
// LiveVoice panel: accent tab + title on top, children stacked below.
class Section : public QWidget {
public:
	Section(const QString &title, uint32_t accent, QWidget *parent) : QWidget(parent), title_(title), accent_(accent)
	{
		layout_ = new QVBoxLayout(this);
		relayout();
	}

	void addChild(QWidget *child, int stretch = 0) { layout_->addWidget(child, stretch); }

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
		const QRectF r = QRectF(rect());
		paint::panel(p, r);
		paint::sectionHeader(p, r, accent_, title_, scale_);
	}

private:
	void relayout()
	{
		const int pad = static_cast<int>(std::lround(9 * scale_));
		layout_->setContentsMargins(pad, static_cast<int>(std::lround(31 * scale_)), pad, pad);
		layout_->setSpacing(static_cast<int>(std::lround(7 * scale_)));
	}

	QString title_;
	uint32_t accent_;
	QVBoxLayout *layout_ = nullptr;
	qreal scale_ = 1.0;
};

// ---------------------------------------------------------------------------
// Label / value rows (label dim on the left, value on the right).
class StatLines : public QWidget {
public:
	struct Line {
		QString label;
		QString value;
		uint32_t color = theme::kText;
	};

	explicit StatLines(QWidget *parent) : QWidget(parent) {}

	void setLines(std::vector<Line> lines, qreal scale)
	{
		lines_ = std::move(lines);
		scale_ = scale;
		setFixedHeight(static_cast<int>(std::lround(lineH() * static_cast<qreal>(lines_.size()))));
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		qreal y = 0;
		for (const auto &l : lines_) {
			const QRectF row(0, y, width(), lineH());
			paint::text(p, row, l.label, col(theme::kTextDim), paint::font(10.5 * scale_));
			paint::text(p, row.adjusted(row.width() * 0.34, 0, 0, 0), l.value, col(l.color),
				    paint::font(10.5 * scale_, true), Qt::AlignRight | Qt::AlignVCenter);
			y += lineH();
		}
	}

private:
	qreal lineH() const { return 16 * scale_; }
	std::vector<Line> lines_;
	qreal scale_ = 1.0;
};

// ---------------------------------------------------------------------------
// Network: upload speed, 5-minute bitrate history, dropped frames, connection.
class NetworkStats : public QWidget {
public:
	explicit NetworkStats(QWidget *parent) : QWidget(parent) { setScale(1.0); }

	void setScale(qreal scale)
	{
		scale_ = scale;
		setFixedHeight(static_cast<int>(std::lround((28 + 6 + 34 + 6 + 16 * 3) * scale_)));
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		const Metrics &m = Metrics::get();
		const HealthReport &h = m.health;
		const qreal s = scale_;
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect());

		// ---- upload ----
		const QRectF top(r.left(), r.top(), r.width(), 28 * s);
		const QString speed = m.stream.active ? paint::mbps(m.stream.kbps) : QStringLiteral("—");
		const QFont big = paint::font(21 * s, true);
		paint::text(p, top, speed, col(m.stream.active ? theme::kText : theme::kTextFaint), big);
		const qreal sw = QFontMetricsF(big).horizontalAdvance(speed);
		paint::text(p, QRectF(top.left() + sw + 5 * s, top.top() + 4 * s, top.width() / 2, top.height()),
			    QStringLiteral("Mbps up"), col(theme::kTextDim), paint::font(10.5 * s));

		// ---- bitrate history ----
		const auto &hist = m.bitrateHistory;
		const size_t cap = hist.capacity();
		double peak = 1000.0;
		for (size_t i = 0; i < hist.size(); ++i)
			peak = std::max(peak, hist.at(i).kbps);
		paint::text(p, top, QStringLiteral("peak %1").arg(paint::mbps(peak)), col(theme::kTextFaint),
			    paint::font(9.5 * s), Qt::AlignRight | Qt::AlignTop);
		paint::text(p, top, QStringLiteral("5 min"), col(theme::kTextFaint), paint::font(9.5 * s),
			    Qt::AlignRight | Qt::AlignBottom);
		peak *= 1.15;

		const QRectF graph(r.left(), top.bottom() + 6 * s, r.width(), 34 * s);
		p.setPen(Qt::NoPen);
		p.setBrush(col(theme::kBg));
		p.drawRoundedRect(graph, 3, 3);
		const QRectF plot = graph.adjusted(2, 2, -2, -2);
		const qreal bw = plot.width() / static_cast<qreal>(cap);
		const size_t offset = cap - hist.size(); // newest at the right edge
		for (size_t i = 0; i < hist.size(); ++i) {
			const BitrateSample &b = hist.at(i);
			if (!b.active)
				continue;
			const qreal bx = plot.left() + static_cast<qreal>(offset + i) * bw;
			QColor c = col(theme::kAccentTeal);
			if (b.reconnecting || b.dropped >= 10)
				c = col(theme::kBad);
			else if (b.dropped > 0)
				c = col(theme::kWarn);
			const qreal bh = std::max(1.0, plot.height() * b.kbps / peak);
			p.setBrush(c);
			p.drawRect(QRectF(bx, plot.bottom() - bh, std::max(0.6, bw - 0.25), bh));
		}

		// ---- dropped / connection / server ----
		qreal y = graph.bottom() + 6 * s;
		const qreal lh = 16 * s;
		auto row = [&](const QString &label, const QString &value, uint32_t color) {
			const QRectF rr(r.left(), y, r.width(), lh);
			paint::text(p, rr, label, col(theme::kTextDim), paint::font(10.5 * s));
			paint::text(p, rr.adjusted(rr.width() * 0.3, 0, 0, 0), value, col(color), paint::font(10.5 * s, true),
				    Qt::AlignRight | Qt::AlignVCenter);
			y += lh;
		};

		const bool dropWarn = h.levelOf("Network") >= Level::Warn || h.levelOf("Encoder") >= Level::Warn ||
				      h.levelOf("Rendering") >= Level::Warn;
		row(QStringLiteral("Dropped"),
		    QStringLiteral("net %1 · enc %2 · ren %3")
			    .arg(m.stream.framesDropped)
			    .arg(m.system.encoderSkippedTotal)
			    .arg(m.system.renderLaggedTotal),
		    dropWarn ? theme::kWarn : theme::kText);

		if (!m.stream.active) {
			row(QStringLiteral("Connection"), QStringLiteral("Offline"), theme::kTextFaint);
			row(QStringLiteral("Server"), QStringLiteral("—"), theme::kTextFaint);
		} else {
			if (m.stream.reconnecting)
				row(QStringLiteral("Connection"), QStringLiteral("Reconnecting…"), theme::kBad);
			else
				row(QStringLiteral("Connection"),
				    QStringLiteral("%1 · %2")
					    .arg(m.stream.reconnects == 0   ? QStringLiteral("Connected")
						 : m.stream.reconnects == 1 ? QStringLiteral("1 reconnect")
									    : QStringLiteral("%1 reconnects").arg(m.stream.reconnects))
					    .arg(paint::duration(m.stream.uptimeSec)),
				    m.stream.reconnects ? theme::kWarn : theme::kGood);
			const int cong = static_cast<int>(std::lround(m.stream.congestion * 100.0f));
			row(QStringLiteral("Server"),
			    m.stream.connectMs >= 0 ? QStringLiteral("%1 ms · load %2%").arg(m.stream.connectMs).arg(cong)
						    : QStringLiteral("load %1%").arg(cong),
			    cong >= 50 ? theme::kWarn : theme::kText);
		}
	}

private:
	qreal scale_ = 1.0;
};

// ---------------------------------------------------------------------------
// Programme meter (LiveVoice style): L / R sample peak and short-term loudness
// with zone colours, a peak-hold line and a target bracket.
class AudioMeterWidget : public QAbstractButton {
public:
	explicit AudioMeterWidget(QWidget *parent) : QAbstractButton(parent)
	{
		setCursor(Qt::PointingHandCursor);
		setToolTip(QStringLiteral("Click to reset the integrated loudness and peak hold"));
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMinimumHeight(110);
		timer_ = new QTimer(this);
		timer_->setInterval(50);
		connect(timer_, &QTimer::timeout, this, [this] {
			if (isVisible()) {
				Metrics::get().refreshAudio();
				update();
			}
		});
		timer_->start();
		connect(this, &QAbstractButton::clicked, this, [this] {
			ProgramLoudness::get().resetIntegrated();
			for (auto &h : hold_)
				h = Hold{};
		});
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
		const qreal s = scale_;
		const qint64 now = QDateTime::currentMSecsSinceEpoch();

		const float values[3] = {a.active ? a.peakL : -120.0f, a.active ? a.peakR : -120.0f,
					 a.active ? a.shortTerm : -120.0f};
		for (int i = 0; i < 3; ++i) {
			Hold &h = hold_[i];
			if (values[i] >= h.value || now - h.at > 2000) {
				h.value = values[i];
				h.at = now;
			}
		}

		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect());

		constexpr float kMin = -60.0f;
		const qreal scaleW = 20 * s;
		const qreal header = 28 * s;
		const qreal barW = std::clamp(r.width() * 0.085, 7.0 * s, 14.0 * s);
		const qreal gap = 5 * s;
		const QRectF bars(r.left() + scaleW, r.top() + header, barW * 3 + gap * 2, r.height() - header - 2 * s);
		auto yFor = [&](float v) {
			const float t = std::clamp((v - kMin) / (0.0f - kMin), 0.0f, 1.0f);
			return bars.bottom() - t * bars.height();
		};

		// Peak bars: quiet / normal / hot zones; loudness bar: zones around the target.
		auto peakZone = [&](float v) { return paint::zone(v, -18.0f, -6.0f, ceiling); };
		auto loudZone = [&](float v) { return paint::zone(v, target - tol, target + tol, target + tol + 3.0f); };

		auto gradient = [&](const QRectF &bar, bool loudness) {
			QLinearGradient g(bar.bottomLeft(), bar.topLeft());
			auto pos = [&](float db) { return std::clamp(static_cast<qreal>((db - kMin) / -kMin), 0.0, 1.0); };
			const float lo = loudness ? target - tol : -18.0f;
			const float hi = loudness ? target + tol : -6.0f;
			const float warn = loudness ? target + tol + 3.0f : ceiling;
			g.setColorAt(0.0, col(theme::kAccentSlate));
			g.setColorAt(pos(lo - 12.5f), col(theme::kAccentSlate));
			g.setColorAt(pos(lo - 11.5f), col(theme::kAccentBlue));
			g.setColorAt(pos(lo - 0.5f), col(theme::kAccentBlue));
			g.setColorAt(pos(lo + 0.5f), col(theme::kGood));
			g.setColorAt(pos(hi - 0.5f), col(theme::kGood));
			g.setColorAt(pos(hi + 0.5f), col(theme::kWarn));
			g.setColorAt(pos(warn - 0.3f), col(theme::kWarn));
			g.setColorAt(pos(warn + 0.3f), col(theme::kBad));
			g.setColorAt(1.0, col(theme::kBad));
			return g;
		};

		const QRectF barRects[3] = {QRectF(bars.left(), bars.top(), barW, bars.height()),
					    QRectF(bars.left() + barW + gap, bars.top(), barW, bars.height()),
					    QRectF(bars.left() + 2 * (barW + gap), bars.top(), barW, bars.height())};

		// target bracket behind the loudness bar
		{
			const qreal y0 = yFor(target + tol), y1 = yFor(target - tol);
			QColor band = col(theme::kGood);
			band.setAlphaF(0.12f);
			p.setPen(Qt::NoPen);
			p.setBrush(band);
			p.drawRect(QRectF(barRects[2].left() - 3 * s, y0, barW + 6 * s, std::max(1.5, y1 - y0)));
		}

		const char *names[3] = {"L", "R", "ST"};
		for (int i = 0; i < 3; ++i) {
			const QRectF &bar = barRects[i];
			const bool loud = i == 2;
			// track with a faint zone tint
			p.setPen(Qt::NoPen);
			p.setBrush(col(theme::kBg));
			p.drawRoundedRect(bar, 2, 2);
			p.setOpacity(0.12);
			p.setBrush(gradient(bar, loud));
			p.drawRoundedRect(bar, 2, 2);
			p.setOpacity(1.0);
			// level
			if (values[i] > kMin) {
				p.setBrush(gradient(bar, loud));
				p.drawRoundedRect(QRectF(bar.left(), yFor(values[i]), bar.width(), bar.bottom() - yFor(values[i])),
						  2, 2);
			}
			// peak hold
			const float held = hold_[i].value;
			if (held > kMin) {
				p.setBrush(loud ? loudZone(held) : peakZone(held));
				p.drawRect(QRectF(bar.left(), yFor(held) - 1.0, bar.width(), 2.0));
			}
			// header: name + held value
			const QRectF hr(bar.left() - 8 * s, r.top(), bar.width() + 16 * s, 13 * s);
			paint::text(p, hr, QString::fromLatin1(names[i]), col(theme::kTextDim), paint::font(9.5 * s, true),
				    Qt::AlignCenter);
			paint::text(p, hr.translated(0, 13 * s), held > -99.0f ? paint::db(held, loud ? 1 : 0) : QStringLiteral("−∞"),
				    held > kMin ? (loud ? loudZone(held) : peakZone(held)) : col(theme::kTextFaint),
				    paint::font(10 * s, true), Qt::AlignCenter);
		}

		// ceiling tick across the peak bars
		p.setPen(QPen(col(theme::kBad), 1.0));
		const qreal yc = yFor(ceiling);
		p.drawLine(QPointF(barRects[0].left() - 2 * s, yc), QPointF(barRects[1].right() + 2 * s, yc));

		// scale
		for (float tick : {0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f}) {
			const qreal y = yFor(tick);
			if (y - bars.top() < 5 * s && tick != 0.0f)
				continue;
			paint::text(p, QRectF(r.left(), y - 6 * s, scaleW - 4 * s, 12 * s), QString::number(static_cast<int>(tick)),
				    col(theme::kTextFaint), paint::font(9 * s), Qt::AlignRight | Qt::AlignVCenter);
		}

		// ---- numbers ----
		const qreal tx = barRects[2].right() + 12 * s;
		const QRectF nr(tx, r.top(), r.right() - tx, r.height());
		if (nr.width() < 40)
			return;
		qreal y = nr.top();
		const QColor stColor = a.active && a.shortTerm > kMin ? loudZone(a.shortTerm) : col(theme::kTextFaint);
		const qreal big = 22 * s;
		paint::text(p, QRectF(nr.left(), y, nr.width(), big * 1.25),
			    a.active && a.shortTerm > -70.0f ? paint::db(a.shortTerm) : QStringLiteral("—"), stColor,
			    paint::font(big, true));
		y += big * 1.25;
		paint::text(p, QRectF(nr.left(), y, nr.width(), 13 * s), QStringLiteral("LUFS short-term"),
			    col(theme::kTextDim), paint::font(9.5 * s));
		y += 20 * s;

		auto stat = [&](const QString &label, const QString &value, const QColor &c) {
			paint::text(p, QRectF(nr.left(), y, nr.width(), 13 * s), label, col(theme::kTextDim), paint::font(9.5 * s));
			paint::text(p, QRectF(nr.left(), y + 12 * s, nr.width(), 16 * s), value, c, paint::font(12 * s, true));
			y += 31 * s;
		};
		stat(QStringLiteral("Integrated"),
		     a.integrated > -70.0f ? QStringLiteral("%1 LUFS").arg(paint::db(a.integrated)) : QStringLiteral("—"),
		     a.integrated > -70.0f ? loudZone(a.integrated) : col(theme::kTextFaint));
		stat(QStringLiteral("True peak max"),
		     a.truePeakMax > -100.0f ? QStringLiteral("%1 dBTP").arg(paint::db(a.truePeakMax)) : QStringLiteral("—"),
		     a.truePeakMax > ceiling ? col(theme::kBad) : col(theme::kText));
		if (y + 28 * s <= nr.bottom())
			stat(QStringLiteral("Target"), QStringLiteral("%1 · %2").arg(paint::db(target, 0), m.targetName()),
			     col(theme::kGood));
	}

private:
	struct Hold {
		float value = -120.0f;
		qint64 at = 0;
	};
	Hold hold_[3];
	QTimer *timer_ = nullptr;
	qreal scale_ = 1.0;
};

// ---------------------------------------------------------------------------
// Short-term loudness over the last 5 minutes against the target band.
class LoudnessHistory : public QWidget {
public:
	explicit LoudnessHistory(QWidget *parent) : QWidget(parent) { setScale(1.0); }

	void setScale(qreal scale)
	{
		scale_ = scale;
		setFixedHeight(static_cast<int>(std::lround(74 * scale)));
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		const Metrics &m = Metrics::get();
		const qreal s = scale_;
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect());
		const float target = m.targetLufs();
		const float tol = m.toleranceLu();

		const QRectF head(r.left(), r.top(), r.width(), 14 * s);
		paint::text(p, head, QStringLiteral("Loudness · 5 min"), col(theme::kTextDim), paint::font(9.5 * s, true));
		paint::text(p, head, QStringLiteral("target %1 ±%2").arg(paint::db(target, 0)).arg(tol, 0, 'g', 2),
			    col(theme::kGood), paint::font(9.5 * s), Qt::AlignRight | Qt::AlignVCenter);

		const QRectF graph(r.left(), head.bottom() + 4 * s, r.width(), r.height() - head.height() - 4 * s);
		p.setPen(Qt::NoPen);
		p.setBrush(col(theme::kBg));
		p.drawRoundedRect(graph, 3, 3);
		const QRectF plot = graph.adjusted(18 * s, 3, -3, -3);

		// Range shown: target −20 … target +8 LU.
		const float lo = target - 20.0f, hi = target + 8.0f;
		auto yFor = [&](float v) {
			const float t = std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
			return plot.bottom() - t * plot.height();
		};
		QColor band = col(theme::kGood);
		band.setAlphaF(0.12f);
		p.setBrush(band);
		p.drawRect(QRectF(plot.left(), yFor(target + tol), plot.width(), yFor(target - tol) - yFor(target + tol)));
		for (float v : {target, target - 10.0f}) {
			paint::text(p, QRectF(graph.left() + 2, yFor(v) - 6 * s, 16 * s, 12 * s), paint::db(v, 0),
				    col(theme::kTextFaint), paint::font(8.5 * s), Qt::AlignLeft | Qt::AlignVCenter);
			p.setPen(QPen(col(theme::kGrid), 1.0));
			p.drawLine(QPointF(plot.left(), yFor(v)), QPointF(plot.right(), yFor(v)));
		}

		const auto &hist = m.loudnessHistory;
		const size_t cap = hist.capacity();
		const qreal step = plot.width() / static_cast<qreal>(std::max<size_t>(1, cap - 1));
		const size_t offset = cap - hist.size();
		QPointF prev;
		bool havePrev = false;
		for (size_t i = 0; i < hist.size(); ++i) {
			const float v = hist.at(i);
			if (v <= -70.0f) {
				havePrev = false;
				continue;
			}
			const QPointF pt(plot.left() + static_cast<qreal>(offset + i) * step, yFor(v));
			if (havePrev) {
				p.setPen(QPen(paint::zone(v, target - tol, target + tol, target + tol + 3.0f), 1.4 * s, Qt::SolidLine,
					      Qt::RoundCap));
				p.drawLine(prev, pt);
			}
			prev = pt;
			havePrev = true;
		}
	}

private:
	qreal scale_ = 1.0;
};

// ---------------------------------------------------------------------------

namespace {
// True when OBS has what it needs to try to stream (service, server and key).
bool streamConfigured()
{
	obs_service_t *service = obs_frontend_get_streaming_service();
	return service && obs_service_can_try_to_connect(service);
}
} // namespace

SidePanel::SidePanel(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_StyledBackground, false);

	// header: title on the left, icon buttons on the right
	header_ = new QHBoxLayout();
	header_->setContentsMargins(4, 0, 0, 0);
	header_->setSpacing(2);
	auto *title = new QLabel(QStringLiteral("OBSViewer"), this);
	title->setStyleSheet(QStringLiteral("color: #7C8794; font-weight: 600; font-size: 12px;"));
	header_->addWidget(title, 1);

	health_ = new HealthButton(this);
	connect(health_, &QAbstractButton::clicked, this, [this] { showPage(stack_->currentIndex() == 1 ? 0 : 1); });
	perf_ = new HealthButton(this);
	perf_->setToolTip(QStringLiteral("Frame rate, render time and encoders"));
	connect(perf_, &QAbstractButton::clicked, this, [this] { showPage(stack_->currentIndex() == 2 ? 0 : 2); });

	stack_ = new QStackedWidget(this);

	auto *controls = new QWidget(stack_);
	auto *cl = new QVBoxLayout(controls);
	cl->setContentsMargins(0, 0, 0, 0);
	cl->setSpacing(6);

	streamSection_ = new Section(QStringLiteral("Stream"), theme::kAccentTeal, controls);
	streamButton_ = new StateButton(streamSection_);
	network_ = new NetworkStats(streamSection_);
	streamSection_->addChild(streamButton_);
	streamSection_->addChild(network_);
	connect(streamButton_, &QAbstractButton::clicked, this, &SidePanel::onStreamClicked);

	recordSection_ = new Section(QStringLiteral("Record"), theme::kAccentOrange, controls);
	recordButton_ = new StateButton(recordSection_);
	recordLines_ = new StatLines(recordSection_);
	recordSection_->addChild(recordButton_);
	recordSection_->addChild(recordLines_);
	connect(recordButton_, &QAbstractButton::clicked, this, &SidePanel::onRecordClicked);

	audioSection_ = new Section(QStringLiteral("Programme audio"), theme::kAccentBlue, controls);
	meter_ = new AudioMeterWidget(audioSection_);
	loudness_ = new LoudnessHistory(audioSection_);
	audioSection_->addChild(meter_, 1);
	audioSection_->addChild(loudness_);

	cl->addWidget(streamSection_);
	cl->addWidget(recordSection_);
	cl->addWidget(audioSection_, 1);

	details_ = new HealthList(stack_);
	connect(details_, &QAbstractButton::clicked, this, [this] { showPage(0); });
	perfDetails_ = new HealthList(stack_);
	connect(perfDetails_, &QAbstractButton::clicked, this, [this] { showPage(0); });

	stack_->addWidget(controls);
	stack_->addWidget(details_);
	stack_->addWidget(perfDetails_);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);
	layout->addLayout(header_);
	auto *buttons = new QVBoxLayout();
	buttons->setSpacing(4);
	buttons->addWidget(health_);
	buttons->addWidget(perf_);
	layout->addLayout(buttons);
	layout->addWidget(stack_, 1);

	blinkTimer_ = new QTimer(this);
	blinkTimer_->setInterval(500);
	connect(blinkTimer_, &QTimer::timeout, this, [this] {
		blink_ = !blink_;
		if (Metrics::get().health.overall == Level::Bad)
			refresh();
	});
	blinkTimer_->start();

	connect(&Metrics::get(), &Metrics::updated, this, &SidePanel::refresh);
	setCompact(false);
}

IconButton *SidePanel::addHeaderButton(IconButton::Icon icon, const QString &tip, std::function<void()> onClick)
{
	auto *button = new IconButton(icon, tip, this);
	connect(button, &QAbstractButton::clicked, this, [onClick] {
		if (onClick)
			onClick();
	});
	header_->addWidget(button);
	return button;
}

void SidePanel::setCompact(bool compact)
{
	compact_ = compact;
	const qreal s = compact ? 0.84 : 1.0;
	setFixedWidth(compact ? 184 : 236);
	streamSection_->setScale(s);
	recordSection_->setScale(s);
	audioSection_->setScale(s);
	network_->setScale(s);
	meter_->setScale(s);
	loudness_->setScale(s);
	refresh();
}

void SidePanel::paintEvent(QPaintEvent *)
{
	// transparent: the parent paints the background
}

void SidePanel::showPage(int page)
{
	stack_->setCurrentIndex(page);
	refresh();
}

void SidePanel::refresh()
{
	const Metrics &m = Metrics::get();
	const qreal s = compact_ ? 0.84 : 1.0;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const HealthReport &h = m.health;

	health_->setState(h, stack_->currentIndex() == 1, blink_, s);
	details_->setState(h, s, QStringLiteral("Health checks"), theme::kAccentGreen);
	perf_->setState(m.performance, stack_->currentIndex() == 2, blink_, s,
			QStringLiteral("%1 fps · %2 ms").arg(m.perf.fpsActive, 0, 'f', 2).arg(m.perf.renderMs, 0, 'f', 1));
	perfDetails_->setState(m.performance, s, QStringLiteral("Performance"), theme::kAccentViolet);

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
	else if (!streamConfigured())
		streamButton_->setLook(QStringLiteral("Stream not set up"), theme::kPanel, theme::kTextFaint, true, s, false,
				       QStringLiteral("Choose a service and stream key in OBS Settings ▸ Stream first."));
	else
		streamButton_->setLook(QStringLiteral("Start stream"), theme::kPanelRaised, theme::kText, true, s);
	network_->update();

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
	else if (m.record.diskFreeGB < 0.0)
		recordButton_->setLook(QStringLiteral("No recording folder"), theme::kPanel, theme::kTextFaint, true, s, false,
				       QStringLiteral("Choose a recording path in OBS Settings ▸ Output ▸ Recording first."));
	else
		recordButton_->setLook(QStringLiteral("Start recording"), theme::kPanelRaised, theme::kText, true, s);

	std::vector<StatLines::Line> rl;
	if (m.record.diskFreeGB >= 0.0) {
		QString value = QStringLiteral("%1 GB free").arg(m.record.diskFreeGB, 0, 'f', 0);
		if (m.record.hoursLeft >= 0.0)
			value += QStringLiteral(" · ~%1 h").arg(m.record.hoursLeft, 0, 'f', 1);
		rl.push_back({QStringLiteral("Disk"), value, h.levelOf("Disk") >= Level::Warn ? theme::kWarn : theme::kText});
	} else {
		rl.push_back({QStringLiteral("Disk"), QStringLiteral("folder not found"), theme::kTextFaint});
	}
	recordLines_->setLines(std::move(rl), s);
	loudness_->update();
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
