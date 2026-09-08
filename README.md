# Gifler

[![Windows build](https://github.com/lmccandless/gifler/actions/workflows/windows-build.yml/badge.svg)](https://github.com/lmccandless/gifler/actions/workflows/windows-build.yml)
[![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue.svg)](LICENSE)
[![Windows](https://img.shields.io/badge/platform-Windows%2010%2F11-0078D4.svg)](https://github.com/lmccandless/gifler/releases)

Gifler is a compact native Windows screen recorder built around a transparent, click-through viewfinder. Position the window over anything on your desktop, record it, preview it, and export or copy the result as GIF, MP4, animated WebP, or WebM.

![Gifler recording window](docs/reference/gifler-chrome.png)

Native UI render; the neutral center represents the transparent capture area.

## Download

Download the current Windows executable from [GitHub Releases](https://github.com/lmccandless/gifler/releases/latest), or use the direct [Gifler.exe download](https://github.com/lmccandless/gifler/releases/latest/download/Gifler.exe).

Gifler is an unsigned early-preview executable. Windows SmartScreen may ask for confirmation until releases are code-signed.

## Highlights

- Native Win32/C++20 application with no .NET, Electron, Qt, or installer.
- Transparent click-through recording region that remains visible while idle and recording.
- Movable capture window whose recording region follows the window dynamically.
- Resize from any inner edge or corner, with directional cursors and hover marks.
- Lock the capture region to 16:9, 9:16, 1:1, 4:5, 4:3, or 3:4 while resizing.
- Dark camera-inspired frame with a single 34-pixel control strip, compact icons,
  accessible button names, and DPI-aware controls. No tooltip popups, bottom bar, or minimize/maximize buttons.
- 5, 10, 15, 24, 30, 48, 60, and 120 FPS presets, plus custom 1-240 FPS.
- GIF, H.264 MP4, animated WebP, and VP9 WebM export.
- Optional system audio in exported MP4 (AAC) and WebM (Opus).
- Social / X MP4 preset with 30 FPS H.264 and AAC-LC stereo; selectable 48 or 44.1 kHz MP4 audio.
- Size targets of 5, 10, 20, 50, or 100 MB, plus No limit.
- Copy the selected media type to the Windows clipboard as a file drop.
- Responsive background export with visible progress and cancellation.
- Preview playback and a basic frame editor.
- Remembers FPS, media format, size target, cursor, and audio preferences.

## Quick Start

1. Download `Gifler.exe` from the latest release.
2. Put `ffmpeg.exe` on `PATH`, or place it at `encoders\ffmpeg.exe` beside Gifler.
3. Run Gifler, position and resize the viewfinder, then select **Record**.
4. Stop recording, select an output format, and use **Save** or **Copy**.

Enable **More > Record system audio** before recording to include the default
playback device in MP4/WebM exports. This records all system playback, not a
microphone or just the application inside the viewfinder. The built-in preview
is visual only; check audio in the exported video. GIF and WebP remain silent.
Edited video exports splice the matching audio intervals along with the frames.

Choose **More > Capture aspect ratio** for a locked capture shape. Select
**MP4 Social / X** in the format dropdown for a conservative social-video export,
and **MP4 audio rate** for optional 44.1 kHz resampling. The default 48 kHz rate
is appropriate for video. See [social exports](docs/social-exports.md) for limits
and platform compatibility details.

Choose a target under **the media-format dropdown > Size target**. Targets use
a 5% safety margin. If the encoder cannot meet the budget, choose a larger target
or No limit. Changing the target also invalidates the prepared Copy cache.
During export, **Save** becomes **Cancel**; canceling keeps the recording.
An export failure writes details to `%LOCALAPPDATA%\Gifler\last-export-error.txt`.

Use the FPS dropdown for presets or **Custom...** to enter a whole number from
1 through 240. Higher rates are capture targets, not a guarantee: display refresh,
capture area, and hardware determine the achieved rate. MP4/WebM are preferable
for high-frame-rate playback; GIF timing is quantized and varies by player.

Recording and preview are contained in the small Gifler executable. Media encoding is delegated to FFmpeg so the application can remain small. Gifler also discovers optional `gifski.exe` and `gifsicle.exe` tools in the same `encoders` directory or on `PATH`. See [encoder setup](docs/encoder-setup.md).

## Building

Requirements:

- Windows 10 or 11 x64
- Visual Studio 2022 with the Desktop development with C++ workload
- Windows 10/11 SDK
- CMake 3.24 or newer

Run the canonical scripts from PowerShell:

```powershell
./scripts/build-debug.ps1
./scripts/run-tests.ps1
./scripts/run-export-smoke.ps1
./scripts/run-ui-smoke.ps1
# Optional: one-second probe of the default audio playback device.
./scripts/run-audio-smoke.ps1
./scripts/build-release.ps1
```

Release output is written under `build/<preset>/Release/Gifler.exe`. The packaging script also places a standalone executable at `dist/Gifler.exe`.

## Project Layout

```text
src/gifler_app/           Main Win32 application and UI
src/gifler_capture_dxgi/  Desktop capture providers
src/gifler_core/          Frames, geometry, diffing, and settings
src/gifler_editor/        Frame editor
src/gifler_export/        GIF and video export pipeline
src/gifler_record/        Recording session and frame storage
src/gifler_win32/         Focused Win32 helpers
spikes/                   Native behavior and integration probes
tests/                    Dependency-free unit tests
docs/                     Architecture, decisions, and manual checks
```

## License and Authorship

Copyright (C) 2026 lmccandless.

Gifler is licensed under the [GNU Affero General Public License v3.0 only](LICENSE). This is a strong copyleft open-source license: covered modifications and redistributed versions must preserve the license and corresponding-source obligations. Copyright and authorship notices may not be removed.

Open source necessarily permits use, modification, and redistribution under the license terms. It does not transfer ownership of the original copyright or grant rights to misrepresent a modified build as the original project. See [NOTICE](NOTICE) for attribution and project-name terms.

## Security

Please report security issues according to [SECURITY.md](SECURITY.md) rather than posting sensitive details in a public issue.
