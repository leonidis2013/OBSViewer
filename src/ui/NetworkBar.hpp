#pragma once

#include <QWidget>

class QTimer;

namespace obsviewer {

// Bottom bar: upload rate, 5-minute bitrate history, dropped frames, connection
// state and 5-minute loudness history against the target.
class NetworkBar : public QWidget {
public:
	explicit NetworkBar(QWidget *parent = nullptr);

	void setCompact(bool compact);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	bool compact_ = false;
	bool blink_ = false;
	QTimer *blinkTimer_ = nullptr;
};

} // namespace obsviewer
