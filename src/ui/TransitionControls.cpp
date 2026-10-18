#include "ui/TransitionControls.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>

namespace obsviewer {

namespace {

std::vector<obs_source_t *> transitions() // strong refs; caller releases
{
	obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	std::vector<obs_source_t *> out;
	for (size_t i = 0; i < list.sources.num; ++i)
		out.push_back(obs_source_get_ref(list.sources.array[i]));
	obs_frontend_source_list_free(&list);
	return out;
}

void releaseAll(std::vector<obs_source_t *> &v)
{
	for (obs_source_t *s : v)
		obs_source_release(s);
	v.clear();
}

} // namespace

void TransitionControls::autoTransition()
{
	if (!obs_frontend_preview_program_mode_active())
		return;
	obs_log(LOG_INFO, "transition: preview to program");
	obs_frontend_preview_program_trigger_transition();
}

void TransitionControls::cut()
{
	if (!obs_frontend_preview_program_mode_active())
		return;
	// Switch to OBS's built-in Cut for this one take, then put the chosen
	// transition back (the calls run in order on OBS's main thread).
	std::vector<obs_source_t *> all = transitions();
	obs_source_t *cutSource = nullptr;
	for (obs_source_t *t : all)
		if (std::strcmp(obs_source_get_unversioned_id(t), "cut_transition") == 0)
			cutSource = t;
	obs_source_t *current = obs_frontend_get_current_transition();
	if (cutSource && cutSource != current) {
		obs_frontend_set_current_transition(cutSource);
		obs_frontend_preview_program_trigger_transition();
		obs_frontend_set_current_transition(current);
	} else {
		obs_frontend_preview_program_trigger_transition();
	}
	obs_log(LOG_INFO, "transition: cut to program");
	obs_source_release(current);
	releaseAll(all);
}

TransitionControls::TransitionControls(bool compact, QWidget *parent) : QWidget(parent)
{
	setStyleSheet(QStringLiteral(R"(
QPushButton#cut { background-color: #2A1A1C; border-color: #EF4C4C; color: #FFFFFF; font-weight: 700; }
QPushButton#cut:hover { background-color: #3A2124; }
QPushButton#auto { background-color: #1B2A3F; border-color: #4C8DFF; color: #FFFFFF; font-weight: 700; }
QPushButton#auto:hover { background-color: #22355A; }
)"));

	// ---- Studio Mode on ----
	studio_ = new QWidget(this);
	cut_ = new QPushButton(QStringLiteral("CUT"), studio_);
	cut_->setObjectName(QStringLiteral("cut"));
	cut_->setToolTip(QStringLiteral("Take Preview to Program instantly"));
	auto_ = new QPushButton(QStringLiteral("AUTO"), studio_);
	auto_->setObjectName(QStringLiteral("auto"));
	auto_->setToolTip(QStringLiteral("Take Preview to Program with the chosen transition"));
	auto *buttons = new QHBoxLayout();
	buttons->setContentsMargins(0, 0, 0, 0);
	buttons->setSpacing(4);
	buttons->addWidget(cut_, 1);
	buttons->addWidget(auto_, 1);

	auto *sl = new QVBoxLayout(studio_);
	sl->setContentsMargins(0, 0, 0, 0);
	sl->setSpacing(4);
	sl->addLayout(buttons);

	type_ = new QComboBox(studio_);
	type_->setToolTip(QStringLiteral("Transition used by AUTO (same as OBS's transition list)"));
	duration_ = new QSpinBox(studio_);
	duration_->setRange(50, 20000);
	duration_->setSingleStep(50);
	duration_->setSuffix(QStringLiteral(" ms"));
	duration_->setToolTip(QStringLiteral("Transition duration"));
	auto *typeRow = new QHBoxLayout();
	typeRow->setContentsMargins(0, 0, 0, 0);
	typeRow->setSpacing(4);
	typeRow->addWidget(type_, 1);
	typeRow->addWidget(duration_);
	sl->addLayout(typeRow);
	setCompact(compact);

	// ---- Studio Mode off ----
	noStudio_ = new QWidget(this);
	auto *note = new QLabel(QStringLiteral("Transitions need Studio Mode."), noStudio_);
	note->setProperty("dim", true);
	note->setWordWrap(true);
	auto *studioOn = new QPushButton(QStringLiteral("Turn On Studio Mode"), noStudio_);
	auto *nl = new QVBoxLayout(noStudio_);
	nl->setContentsMargins(0, 0, 0, 0);
	nl->setSpacing(4);
	nl->addWidget(note);
	nl->addWidget(studioOn);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(studio_);
	layout->addWidget(noStudio_);

	connect(cut_, &QPushButton::clicked, this, [] { QTimer::singleShot(0, [] { cut(); }); });
	connect(auto_, &QPushButton::clicked, this, [] { QTimer::singleShot(0, [] { autoTransition(); }); });
	connect(studioOn, &QPushButton::clicked, this, [] {
		QTimer::singleShot(0, [] { obs_frontend_set_preview_program_mode(true); });
	});
	connect(type_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (updating_ || index < 0)
			return;
		const std::string name = type_->itemData(index).toString().toStdString();
		std::vector<obs_source_t *> all = transitions();
		for (obs_source_t *t : all)
			if (name == obs_source_get_name(t))
				obs_frontend_set_current_transition(t);
		releaseAll(all);
		QTimer::singleShot(50, this, [this] { refresh(); });
	});
	connect(duration_, &QSpinBox::valueChanged, this, [this](int ms) {
		if (!updating_)
			obs_frontend_set_transition_duration(ms);
	});

	auto *timer = new QTimer(this);
	timer->setInterval(500);
	connect(timer, &QTimer::timeout, this, [this] {
		if (isVisible())
			refresh();
	});
	timer->start();
	refresh();
}

void TransitionControls::setCompact(bool compact)
{
	type_->setVisible(!compact);
	duration_->setVisible(!compact);
	cut_->setMinimumHeight(compact ? 26 : 32);
	auto_->setMinimumHeight(compact ? 26 : 32);
}

void TransitionControls::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	refresh();
}

void TransitionControls::refresh()
{
	updating_ = true;
	const bool studio = obs_frontend_preview_program_mode_active();
	studio_->setVisible(studio);
	noStudio_->setVisible(!studio);

	std::vector<obs_source_t *> all = transitions();
	std::vector<std::string> names;
	for (obs_source_t *t : all)
		names.emplace_back(obs_source_get_name(t));
	if (names != names_) {
		names_ = names;
		type_->clear();
		for (const auto &n : names_)
			type_->addItem(QString::fromStdString(n), QString::fromStdString(n));
	}
	obs_source_t *current = obs_frontend_get_current_transition();
	const QString currentName = current ? QString::fromUtf8(obs_source_get_name(current)) : QString();
	const int i = type_->findData(currentName);
	if (i >= 0 && i != type_->currentIndex())
		type_->setCurrentIndex(i);
	const bool fixed = current && obs_transition_fixed(current);
	const bool isCut = current && std::strcmp(obs_source_get_unversioned_id(current), "cut_transition") == 0;
	duration_->setEnabled(!fixed && !isCut);
	const int ms = obs_frontend_get_transition_duration();
	if (!duration_->hasFocus() && duration_->value() != ms)
		duration_->setValue(ms);
	auto_->setText(isCut || currentName.isEmpty() ? QStringLiteral("AUTO")
						       : QStringLiteral("AUTO · %1").arg(currentName));
	obs_source_release(current);
	releaseAll(all);
	updating_ = false;
}

} // namespace obsviewer
