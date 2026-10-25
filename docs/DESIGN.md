# OBSViewer — Design Specification

Version 0.1 (stage 1) · October 2026 · Owner: leonidis2013

OBSViewer is an OBS Studio plugin for macOS (Apple Silicon and Intel, one universal
build) that replaces OBS's built-in Multiview with an ATEM Mini–style monitoring view.
It adds stream and audio metrics, alerts, utilities, and later a LiveProfessor-style
channel strip that hosts VST3 and Audio Unit plugins.

---

## 1. Decisions

| Area | Decision |
|---|---|
| Approach | Native OBS plugin (C++, libobs + obs-frontend-api + Qt 6). OBS's own Multiview cannot be extended by plugins, so OBSViewer draws its own. |
| Platforms | macOS 13+, universal binary (arm64 + x86_64). Windows and Linux are out of scope. |
| Minimum OBS | 32.x (built against 32.2.2). |
| Where it shows | An OBS dock **and** one viewer window that is either windowed or fullscreen on any monitor. |
| Layouts | Dynamic (default) plus fixed presets, switchable from Settings, the right-click menu or hotkeys. Dock and window each keep their own layout; each scene view can be set to any scene. |
| Click on tiles | Off by default. When enabled: click = send scene to Preview (or cut to Program); double-click Preview = transition. |
| Theme | LiveVoice dark palette and panel style (see §6; BroadcastMaster palette until 0.5.0). |
| Loudness | LUFS / true peak measured inside OBSViewer (no dependency on BroadcastMaster). |
| Channel strip | Master bus only (per-source strip dropped). Hosts VST3 + Audio Unit in-process; scanning is out of process. Insert slots with reorder, trim, fader + meters, status on the multiview. No chain presets. |
| Product | Stays an OBS plugin (no standalone app). Private church use; not shared or sold. |
| Distribution | Public GitHub repo `leonidis2013/OBSViewer`. Each new version on main publishes a macOS `.pkg` installer. |
| Updates | The plugin checks GitHub Releases, shows release notes, downloads the `.pkg`, verifies its SHA-256, and **Restart OBS** installs it and reopens OBS. No token needed (public repo). |

---

## 2. Build stages

| Stage | Version | Contents |
|---|---|---|
| **1 — Multiview core** | 0.1.x | Plugin project, universal CI, `.pkg` installer, in-plugin updater, dock + fullscreen window, program/preview tiles, tally, labels, layout presets, stream/record status lights, settings. |
| 2 — Stream health | 0.2.x | Stream and record panels: bitrate (graph), dropped frames (network / encoder / render), congestion, uptime, reconnects, record duration and file size, multiple outputs (stream, record, virtual cam, replay buffer). |
| 3 — Audio | 0.3.x | Per-tile audio meters (peak + RMS), program LUFS (momentary, short-term, integrated) and true peak with platform targets, mute indicators, clip and silence alarms. |
| 4 — Alerts & utilities | 0.4.x | Black / frozen frame detection, clock, stream timer, countdown, CPU / render-lag readout, safe-area guides, click-to-switch, tile→scene assignment. |
| 5 — Master bus | 0.5.x | Plugin host (JUCE), master bus via a send track, insert slots, gain stages, meters, latency compensation. Per-source strip filter follows. |
| 6 — Strip ↔ multiview | 0.6.x | Master LUFS, gain reduction and chain status on the multiview; out-of-process (crash-safe) plugin mode. |

---

## 3. Architecture

```
OBS Studio (Qt main thread)                       libobs graphics thread
┌──────────────────────────────────────┐          ┌──────────────────────────┐
│ plugin-main.cpp                      │          │ obs_display draw callback│
│  ├─ frontend event callback ──► ViewerState ◄───┤  Renderer::render()      │
│  │   (scene list, program/preview,   (mutex)    │   tiles · tally · labels │
│  │    studio mode, stream/record)               │   label bands · meters   │
│  ├─ Tools ▸ OBSViewer menu                      └──────────────────────────┘
│  ├─ ViewerDock (OBS dock)  ─┐                             ▲
│  ├─ ViewerWindow (window ⇄ fullscreen) ┴─ ViewerDisplay (native view, obs_display)
│  ├─ SidePanel (health · stream · network · record · audio)
│  ├─ MasterBusWindow ─► bus::Engine (JUCE) ─► obsviewer-scan helper (one process per plugin scan)
│  ├─ SettingsDialog
│  └─ Updater (worker thread: libcurl ▸ GitHub REST)
└──────────────────────────────────────┘
```

Key rules:

- **All OBS frontend calls happen on the Qt main thread.** Frontend events update a
  shared `ViewerState` (weak references to program, preview and scene sources plus
  stream/record flags) under a mutex. The graphics thread only reads that state.
- **Labels are private `text_ft2_source` sources**, created and destroyed on the main
  thread. They are rebuilt when the scene list changes or the canvas size changes.
- **Scenes shown in tiles are marked "showing"** (`obs_source_inc_showing`) while at
  least one view is open, so their sources stay active, exactly as OBS's own
  Multiview does.
- **Program** is drawn with `obs_render_main_texture()`, so transitions are visible.
  **Preview** in Studio Mode is the preview scene; outside Studio Mode the preview tile
  shows the current scene with a "STUDIO MODE OFF" note.
- Tiles are placed in logical window pixels (`placeTiles`, pure and unit-tested) and
  drawn through a device-pixel viewport. Each picture gets its own viewport so sources
  render at full quality into exactly the tile's pixels; clicks use the same placement.

### Source tree

```
src/
  plugin-main.cpp           module load/unload, menu, events
  plugin-support.{h,c.in}   logging, name, version (from buildspec.json)
  core/Settings.*           JSON settings in OBS's plugin config folder
  core/Version.*            semantic-version compare
  viewer/Layouts.*          layout presets → tile placement for a given window size
  viewer/ViewerState.*      shared program/preview/scene/output state
  viewer/Renderer.*         draws a layout into a display
  ui/Theme.*, ui/Paint.hpp  LiveVoice palette, style sheet and painting helpers
  ui/ViewerDisplay.*        native Qt widget owning an obs_display
  ui/MultiviewPanel.*       display + side bar
  ui/SidePanel.*            header icons, health / performance, stream + network, record, audio
  ui/ViewerDock.*           dock widget (compact panel)
  ui/ViewerWindow.*         the viewer window: windowed or fullscreen, never both
  ui/Menu.*                 right-click menu, screen menus
  ui/MasterBusWindow.*      master bus channel strip and plugin browser
  bus/Engine.*              JUCE plugin host, chain, out-of-process scan driver
  bus/ScanMain.cpp          obsviewer-scan helper executable
  bus/MasterBus.*           routing through OBS tracks, persistence
  ui/SettingsDialog.*       settings and updates
  update/Updater.*          GitHub release check, download, verify, open
  update/UpdateDialog.*     release-notes dialog
  update/Keychain.*         stores the GitHub token in the macOS Keychain
tests/                      host-side unit tests (layouts, versions) run in CI
```

---

## 4. Multiview

### 4.1 Tiles (0.6.0)

- Tiles are placed in window pixels for the actual window shape (no letterboxing of a
  fixed canvas), with a 4-px gap. Each tile is the picture at the canvas aspect ratio
  plus a **label band just below it** (height = 2.8 % of the view, 14–26 px).
- Label band: panel colour with the name centred; a 3 × 12 accent tab on the left
  (red Program, green Preview, grey Preview when Studio Mode is off). A scene that is
  on Program gets a red band and a 2-px red frame; in Preview a green band and frame.
  Program and Preview tiles have no frame. Other tiles have a 1-device-pixel border.
- Scene tiles are numbered ("1   Interview"), centred. Program / Preview bands (0.6.2):
  accent tab, fixed title PROGRAM / PREVIEW (both reserve the wider title's width, so
  they line up), then the scene name left-aligned in its own darker field. "STUDIO
  MODE OFF" sits top-left on the Preview picture.
- Label size (0.6.2): text_ft2 puts every label's baseline at the font's tallest
  glyph and only grows the texture for descenders, so all labels are scaled from one
  reference height (the all-caps PROGRAM title) and top-aligned: same size and
  baseline everywhere; a name only shrinks if it doesn't fit.
- Clicks use the same placement (`placeTiles`), so a click on a label counts as the tile.
- Performance (0.6.3): in Dynamic, scene views render into a cached texture every
  other frame (staggered), Preview / Program every frame. Only the first N scenes a
  visible view shows (N = the layout's scene count; all for Dynamic) are marked
  showing; new marks are applied before old ones are removed so nothing flickers off.
  The dock hides its display while the viewer window is open ("Show Here Too"
  overrides for the session). Scene audio lists rebuild on scene item signals
  (`item_add`, `item_remove`, `item_visible`, `refresh`), on scene list / Program /
  Preview changes, and every 5 s as a safety net; between rebuilds only mute states
  are refreshed.

### 4.2 Layout presets

| Preset | Description |
|---|---|
| **Dynamic** (`auto`, default) | Preview + Program large on top, **every** scene below. Tries 81 sizes for the top pair (25–100 % of the width) and every column count for the scenes, and keeps the arrangement with the most picture (Preview / Program weighted 1.5×, scenes never larger than ¾ of the top tiles). Rows are centred; the whole block is centred vertically. |
| **ATEM** | Preview + Program on top, 4 scenes below. |
| **Classic 2 + 8** | Preview + Program on top, 8 scenes in two rows. |
| **Big Program** | Large Program (¾ width), Preview + 2 scenes stacked on the right, 4 scenes along the bottom. |
| **Scenes 3×3 / 4×4** | Nine / sixteen scenes, no Program or Preview. |

Fixed presets are built for a trial width and binary-searched to the largest width
whose height still fits, then centred. Settings written by older versions move to
Dynamic once (`settings_version` < 2).

### 4.3 Views (0.6.0)

- **Dock**: "OBSViewer" in the Docks menu, compact side bar. Header icons: Settings,
  Open in window, Open fullscreen.
- **Viewer window**: one window that is either **windowed** or **fullscreen**, never
  both. Tools ▸ OBSViewer ▸ Open Window / Open Fullscreen ▸ *screen* / Close Window.
  Opening one mode switches the existing window. Header icons: Settings,
  Fullscreen ↔ Windowed, Close. Esc leaves fullscreen; ⌘W closes. The windowed size
  and position, the fullscreen screen and the layout are remembered.
- **Fullscreen on a chosen screen (0.7.3):** macOS enters fullscreen on the screen
  the window is on, and a move requested in the same moment hasn't happened yet. So
  the window is first shown as a normal window centred on the target screen, then
  `showFullScreen()` once `screen()` reports the target (polled every 100 ms, up to
  2 s). Already fullscreen elsewhere → `showNormal()`, 0.9 s for the exit animation,
  then the same steps. Window-state changes on the way are ignored; Esc or choosing
  Window cancels.
- **Transition strip (0.8.0)**: in the Dynamic, ATEM and Classic layouts a column is
  reserved between Preview and Program (`PlaceParams::centerStrip`,
  `transitionStripWidth()` ≈ 4.5 % of the width, 52–84 px). `transitionStrip()` places
  CUT, AUTO, a progress bar and the transition name / length in it; the renderer draws
  them (rounded LiveVoice buttons from text_ft2 labels) and `ViewerDisplay` hit-tests
  the same boxes (hover = pointer cursor + lighter button; double-clicks ignored).
  AUTO lights red and the bar fills from `obs_transition_get_time()` while a
  transition runs. Outside Studio Mode the strip is dimmed and inert. Settings ▸
  `show_transitions` hides the strip (and the dock's controls); the side bar section
  of 0.7.4 is gone. Big Program and the scene grids have no strip.
- **Transition controls (0.7.4)**: controls dock (was also the side bar). CUT =
  OBS's built-in Cut transition for one take (set Cut → trigger → restore the chosen
  transition, all synchronous on the main thread); AUTO =
  `obs_frontend_preview_program_trigger_transition` with the current transition.
  Transition picker and duration mirror OBS (`obs_frontend_get/set_current_transition`,
  `…_transition_duration`); duration is disabled for Cut and fixed-length transitions.
  Outside Studio Mode: "Turn On Studio Mode". Compact side bar: buttons only.
- **Controls dock (0.7.2)**: Docks ▸ OBSViewer Controls — viewer state (closed /
  windowed / fullscreen + screen), Window · Fullscreen (toggle; arrow = screen menu) ·
  Close, the window layout, Bus ON/OFF with plugin count and gain reduction, Open Bus,
  Settings. Polls App / MasterBus state every 0.4 s while visible. Contents sit in a
  scroll area so the dock's minimum size is tiny (0.8.0) — Qt won't dock a widget
  into an area smaller than its minimum. Open Bus always opens the bus window.
- **No toolbar**: layout and tile-click action live in Settings (and layout in the
  right-click menu). Right-click: Layout, Send to Preview / Cut to Program (or Switch
  Scene), window / fullscreen / screen actions, Master Bus…, Settings…, Close.

---

## 4b. Side bar (0.6.0)

Qt-drawn, LiveVoice style, in the dock (compact, 184 px) and the viewer window (236 px).
There is no bottom bar any more; network and loudness history moved into the side bar.

- **Header** — "OBSViewer" and the icon buttons: Master bus (mixer icon, 0.6.2), then
  the view's own icons (see 4.3). Fullscreen screens in menus are ticked only while the
  viewer is fullscreen on that screen.
- **Health light** and **Performance light** — click to replace the sections with the
  full list of checks; click again to return. Only the health light can flash (red
  outline, Problem only, when "Flash the health light" is on).
- **Stream** (teal) — Start stream / ON AIR hh:mm:ss / Reconnecting… (stop asks first);
  disabled as "Stream not set up" while `obs_service_can_try_to_connect` is false (0.6.2);
  below it the network stats: upload in Mbps with peak, bitrate history for the last
  5 minutes (one bar per second: teal, amber with drops, red heavy drops /
  reconnecting), Dropped (network · encoder · render), Connection (status · uptime),
  Server (connect time · load).
- **Record** (orange) — Start recording / REC / PAUSED (stop asks first); disk free
  and hours left. Disabled as "No recording folder" while the configured path doesn't
  exist (re-checked every 10 s) (0.6.2).
- **Programme audio** (blue) — L / R sample peak and short-term loudness (ST) bars
  with LiveVoice zone colours (slate too quiet, blue quiet, green on target, amber
  hot, red over), 2-px peak hold (2 s), held values on top, target bracket behind
  ST, ceiling line across L / R; short-term, integrated, true-peak max and target.
  Click to reset. Below it the **loudness history** (5 min) against the target band.
  No warning borders around the meter or the history.

Note: "Upload" is what OBS is sending, not a speed test of the internet line.

**Health checks and thresholds**

| Check | Warning | Problem |
|---|---|---|
| Stream | a past reconnect | reconnecting now |
| Network | any drop in last 10 s, or ≥ 0.5 % overall | ≥ 30 drops in 10 s, or ≥ 2 % overall |
| Encoder | ≥ 0.5 % skipped (10 s) | ≥ 5 % |
| Rendering | ≥ 1 % lagged (10 s) | ≥ 5 % |
| CPU | ≥ 75 % | ≥ 90 % |
| Disk | < 20 GB free or < 3 h left | < 5 GB free or < 1 h left |
| Loudness | outside target ± tolerance | more than tolerance + 3 LU too loud |
| Peaks | above the true-peak ceiling | above 0 dBTP (clipping) |
| Silence | silent ≥ 10 s while not live | silent ≥ 10 s while live |

Every check appears in the health list; only the health light itself flashes, and
only for a Problem.


### Tile audio meters (stage 3, 0.3.0)

- Always shown (0.6.2); a view with no audio gets grey empty tracks. "No audio"
  (0.10.1) = no source in the scene has sent audio in the last 3 s: cameras,
  capture cards and stopped media are flagged as audio sources but send nothing.
- Two bars (L / R) at the right edge of each picture: RMS solid, peak tick; green,
  amber from −9 dBFS, red from −3 dBFS; scale −60 to 0 dBFS; 20 dB/s fall-back.
- Scene tiles: loudest unmuted source among the scene's visible audio sources
  (groups and nested scenes included). Sources set in OBS Settings ▸ Audio as global
  devices aren't part of any scene, so they show only in the programme meter.
- Preview tile: the preview scene's sources. Program tile: no meter (0.10.1); the
  side bar's Programme audio meter shows the stream mix.
- Muted: all sources in the scene muted → red meter background and a MUTED tag.
- Clip: sample peak ≥ −0.5 dBFS → red cap for 2 s.
- One OBS volume meter per audio source, shared by all tiles; created and removed as
  scenes change.


### Control and performance (0.4.0, revised 0.6.0)

- **Tile clicks:** single click on a scene tile sends it to Preview (Studio Mode) or
  cuts it to Program, per Settings ▸ Clicking a view; outside Studio Mode both switch
  the scene. Double-click the Preview tile = transition to Program. Default: To Preview.
- **Performance light:** Frame rate (warn < 97 %, problem < 90 % of target),
  Render time (warn ≥ 60 %, problem ≥ 90 % of the frame budget), Rendering
  missed frames, Encoding skipped frames, Stream / Record encoder description,
  CPU, Memory.
- **Meter track:** follows the stream's audio encoder (or the profile's stream track)
  unless a track is picked manually.

### Utilities (0.7.0)

- **Picture check:** after OBS renders its main view, every 10th frame the Program
  texture is shrunk to 32 × 18 on the GPU and staged; the previous sample is read
  back (no stall). Average luma < 0.035 counts as black; mean per-pixel change
  < 0.0008 as still. Health "Picture": black ≥ 3 s warning, ≥ 5 s while streaming or
  recording problem; unchanged ≥ 60 s while live warning (Settings can turn the
  frozen warning off).
- **Clock:** side bar section with the time of day; optional countdown to a set time
  on chosen days (0.8.1) with a label ("Service starts in 4:12", amber in the last 2
  minutes, then "+m:ss started" for an hour). More than 12 h before, it shows when
  the next start is ("Service starts Sun 10:30 AM"). `nextCountdown()`
  (core/Countdown) finds the target, across midnight too; unit-tested.
- **Scene per view:** right-click a scene view ▸ This View Shows ▸ Automatic or any
  scene (per fixed layout, by scene name). Dynamic: right-click ▸ Hide “scene”;
  Hidden Scenes ▸ Show / Show All. Renderer, clicks and the "showing" marks all use
  the same per-layout map (`ViewerState::viewMap`).
- **Safe areas:** optional action-safe (93 %, white) and title-safe (90 %, amber)
  frames and a centre mark on Program and Preview.
- **Hotkeys:** OBS ▸ Settings ▸ Hotkeys lists "OBSViewer: …" — open window,
  fullscreen on/off, close window, next layout, each window layout, master bus
  on/off, show master bus. Saved in `hotkeys.json` on exit. Stream Deck can trigger
  them with its Hotkey action.
- **Recording:** the side bar shows the file being written and its size.
- **OBS preview off (0.7.1):** while the viewer window is visible (setting
  `hide_obs_preview`, default on), OBSViewer invokes OBS's own `DisablePreview` slot
  (same as unticking Enable Preview, so OBS's state stays consistent), or in Studio
  Mode `EnablePreviewDisplay(false)` (Studio Mode keeps the preview enabled; its
  Program view can't be reached). Restored with `EnablePreview` /
  `EnablePreviewDisplay(true)` when the window closes, on Studio Mode changes
  (re-applied), and on exit before OBS saves its settings. Only what OBSViewer turned
  off is turned back on. (`obs_frontend_set_preview_enabled` isn't used: it doesn't
  update OBS's own flag, so it can't turn the preview back on.)

## 5. Settings

Stored as JSON in `~/Library/Application Support/obs-studio/plugin_config/obsviewer/config.json`.

| Key | Default | Meaning |
|---|---|---|
| `settings_version` | `3` | Config format; < 2 moves the layouts to Dynamic once, < 3 turns tile clicks off once. |
| `dock_layout` | `auto` | Layout shown in the dock. |
| `window_layout` | `auto` | Layout shown in the viewer window. |
| `window_screen` | primary | Screen for fullscreen. |
| `window_geometry` | — | Windowed size and position (Qt `saveGeometry`, base64). |
| `show_labels` | `true` | Names in a band below each view. |
| `click_action` | `0` | Tile click: 0 off, 1 to Preview, 2 to Program (reset to 0 once by 0.6.3). |
| `show_sidebar` | `true` | Right-hand side bar. |
| `alarms` | `true` | Flash the health light on a Problem. |
| `master_bus_embedded` | `true` | Master bus opens next to the side bar (else in its own window). |
| `safe_areas` | `false` | Safe-area guides on Program and Preview. |
| `hide_obs_preview` | `true` | Turn OBS's own preview off while the viewer window is open. |
| `show_transitions` | `true` | CUT / AUTO strip between Preview and Program, and in the controls dock. |
| `warn_frozen` | `true` | Health warns when Program hasn't changed for 60 s while live. |
| `show_clock` | `true` | Clock section in the side bar. |
| `countdown_on` / `countdown_minutes` / `countdown_label` | off / 630 (10:30) / "Service starts" | Countdown to a start time. |
| `countdown_days` | `127` (every day) | Days the countdown applies: bit 0 = Monday … bit 6 = Sunday. Other days show when the next start is. |
| `view_scenes` | — | Per fixed layout: scene name for each view ("" = automatic). |
| `dynamic_hidden` | — | Scenes left out of the Dynamic layout. |
| `loudness_preset` | `0` (YouTube −14) | Loudness target preset. |
| `custom_target_lufs` | `-16` | Target when the preset is Custom. |
| `meter_track` | `0` | 0 = follow the stream track, else Track 1–6. |
| `tile_meters` | `true` | Audio meters on each tile. |
| `update_auto_check` | `true` | Check for updates when OBS starts (at most once every 12 h). |
| `update_repo` | `leonidis2013/OBSViewer` | GitHub repository releases come from. |
| `update_include_prereleases` | `false` | Also offer beta / rc releases. |
| `update_skipped_version` | — | A version the user chose to skip. |

A GitHub token saved by older versions lives in the macOS Keychain; it's no longer
needed (the repository is public), is skipped if GitHub refuses it, and can be removed
in Settings ▸ Updates. Hotkey bindings are in `hotkeys.json`.

---

## 6. Theme (LiveVoice, 0.6.0)

| Token | Colour | Use |
|---|---|---|
| bg | `#0D1015` | Window and multiview background, meter tracks |
| panel | `#151A21` | Panels, label bands, empty tiles |
| panelHi | `#1B2129` | Buttons, filled insert slots |
| border | `#252C36` | 1-px panel and tile borders |
| grid | `#1E252E` | Graph grid lines |
| text / textDim / textFaint | `#D8DEE6` / `#7C8794` / `#4C5561` | Text levels |
| good | `#3ECF6E` | On target, Preview tally, bus on |
| warn | `#F2C94C` | Warnings, hot levels |
| danger | `#EF4C4C` | Program tally, ON AIR, REC, over |
| accents | teal `#2EC4B6`, blue `#4C8DFF`, amber `#F6C453`, orange `#FF8A3D`, violet `#A78BFA`, green `#8BD450`, slate `#5B6676` | Section tabs, meter zones, gain reduction |

Panels: radius 8, 1-px border; section header = 3 × 12 accent tab + bold 12.5-px title
in normal case; buttons radius 5 on panelHi.

---

## 7. Installer and updates

### 7.1 Installer

- CI builds a universal `obsviewer.plugin` and wraps it in a flat `.pkg`
  (`pkgbuild` + `productbuild`). The installer targets the **current user's home**,
  installing to `~/Library/Application Support/obs-studio/plugins/` — no admin
  password needed. It refuses to install on macOS older than 13.
- Builds are ad-hoc signed. If Apple Developer ID secrets are added to the repo later,
  CI signs and notarises the installer automatically (see README).

### 7.2 Releases

- A push to `main` whose `buildspec.json` version has no release yet (or a tag
  `vX.Y.Z`) → CI builds, packages and publishes a
  GitHub Release named `OBSViewer X.Y.Z` with `OBSViewer-X.Y.Z-macos-universal.pkg`
  and a `SHA256SUMS.txt`. Release notes come from that version's section of `CHANGELOG.md`.
- Every push to `main` also refreshes a rolling **Latest build** pre-release for
  testing; the updater ignores it unless pre-releases are enabled.

### 7.3 In-plugin updater

1. On OBS start (if auto-check is on and 12 h have passed) or from
   Tools ▸ OBSViewer ▸ Check for Updates, a worker thread calls
   `GET https://api.github.com/repos/{repo}/releases` with libcurl.
2. It picks the newest non-draft release (pre-releases only if enabled) whose version
   is greater than the running version, and finds its `.pkg` asset.
3. A dialog shows the version, release date and release notes, with
   **Download & Install**, **Skip This Version** and **Later**.
4. Download & Install fetches the asset to `~/Downloads`, shows progress, and checks
   its SHA-256 against GitHub's asset digest (or `SHA256SUMS.txt`). A mismatch
   deletes the file and stops.
5. When verified, the dialog offers **Restart OBS** (0.6.1). It writes a small script
   to the temp folder and starts it detached with OBS's pid, the package and the
   OBS.app path, then closes OBS's main window (OBS asks first if you're live). The
   script waits for OBS to exit (gives up after 15 min), runs
   `installer -pkg … -target CurrentUserHomeDirectory` (the package only installs into
   the user's home, so no admin password), falls back to opening macOS Installer if
   that fails, and opens OBS again. It logs to `~/Library/Logs/OBSViewer/update.log`.
   If OBS is still open 4 s after the close request (quit cancelled), the dialog says
   so and offers **Restart OBS** again or **Cancel Update** (kills the script).
   **Install Manually…** opens the package in macOS Installer as before.

**Private repository:** GitHub's API hides private releases from anonymous requests.
While `leonidis2013/OBSViewer` is private, the updater needs a fine-grained personal
access token with read-only *Contents* access to that one repository. It is entered
once in Settings ▸ Updates and stored in the Keychain. Making the repo (or a separate
releases-only repo) public removes this step.

---

## 8. Master bus (0.5.0, strip and scanner 0.6.0)

### 8.1 Routing

OBS has no hook on its final mix, so the bus is built from OBS's own audio tracks:

```
routed sources ──► send track (default Track 6) ──► plugin chain ──► "Master Bus" source ──► output tracks (default Track 1)
other sources  ───────────────────────────────────────────────────────────────────────────► output tracks
```

- Routing a source sets its track assignment to (original − output tracks) + send
  track. The original assignment is remembered per scene collection
  (`masterbus.json`) and restored on un-routing or when the bus is turned off.
- The bus source is a private, hidden OBS audio source on global output channel 63,
  assigned only to the output tracks (never the send track, so no feedback).
- The send track's mix arrives through a raw audio callback on OBS's audio thread,
  is processed in place, and is output **stamped for the next mix block**
  (timestamp + 1024 frames). The callback runs after OBS has mixed that block, so
  its own timestamp is already in the past. Up to 0.8.2 the bus used it (minus the
  plugin latency as a negative sync offset): OBS treated the bus as late, added
  audio buffering every tick until "Max audio buffering reached", then discarded
  its audio — the bus processed but nothing reached the output tracks. Now the bus
  is exactly one block (≈ 21 ms at 48 kHz) behind, plus plugin latency, which can't
  be pulled earlier (anything earlier than the block being mixed is dropped). The
  strip footer shows the total delay; sync offset on the bus source is 0.
- If OBS starts with the bus off but sources still routed (e.g. after a crash),
  their tracks are restored automatically.
- Track assignments are ordinary OBS settings saved in the scene collection, and
  are visible in Edit ▸ Advanced Audio Properties.
- **Stream-track check (0.8.2)**: the stream only sends one track
  (`Metrics::streamTrackIndex()`: the live audio encoder's mixer, else the profile's
  `AdvOut/TrackIndex`, Simple mode = 1). With the bus running and sources routed,
  health check "Master bus" is Good when the output mask holds that track (and it
  isn't the send track), else Warn (Bad while streaming). The strip shows a red row
  with **Fix** → `busTracksFor()`: add the stream track to the outputs; if the stream
  sends the send track, move the send to the highest free track. Changing the output
  mask now re-applies routing (routed sources = original − outputs + send), so a
  newly added output track never carries a routed source dry as well.

### 8.0 YouTube stream title (0.10.0)

`StreamTitle` (src/stream) runs a fetch on a worker thread every 60 s while
streaming, every 3 min otherwise, and 3 s after the stream starts:
1. `broadcast_id` in the streaming service's settings (OBS sets it when connected to
   a YouTube account and using Manage Broadcast) → `youtube.com/oembed` → title
   (public / unlisted only).
2. Settings `youtube_channel` → `youtubeLiveUrl` (`@handle`, `UC…` id or channel
   URL → `…/live`) → the page's canonical `watch?v=` id → its `og:title`
   (core/YouTube, unit-tested). No watch id = no live or upcoming stream.
3. The profile's `[YouTube] Title` (Manage Broadcast's remembered title), for a
   YouTube service only.
`ViewerState::setStreamTitle` builds a text label; the renderer then reserves a
banner (`titleBannerHeight`: label band + 6 px) above the tiles and draws it like a
Program title band ("YOUTUBE" + title field, red tab while streaming).
`ViewerDisplay` shifts its hit-test tiles by the same amount. Settings:
`show_stream_title` (true), `youtube_channel` ("").

### 8.1b Routing window (0.9.0)

The strip's **Routing** button opens `RoutingDialog` (one shared, non-modal window):
`RoutingDiagram` paints sources (left), the bus box (input track, plugin chain with
bypass state, outputs, ON/OFF), and Tracks 1–6 (right, tagged STREAM from
`streamTrackIndex`, REC from the profile's `AdvOut/RecTracks` or Track 1 in Simple
mode, BUS IN). Wires are cubic curves: blue = source → bus and bus → output tracks
(dashed if the bus isn't running), grey = un-routed source → each of its tracks.
Clicking a source toggles `setRouted`; clicking a track toggles it in the output mask.
Below it: All / None, bus input track, output checkboxes. Refreshed every 0.5 s from a
signature of the routing state. Replaces the 0.6 Inputs popup.

### 8.1a Listen to stream (0.8.2)

`StreamMonitor`: raw audio callback on the stream track (final mix, bus included)
→ a private source on no track (`mixers = 0`) with monitoring "Monitor Only", on
global output channel 62 (OBS's monitor only plays active sources). It plays through
OBS's monitoring device; monitor-only audio is never placed in the mix. Follows the
stream track (polled twice a second); off at OBS start. Toggles: **Listen to
Stream** in the bus strip and under Programme audio in the side bar. Latency: one or
two audio ticks plus the device buffer.

### 8.2 Plugin host

- JUCE 8 (`juce_audio_processors`), VST3 + Audio Unit, compiled into a separate
  static library (`obsviewer_bus`) with Objective-C ARC off; the rest of the plugin
  never includes JUCE. JUCE runs as a guest on OBS's run loop, exactly as plug-ins
  do inside a DAW (`JUCE_STANDALONE_APPLICATION=0`).
- Plugins with other channel layouts get a wider scratch buffer; stereo is
  requested first.
- Audio-thread safety: only the main thread changes the chain, under a lock held
  for a few vector operations; plugins are created, prepared and destroyed outside
  the lock. The audio thread holds the lock while processing; UI reads take none.
- Persistence: chain (plugin id, bypass, slot gains, plugin state as base64) and
  master trim / fader in `masterbus.json`, restored at start-up.

### 8.3 Out-of-process scanning (0.6.0)

Scanning instantiates every plugin on the system, and some crash while being
inspected (0.5.0: a Waves shell took OBS down inside `AudioComponentInstanceNew`).

- `obsviewer-scan` is a small executable built from the same static library and
  shipped at `obsviewer.plugin/Contents/Helpers/obsviewer-scan` (universal, signed with
  `scan-entitlements.plist`: library validation off, JIT / unsigned memory allowed,
  so third-party plugins load). It is copied in before Xcode signs the bundle.
- OBS lists candidates without loading them (`searchPathsForPlugins`: `.vst3` bundles,
  registered AU components; AU instruments, MIDI effects and generators skipped), then
  launches the helper once per file: `obsviewer-scan <format> <file-or-id> <result>`.
- The helper writes the plugin descriptions as XML to the result file only after a
  clean scan. No file (crash, error) or 45 s timeout (helper killed) → the file is
  blacklisted and listed as "failed" in the browser until **Rescan all**.
- Polling is non-blocking (30 ms timer), so OBS stays responsive. If the helper is
  missing the scan stops; it never falls back to scanning inside OBS.
- Loading a plugin into the chain still happens inside OBS. A marker file names the
  plugin being created; if OBS dies there, the next start removes that plugin from
  the list and says so, so a saved chain can't crash OBS on every launch.

### 8.4 Channel strip window (0.6.0)

Vertical strip (LiveProfessor / Waves style) on the left, plugin browser on the right.

- **Top**: ON / OFF switch and status; **Inputs** button → popup with the source list
  (tick = through the bus), All / None, bus input track and output tracks.
- **Inserts**: at least 8 slots (always one empty). Filled slot: number, power dot
  (click = bypass; hollow = bypassed), name and maker, gain reduction in amber, thin
  output level along the bottom. Click opens the plugin window; drag onto another slot
  to reorder; right-click: Open, Bypass, Replace With ▸, Move Up / Down, Reset Slot
  Gains (only if set by 0.5.0), Remove. Empty slot: click for a maker ▸ plugin menu.
- **Input trim** knob (±24 dB; drag, scroll, double-click = 0).
- **Output**: fader (−60 … +12 dB, unity at ¾ height; drag, Shift = fine, scroll,
  double-click = 0), stereo output meters with zone gradient, 2-s peak hold and clip
  lights, total gain reduction (0–24 dB, hanging from the top), readouts for fader,
  peak and GR. Footer: tracks in / out and latency.
- **Browser**: search, maker ▸ plugin tree, Insert (after the selected slot, or at the
  end; double-click does the same), Scan, Rescan all, progress, failed plugins.

### 8.4b Embedded strip (0.6.2)

- `MasterBusPanel` is the strip (+ browser in a window). The viewer and the dock can
  show it as a third column right of the side bar (strip only, 272 px; plugins go in
  from an empty slot's menu). Header: pop out (→ window, setting off) and hide.
  The window's header has dock-in (→ setting on, shown in the viewer, or the dock, or a
  newly opened viewer window).
- Setting `master_bus_embedded` (default on) decides where the mixer icon, Tools ▸
  Master Bus… and right-click ▸ Master Bus… open it.
- Several panels can be open; each compares a signature of the bus (on/off, routing,
  tracks, plugin list, slots and bypass) on its meter tick and refreshes when another
  view changed it. Trim and fader follow unless being dragged.

### 8.4c Loading, saving and sync (0.6.3)

- Plugins are created with `createPluginInstanceAsync` (out-of-process AUv3 need the
  run loop free); the slot is inserted when the instance arrives. A generation
  counter drops instances that arrive after the chain was cleared.
- The saved chain loads 2 s after OBS finishes loading, one plugin per event-loop
  turn. While it loads, saving writes the saved chain back unchanged, so a quit in
  that window can't lose plugins.
- Fader, trim and bypass changes save 0.8 s after the last change (`saveSoon`).
- The chain latency is re-read every 0.5 s (shown in the strip footer as the bus delay).
- Panels detect changes from an engine revision counter plus the routing settings,
  run their meter timer only while shown, and the mixer icon shows bus status (green
  running, amber ≥ 1 dB reduction).

### 8.5 Decided not to build

- OBSViewer stays an OBS plugin (no standalone app).
- No per-source channel strip; the master bus is the only processing.
- Running the plugins themselves out of process.
- Licensing: JUCE is AGPLv3 / commercial; OBSViewer is for private (church) use, so
  no licence is needed. Revisit only if it is ever shared.

## 9. Testing

- **CI unit tests** (Linux, every push): tile placement for every preset across
  window shapes (inside the view, no overlaps, picture aspect, label band under the
  picture, scene counts, fills the view), Dynamic layout (all scenes shown, smaller
  than the top pair, below it), hit testing, version comparison / release selection.
- **CI build** (macOS 26 runner, Xcode 26.5 SDK — required by OBS 32.2): universal Release build, `lipo` check that both
  architectures are present in the plugin and in `obsviewer-scan`, helper entitlements
  printed, bundle signature verified, `.pkg` creation.
- **Manual checklist per release** (Apple Silicon and Intel):
  dock opens and renders; window ↔ fullscreen toggle on each monitor; tally follows cuts and Studio
  Mode; plugin scan survives a crashing plugin; labels follow scene renames; layout switching; settings persist; update
  check with and without a token; installer upgrades an existing install.
