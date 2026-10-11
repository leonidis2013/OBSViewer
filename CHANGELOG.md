# Changelog

## [0.6.2] — 2026-10-10

- **Master bus in the viewer:** a new mixer icon at the top of the side bar opens the master bus strip right next to the side bar, in the dock or the viewer window. Its pop-out icon moves it to its own window (with the plugin browser); in the window, the dock-in icon puts it back beside the side bar. Your choice is remembered (also in Settings).
- **Preview and Program titles stay put:** "PREVIEW" and "PROGRAM" are fixed titles, and the scene name sits in its own field beside them, so nothing shifts when scenes change.
- **All view names are the same size.** Names with letters like g or y were drawn smaller before.
- **Audio meters show on every view.** A view without audio shows a greyed-out meter.
- **Start stream / Start recording are greyed out until OBS is set up:** "Stream not set up" until a service and key are set, and "No recording folder" while the recording path doesn't exist.
- **Fullscreen screens are only ticked while the viewer really is fullscreen** on that screen (Tools menu and right-click menu).


## [0.6.1] — 2026-10-10

- **Updates now restart OBS for you.** After an update downloads and is verified, click **Restart OBS**: OBS closes, the update installs (no password needed), and OBS opens again with the new version. This replaces "Quit OBS". If you're streaming or recording, OBS still asks before closing; if you cancel, the dialog lets you try again or cancel the update. **Install Manually…** opens the installer the old way.
- Takes effect from the next update after 0.6.1 (the dialog that runs is the one from the version you have installed).


## [0.6.0] — 2026-10-10

Cleaner multiview, a proper window mode, and a channel-strip master bus.

- **Fixed: OBS no longer crashes while scanning plugins.** Each plugin is now checked in a separate helper process. A plugin that crashes or hangs during the scan is skipped and listed as "failed" instead of taking OBS down. "Rescan all" gives failed plugins another try. If OBS ever quits while a plugin is being loaded into the chain, that plugin is skipped next time instead of crashing OBS again.
- **New default layout, Dynamic:** Preview and Program large on top, every scene in your collection below, arranged to fill the window whatever its shape. Existing setups switch to it once; pick another layout in Settings or by right-clicking.
- **Names sit just below each view** in a slim band instead of over the picture. A scene on Program gets a red band, one in Preview a green band.
- **No more toolbar.** Layout and what clicking a view does are in Settings (layout is also in the right-click menu). Small icons at the top of the side bar open Settings, switch window / fullscreen and close.
- **Window mode:** Tools ▸ OBSViewer ▸ Open Window. It is the same window as fullscreen: the icon (or Esc) switches between the two, so both can never be open at once. Size and position are remembered.
- **Network moved into the side bar**, under Start stream: upload, 5-minute bitrate history, dropped frames, connection and server. The bottom bar is gone; the loudness history now sits under the audio meter.
- **No warning borders** around the audio meter or the loudness history. Only the health light flashes, and only for a real problem.
- **LiveVoice look:** the same palette, panels, section tabs, buttons and zone-coloured meters as LiveVoice, in the multiview, side bar, Settings and Master Bus.
- **Master Bus redesigned as a vertical channel strip** (LiveProfessor / Waves style): on / off and Inputs at the top, insert slots (click to open, dot to bypass, drag to reorder, right-click to replace or remove), input trim, then an output fader with stereo meters and gain reduction. A searchable plugin browser sits beside it.
- The output fader goes down to −60 dB.


## [0.5.0] — 2026-10-10

Master bus with VST3 and Audio Unit inserts.

- New Master Bus window (Tools ▸ OBSViewer ▸ Master Bus…, or right-click the multiview).
- Tick the sources that should go through the bus. OBSViewer moves them off your output track (Track 1) onto a spare "bus input" track (Track 6 by default), runs that mix through your plugin chain, and sends the result to the output tracks. Unticking a source, or turning the bus off, puts its tracks back exactly as they were.
- Plugin chain: VST3 and Audio Unit effects, in order top to bottom. Each slot has on / bypass, input trim and output gain (±12 dB), an input / output meter with gain-reduction readout, its own plugin window, and move up / down / remove.
- Master input trim and output gain with a bus meter.
- Scan for plugins finds VST3 and Audio Units installed on the Mac; the list is remembered.
- Plugin latency is compensated so the bus stays in sync with video.
- The chain and every plugin's settings are saved and restored when OBS starts.
- Requires macOS 13 or newer (as OBS 32 does).

## [0.4.0] — 2026-10-10

Control from fullscreen, scene switching and performance stats.

- Fullscreen now has a toolbar on top: layout, what clicking a tile does, move to another screen, Settings and Exit. Hide it from the right-click menu or Settings.
- Settings opens on top of the fullscreen view instead of behind it.
- Scene switching: click a scene tile to send it to Preview or cut it to Program (choose in the toolbar, the right-click menu or Settings; default Preview). Outside Studio Mode a click switches the scene. Double-click the Preview tile to take it to Program.
- Right-click menu now lists scenes: Send to Preview / Cut to Program (Studio Mode) or Switch Scene.
- New "Auto (2 + all scenes)" layout: Preview and Program large on top, every scene below, resized to fit (4 to 12 per row).
- Program and Preview tiles no longer have red / green boxes; scene tiles still show tally.
- Performance button under the health light: frame rate against target, render time per frame, missed and skipped frames, stream and record encoder (name, resolution, bitrate), CPU and memory. Click it to expand.
- The loudness meter now follows the track your stream sends automatically (Settings ▸ Audio ▸ Measure ▸ "Stream output"). It only listens; it never changes audio.

## [0.3.0] — 2026-10-10

Stage 3: audio on every tile.

- Audio meters on each tile: left / right bars at the right edge of the picture, with RMS as a solid bar and a peak tick (green, amber from −9 dB, red from −3 dB).
- Each scene tile shows the loudest unmuted audio source in that scene (visible items, including groups and nested scenes). The Preview tile shows the preview scene; the Program tile shows the actual stream mix.
- Mute indicator: when every audio source in a scene is muted, its meter turns red and shows MUTED.
- Clip indicator: a red cap on the meter for 2 seconds when a source hits −0.5 dBFS or above.
- Settings ▸ Multiview: "Show audio meters on tiles" switch.

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
