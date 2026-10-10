#pragma once

#include <cstdint>

class QString;

// LiveVoice palette, shared by the multiview renderer (ARGB) and the Qt UI.
namespace obsviewer::theme {

constexpr uint32_t kBg = 0xFF0D1015;
constexpr uint32_t kPanel = 0xFF151A21;
constexpr uint32_t kPanelRaised = 0xFF1B2129; // "panelHi": buttons, raised areas
constexpr uint32_t kBorder = 0xFF252C36;
constexpr uint32_t kGrid = 0xFF1E252E;
constexpr uint32_t kText = 0xFFD8DEE6;
constexpr uint32_t kTextDim = 0xFF7C8794;
constexpr uint32_t kTextFaint = 0xFF4C5561;

constexpr uint32_t kGood = 0xFF3ECF6E;
constexpr uint32_t kWarn = 0xFFF2C94C;
constexpr uint32_t kBad = 0xFFEF4C4C; // "danger": program tally, ON AIR, REC

// Section accents (the 3 × 12 tab next to each section title).
constexpr uint32_t kAccentTeal = 0xFF2EC4B6;   // stream / network
constexpr uint32_t kAccentBlue = 0xFF4C8DFF;   // output, programme audio
constexpr uint32_t kAccentAmber = 0xFFF6C453;  // dynamics, gain reduction
constexpr uint32_t kAccentOrange = 0xFFFF8A3D; // record
constexpr uint32_t kAccentViolet = 0xFFA78BFA; // performance
constexpr uint32_t kAccentGreen = 0xFF8BD450;  // health
constexpr uint32_t kAccentSlate = 0xFF5B6676;  // input
constexpr uint32_t kAccentSky = 0xFF7EA9CC;

constexpr uint32_t kBlack = 0xFF000000;
constexpr uint32_t kLabelPlate = 0xCC0D1015; // translucent bg behind notes on a picture
// Label band tints for scenes that are on Program / in Preview.
constexpr uint32_t kTallyProgram = 0xFFB53434;
constexpr uint32_t kTallyPreview = 0xFF207A45;

// Qt style sheet for OBSViewer's own widgets (dock, windows, dialogs).
QString styleSheet();

} // namespace obsviewer::theme
