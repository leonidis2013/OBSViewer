#pragma once

#include <cstdint>

class QString;

// BroadcastMaster dark palette, shared by the multiview renderer (ARGB) and the Qt UI.
namespace obsviewer::theme {

constexpr uint32_t kBg = 0xFF0E1115;
constexpr uint32_t kPanel = 0xFF161A20;
constexpr uint32_t kPanelRaised = 0xFF1E242C;
constexpr uint32_t kBorder = 0xFF2A323D;
constexpr uint32_t kText = 0xFFE9EDF2;
constexpr uint32_t kTextDim = 0xFF8794A5;
constexpr uint32_t kBad = 0xFFE5484D; // program tally, ON AIR, REC, accent
constexpr uint32_t kGood = 0xFF3ECF8E; // preview tally
constexpr uint32_t kWarn = 0xFFF5A524;
constexpr uint32_t kBlack = 0xFF000000;
constexpr uint32_t kLabelPlate = 0xCC0E1115; // translucent bg behind tile labels

// Qt style sheet for OBSViewer's own widgets (dock toolbar, dialogs).
QString styleSheet();

} // namespace obsviewer::theme
