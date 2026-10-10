#include "ui/NetworkBar.hpp"

#include "core/Settings.hpp"
#include "metrics/Metrics.hpp"
#include "ui/Paint.hpp"

#include <QPaintEvent>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace obsviewer {

using paint::col;

NetworkBar::NetworkBar(QWidget *parent) : QWidget(parent)
{
	connect(&Metrics::get(), &Metrics::updated, this, [this] { update(); });
	blinkTimer_ = new QTimer(this);
	blinkTimer_->setInterval(500);
	connect(blinkTimer_, &QTimer::timeout, this, [this] {
		blink_ = !blink_;
		if (Metrics::get().health.overall >= Level::Warn)
			update();
	});
	blinkTimer_->start();
	setCompact(false);
}

void NetworkBar::setCompact(bool compact)
{
	compact_ = compact;
	setFixedHeight(compact ? 66 : 86);
	update();
}

void NetworkBar::paintEvent(QPaintEvent *)
{
	const Metrics &m = Metrics::get();
	const HealthReport &h = m.health;
	const bool alarms = Settings::get().alarms;
	const qreal s = compact_ ? 0.82 : 1.0;

	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF all = QRectF(rect());
	const qreal gap = 6;

	// Section widths (fractions of the bar).
	const qreal weights[] = {0.11, 0.30, 0.13, 0.14, 0.32};
	QRectF cells[5];
	qreal x = all.left();
	const qreal usable = all.width() - gap * 4;
	for (int i = 0; i < 5; ++i) {
		const qreal w = usable * weights[i];
		cells[i] = QRectF(x, all.top(), w, all.height());
		x += w + gap;
	}

	const qreal pad = 9 * s;
	auto title = [&](const QRectF &c, const QString &t) {
		paint::text(p, QRectF(c.left() + pad, c.top() + 4 * s, c.width() - 2 * pad, 16 * s), t.toUpper(),
			    col(theme::kTextDim), paint::font(10 * s, true));
	};
	auto line = [&](const QRectF &c, int row, const QString &t, uint32_t color) {
		const qreal top = c.top() + 22 * s + row * 16 * s;
		paint::text(p, QRectF(c.left() + pad, top, c.width() - 2 * pad, 16 * s), t, col(color), paint::font(11 * s));
	};

	const Level netLevel = std::max(h.levelOf("Network"), h.levelOf("Stream"));

	// ---- 1. upload ----
	{
		const QRectF c = cells[0];
		paint::panel(p, c, netLevel, blink_, alarms);
		title(c, QStringLiteral("Upload"));
		const qreal big = 22 * s;
		paint::text(p, QRectF(c.left() + pad, c.top() + 20 * s, c.width() - 2 * pad, big * 1.25),
			    m.stream.active ? paint::mbps(m.stream.kbps) : QStringLiteral("—"), col(theme::kText),
			    paint::font(big, true));
		paint::text(p, QRectF(c.left() + pad, c.bottom() - 20 * s, c.width() - 2 * pad, 16 * s),
			    m.stream.active ? QStringLiteral("Mbps to server") : QStringLiteral("Not streaming"),
			    col(theme::kTextDim), paint::font(10 * s));
	}

	// ---- 2. bitrate history ----
	{
		const QRectF c = cells[1];
		paint::panel(p, c, Level::Off, blink_, alarms);
		title(c, QStringLiteral("Bitrate · last 5 min"));
		const QRectF plot(c.left() + pad, c.top() + 22 * s, c.width() - 2 * pad, c.height() - 22 * s - pad);
		const auto &hist = m.bitrateHistory;
		const size_t cap = hist.capacity();
		double peak = 1000.0;
		for (size_t i = 0; i < hist.size(); ++i)
			peak = std::max(peak, hist.at(i).kbps);
		peak *= 1.15;
		const qreal bw = plot.width() / static_cast<qreal>(cap);
		p.setPen(Qt::NoPen);
		const size_t offset = cap - hist.size(); // newest at the right edge
		for (size_t i = 0; i < hist.size(); ++i) {
			const BitrateSample &b = hist.at(i);
			const qreal bx = plot.left() + (offset + i) * bw;
			if (!b.active) {
				p.setBrush(col(theme::kPanelRaised));
				p.drawRect(QRectF(bx, plot.bottom() - 1, std::max(0.5, bw - 0.3), 1));
				continue;
			}
			QColor c2 = col(theme::kGood);
			if (b.reconnecting || b.dropped >= 10)
				c2 = col(theme::kBad);
			else if (b.dropped > 0)
				c2 = col(theme::kWarn);
			const qreal bh = std::max(1.0, plot.height() * b.kbps / peak);
			p.setBrush(c2);
			p.drawRect(QRectF(bx, plot.bottom() - bh, std::max(0.5, bw - 0.3), bh));
		}
		// average line + scale
		if (m.stream.active && hist.size() > 5) {
			double sum = 0.0;
			int n = 0;
			for (size_t i = 0; i < hist.size(); ++i)
				if (hist.at(i).active) {
					sum += hist.at(i).kbps;
					++n;
				}
			if (n > 0) {
				const qreal y = plot.bottom() - plot.height() * (sum / n) / peak;
				p.setPen(QPen(col(theme::kTextDim), 0.75, Qt::DashLine));
				p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
			}
		}
		paint::text(p, QRectF(c.left() + pad, c.top() + 4 * s, c.width() - 2 * pad, 16 * s),
			    QStringLiteral("peak %1 Mbps").arg(paint::mbps(peak / 1.15)), col(theme::kTextDim),
			    paint::font(10 * s), Qt::AlignRight | Qt::AlignVCenter);
	}

	// ---- 3. dropped frames ----
	{
		const QRectF c = cells[2];
		const Level dropLevel = std::max({h.levelOf("Network"), h.levelOf("Encoder"), h.levelOf("Rendering")});
		paint::panel(p, c, dropLevel, blink_, alarms);
		title(c, QStringLiteral("Dropped"));
		line(c, 0, QStringLiteral("Network  %1").arg(m.stream.framesDropped),
		     h.levelOf("Network") >= Level::Warn ? theme::kWarn : theme::kText);
		line(c, 1, QStringLiteral("Encoder  %1").arg(m.system.encoderSkippedTotal),
		     h.levelOf("Encoder") >= Level::Warn ? theme::kWarn : theme::kText);
		line(c, 2, QStringLiteral("Render  %1").arg(m.system.renderLaggedTotal),
		     h.levelOf("Rendering") >= Level::Warn ? theme::kWarn : theme::kText);
	}

	// ---- 4. connection ----
	{
		const QRectF c = cells[3];
		paint::panel(p, c, h.levelOf("Stream"), blink_, alarms);
		title(c, QStringLiteral("Connection"));
		if (!m.stream.active) {
			line(c, 0, QStringLiteral("Offline"), theme::kTextDim);
		} else {
			if (m.stream.reconnecting)
				line(c, 0, QStringLiteral("Reconnecting…"), theme::kBad);
			else
				line(c, 0,
				     m.stream.reconnects == 0   ? QStringLiteral("Connected")
				     : m.stream.reconnects == 1 ? QStringLiteral("1 reconnect")
								: QStringLiteral("%1 reconnects").arg(m.stream.reconnects),
				     m.stream.reconnects ? theme::kWarn : theme::kGood);
			line(c, 1, QStringLiteral("Up %1").arg(paint::duration(m.stream.uptimeSec)), theme::kText);
			const int cong = static_cast<int>(std::lround(m.stream.congestion * 100.0f));
			line(c, 2,
			     m.stream.connectMs >= 0 ? QStringLiteral("Connect %1 ms · load %2%").arg(m.stream.connectMs).arg(cong)
						     : QStringLiteral("Load %1%").arg(cong),
			     cong >= 50 ? theme::kWarn : theme::kTextDim);
		}
	}

	// ---- 5. loudness history ----
	{
		const QRectF c = cells[4];
		const Level loud = std::max(h.levelOf("Silence"), h.levelOf("Peaks"));
		paint::panel(p, c, loud, blink_, alarms);
		title(c, QStringLiteral("Loudness · last 5 min"));
		const float target = m.targetLufs();
		const float tol = m.toleranceLu();
		paint::text(p, QRectF(c.left() + pad, c.top() + 4 * s, c.width() - 2 * pad, 16 * s),
			    QStringLiteral("target %1 ±%2").arg(paint::db(target, 0)).arg(tol, 0, 'g', 2), col(theme::kGood),
			    paint::font(10 * s), Qt::AlignRight | Qt::AlignVCenter);

		const QRectF plot(c.left() + pad + 18 * s, c.top() + 22 * s, c.width() - 2 * pad - 18 * s,
				  c.height() - 22 * s - pad);
		// Range shown: target −20 … target +8 LU.
		const float lo = target - 20.0f, hi = target + 8.0f;
		auto yFor = [&](float v) {
			const float t = std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
			return plot.bottom() - t * plot.height();
		};
		// target band
		QColor band = col(theme::kGood);
		band.setAlpha(38);
		p.setPen(Qt::NoPen);
		p.setBrush(band);
		p.drawRect(QRectF(plot.left(), yFor(target + tol), plot.width(), yFor(target - tol) - yFor(target + tol)));
		// scale
		for (float v : {target, target - 10.0f}) {
			paint::text(p, QRectF(c.left() + pad - 2, yFor(v) - 7 * s, 18 * s, 14 * s), paint::db(v, 0),
				    col(theme::kTextDim), paint::font(9 * s), Qt::AlignLeft | Qt::AlignVCenter);
			p.setPen(QPen(col(theme::kBorder), 0.5));
			p.drawLine(QPointF(plot.left(), yFor(v)), QPointF(plot.right(), yFor(v)));
		}
		// line, coloured by whether it sits in the band
		const auto &hist = m.loudnessHistory;
		const size_t cap = hist.capacity();
		const qreal step = plot.width() / static_cast<qreal>(cap - 1);
		const size_t offset = cap - hist.size();
		QPointF prev;
		bool havePrev = false;
		for (size_t i = 0; i < hist.size(); ++i) {
			const float v = hist.at(i);
			if (v <= -70.0f) {
				havePrev = false;
				continue;
			}
			const QPointF pt(plot.left() + (offset + i) * step, yFor(v));
			if (havePrev) {
				QColor lc = col(0xFF6AA8FF);
				if (v >= target - tol && v <= target + tol)
					lc = col(theme::kGood);
				else if (v > target + tol)
					lc = col(theme::kWarn);
				p.setPen(QPen(lc, 1.4 * s, Qt::SolidLine, Qt::RoundCap));
				p.drawLine(prev, pt);
			}
			prev = pt;
			havePrev = true;
		}
	}
}

} // namespace obsviewer
