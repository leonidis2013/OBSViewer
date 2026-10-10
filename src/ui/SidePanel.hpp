#pragma once

#include <QWidget>

class QStackedWidget;
class QTimer;

namespace obsviewer {

class HealthButton;
class HealthList;
class Section;
class StateButton;
class AudioMeterWidget;

// Right-hand bar: overall health light (click to expand every check), stream and
// record start/stop with status, and the programme loudness meter.
class SidePanel : public QWidget {
public:
	explicit SidePanel(QWidget *parent = nullptr);

	void setCompact(bool compact);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	void refresh();
	void toggleDetails();
	void onStreamClicked();
	void onRecordClicked();

	bool compact_ = false;
	bool blink_ = false;
	qint64 streamPendingUntil_ = 0;
	qint64 recordPendingUntil_ = 0;

	HealthButton *health_ = nullptr;
	QStackedWidget *stack_ = nullptr;
	Section *streamSection_ = nullptr;
	Section *recordSection_ = nullptr;
	Section *audioSection_ = nullptr;
	StateButton *streamButton_ = nullptr;
	StateButton *recordButton_ = nullptr;
	AudioMeterWidget *meter_ = nullptr;
	HealthList *details_ = nullptr;
	QTimer *blinkTimer_ = nullptr;
};

} // namespace obsviewer
