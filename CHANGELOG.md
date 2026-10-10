# Changelog

## [0.2.0] — 2026-10-10

New layout with stream, network and audio monitoring.

- Slimmer look: 1-pixel tile borders, 2-pixel tally borders, narrow gaps and smaller labels. Layouts now fill the space instead of leaving room for the old status strip.
- Right-hand side bar:
  - Health light. Click it to see every check (stream, network, encoder, rendering, CPU, disk, loudness, peaks, silence) with its status.
  - Start / stop stream and recording, with status and running time. Stopping asks first.
  - Programme audio meter: left / right peak, short-term loudness against your target band, integrated loudness, true-peak max. Click the meter to reset.
- Bottom network bar: upload rate, 5-minute bitrate history (amber / red where frames dropped or the stream reconnected), dropped frames (network, encoder, render), connection state, uptime and server load, and a 5-minute loudness history against the target.
- Alarms: sections flash amber or red when they need attention (dropped frames, reconnects, overload, low disk, clipping, silence).
- Settings: loudness target presets (YouTube −14, Apple −16, EBU R128 −23, ATSC −24 and more, same list as BroadcastMaster), which audio track to measure, and switches for the side bar, network bar and alarms.

## [0.1.1] — 2026-10-10

Fixes from the first test on macOS.

- Settings and other Tools ▸ OBSViewer items now open reliably. The menu used to rebuild itself as it opened, which made clicks get lost on macOS.
- Fullscreen now works. The window is now a normal window switched to fullscreen, the same way OBS's own projectors work; macOS refuses to make a borderless window fullscreen.
- Menu actions now run after the menu closes, so macOS doesn't ignore them.
- More detail in the OBS log (opening Settings, opening fullscreen and what macOS did) to make problems easier to trace.

## [0.1.0] — 2026-10-09

First build (stage 1: multiview core).

- OBSViewer dock and fullscreen multiview window (any screen).
- Program and Preview tiles; Program shows transitions live.
- Red / green tally borders that follow cuts and Studio Mode.
- ATEM-style tile labels with scene numbers and names.
- Five layouts: ATEM (2 + 4 + status), Classic (2 + 8), Big Program, Scenes 3 × 3, Scenes 4 × 4.
- Status strip with ON AIR and REC lamps and the current Program scene.
- Settings dialog (layouts, labels, updates).
- macOS universal installer (Apple Silicon + Intel), no admin password needed.
- In-plugin updater: checks GitHub Releases, shows release notes, downloads and verifies the installer, then opens it.
