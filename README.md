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

- **Dock:** pick a layout from the toolbar, open fullscreen on any screen, or open
  Settings. Right-click the multiview for the same options.
- **Fullscreen:** Esc or double-click closes it. Right-click to change layout or move
  it to another screen.
- **Tally:** red = on Program, green = on Preview (Studio Mode). Scene tiles show the
  first scenes in your scene list, in order.

## Master bus

Tools ▸ OBSViewer ▸ **Master Bus…** (or right-click the multiview).

1. Click **Scan for plugins** once. It finds VST3 and Audio Unit effects on your Mac.
2. Use **Add plugin** to build your chain (top to bottom). **Edit** opens a plugin's
   own window. Each slot has on / bypass, input trim, output gain and a meter.
3. Turn **Master bus on**, then tick the sources that should go through it (or
   **Route all**).

How it works: ticked sources are moved off your output track (Track 1, what the
stream and recording send) onto a spare track (Track 6 by default). OBSViewer
processes that track through your chain and puts the result back on Track 1.
Unticking a source, or turning the bus off, restores its tracks exactly. Don't use
the bus input track for anything else. Plugin latency is compensated automatically.

## Updates

OBSViewer checks for a new version when OBS starts (at most every 12 hours), or
whenever you choose **Tools ▸ OBSViewer ▸ Check for Updates**. If one is available
you'll see its release notes and can **Download & Install**: the installer is saved to
your Downloads folder, its SHA-256 checksum is verified, and it opens in macOS
Installer. Quit OBS before clicking Install, then reopen OBS.

**While this repository is private**, GitHub only shows its releases to signed-in
requests, so the updater needs a token:

1. On GitHub: Settings ▸ Developer settings ▸ Personal access tokens ▸
   **Fine-grained tokens** ▸ Generate new token.
2. Repository access: **Only select repositories** ▸ `OBSViewer`.
   Permissions: **Contents: Read-only**. Nothing else.
3. In OBS: Tools ▸ OBSViewer ▸ Settings ▸ Updates ▸ paste it into **GitHub token**,
   then OK. It's stored in your macOS Keychain.

Making the repository public (or publishing releases from a separate public repo and
changing **Repository** in Settings) removes the need for a token.

## Build

Builds run on GitHub Actions (`.github/workflows/build.yml`):

- Every push runs the unit tests and a universal macOS build, and refreshes the
  **Latest build (main)** pre-release with the installer.
- Publishing a release (below) creates **OBSViewer X.Y.Z** with the installer and
  `SHA256SUMS.txt`. The release notes come from that version's section in
  [CHANGELOG.md](CHANGELOG.md). Tags like `v0.2.0-beta.1` publish pre-releases.

### Releasing a new version

1. Add a `## [X.Y.Z]` section to `CHANGELOG.md` and set `"version"` in
   `buildspec.json`, then push to `main`.
2. Publish it, either way:
   - On GitHub (works from the GitHub mobile app): **Actions ▸ Build OBSViewer ▸
     Run workflow**, tick **Publish a release**, and run it on `main`; or
   - from a computer: `git tag vX.Y.Z && git push origin vX.Y.Z`.
3. Installed copies offer the update on their next check.

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
