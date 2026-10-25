#pragma once

#include "viewer/Renderer.hpp"

#include <QWidget>

#include <atomic>
#include <string>

struct obs_display;
typedef struct obs_display obs_display_t;

namespace obsviewer {

struct LayoutInfo;

// A native Qt widget that owns an obs_display and draws a multiview layout into it.
class ViewerDisplay : public QWidget {
	Q_OBJECT

public:
	explicit ViewerDisplay(QWidget *parent = nullptr);
	~ViewerDisplay() override;

	void setLayoutId(const std::string &id);
	std::string layoutId() const;

	QPaintEngine *paintEngine() const override { return nullptr; }

signals:
	// kind: TileKind as int; sceneIndex for scene tiles, else -1
	void tileClicked(int kind, int sceneIndex);
	void tileDoubleClicked(int kind, int sceneIndex);
	// slot: position of the scene view under the mouse (-1 if not a scene view),
	// scene: the scene it shows (-1 if none).
	void contextMenuRequested(const QPoint &globalPos, int slot, int scene);

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	void createDisplay();
	void destroyDisplay();
	bool tileAt(const QPointF &pos, int &kind, int &sceneIndex, int &slot) const;
	// Renderer::StripButton under pos (kNone if none, or the strip is hidden).
	int stripButtonAt(const QPointF &pos) const;
	void setHover(int button);
	QSize pixelSize() const;
	static void draw(void *param, uint32_t cx, uint32_t cy);

	obs_display_t *display_ = nullptr;
	std::atomic<const LayoutInfo *> layout_;
	std::atomic<float> dpr_{1.0f};
	std::atomic<int> hover_{0};   // transition button under the mouse
	std::atomic<int> pressed_{0}; // transition button held down
	RenderCache cache_; // graphics thread only
	bool counted_ = false;
};

} // namespace obsviewer
