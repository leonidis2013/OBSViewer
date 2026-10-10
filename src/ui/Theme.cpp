#include "ui/Theme.hpp"

#include <QString>

namespace obsviewer::theme {

// LiveVoice look: near-black background, slightly lifted panels with 1-px
// borders and rounded corners, quiet grey text, colour only where it means something.
QString styleSheet()
{
	return QStringLiteral(R"(
QWidget#OBSViewerRoot, QDialog#OBSViewerDialog {
	background-color: #0D1015;
	color: #D8DEE6;
}
QDialog#OBSViewerDialog QLabel {
	color: #D8DEE6;
}
QLabel[dim="true"] {
	color: #7C8794;
}
QLabel[heading="true"] {
	color: #D8DEE6;
	font-weight: 600;
}
QComboBox, QLineEdit, QTextBrowser, QPlainTextEdit, QDoubleSpinBox, QSpinBox, QListWidget, QTreeWidget {
	background-color: #1B2129;
	color: #D8DEE6;
	border: 1px solid #252C36;
	border-radius: 5px;
	padding: 4px 8px;
	selection-background-color: #4C8DFF;
}
QComboBox:hover, QLineEdit:hover, QDoubleSpinBox:hover, QSpinBox:hover {
	border-color: #4C5561;
}
QComboBox QAbstractItemView {
	background-color: #1B2129;
	color: #D8DEE6;
	border: 1px solid #252C36;
	selection-background-color: #252C36;
}
QTreeWidget::item, QListWidget::item {
	padding: 3px 2px;
}
QTreeWidget::item:selected, QListWidget::item:selected {
	background-color: #252C36;
	color: #D8DEE6;
}
QHeaderView::section {
	background-color: #151A21;
	color: #7C8794;
	border: none;
	border-bottom: 1px solid #252C36;
	padding: 4px 6px;
}
QPushButton, QToolButton {
	background-color: #1B2129;
	color: #D8DEE6;
	border: 1px solid #252C36;
	border-radius: 5px;
	padding: 5px 12px;
	font-weight: 600;
}
QPushButton:hover, QToolButton:hover {
	background-color: #222933;
	border-color: #4C5561;
}
QPushButton:pressed, QToolButton:pressed {
	background-color: #151A21;
}
QPushButton:checked, QToolButton:checked {
	background-color: #252C36;
	border-color: #4C5561;
}
QPushButton:disabled, QToolButton:disabled {
	color: #4C5561;
}
QPushButton[accent="true"] {
	background-color: #4C8DFF;
	border-color: #4C8DFF;
	color: #FFFFFF;
}
QCheckBox {
	color: #D8DEE6;
	spacing: 8px;
}
QGroupBox {
	background-color: #151A21;
	color: #D8DEE6;
	border: 1px solid #252C36;
	border-radius: 8px;
	margin-top: 8px;
	padding: 30px 12px 10px 12px;
	font-weight: 600;
	font-size: 13px;
}
QGroupBox::title {
	subcontrol-origin: padding;
	subcontrol-position: top left;
	left: 12px;
	top: 8px;
	padding: 0 0 0 8px;
	border-left: 3px solid #4C8DFF;
}
QProgressBar {
	background-color: #0D1015;
	border: 1px solid #252C36;
	border-radius: 5px;
	color: #D8DEE6;
	text-align: center;
}
QProgressBar::chunk {
	background-color: #3ECF6E;
	border-radius: 4px;
}
QMenu {
	background-color: #151A21;
	color: #D8DEE6;
	border: 1px solid #252C36;
	padding: 4px;
}
QMenu::item {
	padding: 4px 18px 4px 22px;
	border-radius: 4px;
}
QMenu::item:selected {
	background-color: #252C36;
}
QMenu::item:disabled {
	color: #4C5561;
}
QMenu::separator {
	height: 1px;
	background: #252C36;
	margin: 4px 6px;
}
QScrollBar:vertical {
	background: transparent;
	width: 8px;
}
QScrollBar::handle:vertical {
	background: #252C36;
	border-radius: 4px;
	min-height: 24px;
}
QScrollBar::add-line, QScrollBar::sub-line {
	height: 0;
}
)");
}

} // namespace obsviewer::theme
