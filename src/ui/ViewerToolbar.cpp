#include "ui/ViewerToolbar.hpp"

#include "App.hpp"
#include "core/Settings.hpp"
#include "ui/Menu.hpp"
#include "viewer/Layouts.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QTimer>
#include <QToolButton>

namespace obsviewer {

ViewerToolbar::ViewerToolbar(bool fullscreen, std::function<void(const std::string &)> onLayout, QWidget *parent)
	: QWidget(parent),
	  onLayout_(std::move(onLayout))
{
	setObjectName(QStringLiteral("OBSViewerToolbar"));
	setAttribute(Qt::WA_StyledBackground, true);

	auto *bar = new QHBoxLayout(this);
	bar->setContentsMargins(6, 4, 6, 4);
	bar->setSpacing(6);

	layoutBox_ = new QComboBox(this);
	for (const Layout &layout : allLayouts())
		layoutBox_->addItem(QString::fromStdString(layout.name), QString::fromStdString(layout.id));
	layoutBox_->setToolTip(QStringLiteral("Multiview layout"));
	connect(layoutBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (index >= 0 && onLayout_)
			onLayout_(layoutBox_->itemData(index).toString().toStdString());
	});

	auto *clickLabel = new QLabel(QStringLiteral("Click:"), this);
	clickLabel->setProperty("dim", true);
	clickBox_ = new QComboBox(this);
	clickBox_->addItem(QStringLiteral("Off"), Settings::ClickOff);
	clickBox_->addItem(QStringLiteral("To Preview"), Settings::ClickPreview);
	clickBox_->addItem(QStringLiteral("To Program"), Settings::ClickProgram);
	clickBox_->setToolTip(QStringLiteral("What clicking a scene tile does"));
	connect(clickBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (index < 0)
			return;
		Settings &s = Settings::get();
		const int action = clickBox_->itemData(index).toInt();
		if (s.clickAction != action) {
			s.clickAction = action;
			s.save();
			App::get().applySettings();
		}
	});

	bar->addWidget(layoutBox_, 1);
	bar->addWidget(clickLabel);
	bar->addWidget(clickBox_);

	if (fullscreen) {
		auto *screens = new QToolButton(this);
		screens->setText(QStringLiteral("Screen"));
		screens->setToolTip(QStringLiteral("Move to another screen"));
		screens->setPopupMode(QToolButton::InstantPopup);
		screens->setMenu(new ui::ScreenMenu(QStringLiteral("Move to Screen"),
						    [](QScreen *screen) { App::get().openFullscreen(screen); }, screens));
		bar->addWidget(screens);
	} else {
		auto *full = new QToolButton(this);
		full->setText(QStringLiteral("Fullscreen"));
		full->setToolTip(QStringLiteral("Open the multiview fullscreen on a screen"));
		full->setPopupMode(QToolButton::InstantPopup);
		auto *menu = new ui::ScreenMenu(QStringLiteral("Open Fullscreen"),
						[](QScreen *screen) { App::get().openFullscreen(screen); }, full);
		menu->setExtraAction(
			QStringLiteral("Close Fullscreen"), [] { App::get().closeFullscreen(); },
			[] { return App::get().fullscreenOpen(); });
		full->setMenu(menu);
		bar->addWidget(full);
	}

	auto *settings = new QToolButton(this);
	settings->setText(QStringLiteral("Settings"));
	settings->setToolTip(QStringLiteral("OBSViewer settings and updates"));
	connect(settings, &QToolButton::clicked, this, [this] { App::get().openSettings(window()); });
	bar->addWidget(settings);

	if (fullscreen) {
		auto *exit = new QToolButton(this);
		exit->setText(QStringLiteral("Exit"));
		exit->setToolTip(QStringLiteral("Leave fullscreen (Esc)"));
		connect(exit, &QToolButton::clicked, this,
			[] { QTimer::singleShot(0, &App::get(), [] { App::get().closeFullscreen(); }); });
		bar->addWidget(exit);
	}

	syncFromSettings();
}

void ViewerToolbar::setLayoutId(const std::string &id)
{
	const int index = layoutBox_->findData(QString::fromStdString(layoutById(id).id));
	if (index >= 0 && index != layoutBox_->currentIndex()) {
		const QSignalBlocker block(layoutBox_);
		layoutBox_->setCurrentIndex(index);
	}
}

void ViewerToolbar::syncFromSettings()
{
	const int index = clickBox_->findData(Settings::get().clickAction);
	if (index >= 0 && index != clickBox_->currentIndex()) {
		const QSignalBlocker block(clickBox_);
		clickBox_->setCurrentIndex(index);
	}
}

} // namespace obsviewer
