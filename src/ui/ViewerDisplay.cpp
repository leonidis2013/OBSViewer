#include "ui/ViewerDisplay.hpp"

#include "ui/Theme.hpp"
#include "viewer/Layouts.hpp"
#include "viewer/Renderer.hpp"
#include "viewer/ViewerState.hpp"

#include <obs.h>

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWindow>

namespace obsviewer {

ViewerDisplay::ViewerDisplay(QWidget *parent) : QWidget(parent), layout_(&layoutById(kDefaultLayoutId))
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
	setMinimumSize(160, 90);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setMouseTracking(false);

	winId(); // force creation of the native view

	if (QWindow *window = windowHandle()) {
		connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
			if (visible)
				createDisplay();
		});
		connect(window, &QWindow::screenChanged, this, [this](QScreen *) {
			// Retina ↔ non-Retina moves change the backing scale; rebuild.
			destroyDisplay();
			createDisplay();
		});
	}
}

ViewerDisplay::~ViewerDisplay()
{
	destroyDisplay();
	if (counted_) {
		ViewerState::get().removeView();
		counted_ = false;
	}
}

void ViewerDisplay::setLayoutId(const std::string &id)
{
	layout_.store(&layoutById(id));
}

std::string ViewerDisplay::layoutId() const
{
	return layout_.load()->id;
}

QSize ViewerDisplay::pixelSize() const
{
	return size() * devicePixelRatioF();
}

void ViewerDisplay::createDisplay()
{
	if (display_ || !windowHandle() || !windowHandle()->isExposed())
		return;

	const QSize size = pixelSize();
	if (size.width() <= 0 || size.height() <= 0)
		return;

	gs_init_data info = {};
	info.cx = static_cast<uint32_t>(size.width());
	info.cy = static_cast<uint32_t>(size.height());
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
#ifdef __APPLE__
	info.window.view = reinterpret_cast<id>(windowHandle()->winId());
#else
#error "OBSViewer currently targets macOS only"
#endif

	// obs_display_create takes 0xAABBGGRR; theme bg #0E1115
	display_ = obs_display_create(&info, 0xFF15110E);
	if (display_)
		obs_display_add_draw_callback(display_, &ViewerDisplay::draw, this);
}

void ViewerDisplay::destroyDisplay()
{
	if (!display_)
		return;
	obs_display_remove_draw_callback(display_, &ViewerDisplay::draw, this);
	obs_display_destroy(display_);
	display_ = nullptr;
}

void ViewerDisplay::draw(void *param, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<ViewerDisplay *>(param);
	Renderer::render(*self->layout_.load(), cx, cy);
}

void ViewerDisplay::paintEvent(QPaintEvent *event)
{
	createDisplay();
	QWidget::paintEvent(event);
}

void ViewerDisplay::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	createDisplay();
	if (display_ && isVisible()) {
		const QSize size = pixelSize();
		obs_display_resize(display_, static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height()));
	}
}

void ViewerDisplay::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	if (!counted_) {
		ViewerState::get().addView();
		counted_ = true;
	}
	createDisplay();
	if (display_)
		obs_display_set_enabled(display_, true);
}

void ViewerDisplay::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	if (display_)
		obs_display_set_enabled(display_, false);
	if (counted_) {
		ViewerState::get().removeView();
		counted_ = false;
	}
}

bool ViewerDisplay::tileAt(const QPointF &pos, int &kind, int &sceneIndex) const
{
	const ViewerState &state = ViewerState::get();
	const Layout layout = resolveLayout(*layout_.load(), state.sceneCount());
	const TileHit hit = Renderer::hitTest(layout, state.canvasWidth(), state.canvasHeight(), width(), height(),
					      pos.x(), pos.y());
	if (hit.tile < 0)
		return false;
	kind = static_cast<int>(hit.kind);
	sceneIndex = hit.sceneIndex;
	return true;
}

void ViewerDisplay::mousePressEvent(QMouseEvent *event)
{
	int kind = 0, scene = -1;
	if (event->button() == Qt::LeftButton && tileAt(event->position(), kind, scene))
		emit tileClicked(kind, scene);
	QWidget::mousePressEvent(event);
}

void ViewerDisplay::mouseDoubleClickEvent(QMouseEvent *event)
{
	int kind = 0, scene = -1;
	if (event->button() == Qt::LeftButton && tileAt(event->position(), kind, scene))
		emit tileDoubleClicked(kind, scene);
	QWidget::mouseDoubleClickEvent(event);
}

void ViewerDisplay::contextMenuEvent(QContextMenuEvent *event)
{
	emit contextMenuRequested(event->globalPos());
	event->accept();
}

} // namespace obsviewer
