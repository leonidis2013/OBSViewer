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

// Small painting helpers for OBSViewer's Qt-drawn panels (LiveVoice style).
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

// LiveVoice panel: dark fill, 1-px border, rounded corners.
inline void panel(QPainter &p, const QRectF &r, qreal radius = 8.0, uint32_t fill = theme::kPanel,
		  uint32_t border = theme::kBorder)
{
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(QPen(col(border), 1.0));
	p.setBrush(col(fill));
	p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
	p.restore();
}

// Section header: 3 × 12 accent tab and a bold title, LiveVoice style.
inline void sectionHeader(QPainter &p, const QRectF &r, uint32_t accent, const QString &title, qreal s = 1.0)
{
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(Qt::NoPen);
	p.setBrush(col(accent));
	p.drawRoundedRect(QRectF(r.left() + 10 * s, r.top() + 10 * s, 3 * s, 12 * s), 1.5 * s, 1.5 * s);
	p.restore();
	QFont f;
	f.setPixelSize(std::max(10, static_cast<int>(std::lround(12.5 * s))));
	f.setWeight(QFont::DemiBold);
	p.setFont(f);
	p.setPen(col(theme::kText));
	p.drawText(QRectF(r.left() + 20 * s, r.top() + 4 * s, r.width() - 28 * s, 24 * s),
		   static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), title);
}

// Meter zone colour (LiveVoice): too quiet, quiet, on target, hot, clipping.
inline QColor zone(float db, float targetLo, float targetHi, float warnDb)
{
	if (db < targetLo - 12.0f)
		return col(theme::kAccentSlate);
	if (db < targetLo)
		return col(theme::kAccentBlue);
	if (db < targetHi)
		return col(theme::kGood);
	if (db < warnDb)
		return col(theme::kWarn);
	return col(theme::kBad);
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
