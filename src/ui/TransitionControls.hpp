#pragma once

#include <QWidget>

#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace obsviewer {

// Studio Mode transition controls: CUT (instant) and AUTO (the chosen
// transition), plus the transition and its duration. Outside Studio Mode it
// offers to turn Studio Mode on. Used in the side bar and the controls dock.
class TransitionControls : public QWidget {
public:
	// compact: buttons only (the dock-size side bar).
	explicit TransitionControls(bool compact, QWidget *parent = nullptr);

	// Buttons only when compact.
	void setCompact(bool compact);

	static void cut();
	static void autoTransition();

protected:
	void showEvent(QShowEvent *event) override;

private:
	void refresh();

	QWidget *studio_ = nullptr;   // controls shown in Studio Mode
	QWidget *noStudio_ = nullptr; // "Turn on Studio Mode"
	QPushButton *cut_ = nullptr;
	QPushButton *auto_ = nullptr;
	QComboBox *type_ = nullptr;
	QSpinBox *duration_ = nullptr;
	std::vector<std::string> names_;
	bool updating_ = false;
};

} // namespace obsviewer
