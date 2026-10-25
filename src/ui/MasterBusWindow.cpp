#include "ui/MasterBusWindow.hpp"

#include "bus/Engine.hpp"
#include "audio/StreamMonitor.hpp"
#include "bus/MasterBus.hpp"
#include "core/Health.hpp"
#include "ui/Paint.hpp"
#include "ui/RoutingView.hpp"
#include "ui/SidePanel.hpp"
#include "ui/Theme.hpp"

#include <plugin-support.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHideEvent>
#include <QShowEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace obsviewer {

using paint::col;

namespace {

constexpr const char *kSlotMime = "application/x-obsviewer-slot";
constexpr int kMinSlots = 8;

QString formatShort(const std::string &format)
{
	return format == "AudioUnit" ? QStringLiteral("AU") : QString::fromStdString(format);
}

// Shared fader / meter scale: −60 … +12 dB, with the top three quarters given
// to −24 … +12 (where the useful range is) and unity at 75 % of the height.
float scalePos(float db)
{
	db = std::clamp(db, -60.0f, 12.0f);
	return db >= -24.0f ? 0.25f + 0.75f * (db + 24.0f) / 36.0f : 0.25f * (db + 60.0f) / 36.0f;
}

float scaleDb(float pos)
{
	pos = std::clamp(pos, 0.0f, 1.0f);
	return pos >= 0.25f ? -24.0f + (pos - 0.25f) / 0.75f * 36.0f : -60.0f + pos / 0.25f * 36.0f;
}

QLinearGradient peakGradient(const QRectF &bar)
{
	QLinearGradient g(bar.bottomLeft(), bar.topLeft());
	auto at = [&](float db) { return static_cast<qreal>(scalePos(db)); };
	g.setColorAt(0.0, col(theme::kAccentSlate));
	g.setColorAt(at(-30.5f), col(theme::kAccentSlate));
	g.setColorAt(at(-29.5f), col(theme::kAccentBlue));
	g.setColorAt(at(-18.5f), col(theme::kAccentBlue));
	g.setColorAt(at(-17.5f), col(theme::kGood));
	g.setColorAt(at(-6.5f), col(theme::kGood));
	g.setColorAt(at(-5.5f), col(theme::kWarn));
	g.setColorAt(at(-1.2f), col(theme::kWarn));
	g.setColorAt(at(-0.8f), col(theme::kBad));
	g.setColorAt(1.0, col(theme::kBad));
	return g;
}

QColor peakColor(float db)
{
	return paint::zone(db, -18.0f, -6.0f, -1.0f);
}

} // namespace

// ---------------------------------------------------------------------------
// Pill-shaped on / off switch.
class PowerButton : public QAbstractButton {
public:
	explicit PowerButton(QWidget *parent) : QAbstractButton(parent)
	{
		setCheckable(true);
		setCursor(Qt::PointingHandCursor);
		setFixedSize(52, 24);
		setToolTip(QStringLiteral("Turn the master bus on or off"));
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		const bool on = isChecked();
		p.setPen(QPen(col(on ? theme::kGood : theme::kBorder), 1.0));
		p.setBrush(on ? col(theme::kGood) : col(theme::kPanelRaised));
		p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
		paint::text(p, r, on ? QStringLiteral("ON") : QStringLiteral("OFF"), col(on ? theme::kBg : theme::kTextDim),
			    paint::font(11, true), Qt::AlignCenter);
	}
};

// ---------------------------------------------------------------------------
// LiveVoice-style button with a label on the left and a chevron on the right.
class StripButton : public QAbstractButton {
public:
	explicit StripButton(QWidget *parent) : QAbstractButton(parent)
	{
		setCursor(Qt::PointingHandCursor);
		setFixedHeight(30);
	}
	void setTexts(const QString &label, const QString &value)
	{
		label_ = label;
		value_ = value;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		p.setPen(QPen(col(underMouse() ? theme::kTextFaint : theme::kBorder), 1.0));
		p.setBrush(col(theme::kPanelRaised));
		p.drawRoundedRect(r, 5, 5);
		paint::text(p, r.adjusted(10, 0, -24, 0), label_, col(theme::kText), paint::font(11.5, true));
		paint::text(p, r.adjusted(10, 0, -24, 0), value_, col(theme::kTextDim), paint::font(11),
			    Qt::AlignRight | Qt::AlignVCenter);
		paint::text(p, r.adjusted(0, 0, -9, 0), QStringLiteral("▾"), col(theme::kTextDim), paint::font(11),
			    Qt::AlignRight | Qt::AlignVCenter);
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
	QString label_, value_;
};

// ---------------------------------------------------------------------------
// One insert slot. Filled: power dot (bypass), name, gain reduction; click opens
// the plugin, drag to reorder, right-click for more. Empty: click to insert.
class SlotWidget : public QWidget {
public:
	struct Callbacks {
		std::function<void(int)> select;
		std::function<void(int)> open;
		std::function<void(int)> toggleBypass;
		std::function<void(int, const QPoint &)> empty;   // click on an empty slot
		std::function<void(int, const QPoint &)> context; // right-click on a filled slot
		std::function<void(int, int)> move;               // drag from → to
	};

	SlotWidget(int index, const Callbacks &cb, QWidget *parent) : QWidget(parent), index_(index), cb_(cb)
	{
		setFixedHeight(36);
		setAcceptDrops(true);
		setCursor(Qt::PointingHandCursor);
		setMouseTracking(true);
	}

	void setPlugin(const bus::SlotInfo &info)
	{
		filled_ = true;
		info_ = info;
		setToolTip(QStringLiteral("%1 · %2 %3\nClick to open · dot = bypass · drag to reorder · right-click for more")
				   .arg(QString::fromStdString(info.name), QString::fromStdString(info.vendor),
					formatShort(info.format)));
		update();
	}
	void setEmpty()
	{
		filled_ = false;
		setToolTip(QStringLiteral("Click to insert a plugin here"));
		update();
	}
	void setSelected(bool selected)
	{
		selected_ = selected;
		update();
	}
	void setMeter(float reduction, float postDb)
	{
		if (std::fabs(reduction - reduction_) < 0.05f && std::fabs(postDb - postDb_) < 0.3f)
			return;
		reduction_ = reduction;
		postDb_ = postDb;
		update();
	}
	bool filled() const { return filled_; }

protected:
	QRectF dotRect() const { return QRectF(26, height() / 2.0 - 6, 12, 12); }

	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		const bool hot = underMouse() || dropHover_;
		uint32_t border = selected_ ? theme::kAccentBlue : hot ? theme::kTextFaint : theme::kBorder;
		p.setPen(QPen(col(border), selected_ || dropHover_ ? 1.5 : 1.0));
		p.setBrush(col(filled_ ? theme::kPanelRaised : theme::kBg));
		p.drawRoundedRect(r, 5, 5);

		paint::text(p, QRectF(r.left() + 6, r.top(), 16, r.height()), QString::number(index_ + 1),
			    col(theme::kTextFaint), paint::font(10, true), Qt::AlignCenter);

		if (!filled_) {
			paint::text(p, r.adjusted(26, 0, -8, 0), QStringLiteral("+  Insert"),
				    col(hot ? theme::kTextDim : theme::kTextFaint), paint::font(11), Qt::AlignLeft | Qt::AlignVCenter);
			return;
		}

		// power dot: green = processing, hollow = bypassed
		const QRectF dot = dotRect().adjusted(1.5, 1.5, -1.5, -1.5);
		if (info_.bypass) {
			p.setPen(QPen(col(theme::kTextFaint), 1.5));
			p.setBrush(Qt::NoBrush);
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(col(theme::kGood));
		}
		p.drawEllipse(dot);

		const bool showGr = !info_.bypass && reduction_ >= 0.5f;
		const qreal right = showGr ? 46 : 10;
		const QRectF textR(r.left() + 46, r.top() + 3, r.width() - 46 - right, r.height() - 6);
		paint::text(p, QRectF(textR.left(), textR.top(), textR.width(), textR.height() * 0.55),
			    QString::fromStdString(info_.name), col(info_.bypass ? theme::kTextDim : theme::kText),
			    paint::font(11.5, true), Qt::AlignLeft | Qt::AlignBottom);
		paint::text(p, QRectF(textR.left(), textR.top() + textR.height() * 0.55, textR.width(), textR.height() * 0.45),
			    info_.bypass ? QStringLiteral("bypassed") : QString::fromStdString(info_.vendor),
			    col(theme::kTextFaint), paint::font(9.5), Qt::AlignLeft | Qt::AlignTop);
		if (showGr)
			paint::text(p, QRectF(r.right() - 44, r.top(), 38, r.height()),
				    QStringLiteral("−%1").arg(reduction_, 0, 'f', 1), col(theme::kAccentAmber),
				    paint::font(10.5, true), Qt::AlignRight | Qt::AlignVCenter);

		// thin output level along the bottom edge
		if (!info_.bypass && postDb_ > -60.0f) {
			const qreal w = (r.width() - 52) * static_cast<qreal>(std::clamp((postDb_ + 60.0f) / 60.0f, 0.0f, 1.0f));
			QColor c = peakColor(postDb_);
			c.setAlphaF(0.75f);
			p.setPen(Qt::NoPen);
			p.setBrush(c);
			p.drawRoundedRect(QRectF(r.left() + 46, r.bottom() - 3, w, 2), 1, 1);
		}
	}

	void mousePressEvent(QMouseEvent *e) override
	{
		if (e->button() != Qt::LeftButton)
			return;
		pressPos_ = e->position().toPoint();
		pressed_ = true;
		if (filled_ && dotRect().adjusted(-4, -6, 4, 6).contains(e->position())) {
			pressed_ = false;
			if (cb_.toggleBypass)
				cb_.toggleBypass(index_);
			return;
		}
		if (cb_.select)
			cb_.select(index_);
	}

	void mouseMoveEvent(QMouseEvent *e) override
	{
		update();
		if (!pressed_ || !filled_ || !(e->buttons() & Qt::LeftButton))
			return;
		if ((e->position().toPoint() - pressPos_).manhattanLength() < QApplication::startDragDistance())
			return;
		pressed_ = false;
		auto *drag = new QDrag(this);
		auto *mime = new QMimeData();
		mime->setData(QString::fromLatin1(kSlotMime), QByteArray::number(index_));
		drag->setMimeData(mime);
		QPixmap pm(size());
		pm.fill(Qt::transparent);
		render(&pm);
		drag->setPixmap(pm);
		drag->setHotSpot(pressPos_);
		drag->exec(Qt::MoveAction);
	}

	void mouseReleaseEvent(QMouseEvent *e) override
	{
		if (e->button() != Qt::LeftButton || !pressed_)
			return;
		pressed_ = false;
		if (!rect().contains(e->position().toPoint()))
			return;
		const QPoint global = e->globalPosition().toPoint();
		const int index = index_;
		auto cb = cb_;
		// Deferred: the handler may rebuild the slot list (and delete this widget).
		QTimer::singleShot(0, parentWidget(), [cb, index, global, filled = filled_] {
			if (filled && cb.open)
				cb.open(index);
			else if (!filled && cb.empty)
				cb.empty(index, global);
		});
	}

	void contextMenuEvent(QContextMenuEvent *e) override
	{
		const QPoint global = e->globalPos();
		const int index = index_;
		auto cb = cb_;
		QTimer::singleShot(0, parentWidget(), [cb, index, global, filled = filled_] {
			if (filled && cb.context)
				cb.context(index, global);
			else if (!filled && cb.empty)
				cb.empty(index, global);
		});
	}

	void enterEvent(QEnterEvent *e) override
	{
		QWidget::enterEvent(e);
		update();
	}
	void leaveEvent(QEvent *e) override
	{
		QWidget::leaveEvent(e);
		update();
	}

	void dragEnterEvent(QDragEnterEvent *e) override
	{
		if (e->mimeData()->hasFormat(QString::fromLatin1(kSlotMime))) {
			e->acceptProposedAction();
			dropHover_ = true;
			update();
		}
	}
	void dragLeaveEvent(QDragLeaveEvent *) override
	{
		dropHover_ = false;
		update();
	}
	void dropEvent(QDropEvent *e) override
	{
		dropHover_ = false;
		update();
		if (!e->mimeData()->hasFormat(QString::fromLatin1(kSlotMime)))
			return;
		const int from = e->mimeData()->data(QString::fromLatin1(kSlotMime)).toInt();
		e->acceptProposedAction();
		const int to = index_;
		auto cb = cb_;
		QTimer::singleShot(0, parentWidget(), [cb, from, to] {
			if (cb.move)
				cb.move(from, to);
		});
	}

private:
	int index_;
	Callbacks cb_;
	bool filled_ = false;
	bool selected_ = false;
	bool pressed_ = false;
	bool dropHover_ = false;
	QPoint pressPos_;
	bus::SlotInfo info_;
	float reduction_ = 0.0f;
	float postDb_ = -120.0f;
};

// ---------------------------------------------------------------------------
// Rotary dB control: drag up / down or scroll; double-click for 0 dB.
class DbKnob : public QWidget {
public:
	DbKnob(const QString &label, float minDb, float maxDb, QWidget *parent)
		: QWidget(parent),
		  label_(label),
		  min_(minDb),
		  max_(maxDb)
	{
		setFixedHeight(46);
		setCursor(Qt::SizeVerCursor);
		setToolTip(QStringLiteral("Drag up / down or scroll · double-click for 0 dB"));
	}

	std::function<void(float)> onChange;

	void setValue(float db)
	{
		value_ = std::clamp(db, min_, max_);
		update();
	}
	float value() const { return value_; }
	bool dragging() const { return dragging_; }

protected:
	void change(float db)
	{
		const float v = std::clamp(std::round(db * 10.0f) / 10.0f, min_, max_);
		if (std::fabs(v - value_) < 1e-4f)
			return;
		value_ = v;
		update();
		if (onChange)
			onChange(value_);
	}

	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect());
		const qreal d = 36;
		const QRectF knob(r.left() + 4, r.center().y() - d / 2, d, d);
		const QRectF arc = knob.adjusted(3, 3, -3, -3);
		// track 225° → −45° (Qt angles are counter-clockwise, in 1/16 degree)
		p.setPen(QPen(col(theme::kGrid), 3, Qt::SolidLine, Qt::RoundCap));
		p.drawArc(arc, 225 * 16, -270 * 16);
		const float t = (value_ - min_) / (max_ - min_);
		const float t0 = (0.0f - min_) / (max_ - min_);
		p.setPen(QPen(col(theme::kAccentSlate), 3, Qt::SolidLine, Qt::RoundCap));
		const int a0 = static_cast<int>((225.0f - 270.0f * t0) * 16.0f);
		const int a1 = static_cast<int>((225.0f - 270.0f * t) * 16.0f);
		if (a1 != a0)
			p.drawArc(arc, a0, a1 - a0);
		p.setPen(QPen(col(theme::kBorder), 1));
		p.setBrush(col(theme::kPanelRaised));
		p.drawEllipse(knob.adjusted(7, 7, -7, -7));
		const qreal ang = (225.0 - 270.0 * t) * M_PI / 180.0;
		const QPointF c = knob.center();
		p.setPen(QPen(col(theme::kText), 1.6, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(c + QPointF(std::cos(ang), -std::sin(ang)) * 4.0, c + QPointF(std::cos(ang), -std::sin(ang)) * 10.0);

		const QRectF tr(knob.right() + 10, r.top(), r.width() - knob.width() - 14, r.height());
		paint::text(p, QRectF(tr.left(), tr.top() + 6, tr.width(), 16), label_, col(theme::kTextDim), paint::font(10.5));
		paint::text(p, QRectF(tr.left(), tr.top() + 22, tr.width(), 18),
			    QStringLiteral("%1%2 dB").arg(value_ > 0.05f ? QStringLiteral("+") : QString()).arg(paint::db(value_)),
			    col(std::fabs(value_) < 0.05f ? theme::kTextDim : theme::kText), paint::font(12.5, true));
	}

	void mousePressEvent(QMouseEvent *e) override
	{
		dragging_ = true;
		dragY_ = e->position().y();
		dragStart_ = value_;
	}
	void mouseReleaseEvent(QMouseEvent *) override { dragging_ = false; }
	void mouseMoveEvent(QMouseEvent *e) override
	{
		if (!(e->buttons() & Qt::LeftButton))
			return;
		const float perPx = (e->modifiers() & Qt::ShiftModifier) ? 0.02f : 0.1f;
		change(dragStart_ + static_cast<float>(dragY_ - e->position().y()) * perPx);
	}
	void mouseDoubleClickEvent(QMouseEvent *) override { change(0.0f); }
	void wheelEvent(QWheelEvent *e) override
	{
		const float steps = static_cast<float>(e->angleDelta().y()) / 120.0f;
		change(value_ + steps * ((e->modifiers() & Qt::ShiftModifier) ? 0.1f : 0.5f));
		e->accept();
	}

private:
	QString label_;
	float min_, max_;
	float value_ = 0.0f;
	bool dragging_ = false;
	qreal dragY_ = 0;
	float dragStart_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Output section: fader, stereo output meters with peak hold, and the chain's
// total gain reduction, on one shared scale.
class FaderMeter : public QWidget {
public:
	explicit FaderMeter(QWidget *parent) : QWidget(parent)
	{
		setMinimumHeight(240);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setToolTip(QStringLiteral("Output fader: drag or scroll · double-click for 0 dB\nClick the meters to reset the peak hold"));
	}

	std::function<void(float)> onChange;
	bool dragging() const { return dragging_; }

	void setFader(float db)
	{
		fader_ = std::clamp(db, -60.0f, 12.0f);
		update();
	}
	void setLevels(float l, float r, float reduction)
	{
		level_[0] = l;
		level_[1] = r;
		reduction_ = reduction;
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		for (int c = 0; c < 2; ++c)
			if (level_[c] >= hold_[c] || now - holdAt_[c] > 2000) {
				hold_[c] = level_[c];
				holdAt_[c] = now;
			}
		update();
	}

protected:
	struct Geometry {
		QRectF area, scale, track, meters[2], gr, readout;
	};

	Geometry geometry() const
	{
		Geometry g;
		const QRectF r = QRectF(rect());
		g.readout = QRectF(r.left(), r.bottom() - 34, r.width(), 34);
		g.area = QRectF(r.left(), r.top() + 18, r.width(), r.height() - 18 - 40);
		const qreal x = r.left() + (r.width() - (26 + 34 + 10 + 9 + 3 + 9 + 10 + 9)) / 2.0;
		g.scale = QRectF(x, g.area.top(), 26, g.area.height());
		g.track = QRectF(g.scale.right(), g.area.top(), 34, g.area.height());
		g.meters[0] = QRectF(g.track.right() + 10, g.area.top(), 9, g.area.height());
		g.meters[1] = QRectF(g.meters[0].right() + 3, g.area.top(), 9, g.area.height());
		g.gr = QRectF(g.meters[1].right() + 10, g.area.top(), 9, g.area.height() * 0.45);
		return g;
	}

	qreal yFor(const Geometry &g, float db) const
	{
		return g.area.bottom() - static_cast<qreal>(scalePos(db)) * g.area.height();
	}

	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const Geometry g = geometry();

		// headers
		paint::text(p, QRectF(g.track.left() - 6, 0, g.track.width() + 12, 14), QStringLiteral("OUT"),
			    col(theme::kTextDim), paint::font(9.5, true), Qt::AlignCenter);
		paint::text(p, QRectF(g.meters[0].left() - 6, 0, 30, 14), QStringLiteral("L R"), col(theme::kTextDim),
			    paint::font(9.5, true), Qt::AlignCenter);
		paint::text(p, QRectF(g.gr.left() - 8, 0, g.gr.width() + 16, 14), QStringLiteral("GR"),
			    col(theme::kTextDim), paint::font(9.5, true), Qt::AlignCenter);

		// scale + grid
		for (float db : {12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f}) {
			const qreal y = yFor(g, db);
			const bool unity = db == 0.0f;
			paint::text(p, QRectF(g.scale.left(), y - 6, g.scale.width() - 5, 12),
				    db > 0 ? QStringLiteral("+%1").arg(static_cast<int>(db)) : QString::number(static_cast<int>(db)),
				    col(unity ? theme::kTextDim : theme::kTextFaint), paint::font(9, unity),
				    Qt::AlignRight | Qt::AlignVCenter);
			p.setPen(QPen(col(unity ? theme::kTextFaint : theme::kGrid), 1));
			p.drawLine(QPointF(g.track.left() + 4, y), QPointF(g.track.left() + 9, y));
			p.drawLine(QPointF(g.track.right() - 9, y), QPointF(g.track.right() - 4, y));
		}

		// fader slot and cap
		const QRectF slot(g.track.center().x() - 2, g.area.top(), 4, g.area.height());
		p.setPen(Qt::NoPen);
		p.setBrush(col(theme::kBg));
		p.drawRoundedRect(slot, 2, 2);
		const qreal fy = yFor(g, fader_);
		p.setBrush(col(theme::kAccentBlue));
		p.drawRoundedRect(QRectF(slot.left(), fy, slot.width(), slot.bottom() - fy), 2, 2);
		const QRectF cap(g.track.left() + 3, fy - 8, g.track.width() - 6, 16);
		p.setPen(QPen(col(theme::kTextFaint), 1));
		p.setBrush(col(theme::kPanelRaised));
		p.drawRoundedRect(cap, 3, 3);
		p.setPen(QPen(col(theme::kText), 1.5));
		p.drawLine(QPointF(cap.left() + 4, fy), QPointF(cap.right() - 4, fy));

		// output meters
		for (int c = 0; c < 2; ++c) {
			const QRectF &bar = g.meters[c];
			p.setPen(Qt::NoPen);
			p.setBrush(col(theme::kBg));
			p.drawRoundedRect(bar, 2, 2);
			p.setOpacity(0.12);
			p.setBrush(peakGradient(bar));
			p.drawRoundedRect(bar, 2, 2);
			p.setOpacity(1.0);
			if (level_[c] > -60.0f) {
				const qreal y = yFor(g, level_[c]);
				p.setBrush(peakGradient(bar));
				p.drawRoundedRect(QRectF(bar.left(), y, bar.width(), bar.bottom() - y), 2, 2);
			}
			if (hold_[c] > -60.0f) {
				p.setBrush(peakColor(hold_[c]));
				p.drawRect(QRectF(bar.left(), yFor(g, hold_[c]) - 1, bar.width(), 2));
			}
			if (hold_[c] >= -0.1f) { // clip light
				p.setBrush(col(theme::kBad));
				p.drawRoundedRect(QRectF(bar.left(), bar.top() - 7, bar.width(), 4), 1, 1);
			}
		}

		// gain reduction, hanging from the top (0 … 24 dB)
		p.setPen(Qt::NoPen);
		p.setBrush(col(theme::kBg));
		p.drawRoundedRect(g.gr, 2, 2);
		if (reduction_ >= 0.1f) {
			const qreal h = g.gr.height() * std::clamp(static_cast<qreal>(reduction_) / 24.0, 0.0, 1.0);
			p.setBrush(col(theme::kAccentAmber));
			p.drawRoundedRect(QRectF(g.gr.left(), g.gr.top(), g.gr.width(), h), 2, 2);
		}
		for (int db : {6, 12, 18}) {
			const qreal y = g.gr.top() + g.gr.height() * db / 24.0;
			paint::text(p, QRectF(g.gr.right() + 2, y - 6, 18, 12), QString::number(db), col(theme::kTextFaint),
				    paint::font(8.5), Qt::AlignLeft | Qt::AlignVCenter);
		}

		// readouts
		const QRectF ro = g.readout;
		const qreal third = ro.width() / 3.0;
		auto readout = [&](int i, const QString &label, const QString &value, const QColor &c) {
			const QRectF cell(ro.left() + third * i, ro.top(), third, ro.height());
			paint::text(p, QRectF(cell.left(), cell.top(), cell.width(), 14), label, col(theme::kTextFaint),
				    paint::font(9.5), Qt::AlignCenter);
			paint::text(p, QRectF(cell.left(), cell.top() + 14, cell.width(), 18), value, c, paint::font(12, true),
				    Qt::AlignCenter);
		};
		readout(0, QStringLiteral("Fader"),
			fader_ <= -59.9f ? QStringLiteral("−∞")
					 : QStringLiteral("%1%2").arg(fader_ > 0.05f ? QStringLiteral("+") : QString()).arg(paint::db(fader_)),
			col(theme::kText));
		const float peak = std::max(hold_[0], hold_[1]);
		readout(1, QStringLiteral("Peak"), peak > -99.0f ? paint::db(peak) : QStringLiteral("−∞"),
			peak > -60.0f ? peakColor(peak) : col(theme::kTextFaint));
		readout(2, QStringLiteral("GR"), reduction_ >= 0.1f ? QStringLiteral("−%1").arg(reduction_, 0, 'f', 1) : QStringLiteral("0"),
			reduction_ >= 0.1f ? col(theme::kAccentAmber) : col(theme::kTextFaint));
	}

	void mousePressEvent(QMouseEvent *e) override
	{
		const Geometry g = geometry();
		if (g.meters[0].united(g.meters[1]).adjusted(-3, -8, 3, 0).contains(e->position())) {
			hold_[0] = hold_[1] = -120.0f;
			update();
			return;
		}
		dragging_ = g.track.adjusted(-6, -10, 6, 10).contains(e->position());
		dragY_ = e->position().y();
		dragStart_ = scalePos(fader_);
	}
	void mouseMoveEvent(QMouseEvent *e) override
	{
		if (!dragging_ || !(e->buttons() & Qt::LeftButton))
			return;
		const Geometry g = geometry();
		const float fine = (e->modifiers() & Qt::ShiftModifier) ? 0.2f : 1.0f;
		const float pos = dragStart_ + static_cast<float>((dragY_ - e->position().y()) / g.area.height()) * fine;
		change(scaleDb(pos));
	}
	void mouseReleaseEvent(QMouseEvent *) override { dragging_ = false; }
	void mouseDoubleClickEvent(QMouseEvent *e) override
	{
		if (geometry().track.adjusted(-6, -10, 6, 10).contains(e->position()))
			change(0.0f);
	}
	void wheelEvent(QWheelEvent *e) override
	{
		const float steps = static_cast<float>(e->angleDelta().y()) / 120.0f;
		change(fader_ + steps * ((e->modifiers() & Qt::ShiftModifier) ? 0.1f : 0.5f));
		e->accept();
	}

private:
	void change(float db)
	{
		const float v = std::clamp(std::round(db * 10.0f) / 10.0f, -60.0f, 12.0f);
		if (std::fabs(v - fader_) < 1e-4f)
			return;
		fader_ = v;
		update();
		if (onChange)
			onChange(fader_);
	}

	float fader_ = 0.0f;
	float level_[2] = {-120.0f, -120.0f};
	float hold_[2] = {-120.0f, -120.0f};
	qint64 holdAt_[2] = {0, 0};
	float reduction_ = 0.0f;
	bool dragging_ = false;
	qreal dragY_ = 0;
	float dragStart_ = 0.0f;
};

// ---------------------------------------------------------------------------

namespace {

// Plugin picker menu: one submenu per vendor.
void fillPluginMenu(QMenu *menu, QObject *ctx, const std::function<void(const std::string &)> &onPick)
{
	const auto plugins = bus::Engine::get().plugins();
	if (plugins.empty()) {
		menu->addAction(QStringLiteral("No plugins yet. Scan for plugins first."))->setEnabled(false);
		return;
	}
	std::map<QString, QMenu *> vendors;
	for (const auto &p : plugins) {
		const QString vendor = p.vendor.empty() ? QStringLiteral("Other") : QString::fromStdString(p.vendor);
		QMenu *&sub = vendors[vendor];
		if (!sub)
			sub = menu->addMenu(vendor);
		const std::string id = p.id;
		QObject::connect(sub->addAction(QStringLiteral("%1   (%2)").arg(QString::fromStdString(p.name), formatShort(p.format))),
				 &QAction::triggered, ctx, [ctx, onPick, id] { QTimer::singleShot(0, ctx, [onPick, id] { onPick(id); }); });
	}
}

// Panel with a LiveVoice section header, used for the strip and the browser.
class PanelBox : public QWidget {
public:
	PanelBox(const QString &title, uint32_t accent, QWidget *parent) : QWidget(parent), title_(title), accent_(accent)
	{
		layout_ = new QVBoxLayout(this);
		layout_->setContentsMargins(10, 34, 10, 10);
		layout_->setSpacing(8);
	}
	QVBoxLayout *body() const { return layout_; }

	// Small buttons on the right of the title row.
	void addHeaderWidget(QWidget *w)
	{
		if (!header_) {
			header_ = new QHBoxLayout();
			header_->setContentsMargins(0, 0, 0, 0);
			header_->setSpacing(2);
			header_->addStretch(1);
			layout_->insertLayout(0, header_);
			layout_->setContentsMargins(10, 6, 6, 10);
		}
		header_->addWidget(w);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		paint::panel(p, QRectF(rect()));
		paint::sectionHeader(p, QRectF(rect()), accent_, title_);
	}

private:
	QString title_;
	uint32_t accent_;
	QVBoxLayout *layout_;
	QHBoxLayout *header_ = nullptr;
};

QLabel *dimLabel(const QString &text, QWidget *parent)
{
	auto *l = new QLabel(text, parent);
	l->setProperty("dim", true);
	l->setWordWrap(true);
	return l;
}

QWidget *divider(const QString &text, QWidget *parent)
{
	auto *l = new QLabel(text, parent);
	l->setStyleSheet(QStringLiteral("color: #4C5561; font-size: 10px; font-weight: 600; padding-top: 4px;"));
	return l;
}

} // namespace

MasterBusPanel::MasterBusPanel(bool embedded, QWidget *parent) : QWidget(parent)
{
	// =============== channel strip ===============
	auto *strip = new PanelBox(QStringLiteral("Master Bus"), theme::kAccentBlue, this);
	strip->setFixedWidth(272);
	QVBoxLayout *sl = strip->body();
	QPointer<MasterBusPanel> guard(this);
	// Header buttons call back after the click has finished (the owner may hide or delete us).
	auto deferred = [guard](std::function<void()> MasterBusPanel::*member) {
		return [guard, member] {
			QTimer::singleShot(0, guard, [guard, member] {
				if (guard && guard->*member)
					(guard->*member)();
			});
		};
	};
	if (embedded) {
		auto *pop = new IconButton(IconButton::Icon::PopOut,
					   QStringLiteral("Open in its own window (with the plugin browser)"), strip);
		connect(pop, &QAbstractButton::clicked, this, deferred(&MasterBusPanel::onPopOut));
		strip->addHeaderWidget(pop);
		auto *close = new IconButton(IconButton::Icon::Close, QStringLiteral("Hide the master bus"), strip);
		connect(close, &QAbstractButton::clicked, this, deferred(&MasterBusPanel::onHide));
		strip->addHeaderWidget(close);
	} else {
		auto *dock = new IconButton(IconButton::Icon::DockIn, QStringLiteral("Show in the viewer, next to the side bar"),
					    strip);
		connect(dock, &QAbstractButton::clicked, this, deferred(&MasterBusPanel::onDockIn));
		strip->addHeaderWidget(dock);
	}

	power_ = new PowerButton(strip);
	status_ = new QLabel(strip);
	status_->setProperty("dim", true);
	status_->setWordWrap(true);
	auto *powerRow = new QHBoxLayout();
	powerRow->setSpacing(10);
	powerRow->addWidget(power_, 0, Qt::AlignTop);
	powerRow->addWidget(status_, 1);
	sl->addLayout(powerRow);

	inputs_ = new StripButton(strip);
	inputs_->setToolTip(QStringLiteral("Routing: which sources go through the bus and where it goes, as a wire diagram"));
	sl->addWidget(inputs_);

	// Shown when the bus output isn't on the track the stream sends.
	trackWarn_ = new QFrame(strip);
	trackWarn_->setObjectName(QStringLiteral("trackWarn"));
	trackWarn_->setStyleSheet(QStringLiteral(
		"QFrame#trackWarn { background: rgba(239, 76, 76, 0.12); border: 1px solid #EF4C4C; border-radius: 6px; }"
		"QFrame#trackWarn QLabel { color: #D8DEE6; background: transparent; border: none; }"));
	trackWarnText_ = new QLabel(trackWarn_);
	trackWarnText_->setWordWrap(true);
	auto *fix = new QPushButton(QStringLiteral("Fix"), trackWarn_);
	fix->setToolTip(QStringLiteral("Send the bus output to the stream's track too"));
	auto *wl = new QHBoxLayout(trackWarn_);
	wl->setContentsMargins(8, 6, 6, 6);
	wl->setSpacing(6);
	wl->addWidget(trackWarnText_, 1);
	wl->addWidget(fix);
	trackWarn_->hide();
	sl->addWidget(trackWarn_);
	connect(fix, &QPushButton::clicked, this, [this] {
		MasterBus::get().matchStreamTrack();
		refreshAll();
	});

	sl->addWidget(divider(QStringLiteral("INSERTS"), strip));
	slotsBox_ = new QWidget(strip);
	slotsLayout_ = new QVBoxLayout(slotsBox_);
	slotsLayout_->setContentsMargins(0, 0, 0, 0);
	slotsLayout_->setSpacing(4);
	auto *slotScroll = new QScrollArea(strip);
	slotScroll->setWidgetResizable(true);
	slotScroll->setFrameShape(QFrame::NoFrame);
	slotScroll->setWidget(slotsBox_);
	slotScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	slotScroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
	slotScroll->setMinimumHeight(4 * 40);
	slotScroll->setMaximumHeight(kMinSlots * 40 + 4);
	sl->addWidget(slotScroll);

	trim_ = new DbKnob(QStringLiteral("Input trim"), -24.0f, 24.0f, strip);
	sl->addWidget(trim_);

	fader_ = new FaderMeter(strip);
	sl->addWidget(fader_, 1);

	listen_ = new QPushButton(QStringLiteral("Listen to Stream"), strip);
	listen_->setObjectName(QStringLiteral("listen"));
	listen_->setCheckable(true);
	listen_->setStyleSheet(QStringLiteral(
		"QPushButton#listen:checked { background-color: #4C8DFF; border-color: #4C8DFF; color: #0D1015; }"));
	sl->addWidget(listen_);
	connect(listen_, &QPushButton::clicked, this, [this](bool on) {
		StreamMonitor::get().setListening(on);
		refreshStatus();
	});

	footer_ = new QLabel(strip);
	footer_->setProperty("dim", true);
	footer_->setWordWrap(true);
	footer_->setAlignment(Qt::AlignCenter);
	sl->addWidget(footer_);

	// =============== plugin browser ===============
	auto *browser = new PanelBox(QStringLiteral("Plugins"), theme::kAccentTeal, this);
	QVBoxLayout *bl = browser->body();
	search_ = new QLineEdit(browser);
	search_->setPlaceholderText(QStringLiteral("Search plugins or makers"));
	search_->setClearButtonEnabled(true);
	tree_ = new QTreeWidget(browser);
	tree_->setColumnCount(2);
	tree_->setHeaderLabels({QStringLiteral("Plugin"), QStringLiteral("Format")});
	tree_->header()->setStretchLastSection(false);
	tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	tree_->setRootIsDecorated(true);
	tree_->setUniformRowHeights(true);
	insertButton_ = new QPushButton(QStringLiteral("Insert"), browser);
	insertButton_->setProperty("accent", true);
	insertButton_->setToolTip(QStringLiteral("Insert the chosen plugin after the selected slot (or at the end). "
						 "Double-clicking a plugin does the same."));
	scanButton_ = new QPushButton(QStringLiteral("Scan"), browser);
	scanButton_->setToolTip(QStringLiteral("Look for new VST3 and Audio Unit plugins. Each plugin is checked in a "
					       "separate helper process, so a plugin that crashes can't take OBS down."));
	rescanButton_ = new QPushButton(QStringLiteral("Rescan all"), browser);
	rescanButton_->setToolTip(QStringLiteral("Check every plugin again, including ones that failed before"));
	auto *browserButtons = new QHBoxLayout();
	browserButtons->addWidget(insertButton_);
	browserButtons->addStretch(1);
	browserButtons->addWidget(scanButton_);
	browserButtons->addWidget(rescanButton_);
	scanStatus_ = dimLabel(QString(), browser);
	failed_ = dimLabel(QString(), browser);
	failed_->setStyleSheet(QStringLiteral("color: #F2C94C;"));
	bl->addWidget(search_);
	bl->addWidget(tree_, 1);
	bl->addLayout(browserButtons);
	bl->addWidget(scanStatus_);
	bl->addWidget(failed_);

	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(embedded ? 0 : 12, embedded ? 0 : 12, embedded ? 0 : 12, embedded ? 0 : 12);
	layout->setSpacing(10);
	layout->addWidget(strip);
	layout->addWidget(browser, 1);
	// In the viewer only the strip shows; plugins go in from an empty slot's menu.
	browser->setVisible(!embedded);
	if (embedded)
		setFixedWidth(272);

	// =============== behaviour ===============
	connect(power_, &QAbstractButton::toggled, this, [this](bool on) {
		if (updating_)
			return;
		std::string error;
		if (!MasterBus::get().setEnabled(on, error))
			QMessageBox::warning(this, QStringLiteral("Master bus"), QString::fromStdString(error));
		refreshAll();
	});
	connect(inputs_, &QAbstractButton::clicked, this, [this] { QTimer::singleShot(0, this, [this] { showInputs(); }); });
	trim_->onChange = [](float db) {
		bus::Engine::get().setMasterGains(db, bus::Engine::get().masterOutDb());
		MasterBus::get().saveSoon();
	};
	fader_->onChange = [](float db) {
		bus::Engine::get().setMasterGains(bus::Engine::get().masterInDb(), db);
		MasterBus::get().saveSoon();
	};
	connect(search_, &QLineEdit::textChanged, this, [this] { refreshBrowser(); });
	connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
		if (item && !item->data(0, Qt::UserRole).toString().isEmpty())
			QTimer::singleShot(0, this, [this] { insertFromBrowser(); });
	});
	connect(insertButton_, &QPushButton::clicked, this, [this] { insertFromBrowser(); });
	connect(scanButton_, &QPushButton::clicked, this, [this] { startScan(false); });
	connect(rescanButton_, &QPushButton::clicked, this, [this] { startScan(true); });

	meterTimer_ = new QTimer(this);
	meterTimer_->setInterval(40);
	connect(meterTimer_, &QTimer::timeout, this, [this] {
		bus::Engine &engine = bus::Engine::get();
		const int count = engine.slotCount();
		for (size_t i = 0; i < slots_.size() && static_cast<int>(i) < count; ++i) {
			if (!slots_[i]->filled())
				continue;
			const bus::Levels l = engine.slotLevels(static_cast<int>(i));
			slots_[i]->setMeter(l.reduction, std::max(l.post[0], l.post[1]));
		}
		const float total = engine.totalReduction();
		const bus::Levels m = engine.masterLevels();
		fader_->setLevels(m.post[0], m.post[1], total);

		// The stream's track (a profile setting) is re-read twice a second.
		if (++ticks_ % 12 == 0)
			streamTrack_ = MasterBus::streamTrack();

		// Another master bus view (window or viewer) changed something: catch up.
		if (signature() != shownSignature_)
			refreshAll();
		else if (!fader_->dragging())
			fader_->setFader(engine.masterOutDb());
		if (!trim_->dragging())
			trim_->setValue(engine.masterInDb());
	});
	// Started in showEvent, stopped in hideEvent.

	scanTimer_ = new QTimer(this);
	scanTimer_->setInterval(30);
	connect(scanTimer_, &QTimer::timeout, this, &MasterBusPanel::scanTick);

	refreshAll();

	const std::string warning = bus::Engine::get().takeStartupWarning();
	if (!warning.empty())
		QTimer::singleShot(0, this, [this, warning] {
			QMessageBox::warning(this, QStringLiteral("Master bus"), QString::fromStdString(warning));
		});
	if (bus::Engine::get().scanning()) {
		// A scan started earlier is still going; keep it moving.
		scanButton_->setEnabled(false);
		rescanButton_->setEnabled(false);
		scanTimer_->start();
	} else if (bus::Engine::get().plugins().empty()) {
		scanStatus_->setText(QStringLiteral("No plugins found yet. Click “Scan” to find your VST3 and Audio Unit plugins."));
	}
}

std::string MasterBusPanel::signature()
{
	// Cheap: a counter the engine bumps on every chain / plugin-list change, plus
	// the routing settings. Checked on every meter tick.
	const MasterBus &bus = MasterBus::get();
	return std::to_string(bus::Engine::get().revision()) + "|" + std::to_string(bus.enabled()) + "|" +
	       std::to_string(bus.running()) + "|" + std::to_string(bus.routedCount()) + "|" +
	       std::to_string(bus.config().sendTrack) + "|" + std::to_string(bus.config().outputMask) + "|" +
	       std::to_string(streamTrack_) + "|" + std::to_string(StreamMonitor::get().listening());
}

void MasterBusPanel::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	streamTrack_ = MasterBus::streamTrack();
	refreshAll();
	meterTimer_->start();
}

void MasterBusPanel::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	meterTimer_->stop(); // a hidden strip costs nothing
}

void MasterBusPanel::refreshAll()
{
	updating_ = true;
	power_->setChecked(MasterBus::get().enabled());
	trim_->setValue(bus::Engine::get().masterInDb());
	fader_->setFader(bus::Engine::get().masterOutDb());
	updating_ = false;
	refreshSlots();
	refreshStatus();
	refreshBrowser();
}

void MasterBusPanel::refreshSlots()
{
	for (SlotWidget *w : slots_)
		w->deleteLater();
	slots_.clear();
	QLayoutItem *child;
	while ((child = slotsLayout_->takeAt(0)) != nullptr)
		delete child;

	SlotWidget::Callbacks cb;
	QPointer<MasterBusPanel> self(this);
	cb.select = [self](int i) {
		if (self)
			self->selectSlot(i);
	};
	cb.open = [](int i) { bus::Engine::get().openEditor(i); };
	cb.toggleBypass = [self](int i) {
		bus::Engine &engine = bus::Engine::get();
		engine.setBypass(i, !engine.slot(i).bypass);
		MasterBus::get().saveSoon();
		if (self)
			self->refreshSlots();
	};
	cb.empty = [self](int i, const QPoint &pos) {
		if (self)
			self->showInsertMenu(i, pos);
	};
	cb.context = [self](int i, const QPoint &pos) {
		if (self)
			self->showSlotMenu(i, pos);
	};
	cb.move = [self](int from, int to) {
		bus::Engine &engine = bus::Engine::get();
		const int count = engine.slotCount();
		to = std::min(to, count - 1);
		if (from == to || from < 0 || from >= count || to < 0)
			return;
		engine.moveSlot(from, to);
		MasterBus::get().chainChanged();
		if (self) {
			self->selected_ = to;
			self->refreshSlots();
		}
	};

	const int count = bus::Engine::get().slotCount();
	const int shown = std::max(kMinSlots, count + 1);
	for (int i = 0; i < shown; ++i) {
		auto *w = new SlotWidget(i, cb, slotsBox_);
		if (i < count)
			w->setPlugin(bus::Engine::get().slot(i));
		else
			w->setEmpty();
		w->setSelected(i == selected_);
		slotsLayout_->addWidget(w);
		slots_.push_back(w);
	}
	slotsLayout_->addStretch(1);
	refreshStatus();
	shownSignature_ = signature();
}

void MasterBusPanel::selectSlot(int index)
{
	selected_ = index;
	for (size_t i = 0; i < slots_.size(); ++i)
		slots_[i]->setSelected(static_cast<int>(i) == index);
}

void MasterBusPanel::refreshStatus()
{
	const MasterBus &bus = MasterBus::get();
	const int routed = bus.routedCount();
	const int plugins = bus::Engine::get().slotCount();
	if (!bus.enabled())
		status_->setText(QStringLiteral("Off: audio goes out exactly as OBS mixes it."));
	else if (!bus.running())
		status_->setText(QStringLiteral("On, but the bus couldn't start (see the OBS log)."));
	else
		status_->setText(QStringLiteral("Running · %1").arg(plugins == 1 ? QStringLiteral("1 plugin")
										  : QStringLiteral("%1 plugins").arg(plugins)));

	inputs_->setTexts(QStringLiteral("Routing"), routed == 1 ? QStringLiteral("1 source") : QStringLiteral("%1 sources").arg(routed));

	const auto &cfg = bus.config();
	const bool missing = bus.running() && routed > 0 &&
			     !busReachesTrack(cfg.sendTrack, cfg.outputMask, streamTrack_);
	trackWarn_->setVisible(missing);
	if (missing)
		trackWarnText_->setText(QStringLiteral("The stream sends Track %1, but the bus doesn't output there. "
						       "Routed sources are missing from the stream.")
						.arg(streamTrack_));

	const StreamMonitor &mon = StreamMonitor::get();
	listen_->setChecked(mon.listening());
	listen_->setToolTip(
		mon.listening()
			? QStringLiteral("Playing the stream's Track %1 (what viewers hear) through OBS's monitoring device. "
					 "Click to stop.")
				  .arg(mon.track())
			: QStringLiteral("Hear exactly what the stream sends, plugins included, through OBS's monitoring "
					 "device (Settings ▸ Audio ▸ Advanced). Slightly behind live: for checking the mix."));

	QString outs;
	for (int t = 0; t < 6; ++t)
		if (cfg.outputMask & (1u << t))
			outs += (outs.isEmpty() ? QString() : QStringLiteral(", ")) + QString::number(t + 1);
	// The bus is one OBS mix block behind, plus whatever the plugins add.
	const int lat = bus::Engine::get().latencySamples();
	const double sr = std::max(1.0, bus::Engine::get().sampleRate());
	const double blockMs = 1000.0 * AUDIO_OUTPUT_FRAMES / sr;
	const double ms = 1000.0 * lat / sr;
	footer_->setText(QStringLiteral("Track %1 in → track %2 out\n%3")
				 .arg(cfg.sendTrack)
				 .arg(outs.isEmpty() ? QStringLiteral("none") : outs)
				 .arg(lat > 0 ? QStringLiteral("Bus delay %1 ms (plugins %2 ms)").arg(blockMs + ms, 0, 'f', 0).arg(ms, 0, 'f', 1)
					      : QStringLiteral("Bus delay %1 ms").arg(blockMs, 0, 'f', 0)));
	footer_->setToolTip(QStringLiteral("Audio through the bus reaches the stream this much later than the picture "
					   "and than sources that skip the bus. Under about 40 ms is hard to notice; for more, "
					   "delay the picture or the other sources with OBS's Sync Offset."));
}

void MasterBusPanel::refreshBrowser()
{
	const QString filter = search_->text().trimmed();
	QString keep;
	if (QTreeWidgetItem *cur = tree_->currentItem())
		keep = cur->data(0, Qt::UserRole).toString();
	tree_->clear();
	std::map<QString, QTreeWidgetItem *> vendors;
	int shown = 0;
	for (const auto &p : bus::Engine::get().plugins()) {
		const QString name = QString::fromStdString(p.name);
		const QString vendor = p.vendor.empty() ? QStringLiteral("Other") : QString::fromStdString(p.vendor);
		if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive) && !vendor.contains(filter, Qt::CaseInsensitive))
			continue;
		QTreeWidgetItem *&v = vendors[vendor];
		if (!v) {
			v = new QTreeWidgetItem(tree_, QStringList{vendor});
			v->setFlags(Qt::ItemIsEnabled);
			QFont f = v->font(0);
			f.setBold(true);
			v->setFont(0, f);
		}
		auto *item = new QTreeWidgetItem(v, QStringList{name, formatShort(p.format)});
		item->setData(0, Qt::UserRole, QString::fromStdString(p.id));
		item->setForeground(1, col(theme::kTextDim));
		if (item->data(0, Qt::UserRole).toString() == keep)
			tree_->setCurrentItem(item);
		++shown;
	}
	if (!filter.isEmpty() || vendors.size() <= 3)
		tree_->expandAll();
	insertButton_->setEnabled(shown > 0);

	const auto failed = bus::Engine::get().failedPlugins();
	if (failed.empty()) {
		failed_->clear();
		failed_->setToolTip(QString());
	} else {
		QStringList names;
		for (const auto &n : failed)
			names << QString::fromStdString(n);
		failed_->setText(failed.size() == 1
					 ? QStringLiteral("1 plugin failed its check and is hidden (hover for its name).")
					 : QStringLiteral("%1 plugins failed their check and are hidden (hover for names).").arg(failed.size()));
		failed_->setToolTip(names.join(QStringLiteral("\n")));
	}
}

void MasterBusPanel::insertFromBrowser()
{
	QTreeWidgetItem *item = tree_->currentItem();
	if (!item || item->data(0, Qt::UserRole).toString().isEmpty()) {
		scanStatus_->setText(QStringLiteral("Pick a plugin in the list first."));
		return;
	}
	const int count = bus::Engine::get().slotCount();
	// After the selected plugin, or into the selected empty slot (= the end).
	const int at = selected_ >= 0 && selected_ < count ? selected_ + 1 : count;
	insertPlugin(item->data(0, Qt::UserRole).toString().toStdString(), at);
}

void MasterBusPanel::insertPlugin(const std::string &id, int index)
{
	scanStatus_->setText(QStringLiteral("Loading plugin…"));
	const int count = bus::Engine::get().slotCount();
	index = std::clamp(index, 0, count);
	QPointer<MasterBusPanel> self(this);
	bus::Engine::get().addPlugin(id, index, [self](bool ok, const std::string &error, int at) {
		if (!ok) {
			if (self) {
				self->scanStatus_->clear();
				QMessageBox::warning(self, QStringLiteral("Insert plugin"),
						     QStringLiteral("The plugin couldn't be loaded.\n\n%1")
							     .arg(QString::fromStdString(error)));
			}
			return;
		}
		MasterBus::get().chainChanged();
		if (self) {
			self->scanStatus_->clear();
			self->selected_ = at;
			self->refreshSlots();
		}
		bus::Engine::get().openEditor(at);
	});
}

void MasterBusPanel::replacePlugin(const std::string &id, int index)
{
	QPointer<MasterBusPanel> self(this);
	bus::Engine::get().addPlugin(id, index, [self](bool ok, const std::string &error, int at) {
		if (!ok) {
			if (self)
				QMessageBox::warning(self, QStringLiteral("Replace plugin"),
						     QStringLiteral("The plugin couldn't be loaded.\n\n%1")
							     .arg(QString::fromStdString(error)));
			return;
		}
		bus::Engine::get().removeSlot(at + 1); // the one it replaces
		MasterBus::get().chainChanged();
		if (self) {
			self->selected_ = at;
			self->refreshSlots();
		}
		bus::Engine::get().openEditor(at);
	});
}

void MasterBusPanel::showInsertMenu(int index, const QPoint &globalPos)
{
	selectSlot(index);
	QMenu menu(this);
	menu.setStyleSheet(theme::styleSheet());
	const int at = std::min(index, bus::Engine::get().slotCount());
	fillPluginMenu(&menu, this, [this, at](const std::string &id) { insertPlugin(id, at); });
	menu.exec(globalPos);
}

void MasterBusPanel::showSlotMenu(int index, const QPoint &globalPos)
{
	selectSlot(index);
	bus::Engine &engine = bus::Engine::get();
	const bus::SlotInfo info = engine.slot(index);
	const int count = engine.slotCount();
	QMenu menu(this);
	menu.setStyleSheet(theme::styleSheet());
	auto later = [this](std::function<void()> fn) { return [this, fn] { QTimer::singleShot(0, this, fn); }; };

	connect(menu.addAction(QStringLiteral("Open %1").arg(QString::fromStdString(info.name))), &QAction::triggered, this,
		later([index] { bus::Engine::get().openEditor(index); }));
	connect(menu.addAction(info.bypass ? QStringLiteral("Turn On") : QStringLiteral("Bypass")), &QAction::triggered, this,
		later([this, index, bypass = info.bypass] {
			bus::Engine::get().setBypass(index, !bypass);
			MasterBus::get().saveSoon();
			refreshSlots();
		}));
	QMenu *replace = menu.addMenu(QStringLiteral("Replace With"));
	fillPluginMenu(replace, this, [this, index](const std::string &id) { replacePlugin(id, index); });
	menu.addSeparator();
	QAction *up = menu.addAction(QStringLiteral("Move Up"));
	up->setEnabled(index > 0);
	connect(up, &QAction::triggered, this, later([this, index] {
			bus::Engine::get().moveSlot(index, index - 1);
			MasterBus::get().chainChanged();
			selected_ = index - 1;
			refreshSlots();
		}));
	QAction *down = menu.addAction(QStringLiteral("Move Down"));
	down->setEnabled(index < count - 1);
	connect(down, &QAction::triggered, this, later([this, index] {
			bus::Engine::get().moveSlot(index, index + 1);
			MasterBus::get().chainChanged();
			selected_ = index + 1;
			refreshSlots();
		}));
	if (std::fabs(info.inGainDb) > 0.05f || std::fabs(info.outGainDb) > 0.05f) {
		connect(menu.addAction(QStringLiteral("Reset Slot Gains (in %1, out %2 dB)")
					       .arg(paint::db(info.inGainDb), paint::db(info.outGainDb))),
			&QAction::triggered, this, later([this, index] {
				bus::Engine::get().setSlotGains(index, 0.0f, 0.0f);
				MasterBus::get().save();
				refreshSlots();
			}));
	}
	menu.addSeparator();
	connect(menu.addAction(QStringLiteral("Remove")), &QAction::triggered, this, later([this, index] {
			bus::Engine::get().removeSlot(index);
			MasterBus::get().chainChanged();
			selected_ = -1;
			refreshSlots();
		}));
	menu.exec(globalPos);
}

void MasterBusPanel::showInputs()
{
	// One routing window, shared by every bus view.
	static QPointer<RoutingDialog> dialog;
	if (!dialog)
		dialog = new RoutingDialog(window());
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void MasterBusPanel::startScan(bool rescanAll)
{
	if (scanTimer_->isActive())
		return;
	if (rescanAll) {
		const auto answer = QMessageBox::question(
			this, QStringLiteral("Rescan all plugins?"),
			QStringLiteral("Every plugin is checked again, including ones that failed before. This can take a few "
				       "minutes with large collections."),
			QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
		if (answer != QMessageBox::Yes)
			return;
	}
	obs_log(LOG_INFO, "master bus: plugin scan started (%s)", rescanAll ? "all" : "new only");
	bus::Engine::get().startScan(rescanAll);
	scanButton_->setEnabled(false);
	rescanButton_->setEnabled(false);
	scanStatus_->setText(QStringLiteral("Scanning…"));
	scanTimer_->start();
}

void MasterBusPanel::scanTick()
{
	std::string current;
	float progress = 0.0f;
	if (bus::Engine::get().scanStep(current, progress)) {
		scanStatus_->setText(QStringLiteral("Scanning %1%  ·  %2")
					     .arg(static_cast<int>(progress * 100.0f))
					     .arg(QString::fromStdString(current)));
		return;
	}
	finishScan();
}

void MasterBusPanel::finishScan()
{
	scanTimer_->stop();
	scanButton_->setEnabled(true);
	rescanButton_->setEnabled(true);
	const size_t n = bus::Engine::get().plugins().size();
	scanStatus_->setText(n == 1 ? QStringLiteral("1 effect plugin available.")
				    : QStringLiteral("%1 effect plugins available.").arg(n));
	obs_log(LOG_INFO, "master bus: scan finished, %zu effect plugins, %zu failed", n,
		bus::Engine::get().failedPlugins().size());
	refreshBrowser();
	const std::string warning = bus::Engine::get().takeStartupWarning();
	if (!warning.empty())
		QMessageBox::warning(this, QStringLiteral("Master bus"), QString::fromStdString(warning));
}

// ---------------------------------------------------------------------------

MasterBusWindow::MasterBusWindow(QWidget *parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("OBSViewerDialog"));
	setStyleSheet(theme::styleSheet());
	setWindowTitle(QStringLiteral("OBSViewer Master Bus"));
	resize(700, 820);
	panel_ = new MasterBusPanel(false, this);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(panel_);
}

} // namespace obsviewer
