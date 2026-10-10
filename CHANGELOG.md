# Changelog

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
