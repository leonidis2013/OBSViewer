#pragma once

#include <obs.h>

// Calls whose API changed between OBS 32 and 33. OBSViewer is built against
// OBS 32 (so one build loads in 32 and 33: OBS refuses plugins built for a
// newer libobs) and picks the right call at run time.
namespace obsviewer::compat {

// True when running inside OBS 33 or newer.
bool obs33OrNewer();

// Turn audio monitoring on for a source.
//   OBS 32: "Monitor Only" (heard, never placed in the mix).
//   OBS 33: monitoring is just on/off (obs_source_set_monitoring_enabled); a
//   source on no track (mixers 0) is still heard and never mixed.
void enableMonitoring(obs_source_t *source);

} // namespace obsviewer::compat
