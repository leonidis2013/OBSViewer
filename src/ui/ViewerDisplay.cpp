#include "ui/ViewerDisplay.hpp"

#include "core/Settings.hpp"
#include "ui/Theme.hpp"
#include "ui/TransitionControls.hpp"
#include "viewer/Layouts.hpp"
#include "viewer/Renderer.hpp"
#include "viewer/ViewerState.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWindow>

#include <algorithm>

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
	setMouseTracking(true); // hover on the transition buttons

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
		ViewerState::get().removeView(this);
		counted_ = false;
	}
}

void ViewerDisplay::setLayoutId(const std::string &id)
{
	layout_.store(&layoutById(id));
	if (counted_)
		ViewerState::get().setView(this, layout_.load()->id);
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
	dpr_.store(static_cast<float>(devicePixelRatioF()));

	gs_init_data info = {};
	info.cx = static_cast<uint32_t>(size.width());
	info.cy = static_cast<uint32_t>(size.height());
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
#ifdef __APPLE__
	info.window.view = reinterpret_cast<id>(windowHandle()->winId());
#elif !defined(OBSVIEWER_SYNTAX_CHECK)
#error "OBSViewer currently targets macOS only"
#endif

	// obs_display_create takes 0xAABBGGRR; theme bg #0D1015
	display_ = obs_display_create(&info, 0xFF15100D);
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
	obs_enter_graphics();
	cache_.destroy();
	obs_leave_graphics();
}

void ViewerDisplay::draw(void *param, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<ViewerDisplay *>(param);
	Renderer::render(self->layout_.load()->id, cx, cy, self->dpr_.load(), self->cache_, self->hover_.load(),
			 self->pressed_.load());
}

void ViewerDisplay::paintEvent(QPaintEvent *event)
{
	createDisplay();
	QWidget::paintEvent(event);
}

void ViewerDisplay::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	dpr_.store(static_cast<float>(devicePixelRatioF()));
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
		ViewerState::get().setView(this, layout_.load()->id);
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
		ViewerState::get().removeView(this);
		counted_ = false;
	}
}

namespace {

// Same placement the renderer uses, in the same logical pixels.
std::vector<PlacedTile> placeFor(const QWidget *w, const std::string &id, const std::vector<int> &map)
{
	const ViewerState &state = ViewerState::get();
	const Settings &settings = Settings::get();
	PlaceParams params;
	params.width = static_cast<float>(w->width());
	params.height = static_cast<float>(w->height());
	params.aspect = static_cast<float>(state.canvasWidth()) / static_cast<float>(std::max<uint32_t>(1, state.canvasHeight()));
	params.labelHeight = settings.showLabels ? labelBandHeight(params.height) : 0.0f;
	params.gap = 4.0f;
	params.centerStrip = settings.showTransitions ? transitionStripWidth(id, params.width) : 0.0f;
	params.sceneCount = id == kAutoLayoutId ? static_cast<int>(map.size()) : state.sceneCount();
	const float banner = state.hasStreamTitle() ? titleBannerHeight(params.height) : 0.0f;
	params.height -= banner;
	std::vector<PlacedTile> tiles = placeTiles(id, params);
	if (banner > 0.0f)
		shiftTiles(tiles, 0.0f, banner);
	return tiles;
}

} // namespace

int ViewerDisplay::stripButtonAt(const QPointF &pos) const
{
	if (!Settings::get().showTransitions)
		return Renderer::kNone;
	const std::string id = layout_.load()->id;
	if (transitionStripWidth(id, static_cast<float>(width())) <= 0.0f)
		return Renderer::kNone;
	const TransitionStrip st = transitionStrip(placeFor(this, id, ViewerState::get().viewMap(id)), 4.0f);
	if (!st.valid)
		return Renderer::kNone;
	const float x = static_cast<float>(pos.x()), y = static_cast<float>(pos.y());
	if (st.cut.contains(x, y))
		return Renderer::kCut;
	if (st.autoTake.contains(x, y))
		return Renderer::kAuto;
	return Renderer::kNone;
}

void ViewerDisplay::setHover(int button)
{
	if (hover_.exchange(button) == button)
		return;
	const bool clickable = button != Renderer::kNone && obs_frontend_preview_program_mode_active();
	if (clickable)
		setCursor(Qt::PointingHandCursor);
	else
		unsetCursor();
}

void ViewerDisplay::mouseMoveEvent(QMouseEvent *event)
{
	setHover(stripButtonAt(event->position()));
	QWidget::mouseMoveEvent(event);
}

void ViewerDisplay::mouseReleaseEvent(QMouseEvent *event)
{
	pressed_.store(Renderer::kNone);
	QWidget::mouseReleaseEvent(event);
}

void ViewerDisplay::leaveEvent(QEvent *event)
{
	setHover(Renderer::kNone);
	pressed_.store(Renderer::kNone);
	QWidget::leaveEvent(event);
}

bool ViewerDisplay::tileAt(const QPointF &pos, int &kind, int &sceneIndex, int &slot) const
{
	const std::string id = layout_.load()->id;
	const std::vector<int> map = ViewerState::get().viewMap(id);
	const std::vector<PlacedTile> tiles = placeFor(this, id, map);
	const int hit = obsviewer::tileAt(tiles, static_cast<float>(pos.x()), static_cast<float>(pos.y()));
	if (hit < 0)
		return false;
	const PlacedTile &t = tiles[static_cast<size_t>(hit)];
	kind = static_cast<int>(t.kind);
	slot = t.sceneIndex;
	sceneIndex = t.kind == TileKind::Scene
			     ? (static_cast<size_t>(t.sceneIndex) < map.size() ? map[static_cast<size_t>(t.sceneIndex)] : -1)
			     : -1;
	return true;
}

void ViewerDisplay::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		const int button = stripButtonAt(event->position());
		if (button != Renderer::kNone) {
			// Only in Studio Mode: outside it there is nothing to take.
			if (obs_frontend_preview_program_mode_active()) {
				pressed_.store(button);
				if (button == Renderer::kCut)
					TransitionControls::cut();
				else
					TransitionControls::autoTransition();
			}
			event->accept();
			return;
		}
	}
	int kind = 0, scene = -1, slot = -1;
	if (event->button() == Qt::LeftButton && tileAt(event->position(), kind, scene, slot) &&
	    (kind != static_cast<int>(TileKind::Scene) || scene >= 0))
		emit tileClicked(kind, scene);
	QWidget::mousePressEvent(event);
}

void ViewerDisplay::mouseDoubleClickEvent(QMouseEvent *event)
{
	// A quick second click on CUT / AUTO is ignored (no accidental double take).
	if (stripButtonAt(event->position()) != Renderer::kNone) {
		event->accept();
		return;
	}
	int kind = 0, scene = -1, slot = -1;
	if (event->button() == Qt::LeftButton && tileAt(event->position(), kind, scene, slot))
		emit tileDoubleClicked(kind, scene);
	QWidget::mouseDoubleClickEvent(event);
}

void ViewerDisplay::contextMenuEvent(QContextMenuEvent *event)
{
	int kind = -1, scene = -1, slot = -1;
	if (!tileAt(event->pos(), kind, scene, slot) || kind != static_cast<int>(TileKind::Scene))
		slot = scene = -1;
	emit contextMenuRequested(event->globalPos(), slot, scene);
	event->accept();
}

} // namespace obsviewer
