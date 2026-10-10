#include "ui/Theme.hpp"

#include <QString>

namespace obsviewer::theme {

QString styleSheet()
{
	return QStringLiteral(R"(
QWidget#OBSViewerRoot, QDialog#OBSViewerDialog {
	background-color: #0E1115;
	color: #E9EDF2;
}
QWidget#OBSViewerToolbar {
	background-color: #161A20;
	border-bottom: 1px solid #2A323D;
}
QDialog#OBSViewerDialog QLabel, QWidget#OBSViewerToolbar QLabel {
	color: #E9EDF2;
}
QLabel[dim="true"] {
	color: #8794A5;
}
QLabel[heading="true"] {
	color: #8794A5;
	font-weight: bold;
	letter-spacing: 1px;
}
QComboBox, QLineEdit, QTextBrowser, QPlainTextEdit, QDoubleSpinBox {
	background-color: #1E242C;
	color: #E9EDF2;
	border: 1px solid #2A323D;
	border-radius: 6px;
	padding: 4px 8px;
	selection-background-color: #E5484D;
}
QComboBox QAbstractItemView {
	background-color: #1E242C;
	color: #E9EDF2;
	selection-background-color: #2A323D;
}
QPushButton, QToolButton {
	background-color: #1E242C;
	color: #E9EDF2;
	border: 1px solid #2A323D;
	border-radius: 6px;
	padding: 4px 10px;
}
QPushButton:hover, QToolButton:hover {
	border-color: #8794A5;
}
QPushButton:disabled, QToolButton:disabled {
	color: #8794A5;
}
QPushButton[accent="true"] {
	background-color: #E5484D;
	border-color: #E5484D;
	color: #FFFFFF;
	font-weight: bold;
}
QCheckBox {
	color: #E9EDF2;
}
QGroupBox {
	color: #8794A5;
	border: 1px solid #2A323D;
	border-radius: 8px;
	margin-top: 14px;
	padding: 10px 8px 8px 8px;
	font-weight: bold;
}
QGroupBox::title {
	subcontrol-origin: margin;
	left: 10px;
	padding: 0 4px;
}
QProgressBar {
	background-color: #1E242C;
	border: 1px solid #2A323D;
	border-radius: 6px;
	color: #E9EDF2;
	text-align: center;
}
QProgressBar::chunk {
	background-color: #3ECF8E;
	border-radius: 5px;
}
)");
}

} // namespace obsviewer::theme
