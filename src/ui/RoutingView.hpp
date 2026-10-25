#pragma once

#include <QDialog>
#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

namespace obsviewer {

// Wire drawing of the master bus routing: every audio source on the left, the
// bus (input track, plugin chain, output) in the middle, OBS's six tracks on the
// right with the stream / recording tracks marked. Blue wires go through the
// bus; grey wires go straight to a track.
//
// Click a source to send it through the bus (or back); click a track to add or
// remove it as a bus output.
class RoutingDiagram : public QWidget {
public:
	explicit RoutingDiagram(QWidget *parent = nullptr);

	// Re-reads the routing; repaints only if something changed.
	void refresh();
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	struct Source {
		std::string uuid;
		QString name;
		bool routed = false;
		uint32_t mixers = 0;
		QRectF box;
	};
	struct Track {
		QRectF box;
	};
	void layoutBoxes();
	// hit: 0 none, 1 source (index), 2 track (index)
	int hitTest(const QPointF &pos, int &index) const;

	std::vector<Source> sources_;
	std::vector<QString> plugins_; // chain, in order ("" = none)
	std::vector<bool> bypassed_;
	Track tracks_[6];
	QRectF bus_;
	bool busOn_ = false;
	bool busRunning_ = false;
	int sendTrack_ = 6;
	uint32_t outputMask_ = 1;
	int streamTrack_ = 1;
	uint32_t recordMask_ = 1;
	std::string signature_;
	int hoverKind_ = 0;
	int hoverIndex_ = -1;
};

// The routing window: the diagram plus the bus track settings.
class RoutingDialog : public QDialog {
public:
	explicit RoutingDialog(QWidget *parent = nullptr);

protected:
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;

private:
	void refresh();

	RoutingDiagram *diagram_ = nullptr;
	QLabel *summary_ = nullptr;
	QComboBox *sendTrack_ = nullptr;
	std::vector<QCheckBox *> outputs_;
	QPushButton *all_ = nullptr;
	QPushButton *none_ = nullptr;
	QTimer *timer_ = nullptr;
	bool updating_ = false;
};

} // namespace obsviewer
