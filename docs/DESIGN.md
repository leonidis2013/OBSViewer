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
| Where it shows | Both: an OBS dock **and** a fullscreen window on any monitor. |
| Layouts | Several presets, switchable live from the dock, the fullscreen window's right-click menu, or Settings. Dock and fullscreen window each keep their own layout. |
| Click on tiles | Off by default. When enabled: click = send scene to Preview, double-click = cut to Program. |
| Theme | BroadcastMaster dark palette (see §6). |
| Loudness | LUFS / true peak measured inside OBSViewer (no dependency on BroadcastMaster). |
| Channel strip | Master bus **and** per-source strip. Hosts VST3 + Audio Unit. Plugins run in-process first; a sandboxed mode comes later. Insert slots with reorder, gain stages + meters, multiview integration. Chain presets are not in scope. |
| Distribution | Private GitHub repo `leonidis2013/OBSViewer`. Each tagged release publishes a macOS `.pkg` installer. |
| Updates | The plugin checks GitHub Releases, shows release notes, downloads the `.pkg`, verifies its SHA-256, and opens the installer. |

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
│  │    studio mode, stream/record)               │   status strip           │
│  ├─ Tools ▸ OBSViewer menu                      └──────────────────────────┘
│  ├─ ViewerDock (OBS dock)  ─┐                             ▲
│  ├─ FullscreenWindow        ├─ ViewerDisplay (native view, obs_display)
│  ├─ SettingsDialog          │
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
- Rendering happens in canvas coordinates (OBS base resolution). Each tile gets its own
  viewport so sources render at full quality into exactly the tile's pixels.

### Source tree

```
src/
  plugin-main.cpp           module load/unload, menu, events
  plugin-support.{h,c.in}   logging, name, version (from buildspec.json)
  core/Settings.*           JSON settings in OBS's plugin config folder
  core/Version.*            semantic-version compare
  viewer/Layouts.*          layout presets (normalised tile rectangles)
  viewer/ViewerState.*      shared program/preview/scene/output state
  viewer/Renderer.*         draws a layout into a display
  ui/Theme.*                BroadcastMaster palette for Qt and the renderer
  ui/ViewerDisplay.*        native Qt widget owning an obs_display
  ui/ViewerDock.*           dock widget: toolbar + display
  ui/FullscreenWindow.*     borderless fullscreen view on a chosen screen
  ui/SettingsDialog.*       settings and updates
  update/Updater.*          GitHub release check, download, verify, open
  update/UpdateDialog.*     release-notes dialog
  update/Keychain.*         stores the GitHub token in the macOS Keychain
tests/                      host-side unit tests (layouts, versions) run in CI
```

---

## 4. Multiview

### 4.1 Tiles

- 16:9 tiles sized to the OBS canvas aspect ratio; letterboxed to fit the window.
- Border colours: **red** = on Program, **green** = on Preview (Studio Mode), neutral
  otherwise. Active tally borders are drawn twice as thick as idle borders.
- Labels sit bottom-centre on a dark translucent plate, ATEM-style. Scene tiles are
  numbered ("1  Interview"). Program and Preview tiles are labelled PROGRAM / PREVIEW
  and show the scene name.
- Scene tiles map to the first N scenes in OBS's scene list (manual assignment arrives
  in stage 4). Unused tiles render as empty panels.

### 4.2 Layout presets (stage 1)

| Preset | Description |
|---|---|
| **ATEM** (default) | Preview + Program across the top half, 4 scene tiles below, status strip along the bottom. |
| **Classic 2 + 8** | Preview + Program on top, 8 scene tiles in two rows (matches OBS). |
| **Big Program** | Large Program (¾ width), Preview + 2 scenes in a right column, 4 scenes along the bottom. |
| **Scenes 3×3** | Nine scene tiles with tally borders, no Program/Preview. |
| **Scenes 4×4** | Sixteen scene tiles with tally borders. |

Layouts are data (normalised rectangles), so new presets need no rendering changes.

### 4.3 Status strip (ATEM preset)

Stage 1 shows ON AIR (stream) and REC indicators and the current Program scene.
Stage 2 fills the strip with bitrate, dropped frames, uptime and record time; stage 3
adds program loudness.

### 4.4 Views

- **Dock**: "OBSViewer" in the Docks menu. A slim toolbar holds the layout picker, a
  fullscreen button (choose a screen) and a settings button.
- **Fullscreen window**: Tools ▸ OBSViewer ▸ Open Fullscreen ▸ *screen*. Esc or
  double-click closes it (double-click becomes "cut" only when click-to-switch is on
  and the click lands on a tile). Right-click opens a menu: layout, screen, close.
  The chosen screen and layout are remembered.

---

## 4b. Monitoring bars (0.2.0)

The multiview is surrounded by two Qt-drawn bars (crisp text, clickable controls),
shown in both the dock (compact) and the fullscreen window.

**Side bar (right)**
- Health light — overall Good / Warning / Problem. Click to replace the bar with the
  full list of checks; click again to return.
- Stream — Start stream / ON AIR hh:mm:ss / Reconnecting…; bitrate, % dropped,
  reconnects. Stopping asks for confirmation.
- Record — Start recording / REC hh:mm:ss / PAUSED; free disk space and hours left
  at the current recording bitrate. Stopping asks for confirmation.
- Programme audio — L/R sample peak (dBFS) with the true-peak ceiling marked;
  short-term loudness bar coloured against the target band (blue below, green on
  target, amber/red above); integrated LUFS, true-peak max, target name. Click to
  reset integrated and peak hold. Measures one OBS mix track (Track 1 by default)
  with the BS.1770 meter shared with BroadcastMaster.

**Network bar (bottom)**
- Upload — current stream bitrate to the server (Mbps).
- Bitrate · last 5 min — one bar per second; green normal, amber dropped frames,
  red heavy drops or reconnecting; dashed average line.
- Dropped — network, encoder (skipped) and render (lagged) frame counts.
- Connection — connected / reconnects / reconnecting, uptime, connect time and
  server load (congestion).
- Loudness · last 5 min — short-term LUFS line against the target ±tolerance band.

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

Sections pulse amber or red for their checks (Settings ▸ "Flash sections that need
attention"). Loudness simply drifting off target is shown but doesn't flash.


### Tile audio meters (stage 3, 0.3.0)

- Two bars (L / R) at the right edge of each picture: RMS solid, peak tick; green,
  amber from −9 dBFS, red from −3 dBFS; scale −60 to 0 dBFS; 20 dB/s fall-back.
- Scene tiles: loudest unmuted source among the scene's visible audio sources
  (groups and nested scenes included). Sources set in OBS Settings ▸ Audio as global
  devices aren't part of any scene, so they show only in the programme meter.
- Preview tile: the preview scene's sources. Program tile: the measured stream mix.
- Muted: all sources in the scene muted → red meter background and a MUTED tag.
- Clip: sample peak ≥ −0.5 dBFS → red cap for 2 s.
- One OBS volume meter per audio source, shared by all tiles; created and removed as
  scenes change.


### Control and performance (0.4.0)

- **Toolbar** in the dock and the fullscreen window: layout, tile-click action
  (Off / To Preview / To Program), screens, Settings, Exit (fullscreen only). The
  fullscreen toolbar can be hidden (right-click ▸ Show Toolbar, or Settings).
- **Tile clicks:** single click on a scene tile sends it to Preview (Studio Mode) or
  cuts it to Program, per the setting; outside Studio Mode both switch the scene.
  Double-click the Preview tile = transition to Program. Default: To Preview.
- **Right-click menu:** Layout, Send to Preview / Cut to Program (or Switch Scene),
  Clicking a Tile, screens, Show Toolbar, Settings, Exit Fullscreen.
- **Auto layout:** Preview + Program on top (half width each), every scene below,
  4–12 per row (fewest columns that fit the lower half), short rows centred.
- **Program / Preview tiles** have no tally box (labels say which is which).
- **Performance button:** Frame rate (warn < 97 %, problem < 90 % of target),
  Render time (warn ≥ 60 %, problem ≥ 90 % of the frame budget), Rendering
  missed frames, Encoding skipped frames, Stream / Record encoder description,
  CPU, Memory.
- **Meter track:** follows the stream's audio encoder (or the profile's stream track)
  unless a track is picked manually.

## 5. Settings

Stored as JSON in `~/Library/Application Support/obs-studio/plugin_config/obsviewer/config.json`.

| Key | Default | Meaning |
|---|---|---|
| `dock_layout` | `atem` | Layout shown in the dock. |
| `window_layout` | `atem` | Layout shown in the fullscreen window. |
| `window_screen` | primary | Screen name for the fullscreen window. |
| `show_labels` | `true` | Draw tile labels. |
| `click_action` | `1` | Tile click: 0 off, 1 to Preview, 2 to Program. |
| `fullscreen_toolbar` | `true` | Toolbar in the fullscreen view. |
| `show_sidebar` | `true` | Right-hand side bar. |
| `show_netbar` | `true` | Bottom network bar. |
| `alarms` | `true` | Flash sections that need attention. |
| `loudness_preset` | `0` (YouTube −14) | Loudness target preset. |
| `custom_target_lufs` | `-16` | Target when the preset is Custom. |
| `meter_track` | `0` | 0 = follow the stream track, else Track 1–6. |
| `tile_meters` | `true` | Audio meters on each tile. |
| `update_auto_check` | `true` | Check for updates when OBS starts (at most once every 12 h). |
| `update_repo` | `leonidis2013/OBSViewer` | GitHub repository releases come from. |
| `update_include_prereleases` | `false` | Also offer beta / rc releases. |
| `update_skipped_version` | — | A version the user chose to skip. |

The GitHub token is **not** stored in this file; it lives in the macOS Keychain.

---

## 6. Theme (shared with BroadcastMaster)

| Token | Colour | Use |
|---|---|---|
| bg | `#0E1115` | Multiview background, window background |
| panel | `#161A20` | Status strip, empty tiles, label plates (translucent) |
| panelRaised | `#1E242C` | Toolbar, buttons |
| border | `#2A323D` | Idle tile border |
| text | `#E9EDF2` | Labels |
| textDim | `#8794A5` | Secondary text |
| accent / bad | `#E5484D` | Program tally, ON AIR, REC |
| good | `#3ECF8E` | Preview tally |
| warn | `#F5A524` | Warnings (stage 2+) |

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
5. The verified `.pkg` opens in macOS Installer. The dialog tells the user to quit OBS
   before finishing, then reopen it to load the new version.

**Private repository:** GitHub's API hides private releases from anonymous requests.
While `leonidis2013/OBSViewer` is private, the updater needs a fine-grained personal
access token with read-only *Contents* access to that one repository. It is entered
once in Settings ▸ Updates and stored in the Keychain. Making the repo (or a separate
releases-only repo) public removes this step.

---

## 8. Master bus (0.5.0)

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
  is processed in place, and is output with the original timestamp. OBS adds one
  audio tick of buffering (≈ 20 ms) the first time; plugin latency is compensated
  with a negative sync offset on the bus source.
- If OBS starts with the bus off but sources still routed (e.g. after a crash),
  their tracks are restored automatically.
- Track assignments are ordinary OBS settings saved in the scene collection, and
  are visible in Edit ▸ Advanced Audio Properties.

### 8.2 Plugin host

- JUCE 8 (`juce_audio_processors`), VST3 + Audio Unit, compiled into a separate
  static library (`obsviewer_bus`) with Objective-C ARC off; the rest of the plugin
  never includes JUCE. JUCE runs as a guest on OBS's run loop, exactly as plug-ins
  do inside a DAW (`JUCE_STANDALONE_APPLICATION=0`).
- Serial chain of slots: on / bypass (`processBlockBypassed`), input trim and
  output gain ±12 dB, pre / post peak meters and gain-reduction readout, plugin
  editor in a floating window (generic editor if the plugin has none), move up /
  down, remove. Master input trim / output gain with meters.
- Plugins with other channel layouts get a wider scratch buffer; stereo is
  requested first.
- Audio-thread safety: only the main thread changes the chain, under a lock held
  for a few vector operations; plugins are created, prepared and destroyed outside
  the lock. The audio thread holds the lock while processing; UI reads take none.
- Scanning: JUCE `PluginDirectoryScanner`, one plugin per UI tick with progress; a
  crash marker skips a plugin that crashed during the previous scan. The list is
  cached in `plugin_config/obsviewer/bus/plugins.xml`.
- Persistence: chain (plugin id, bypass, gains, plugin state as base64) and master
  gains in `masterbus.json`, restored at start-up.

### 8.3 Not yet

- Per-source channel strip (an OBS filter using the same host).
- Out-of-process plugins (a crashing plugin can take OBS down with it).
- Multiview integration (bus status and gain reduction on the side bar).
- Licensing: JUCE is AGPLv3 / commercial; fine for private use, settle before any
  public release.

## 9. Testing

- **CI unit tests** (Linux, every push): layout geometry (tiles inside canvas, no
  overlaps, aspect ratio) and version comparison / release selection.
- **CI build** (macOS 26 runner, Xcode 26.5 SDK — required by OBS 32.2): universal Release build, `lipo` check that both
  architectures are present, `.pkg` creation.
- **Manual checklist per release** (Apple Silicon and Intel):
  dock opens and renders; fullscreen on each monitor; tally follows cuts and Studio
  Mode; labels follow scene renames; layout switching; settings persist; update
  check with and without a token; installer upgrades an existing install.
