#pragma once

#include "core/Health.hpp"
#include "ui/Theme.hpp"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QString>

#include <cmath>

// Small painting helpers for OBSViewer's Qt-drawn bars (BroadcastMaster palette).
namespace obsviewer::paint {

inline QColor col(uint32_t argb)
{
	return QColor::fromRgba(argb);
}

inline QColor levelColor(Level level)
{
	switch (level) {
	case Level::Good:
		return col(theme::kGood);
	case Level::Warn:
		return col(theme::kWarn);
	case Level::Bad:
		return col(theme::kBad);
	default:
		return col(theme::kTextDim);
	}
}

inline QFont font(qreal px, bool bold = false)
{
	QFont f;
	f.setPixelSize(std::max(9, static_cast<int>(std::lround(px))));
	f.setWeight(bold ? QFont::DemiBold : QFont::Normal);
	return f;
}

inline QFont mono(qreal px, bool bold = false)
{
	QFont f = font(px, bold);
	f.setStyleHint(QFont::Monospace);
	f.setFamily(QStringLiteral("Menlo"));
	return f;
}

// Rounded panel with a hairline border. When `alarm` is Warn/Bad and alarms are
// on, the border turns amber/red and `blinkOn` makes it pulse.
inline void panel(QPainter &p, const QRectF &r, Level alarm = Level::Off, bool blinkOn = false,
		  bool alarmsEnabled = true)
{
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	QColor border = col(theme::kBorder);
	qreal width = 1.0;
	QColor fill = col(theme::kPanel);
	if (alarmsEnabled && (alarm == Level::Warn || alarm == Level::Bad)) {
		border = levelColor(alarm);
		width = 1.5;
		if (alarm == Level::Bad && blinkOn) {
			QColor tint = border;
			tint.setAlpha(36);
			fill = tint; // painted over the panel colour below
			p.setPen(Qt::NoPen);
			p.setBrush(col(theme::kPanel));
			p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
		} else if (!blinkOn) {
			border.setAlpha(150);
		}
	}
	p.setPen(QPen(border, width));
	p.setBrush(fill);
	p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
	p.restore();
}

inline QString duration(double seconds)
{
	const long s = static_cast<long>(std::max(0.0, seconds));
	return QStringLiteral("%1:%2:%3")
		.arg(s / 3600, 2, 10, QLatin1Char('0'))
		.arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
		.arg(s % 60, 2, 10, QLatin1Char('0'));
}

inline QString mbps(double kbps)
{
	return kbps >= 1000.0 ? QString::number(kbps / 1000.0, 'f', 1) : QString::number(kbps / 1000.0, 'f', 2);
}

inline QString db(float v, int decimals = 1)
{
	if (v <= -119.0f)
		return QStringLiteral("−∞");
	QString s = QString::number(v, 'f', decimals);
	if (s.startsWith(QLatin1Char('-')))
		s.replace(0, 1, QStringLiteral("−"));
	return s;
}

// Text helpers that elide to the available width.
inline void text(QPainter &p, const QRectF &r, const QString &s, const QColor &c, const QFont &f,
		 Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter)
{
	p.setFont(f);
	p.setPen(c);
	const QString elided = p.fontMetrics().elidedText(s, Qt::ElideRight, static_cast<int>(r.width()));
	p.drawText(r, static_cast<int>(align), elided);
}

} // namespace obsviewer::paint
