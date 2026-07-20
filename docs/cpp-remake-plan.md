# Gifler C++ Remake Plan

This document is the working plan for remaking Gifler as a native Windows C++ application.
The current C#/.NET/WPF implementation is useful as a behavior prototype, but it does not
fit the required distribution model: a small, portable, standalone `Gifler.exe` without
.NET bundle extraction or sibling managed DLLs.

The native rewrite should preserve the product experience and the good module boundaries,
while replacing WPF, .NET hosting, managed packaging, and managed interop with a compact
Win32/D3D11/DXGI application.

## Product Goal

Gifler is a modern GifCam-style recorder for quick screen clips:

- Open a small always-on-top window.
- Drag the title/top area to move it.
- Resize the capture region from the bottom-right corner.
- The center viewfinder is a real click-through hole, not a transparent overlay.
- Press `Rec` once to start recording.
- The same control becomes `Stop` while recording.
- Preview and play back the recording inside the viewfinder.
- Export GIF as the primary workflow.
- Copy the encoded GIF to the clipboard for Discord/GitHub without asking for a save path.
- Keep MP4/WebM/WebP/AVIF as alternate formats.
- Provide an editor for precise frame trimming and basic GIF-oriented edits.

The first native version should feel closer to classic GifCam than to a full video editor.
It should be compact, direct, and low ceremony.

## Why Rewrite In C++

The C# prototype proved the capture, recording, export, and editor concepts, but the
deployment model conflicts with the product:

- WPF framework-dependent builds require sibling DLLs.
- WPF self-contained single-file builds include the .NET runtime and WPF stack, producing
  a large executable.
- Framework-dependent single-file WPF without extraction is not a supported clean path.
- A custom loader or temp extraction is unacceptable for this product.
- A native Win32/C++ application can be a single small executable that uses Windows system
  DLLs already present on the machine.

Native target:

- One `Gifler.exe`.
- No .NET dependency.
- No extraction on startup.
- No bundled FFmpeg/gifski/gifsicle binaries.
- Target size: under 10 MB for the application executable.
- External encoders are discovered/configured at runtime.

## Current C# Prototype Summary

The current repo is organized as:

- `TransparentRecorder.App`: WPF shell, main window, playback, editor window.
- `TransparentRecorder.Core`: shared geometry, capture, recording, and export contracts.
- `TransparentRecorder.Win32`: P/Invoke, DPI, window region, hotkey/display helpers.
- `TransparentRecorder.Capture.Dxgi`: DXGI Desktop Duplication capture provider.
- `TransparentRecorder.Record`: recorder session, bounded channel, coalescing, frame store.
- `TransparentRecorder.Export`: FFmpeg/gifski/gifsicle process adapters and GIF planner.

The current executable is named `Gifler.exe` through the WPF project assembly name, but the
underlying project names still use the original TransparentRecorder naming.

Useful behaviors to preserve:

- Single main window with a region-cut capture hole.
- Physical-pixel capture rectangles.
- DPI conversion only at the UI boundary.
- DXGI Desktop Duplication capture.
- Real pointer shape capture through Desktop Duplication pointer metadata.
- Bounded capture-to-recording queue.
- Duplicate-frame coalescing.
- Changed-rectangle detection.
- GIF-first export presets.
- Clipboard GIF workflow through a temporary encoded file exposed as a clipboard file drop.
- Preview playback in the viewfinder.
- Editor window with a frame timeline and context-menu operations.

Behaviors to improve in the rewrite:

- Make the UI smaller and more coherent from the start.
- Avoid WPF layout surprises and dead zones.
- Make the top strip look like a compact app/menu bar, not a row of oversized buttons.
- Make preview playback always fill the viewfinder correctly.
- Make GIF size preparation cancellable and cached.
- Make frame timeline virtualization efficient enough for long clips.
- Make packaging native and predictable.

## Native Technical Direction

### Language And Toolchain

- C++20 minimum, C++23 acceptable if the compiler/toolchain is stable.
- MSVC on Windows.
- CMake is recommended for repeatable CLI builds, with a generated Visual Studio solution.
- Keep production dependencies minimal.
- Avoid Qt, wxWidgets, Electron, .NET, WinUI, and large UI frameworks for v1.
- Use Windows SDK APIs directly.
- Tests can use a small header-only framework such as doctest or Catch2, but test
  dependencies must not ship with the app.

Recommended build modes:

- `Debug`: symbols, assertions, validation logging.
- `RelWithDebInfo`: release optimizations plus PDB for debugging.
- `Release`: optimized portable artifact.

Recommended release flags:

- `/O2`
- `/GL` plus linker `/LTCG`
- `/Gy`
- `/OPT:REF`
- `/OPT:ICF`
- Consider `/MT` for a single CRT-free exe, after checking size and license tradeoffs.
- Ship PDB separately, not beside the portable exe.

### Windows API Stack

- UI/windowing: Win32.
- Rendering: Direct2D or Direct3D 11. Direct2D is a good default for toolbar/editor
  rendering; D3D11 is already required for capture.
- Capture: DXGI Desktop Duplication through D3D11.
- Encoding: external processes first: FFmpeg, gifski, gifsicle.
- Clipboard: Win32 clipboard APIs with `CF_HDROP` for Discord-compatible file paste.
- Settings: JSON or simple INI stored under `%APPDATA%\Gifler`, with an optional portable
  settings file next to the exe if the directory is writable.

## Proposed Native Repository Layout

Create a new native tree rather than continuing the managed project names:

```text
native/
  CMakeLists.txt
  Gifler.sln                  # generated or checked in only if desired

  src/
    gifler_app/
      main.cpp
      MainWindow.cpp
      MainWindow.h
      CommandBar.cpp
      CommandBar.h
      ViewfinderWindow.cpp
      ViewfinderWindow.h
      PreviewPlayer.cpp
      PreviewPlayer.h

    gifler_core/
      Geometry.h
      Frame.h
      CaptureContracts.h
      RecordingContracts.h
      ExportContracts.h
      Settings.h

    gifler_win32/
      Hwnd.h
      WindowRegion.cpp
      Dpi.cpp
      Display.cpp
      Clipboard.cpp
      Process.cpp
      FileDialog.cpp
      Hotkeys.cpp

    gifler_capture_dxgi/
      DxgiCaptureProvider.cpp
      DxgiDuplicationSession.cpp
      MonitorEnumerator.cpp
      PointerShape.cpp
      BgraFrameStitcher.cpp

    gifler_record/
      RecorderSession.cpp
      BoundedFrameQueue.cpp
      FrameDiffer.cpp
      DuplicateFrameCoalescer.cpp
      InMemoryFrameStore.cpp
      DiskSpillFrameStore.cpp

    gifler_export/
      GifExportPlanner.cpp
      FfmpegProcess.cpp
      FfmpegPaletteGifExporter.cpp
      GifskiGifExporter.cpp
      GifsicleOptimizer.cpp
      VideoExporters.cpp
      ExportSummary.cpp

    gifler_editor/
      EditorWindow.cpp
      TimelineView.cpp
      FrameOperations.cpp
      GreenScreenView.cpp

  tests/
    geometry_tests.cpp
    frame_differ_tests.cpp
    gif_planner_tests.cpp
    command_builder_tests.cpp
    pointer_shape_tests.cpp
    recorder_tests.cpp

  docs/
    manual-test-plan.md
    encoder-distribution.md
    architecture.md
```

The C++ modules should keep the same boundaries as the prototype:

- UI owns HWNDs and presentation only.
- Win32 owns raw Windows calls.
- Capture owns DXGI/D3D11.
- Recording owns timing, queueing, coalescing, and frame storage.
- Export owns encoders and target-size planning.
- Editor owns timeline rendering and frame edit commands.

## Core Data Model

Use simple value types. Store screen coordinates in physical pixels.

```cpp
struct PixelPoint {
    int x = 0;
    int y = 0;
};

struct PixelSize {
    int width = 0;
    int height = 0;
    bool empty() const { return width <= 0 || height <= 0; }
};

struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    int right() const { return x + width; }
    int bottom() const { return y + height; }
    bool empty() const { return width <= 0 || height <= 0; }
};

enum class PixelFormat {
    Bgra32
};

struct CursorShape {
    bool visible = false;
    PixelPoint position;
    PixelPoint hotSpot;
    PixelSize size;
    std::vector<std::byte> bgraPixels;
};

struct BgraFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    int64_t timestampTicks = 0;
    int64_t durationTicks = 0;
    std::vector<std::byte> pixels;
    std::optional<PixelRect> changedBounds;
    std::optional<CursorShape> cursor;
};
```

Important rules:

- A frame buffer is always BGRA32 in memory for v1.
- `stride` must be explicit. Do not assume every source is tightly packed.
- Timestamps should use `QueryPerformanceCounter`.
- Frame durations should survive duplicate coalescing.
- Original recordings must never be mutated during export planning.
- Export variants are generated from the source sequence.

## Main Window Design

The native app should be one top-level window with a real hole in the client area.

Default visual shape:

- Classic compact titlebar.
- Small command/menu strip directly below the titlebar.
- Large viewfinder hole below the command strip.
- Status text at the lower-left edge.
- Resize grip at the bottom-right corner.

Main strip controls:

- `File` menu.
- `Rec` immediate button.
- Cursor capture toggle with a cursor icon.
- FPS dropdown directly visible: `5`, `10`, `15`, `30`.
- `Frame`.
- `Edit`.
- `Save`.
- `Copy GIF`.
- `Play`.

Record behavior:

- Idle: camera icon plus `Rec`.
- Recording: red recording dot plus `Stop`.
- The record control must not open a dropdown.
- Dropdown recording options belong under `Capture` or `File`, not on the record button.

Save behavior:

- `Save` exports the current default format.
- Default format is GIF for the default preset.
- Advanced export settings live in menus or a settings dialog.
- The main window should not expand to show full platform names or target settings.

Size display:

- The title or status area should show the selected capture size.
- The status area should show the prepared encoded GIF size when available:
  - `GIF: --` before encoding.
  - `GIF: ...` while preparing.
  - `GIF: 2.4 MB` when ready.
  - `GIF: failed` on error.

Viewfinder:

- In capture mode, the viewfinder is a real click-through hole.
- In preview mode, the app clears the hole region and paints playback in that area.
- Playback must fill the viewfinder using cover-style scaling, equivalent to
  `UniformToFill`.
- Switching back to capture mode reapplies the hole.

Dragging and resizing:

- The full titlebar/command strip should drag the window except over interactive controls.
- There must be no dead zone in the top center.
- Bottom-right resizing should be easy to hit.
- Resize should preserve a sensible minimum capture area.
- Capture rectangle updates must be converted to physical pixels.

## Window Region And Hit Testing

Preferred v1 implementation:

- One top-level Win32 window.
- Apply a window region with the capture hole cut out using `SetWindowRgn`.
- Use `WM_NCHITTEST` for move and resize behavior.
- Return appropriate resize codes for borders/corners.
- Return `HTCAPTION` for draggable top areas not occupied by controls.
- Interactive controls must receive normal mouse input.

Alternative if region behavior becomes fragile:

- Use multiple top-level HWNDs for border/control strips, matching GifCam's old approach.
- Keep the center physically empty.
- Coordinate the strips as one logical window.

The one-window approach is preferred because it matches the desired product feel, but it
must still produce a real click-through center.

Do not rely on visual transparency alone. The mouse must reach the window behind Gifler
when clicking inside the viewfinder in capture mode.

## DPI And Coordinates

Rules:

- The recorder stores capture rectangles in physical pixels.
- UI layout may use logical units internally, but conversion happens at the window boundary.
- Handle `WM_DPICHANGED`.
- Recompute the hole and capture rectangle after DPI changes.
- Mixed-DPI monitor movement must keep the visible hole aligned with captured pixels.

Win32 APIs:

- `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`.
- `GetDpiForWindow`.
- `GetDpiForMonitor` where needed.
- `WM_DPICHANGED`.

## Capture System

Use DXGI Desktop Duplication with D3D11.

Core classes:

- `DxgiCaptureProvider`
- `DxgiDuplicationSession`
- `MonitorEnumerator`
- `PointerShapeCache`
- `BgraFrameStitcher`

DXGI flow:

1. Enumerate adapters and outputs.
2. Map each monitor to a physical desktop rectangle.
3. For a capture rectangle, find intersecting outputs.
4. For v1, fully support single-monitor capture.
5. Add multi-monitor stitching after the single-monitor path is stable.
6. Create a D3D11 device compatible with Desktop Duplication.
7. Use `IDXGIOutput1::DuplicateOutput`.
8. Use `IDXGIOutputDuplication::AcquireNextFrame` with a timeout.
9. Copy the acquired desktop texture to a staging texture.
10. Map the staging texture and crop BGRA pixels into the frame buffer.
11. Always call `ReleaseFrame` after a successful acquire.
12. Treat timeout as non-fatal.
13. Treat access loss/display changes as reinitialize-or-stop events with clear UI status.

Pointer handling:

- Use `IDXGIOutputDuplication::GetFramePointerShape`.
- Cache the most recent pointer shape.
- Respect pointer visibility and position metadata.
- Use the pointer hot spot. Do not draw the cursor at the raw top-left position unless the
  metadata says that is correct.
- Support color, masked color, and monochrome pointer shapes.
- Composite the cursor only when the capture cursor toggle is enabled.
- Cursor pixels should be exact-size. Do not scale the cursor.

Frame output:

- BGRA32.
- Timestamped.
- Duration assigned by recorder timing/coalescing.
- Optional changed bounds.

## Recording Pipeline

Recording must never encode on the capture thread.

Threads:

- UI thread: HWND messages, rendering, state updates.
- Capture thread: DXGI acquisition and BGRA frame production.
- Recording worker: coalescing, changed bounds, frame store.
- Export worker: frame materialization and external encoder processes.

Recorder state:

```cpp
enum class RecorderState {
    Idle,
    Recording,
    Paused,
    Stopping,
    Completed,
    Failed
};
```

Pipeline:

1. `MainWindow` creates a `RecorderSession` with capture settings.
2. `RecorderSession` starts capture on a worker thread.
3. Captured frames enter a bounded queue.
4. If the queue is full, apply explicit backpressure or drop policy. Do not grow memory
   unbounded.
5. Recording worker reads frames.
6. Duplicate frames are coalesced by extending the previous frame duration.
7. Changed bounds are computed for non-duplicate frames.
8. Frames are stored in `IFrameStore`.
9. Stop/cancel drains or disposes queued frames deterministically.

Frame store:

- v1 can use memory for short clips.
- Add a disk-spill abstraction early.
- Disk spill should use raw BGRA chunks or a simple lossless format, not GIF/MP4.
- Exporters read from `IFrameSequence`.

## Preview Playback

Preview is part of the main product, not an afterthought.

Requirements:

- After stopping, automatically load a preview sequence.
- Show the first frame in the viewfinder.
- `Play` toggles playback/pause.
- Playback loops by default.
- Frame timing uses recorded frame durations.
- Preview scaling fills the viewfinder without weird offsets.
- Preview mode disables click-through for the viewfinder.
- New recording returns to capture-hole mode.

Rendering options:

- Direct2D bitmap per frame for simple implementation.
- D3D11 texture upload for better performance.
- Cache only what is needed. Very long recordings should not force all frames into GPU
  resources at once.

## Editor

The editor is launched from `Edit` and operates on the last recording.

v1 editor goals:

- Timeline of frames.
- Efficient virtualization, not one HWND/control per frame.
- Precise start-frame and end-frame selection.
- Frame labels: index and delay.
- Right-click context menu on a frame.
- Preview selected frame.
- Apply edits non-destructively until committed.

Context menu operations to support or stub clearly:

- Delete This Frame.
- Delete From This Frame To Start.
- Delete From This Frame To End.
- Delete Even Frames.
- Keyboard Inputs.
- Add Text.
- Resize.
- Crop.
- Add Reverse Frames.
- Hue And Saturation.
- Draw Green Screen.
- Green Screen.

Green screen feature:

- This is a change-visualization mode inspired by GifCam.
- Show unchanged pixels as green or another obvious marker.
- Show changed areas normally.
- Use the same changed-rectangle/diff model as the recorder where possible.
- Do not permanently alter source frames unless the user applies an edit.

Editor architecture:

- `EditorWindow`: top-level editor shell.
- `TimelineView`: virtualized frame strip.
- `FrameOperations`: delete, trim, reverse, crop, resize, text overlay.
- `GreenScreenView`: changed-area visualization.
- `EditedFrameSequence`: non-destructive frame sequence wrapper.

## GIF-First Export System

GIF is the primary format, not a legacy option.

Default preset:

- Platform: `Discord/GitHub Free`.
- Display target: `10 MB`.
- Internal safety target: `9.5 MB`.
- Default format: `Auto GIF`.
- Main button label: `Save` or `Export GIF`, depending on final UI polish. If the visible
  command is generic `Save`, its default action must still be GIF.

Supported formats:

- `OptimizedGif`
- `GifskiGif`
- `Mp4H264`
- `Mp4Av1`
- `WebMAv1`
- `AnimatedWebP`
- `AnimatedAvifExperimental`

Current C# aliases also include:

- `Gif = OptimizedGif`
- `Mp4 = Mp4H264`
- `WebM = WebMAv1`
- `Avi`

The C++ rewrite should only keep aliases if they simplify migration. Avoid exposing old
names in the UI.

GIF modes:

- Auto GIF.
- Best GIF Quality.
- Smallest GIF.
- Compatibility GIF.

Share presets:

- Discord/GitHub Free:
  - Display target: 10 MB.
  - Internal target: 9.5 MB.
  - Default: Auto GIF.
  - Alternates: MP4 H.264, Animated WebP.
- Discord Nitro Basic:
  - Display target: 50 MB.
  - Internal target: 47.5 MB.
  - Default: Auto GIF.
  - Alternates: MP4 H.264, Animated WebP, MP4 AV1.
- Discord Nitro:
  - Display target: 500 MB.
  - Internal target: 475 MB.
  - Default: Auto GIF.
  - Alternates: MP4 H.264, MP4 AV1, WebM AV1.
- GitHub Paid Video:
  - Display target: 100 MB.
  - Internal target: 95 MB.
  - Default: MP4 H.264.
  - Still allow 10 MB GIF image mode.
- Custom:
  - User-selected target and format priority.

Planner inputs:

```cpp
struct GifExportRequest {
    GifExportMode mode;
    int fps;
    int quality;
    bool loop;
    std::filesystem::path outputPath;
    std::optional<std::filesystem::path> ffmpegPath;
    std::optional<std::filesystem::path> gifskiPath;
    std::optional<std::filesystem::path> gifsiclePath;
    std::optional<int> width;
    int maxColors = 256;
    GifDitherMode dither = GifDitherMode::Sierra2_4A;
};

struct TargetSizeOptions {
    bool enabled = true;
    double displayTargetMb = 10.0;
    double internalSafetyTargetMb = 9.5;
    int minimumFps = 5;
    int minimumWidth = 320;
    bool allowAggressiveDownscale = false;
    bool allowVeryLowColorGif = false;
};
```

Planner outputs:

- Full attempt list.
- Selected attempt.
- Final output path.
- Warning message if the target could not be reached.
- Suggested alternate formats.
- Export summary.

Ranking:

1. Under target file size.
2. Visual quality score.
3. Temporal smoothness.
4. Dimensions.
5. FPS.
6. Encode time.

Target-size algorithm:

1. Start with source dimensions and requested FPS.
2. In Auto GIF or Best GIF Quality, try gifski first if configured.
3. If target size is enabled and gifski output is too large, reduce quality.
4. If still too large, try FFmpeg palettegen/paletteuse.
5. Reduce max colors.
6. Reduce FPS.
7. Reduce dimensions.
8. Optionally run gifsicle `-O3`.
9. If no GIF meets the target within constraints, return the best attempt and explain why.

Minimum guards:

- Do not go below 5 FPS unless allowed.
- Do not scale below 320 px wide unless allowed.
- Do not use fewer than 32 colors unless allowed.

Failure message:

```text
Could not hit 10 MB as GIF without dropping below the minimum quality settings.
Best GIF is X MB. Target was Y MB.
```

Offer one-click alternates:

- Export MP4 H.264 under target.
- Export Animated WebP under target.
- Increase target size.
- Lower quality further.

## Encoder Process Adapters

Do not vendor encoder binaries.

Settings fields:

- `ffmpeg.exe` path.
- `gifski.exe` path.
- `gifsicle.exe` path.
- Default export folder.
- Default share target.
- Default GIF mode.
- Target filesize default MB.
- Minimum FPS.
- Minimum width.
- Allow aggressive downscale.
- Allow very low color GIFs.

Missing path behavior:

- Missing `ffmpeg.exe`, `gifski.exe`, or `gifsicle.exe` must produce an actionable UI
  warning.
- Missing gifsicle should skip that optimization pass, not fail the whole export.
- Missing gifski in Auto GIF should fall back to FFmpeg palette GIF.

FFmpeg palette GIF commands:

```text
ffmpeg.exe -y -framerate {fps} -i "{framesDir}\frame_%06d.png" -vf "fps={fps},scale={width}:-2:flags=lanczos,palettegen=stats_mode=diff:max_colors={colors}" "{tempDir}\palette.png"

ffmpeg.exe -y -framerate {fps} -i "{framesDir}\frame_%06d.png" -i "{tempDir}\palette.png" -lavfi "fps={fps},scale={width}:-2:flags=lanczos[x];[x][1:v]paletteuse=dither=sierra2_4a:diff_mode=rectangle" "{tempDir}\raw.gif"
```

Optional gifsicle:

```text
gifsicle.exe -O3 "{tempDir}\raw.gif" -o "{outputPath}"
```

Gifski:

```text
gifski.exe --fps {fps} --width {width} --quality {quality} -o "{outputPath}" "{framesDir}\frame_*.png"
```

MP4 H.264 target-size commands:

```text
ffmpeg.exe -y -framerate {fps} -i "{framesDir}\frame_%06d.png" -c:v libx264 -pix_fmt yuv420p -preset slow -b:v {bitrateKbps}k -pass 1 -an -f mp4 NUL

ffmpeg.exe -y -framerate {fps} -i "{framesDir}\frame_%06d.png" -c:v libx264 -pix_fmt yuv420p -preset slow -b:v {bitrateKbps}k -pass 2 -an -movflags +faststart "{outputPath}"
```

Video alternates:

- MP4 H.264: compatibility video fallback.
- MP4 AV1: modern video alternate.
- WebM AV1: modern web/video alternate.
- Animated WebP: smaller modern loop.
- Animated AVIF: experimental until encode/decode behavior is verified.

## Copy GIF To Clipboard

The user-facing behavior is "copy without saving".

Implementation detail:

- Encode a GIF into an app-managed temporary/cache location.
- Put that `.gif` on the clipboard as a file drop using `CF_HDROP`.
- Discord and many chat apps then treat paste as a file upload.
- Keep the temp file alive long enough for paste to succeed.
- Clean old clipboard cache files on later app launches.

Do not use `CF_DIB` for animated GIF copy. That would usually paste a static bitmap.

UI requirements:

- Show encoded GIF size before the user clicks Copy GIF.
- If size preparation is running, show `GIF: ...`.
- If size preparation fails, show a concise error and let the user retry.
- Copy GIF should reuse a prepared GIF if the recording/export settings have not changed.
- If settings changed, cancel stale preparation and start a new one.

Cache key should include:

- Recording identity/version.
- Frame count.
- Duration.
- Dimensions.
- FPS.
- GIF mode.
- Target settings.
- Encoder paths relevant to GIF.

## Export Summary

After save/copy, show:

- Output path or clipboard status.
- Final size.
- Target size.
- Format.
- Dimensions.
- FPS.
- Frame count.
- Duration.
- GIF engine used.
- Optimization passes used.
- Warning if target was not reached.

Example:

```text
Copied GIF | 8.7 MB | target 10 MB | 389x232 | 10 FPS | 82 frames | 0:00:08.1 | engine FFmpeg palette | passes palettegen, paletteuse, gifsicle -O3
```

## File And Temp Management

Directories:

- Settings: `%APPDATA%\Gifler`.
- Logs: `%LOCALAPPDATA%\Gifler\Logs`.
- Temp export work: `%LOCALAPPDATA%\Gifler\Temp`.
- Clipboard cache: `%LOCALAPPDATA%\Gifler\Clipboard`.

Rules:

- Use deterministic per-export temp directories.
- Clean temp directories on success, failure, and cancellation.
- Keep clipboard GIF cache files until they are old enough to delete safely.
- Never delete a user-selected output file on cancellation unless the current export created
  an incomplete temp file that has not been moved into place.

## Error Handling

Required non-crashing errors:

- Missing encoder path.
- Encoder process exits non-zero.
- Output path not writable.
- Capture unavailable.
- DXGI access lost.
- Display configuration changed.
- Capture rectangle empty.
- No recording to export/copy/edit/play.
- Clipboard unavailable or locked by another process.

UI style:

- Status bar text for common recoverable issues.
- Small modal only when user action is required.
- Never dump raw command lines as the main error, but make details available in logs.

## Testing Plan

Unit tests:

- Pixel geometry intersection, clipping, and DPI conversion.
- Window-region geometry for the hole.
- Frame differ exact changed rectangle.
- Duplicate-frame coalescing extends duration.
- Bounded queue does not grow unbounded.
- Cancellation disposes queued frames.
- GIF default preset is Discord/GitHub Free at 10 MB display and 9.5 MB internal.
- Auto GIF tries gifski before FFmpeg when gifski is configured.
- Auto GIF falls back to FFmpeg when gifski is missing or too large.
- Planner reduces quality before FPS.
- Planner reduces FPS before dimensions.
- Planner reduces dimensions before failing.
- Planner never mutates the original recording.
- Candidate ranking picks the best under-target GIF.
- No-hit GIF returns best failed attempt and suggested alternates.
- FFmpeg palette command construction.
- Gifski command construction.
- Gifsicle optional optimizer behavior.
- Missing external encoder paths produce actionable errors.
- MP4/WebP alternates remain available but do not override the default GIF UX.
- Pointer shape conversion for color, masked color, and monochrome shapes.

Manual smoke tests:

- App launches as a single native exe.
- Window is always on top.
- Full top strip drags the app except over controls.
- No dead spot in the draggable area.
- Bottom-right resize works.
- Viewfinder center passes clicks through to Notepad/browser behind it.
- Controls remain clickable.
- Capture rectangle matches the visible hole.
- Record and stop work through the same immediate button.
- Preview fills the viewfinder.
- Play/pause loops with recorded timing.
- Cursor on/off changes output.
- Cursor shape is crisp, correct size, and correctly positioned.
- Export GIF at Discord/GitHub Free 10 MB target.
- Encoded GIF size is shown before Copy GIF.
- Copy GIF pastes to Discord as an animated GIF file.
- Save GIF writes to selected output folder.
- MP4 H.264 export works.
- Animated WebP export works if FFmpeg supports it.
- Missing encoder path gives an actionable message.
- DPI at 100%, 125%, and 150%.
- Mixed-DPI monitor movement.
- Multi-monitor capture fully on monitor A, fully on monitor B, and spanning monitors.
- Display change during recording stops or recovers cleanly.

## Milestones

### Milestone 0: Native Scaffold

Deliverables:

- `native/` CMake project.
- Win32 app entry point.
- Empty main window named Gifler.
- Basic logging.
- Basic settings load/save.
- Unit test target.

Acceptance:

- Release build creates one `Gifler.exe`.
- App launches.
- Tests run from CLI.

### Milestone 1: GifCam-Style Window

Deliverables:

- Compact titlebar and command strip.
- Real region-cut viewfinder hole.
- Drag and resize behavior.
- DPI-aware capture rectangle.
- Status/size labels.

Acceptance:

- The center passes clicks through.
- Controls remain clickable.
- Window can be dragged from the full intended top area.
- No top dead zone.
- Capture rectangle is logged in physical pixels.

### Milestone 2: DXGI Capture

Deliverables:

- D3D11 device setup.
- Single-monitor Desktop Duplication capture.
- Frame timeout handling.
- Access-lost handling.
- Cursor pointer-shape capture and compositing.
- Save one captured frame to a PNG/BMP debug artifact.

Acceptance:

- Captures the desktop behind the hole.
- Cursor toggle works.
- Cursor is correct-size and crisp.
- Stop releases DXGI frames cleanly.

### Milestone 3: Recording And Preview

Deliverables:

- `RecorderSession`.
- Bounded frame queue.
- Duplicate coalescing.
- Changed-bounds detection.
- In-memory frame store.
- Preview playback in the main viewfinder.

Acceptance:

- Record/Stop produces a playable clip.
- UI remains responsive.
- Preview fills the hole.
- Deterministic recording tests pass.

### Milestone 4: GIF-First Export And Clipboard

Deliverables:

- GIF planner.
- FFmpeg palette GIF exporter.
- Gifski exporter.
- Optional gifsicle optimizer.
- Target-size algorithm.
- Encoded GIF size preparation.
- Copy GIF to clipboard through `CF_HDROP`.
- Export result summary.

Acceptance:

- Default path is Auto GIF for Discord/GitHub Free.
- Size label is available before Copy GIF.
- Copy GIF pastes to Discord.
- Target misses explain the result and offer alternates.

### Milestone 5: Video And Modern Alternates

Deliverables:

- MP4 H.264 export.
- MP4 AV1 export.
- WebM AV1 export.
- Animated WebP export.
- Animated AVIF experimental path or explicit unsupported message.
- Target-size bitrate calculation for MP4 H.264.

Acceptance:

- Alternates are available but do not replace the default GIF UX.
- MP4 H.264 can hit a target size using two-pass ABR.

### Milestone 6: Editor

Deliverables:

- Editor window.
- Virtualized timeline.
- Start/end trimming.
- Frame context menu.
- Delete frame/range/even frames.
- Green screen/change visualization.
- Non-destructive edited sequence output.

Acceptance:

- User can trim precisely.
- Large frame counts remain responsive.
- Edited result can be previewed and exported.

### Milestone 7: Packaging And Hardening

Deliverables:

- Release artifact script.
- Single native `Gifler.exe`.
- PDB separated.
- Encoder distribution documentation.
- Manual smoke test pass.

Acceptance:

- `Gifler.exe` is under 10 MB unless a deliberate dependency decision changes that target.
- No runtime extraction.
- No sibling app DLLs required.
- External encoders remain optional/configured.

## C# To C++ Mapping

| Current C# area | Native C++ replacement |
| --- | --- |
| `TransparentRecorder.Core.Geometry` | `gifler_core/Geometry.h` |
| `CapturedFrame` | `BgraFrame` |
| `IScreenCaptureProvider` | `ICaptureProvider` or concrete `DxgiCaptureProvider` |
| `TransparentRecorder.Win32` | `gifler_win32` wrappers |
| `WindowRegion` | `gifler_win32/WindowRegion.cpp` using `SetWindowRgn` |
| `DxgiScreenCaptureProvider` | `gifler_capture_dxgi/DxgiCaptureProvider` |
| `VorticeDxgiDesktopFrameSource` | `DxgiDuplicationSession` using raw COM interfaces |
| `RecorderSession` | `gifler_record/RecorderSession` |
| `InMemoryFrameStore` | `gifler_record/InMemoryFrameStore` |
| `GifExportPlanner` | `gifler_export/GifExportPlanner` |
| `FfmpegRecordingExporter` | format-specific process adapters |
| `MainWindow.xaml` | custom Win32 `MainWindow` painting/layout |
| `EditorWindow.xaml` | native `EditorWindow` plus `TimelineView` |

## Known Risks

- DXGI Desktop Duplication can fail during display changes, secure desktop, driver resets,
  or sleep/wake. Handle access loss deliberately.
- Mixed-DPI coordinates are easy to get subtly wrong. Keep physical pixels as the source
  of truth.
- Pointer shapes have several formats. Test color, masked color, and monochrome cursors.
- GIF target size is content-dependent. High-motion/full-color recordings may not hit 10 MB
  within acceptable quality constraints.
- Discord clipboard behavior can change. `CF_HDROP` with a real `.gif` file is the most
  practical path, but it still needs a temp/cache file behind the scenes.
- External encoder command behavior varies by build. Log versions when possible.
- Unsigned native executables may trigger reputation warnings. Signing should be planned
  before public release.

## Non-Goals For Native v1

- Audio capture.
- Built-in FFmpeg/gifski/gifsicle binaries.
- Native AVIF encoder if tooling is unstable.
- Full Photoshop-style editor.
- Cloud upload/sharing.
- Cross-platform support.
- A large UI framework.

## Definition Of Done For Native v1

Native v1 is done when:

- A single native `Gifler.exe` launches without .NET.
- The exe does not extract payloads on startup.
- The center viewfinder is genuinely click-through.
- The window is compact and draggable/resizable like GifCam.
- Record/Stop is immediate and reliable.
- Capture uses DXGI Desktop Duplication.
- Cursor capture is optional and visually correct.
- Preview playback fills the viewfinder.
- GIF is the default export path.
- Discord/GitHub Free defaults to 10 MB display target and 9.5 MB internal target.
- Encoded GIF size is visible before Copy GIF.
- Copy GIF pastes into Discord without a user-visible save step.
- MP4 H.264 and Animated WebP are available alternates.
- Editor supports precise start/end trimming.
- Deterministic unit tests pass.
- Manual DPI, multi-monitor, cursor, clipboard, and export smoke tests pass.
- Release artifact is under 10 MB or every excess dependency is explicitly justified.
