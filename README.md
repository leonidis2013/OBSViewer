# OBSViewer

An ATEM Mini–style multiview for OBS Studio on macOS, with tally, labels and stream
status. Stream health, audio metering, alerts and a VST3 / Audio Unit channel strip
follow in later stages — see [docs/DESIGN.md](docs/DESIGN.md).

- **macOS 13 or newer**, Apple Silicon and Intel (one universal installer)
- **OBS Studio 32** or newer

## Install

1. Open the repository's **Releases** page and download
   `OBSViewer-<version>-macos-universal.pkg` from the newest release.
2. Quit OBS, then open the `.pkg`. If macOS says it can't verify the developer,
   right-click the file ▸ **Open**, or allow it in System Settings ▸ Privacy & Security.
3. Click through the installer. It installs for your user only, into
   `~/Library/Application Support/obs-studio/plugins/`, so no admin password is needed.
4. Start OBS. Open **Docks ▸ OBSViewer**, or **Tools ▸ OBSViewer ▸ Open Fullscreen**.

To uninstall, quit OBS and delete
`~/Library/Application Support/obs-studio/plugins/obsviewer.plugin`.

## Use

- **Controls:** Docks ▸ **OBSViewer Controls** is a small remote: Window, Fullscreen
  (pick the screen with the arrow), Close, layout, master bus on/off, Settings.
- **Dock:** Docks ▸ OBSViewer. The icons at the top of the side bar open Settings,
  open the viewer in a window, or open it fullscreen.
- **Window or fullscreen:** Tools ▸ OBSViewer ▸ Open Window, or Open Fullscreen ▸
  *screen*. It's one window: the corner icon (or Esc) switches between windowed and
  fullscreen, so you never have both. ⌘W or the ✕ icon closes it.
- **Layouts:** Settings ▸ Window layout / Dock layout, or right-click ▸ Layout.
  **Dynamic** (the default) puts Preview and Program on top and every scene below,
  sized to fill the window.
- **Switching scenes:** off by default. Turn it on in Settings ▸ Clicking a view: a
  click then sends a scene to Preview (or cuts it to Program). Double-click Preview to
  take it to Program.
- **Tally:** a red name band = on Program, green = in Preview (Studio Mode).
- **Scene per view:** right-click a scene view ▸ This View Shows to pick its scene; in
  Dynamic, right-click ▸ Hide a scene.
- **Hotkeys:** OBS ▸ Settings ▸ Hotkeys ▸ "OBSViewer: …" (also from a Stream Deck).
- **Clock:** Settings ▸ Clock sets a daily countdown (e.g. "Service starts").
- **Side bar:** health and performance lights (click for details), Start stream with
  upload, bitrate history, dropped frames and connection, Start recording with disk
  space, and the programme loudness meter with its 5-minute history.

## Master bus

Click the **mixer icon** at the top of the side bar (or Tools ▸ OBSViewer ▸ **Master
Bus…**). The bus is a channel strip, like LiveProfessor or Waves. It opens right next
to the side bar; its pop-out icon moves it to its own window with a plugin browser,
and the window's dock-in icon puts it back.

1. Click **Scan** once. It finds the VST3 and Audio Unit effects on your Mac. Each
   plugin is checked in a separate helper, so a plugin that crashes can't take OBS
   down; it's listed as failed instead.
2. Build the chain: click an empty slot and pick a plugin, or select one in the
   browser and click **Insert**. Click a slot to open the plugin, click its dot to
   bypass it, drag slots to reorder, right-click to replace or remove.
3. Switch the bus **ON**, then click **Inputs** and tick the sources that should go
   through it.
4. Set the **input trim** and the **output fader**; the meters show the output level
   and how much the chain is reducing.

How it works: ticked sources are moved off your output track (Track 1, what the
stream and recording send) onto a spare track (Track 6 by default). OBSViewer
processes that track through your chain and puts the result back on Track 1.
Unticking a source, or turning the bus off, restores its tracks exactly. Don't use
the bus input track for anything else. Plugin latency is compensated automatically.

## Updates

OBSViewer checks for a new version when OBS starts (at most every 12 hours), or
whenever you choose **Tools ▸ OBSViewer ▸ Check for Updates**. If one is available
you'll see its release notes and can **Download & Install**: the installer is saved to
your Downloads folder and its SHA-256 checksum is verified. Then click **Restart OBS**:
OBS closes, the update installs, and OBS opens again on its own (OBS asks first if
you're streaming or recording). **Install Manually…** opens the installer instead if
you prefer. What the restart did is logged in `~/Library/Logs/OBSViewer/update.log`.

The repository is public, so no GitHub token is needed. (If an older version saved
one, Settings ▸ Updates ▸ Remove Saved Token deletes it.)

## Build

Builds run on GitHub Actions (`.github/workflows/build.yml`):

- Every push runs the unit tests and a universal macOS build, and refreshes the
  **Latest build (main)** pre-release with the installer.
- A new version number publishes **OBSViewer X.Y.Z** with the installer and
  `SHA256SUMS.txt` (see below). Versions like `0.6.0-beta.1` publish pre-releases.

### Releasing a new version

Add a `## [X.Y.Z]` section to `CHANGELOG.md`, set `"version"` in `buildspec.json`,
and push to `main`. The build publishes **OBSViewer X.Y.Z** automatically (with that
changelog section as its notes), and installed copies offer it on their next update
check. Pushes that don't change the version only refresh **Latest build (main)**.

### Building locally (optional)

Requires Xcode 26.5+ and CMake 3.28+:

```sh
cmake --preset macos
cmake --build build_macos --config Release
cmake --install build_macos --config Release --prefix "$PWD/release/Release"
```

### Signing (optional)

Builds are ad-hoc signed. With an Apple Developer ID you can sign and notarise the
installer so macOS opens it without warnings; that is not set up yet.

## Licence

GPL-2.0-or-later, like OBS Studio. See [LICENSE](LICENSE).
