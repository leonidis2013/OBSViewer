#pragma once

#include <QAbstractButton>
#include <QWidget>

#include <functional>

class QHBoxLayout;
class QStackedWidget;
class QTimer;

namespace obsviewer {

class HealthButton;
class HealthList;
class Section;
class StateButton;
class AudioMeterWidget;
class NetworkStats;
class StatLines;
class LoudnessHistory;

// Small square header button with a drawn icon (settings, fullscreen, close…).
class IconButton : public QAbstractButton {
public:
	enum class Icon { Settings, Fullscreen, Windowed, Close, PopOut };

	IconButton(Icon icon, const QString &tip, QWidget *parent = nullptr);
	void setIcon(Icon icon);

protected:
	void paintEvent(QPaintEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	Icon icon_;
};

// Right-hand bar, top to bottom: header icons, overall health and performance
// lights (click either to expand every check), stream start/stop with the
// network stats, record start/stop, and the programme loudness meter with its
// 5-minute history.
class SidePanel : public QWidget {
public:
	explicit SidePanel(QWidget *parent = nullptr);

	void setCompact(bool compact);
	// Adds an icon to the header row (right-aligned, in call order).
	IconButton *addHeaderButton(IconButton::Icon icon, const QString &tip, std::function<void()> onClick);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	void refresh();
	void showPage(int page); // 0 controls, 1 health, 2 performance
	void onStreamClicked();
	void onRecordClicked();

	bool compact_ = false;
	bool blink_ = false;
	qint64 streamPendingUntil_ = 0;
	qint64 recordPendingUntil_ = 0;

	QHBoxLayout *header_ = nullptr;
	HealthButton *health_ = nullptr;
	HealthButton *perf_ = nullptr;
	QStackedWidget *stack_ = nullptr;
	Section *streamSection_ = nullptr;
	Section *recordSection_ = nullptr;
	Section *audioSection_ = nullptr;
	StateButton *streamButton_ = nullptr;
	StateButton *recordButton_ = nullptr;
	NetworkStats *network_ = nullptr;
	StatLines *recordLines_ = nullptr;
	AudioMeterWidget *meter_ = nullptr;
	LoudnessHistory *loudness_ = nullptr;
	HealthList *details_ = nullptr;
	HealthList *perfDetails_ = nullptr;
	QTimer *blinkTimer_ = nullptr;
};

} // namespace obsviewer
