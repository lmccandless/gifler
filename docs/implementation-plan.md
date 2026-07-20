# Gifler Native C++ Spike-First Implementation Plan

This is the implementation plan for rebuilding Gifler as a compact native Windows C++ application after proving the riskiest technical assumptions with focused spikes.

## 1. Core Strategy

Build this in two passes:

1. **Spike pass**: create isolated proof-of-concepts for every critical risk that could invalidate the architecture.
2. **Product pass**: build the final application using only the decisions and code patterns proven by the spikes.

The spikes are not the product. They are disposable experiments with clear pass/fail criteria. Each spike must produce:

- a tiny runnable executable or test target,
- a short findings document,
- screenshots or logs where useful,
- a yes/no decision,
- known limitations,
- the chosen final implementation approach.

Do not start the full product until the critical spikes for window hole behavior, capture alignment, GIF export, and clipboard paste are green.

## 2. Product Constraints

The final application must satisfy these constraints:

- Native Windows C++ application.
- One portable `Gifler.exe` as the main artifact.
- Target executable size under 10 MB.
- No .NET, WPF, WinUI, Electron, Qt, wxWidgets, or large UI framework.
- No runtime extraction.
- No bundled FFmpeg, gifski, or gifsicle binaries.
- External encoders are discovered/configured at runtime.
- Real click-through capture hole, not a visual transparency trick.
- DXGI Desktop Duplication capture.
- GIF-first export workflow.
- Copy GIF directly to clipboard using a real temporary `.gif` file exposed through `CF_HDROP`.
- DPI-aware and multi-monitor-safe geometry.
- Responsive UI during capture and export.
- Bounded recording memory use.

## 3. Agent Operating Rules

The agent should follow these rules throughout the project:

1. Keep all screen/capture geometry in physical pixels below the UI boundary.
2. Keep UI, Win32, capture, recording, export, and editor code separated.
3. Never encode on the capture thread.
4. Never let frame queues grow without a bound.
5. Use RAII wrappers for Win32 handles, COM interfaces, clipboard memory, mapped textures, temp directories, and child processes.
6. Prefer Windows SDK facilities already present on the OS over bundled libraries.
7. Use Windows Imaging Component for PNG materialization when needed.
8. Use external encoder processes for production GIF/video encoding.
9. Every external process command must be constructed through tested argument builders.
10. Every recoverable failure must become a status message plus a log entry, not a crash.
11. Every spike and milestone must update `docs/decision-log.md`.
12. Every milestone must include an executable size check.

## 4. Proposed Repository Layout

```text
native/
  CMakeLists.txt
  cmake/
  docs/
    decision-log.md
    spike-outcomes.md
    architecture.md
    manual-test-plan.md
    encoder-setup.md
  spikes/
    s01_window_hole/
    s02_dxgi_capture_alignment/
    s03_pointer_shape/
    s04_recording_pipeline/
    s05_gif_export_planner/
    s06_clipboard_filedrop/
    s07_preview_rendering/
    s08_editor_timeline/
  src/
    gifler_app/
    gifler_core/
    gifler_win32/
    gifler_capture_dxgi/
    gifler_record/
    gifler_export/
    gifler_editor/
  tests/
  tools/
  dist/
```

## 5. Phase 0 — Native Scaffold

### Goal

Create the smallest possible native application foundation before any product features.

### Tasks

- Create `native/CMakeLists.txt`.
- Configure C++20 or C++23 with MSVC.
- Add `Gifler.exe` Win32 subsystem target.
- Add test target using a small header-only test framework.
- Add a DPI-aware manifest.
- Add logging to `%LOCALAPPDATA%/Gifler/Logs`.
- Add settings loading/saving:
  - portable settings file next to the exe when writable,
  - otherwise `%APPDATA%/Gifler/settings.ini` or `.json`.
- Add basic Win32 helpers:
  - `unique_handle`,
  - COM smart pointer wrapper or `Microsoft::WRL::ComPtr`,
  - UTF-8/UTF-16 conversion,
  - file path helpers,
  - temp directory helper,
  - process launcher skeleton.
- Add `tools/package-release.ps1` or `.cmd`.

### Acceptance Criteria

- `cmake --build` produces `Gifler.exe`.
- App launches and shows an empty native window.
- Tests run with `ctest`.
- Release build size is recorded in `docs/decision-log.md`.
- No non-system DLLs are required beside `Gifler.exe`.

## 6. Spike Pass

## Spike S01 — Real Click-Through Window Hole

### Question

Can one top-level Win32 window provide a real capture hole, clickable controls, dragging, resizing, topmost behavior, and correct DPI behavior?

### Build

Create `spikes/s01_window_hole`.

Implement a minimal window with:

- custom compact title/command strip,
- child buttons for `Rec`, `Frame`, `Edit`, `Save`, `Copy GIF`, `Play`,
- a central viewfinder rectangle,
- `SetWindowRgn` with the viewfinder cut out,
- `WS_EX_TOPMOST`,
- `WM_NCHITTEST` handling for drag and resize,
- `WM_DPICHANGED` handling,
- visible status text showing logical rect and physical rect.

### Tests

Manual tests:

- Click inside the hole reaches Notepad/browser behind the app.
- Buttons remain clickable.
- Top strip drags the window except over interactive controls.
- Bottom-right resize is easy to hit.
- Hole stays aligned while resizing.
- Test at 100%, 125%, and 150% DPI.
- Test moving between monitors with different DPI.

### Decision

If this works, final uses one top-level HWND with a cut-out region.

If this fails, final uses the fallback multi-HWND strip approach:

- top strip HWND,
- left/right/bottom border HWNDs,
- no center HWND at all.

Do not use `WS_EX_TRANSPARENT` for the whole app as the primary solution because it risks making controls unreliable.

## Spike S02 — DXGI Capture Alignment Behind Hole

### Question

Can DXGI Desktop Duplication capture exactly the physical pixels visible through the window hole without capturing Gifler UI chrome?

### Build

Create `spikes/s02_dxgi_capture_alignment`.

Implement:

- D3D11 device creation,
- output/monitor enumeration,
- single-monitor Desktop Duplication,
- capture rectangle derived from the S01 window hole,
- staging texture copy,
- BGRA crop into CPU memory,
- WIC PNG or BMP debug save,
- access-lost handling skeleton.

### Tests

Manual tests:

- Put the hole over a browser with high-contrast content.
- Capture one frame.
- Saved image matches the hole contents pixel-for-pixel within normal compositor expectations.
- App chrome is not captured.
- Repeat at 100%, 125%, and 150% DPI.
- Repeat on each monitor.
- Capture partly outside the monitor is clipped or rejected gracefully.

### Decision

If single-monitor capture works, implement single-monitor capture first in product code.

If spanning monitors is unreliable, product v1 should detect spanning capture and show a clear warning until stitching is implemented.

## Spike S03 — Cursor Pointer Shape Capture

### Question

Can cursor metadata from Desktop Duplication be converted and composited correctly?

### Build

Create `spikes/s03_pointer_shape`.

Implement:

- `IDXGIOutputDuplication::GetFramePointerShape`,
- pointer shape cache,
- color pointer conversion,
- masked-color pointer conversion,
- monochrome pointer conversion,
- hotspot correction,
- cursor visibility handling,
- BGRA compositing into captured frame.

### Tests

Manual tests:

- Normal arrow cursor.
- Text cursor.
- Resize cursor.
- Hand cursor.
- Custom/high-contrast cursor if available.
- Cursor partially outside capture rect.
- Cursor capture toggle on/off.

### Decision

If all pointer types are not solved in the spike, product v1 can ship cursor capture as optional with unsupported shape fallback, but it must never crash.

## Spike S04 — Recording Pipeline, Coalescing, and Frame Store

### Question

Can recording stay responsive and bounded while producing a clean frame sequence with durations and changed bounds?

### Build

Create `spikes/s04_recording_pipeline`.

Implement:

- synthetic frame generator,
- optional live DXGI frame source from S02,
- bounded queue,
- capture thread,
- recording worker thread,
- duplicate-frame detection,
- duplicate coalescing by extending previous duration,
- changed-rectangle computation,
- in-memory frame store,
- preliminary disk-spill frame store interface.

### Tests

Automated tests:

- duplicate frames coalesce into one frame with longer duration,
- changed rectangle is exact for synthetic frames,
- queue never exceeds configured capacity,
- stop drains or cancels deterministically,
- cancellation releases memory,
- timestamps/durations are monotonic.

Manual tests:

- Record 5 seconds at 10 FPS.
- Record 30 seconds at 10 FPS.
- UI remains responsive.
- Memory usage is recorded.

### Decision

Final product uses the same bounded architecture. If memory usage is too high, disk-spill becomes required before GIF export work proceeds.

## Spike S05 — GIF Export Planner and Encoder Integration

### Question

Can the app produce practical GIFs under the default 10 MB Discord/GitHub target without bundling encoders?

### Build

Create `spikes/s05_gif_export_planner`.

Implement:

- encoder path discovery:
  - configured path,
  - app-local `encoders/` folder if user manually places files there,
  - `PATH`,
  - settings UI stub or config file,
- WIC PNG frame materialization from BGRA frames,
- FFmpeg palette GIF exporter,
- gifski exporter if configured,
- optional gifsicle optimizer,
- target-size attempt planner,
- export summary object,
- cache key for prepared GIFs.

### Planner Algorithm

For Auto GIF:

1. Generate source candidate at source size and requested FPS.
2. If gifski is configured, try gifski first.
3. If under internal target, accept.
4. If too large, reduce gifski quality.
5. If still too large, try FFmpeg palette GIF.
6. Reduce palette color count.
7. Reduce FPS down to minimum FPS.
8. Reduce dimensions down to minimum width.
9. Run gifsicle `-O3` when available.
10. Rank candidates:
    - under target first,
    - higher dimensions,
    - higher FPS,
    - higher color count/quality,
    - lower encode time as tie-breaker.
11. If no candidate hits target, return best candidate plus a warning and suggested video alternatives.

### Tests

Automated tests:

- missing encoder path returns actionable error,
- command builder quotes paths correctly,
- planner tries gifski before FFmpeg when configured,
- planner falls back to FFmpeg when gifski is missing,
- planner reduces quality before FPS,
- planner reduces FPS before dimensions,
- planner never mutates source recording,
- candidate ranking picks best under target,
- no-hit case returns best attempt with warning.

Manual tests:

- Export 3 short recordings.
- Record sizes, dimensions, FPS, encode times, encoder used.
- Verify default target is displayed as 10 MB and internal target is 9.5 MB.

### Decision

Final v1 uses PNG materialization through WIC if performance is acceptable.

If PNG materialization is too slow or disk-heavy, implement raw BGRA streaming to FFmpeg as an optimization, but keep PNG as the compatibility path for gifski.

## Spike S06 — Copy GIF to Clipboard

### Question

Can the app copy an animated GIF in a way that Discord/GitHub/browser paste targets treat as a file upload without forcing the user through Save As?

### Build

Create `spikes/s06_clipboard_filedrop`.

Implement:

- encode or use an existing `.gif` file,
- place it in `%LOCALAPPDATA%/Gifler/Clipboard`,
- open the Win32 clipboard,
- empty clipboard,
- allocate a `DROPFILES` structure with a double-NUL-terminated Unicode file path,
- set `CF_HDROP`,
- keep the temp file alive,
- clean old clipboard cache files on app startup.

### Tests

Manual tests:

- Paste into Explorer folder view.
- Paste into Discord.
- Paste into GitHub issue/comment upload area if supported by browser.
- Paste into Slack/Teams if available.
- Try while another app has the clipboard locked.

### Decision

Final product uses `CF_HDROP` for animated GIF copy. Do not use `CF_DIB` for animated GIF because that usually pastes only a still image.

## Spike S07 — Preview Rendering in the Viewfinder

### Question

Can the app switch between capture-hole mode and preview-paint mode cleanly?

### Build

Create `spikes/s07_preview_rendering`.

Implement:

- toggle from cut-out region to full window region,
- render frames in the viewfinder rectangle,
- use Direct2D bitmap rendering or D3D11 texture upload,
- cover-style scaling equivalent to `UniformToFill`,
- play/pause loop using frame durations,
- return to click-through hole mode when recording starts.

### Tests

Manual tests:

- Stop recording and immediately see first frame.
- Play loops at correct timing.
- Resizing while previewing keeps cover scaling correct.
- Starting a new recording restores click-through mode.

### Decision

Use Direct2D for v1 unless profiling shows frame upload/rendering bottlenecks.

## Spike S08 — Editor Timeline Virtualization

### Question

Can the editor show frame timelines for long recordings without creating thousands of controls?

### Build

Create `spikes/s08_editor_timeline`.

Implement:

- native editor window,
- custom-painted timeline view,
- virtualized thumbnails,
- start/end trim markers,
- frame selection,
- right-click context menu,
- non-destructive edit model,
- changed-area/green-screen visualization proof.

### Tests

Manual tests:

- Load 100 frames.
- Load 1,000 frames.
- Scroll remains responsive.
- Trim range updates preview/export sequence.
- Delete-frame commands operate on the non-destructive edit layer.

### Decision

Editor ships after core record/export/copy path. It must not block the MVP.

## 7. Spike Gate

After all critical spikes, produce `docs/spike-outcomes.md` with this table:

```text
Spike | Result | Final decision | Product impact | Follow-up tasks
S01   | Pass/Fail | ... | ... | ...
S02   | Pass/Fail | ... | ... | ...
S03   | Pass/Fail | ... | ... | ...
S04   | Pass/Fail | ... | ... | ...
S05   | Pass/Fail | ... | ... | ...
S06   | Pass/Fail | ... | ... | ...
S07   | Pass/Fail | ... | ... | ...
S08   | Pass/Fail | ... | ... | ...
```

Do not proceed to the final product implementation until these are pass or have explicit fallback decisions:

- S01 real click-through hole,
- S02 capture alignment,
- S05 GIF export planner,
- S06 clipboard file drop.

## 8. Product Pass

## Milestone 1 — Core Library and Geometry

### Implement

In `src/gifler_core`:

- `PixelPoint`, `PixelSize`, `PixelRect`, `DpiScale`.
- Rect intersection, clipping, translation, scaling.
- `BgraFrame` with explicit stride, timestamp, duration, changed bounds, optional cursor.
- `FrameSequence` and `IFrameStore` interfaces.
- `CaptureSettings`, `RecordingSettings`, `ExportSettings`.
- App state enums:
  - idle,
  - recording,
  - stopping,
  - preview,
  - exporting,
  - failed.

### Tests

- Geometry intersection.
- DPI logical/physical conversion.
- Empty/invalid capture rect validation.
- Frame duration aggregation.

## Milestone 2 — Final Main Window Shell

### Implement

In `src/gifler_app` and `src/gifler_win32`:

- top-level main window,
- command strip,
- menus,
- click-through viewfinder region,
- hit testing,
- resize grip,
- status text,
- always-on-top toggle or default always-on-top,
- DPI-change handling,
- capture rect calculation,
- mode switch between capture-hole and preview-paint.

### UI Commands

Visible commands for v1:

- `Rec` / `Stop`,
- cursor toggle,
- FPS dropdown: 5, 10, 15, 30,
- `Frame`,
- `Edit`,
- `Save`,
- `Copy GIF`,
- `Play`.

### Acceptance

- Window feels like GifCam: compact, movable, resizable.
- Hole is genuinely click-through.
- No dead zone in top drag area.
- Physical capture rect shown in debug/status.

## Milestone 3 — DXGI Capture Module

### Implement

In `src/gifler_capture_dxgi`:

- adapter/output enumeration,
- monitor physical rect mapping,
- D3D11 device setup,
- duplication session lifecycle,
- `AcquireNextFrame` loop,
- staging texture copy,
- BGRA crop,
- cursor shape cache and compositing,
- access-lost recovery path,
- single-monitor support first,
- multi-monitor stitching after single-monitor stability.

### Acceptance

- Captures exact visible content behind hole.
- Cursor toggle works.
- Access loss stops or reinitializes cleanly.
- Capture module can be tested without UI through a simple capture command.

## Milestone 4 — Recording Module

### Implement

In `src/gifler_record`:

- `RecorderSession`,
- bounded frame queue,
- capture thread control,
- recording worker,
- duplicate detection,
- duplicate duration coalescing,
- changed-bounds detection,
- in-memory store,
- disk-spill store or at least finalized interface,
- stop/cancel/drain semantics.

### Acceptance

- Record/Stop works repeatedly.
- UI remains responsive.
- Memory use is bounded or clearly limited by recording length.
- Recorded sequence preserves timing.

## Milestone 5 — Preview Playback

### Implement

In `src/gifler_app`:

- `PreviewPlayer`,
- first-frame preview after stop,
- play/pause loop,
- frame-duration scheduling,
- render in viewfinder with cover scaling,
- return to capture-hole mode on new recording.

### Acceptance

- Preview fills the viewfinder correctly.
- No weird offsets or letterboxing unless explicitly selected later.
- Playback timing matches recording durations.

## Milestone 6 — GIF Export and Prepared Size

### Implement

In `src/gifler_export`:

- encoder discovery,
- process runner with stdout/stderr capture,
- WIC PNG sequence materializer,
- FFmpeg palette GIF exporter,
- gifski exporter,
- optional gifsicle optimizer,
- `GifExportPlanner`,
- target-size attempts,
- export summary,
- prepared GIF cache.

### Default Preset

Use this default:

- Display target: 10 MB.
- Internal safety target: 9.5 MB.
- Default format: Auto GIF.
- Minimum FPS: 5.
- Minimum width: 320 px.

### Acceptance

- After recording, app can prepare GIF size in background.
- Status shows:
  - `GIF: --`,
  - `GIF: ...`,
  - `GIF: 2.4 MB`,
  - or `GIF: failed`.
- Save writes GIF to selected path.
- Export summary includes size, target, dimensions, FPS, duration, frame count, engine, and warnings.

## Milestone 7 — Copy GIF

### Implement

In `src/gifler_win32/Clipboard.cpp` and app command handling:

- reuse prepared GIF when cache key matches,
- otherwise prepare GIF first,
- write/copy GIF file into clipboard cache,
- publish `CF_HDROP`,
- status summary after copy,
- cleanup old clipboard cache files on startup.

### Acceptance

- User clicks `Copy GIF` and can paste animated GIF into Discord as a file.
- No user-visible Save As step is required.
- Stale prepared GIFs are not reused after edits/export setting changes.

## Milestone 8 — Video and Modern Alternates

### Implement

- MP4 H.264 exporter through FFmpeg.
- Animated WebP exporter through FFmpeg.
- Optional MP4 AV1/WebM AV1 exporters.
- Target-size bitrate calculation for MP4 H.264.
- UI path for alternates when GIF target cannot be reached.

### Acceptance

- GIF remains the default.
- MP4 H.264 and Animated WebP are available alternates.
- No video alternate blocks GIF copy workflow.

## Milestone 9 — Editor

### Implement

In `src/gifler_editor`:

- editor window,
- virtualized timeline,
- frame thumbnails,
- selected frame preview,
- start/end trimming,
- delete this frame,
- delete from frame to start,
- delete from frame to end,
- delete even frames,
- reverse frames,
- changed-area/green-screen visualization,
- non-destructive edited sequence wrapper.

### Acceptance

- User can trim precisely.
- Large recordings remain responsive.
- Edited sequence previews and exports.

## Milestone 10 — Hardening and Packaging

### Implement

- release build script,
- PDB separated from portable exe,
- executable size report,
- smoke-test checklist,
- encoder setup documentation,
- crash/error logs,
- version info resource,
- application icon,
- optional code signing hook.

### Release Flags

Use release settings similar to:

- `/O2`,
- `/GL`,
- `/Gy`,
- linker `/LTCG`,
- linker `/OPT:REF`,
- linker `/OPT:ICF`.

Evaluate `/MT` versus dynamic CRT based on final size and portability tradeoff.

### Acceptance

- `dist/Gifler.exe` launches on a clean supported Windows machine.
- No app-specific sibling DLLs are required.
- No runtime extraction occurs.
- Exe is under 10 MB or every excess dependency is justified in `docs/decision-log.md`.

## 9. Manual Test Matrix

Run this before declaring v1 done:

- Launch from arbitrary folder.
- Always-on-top behavior.
- Drag from every intended top-strip area.
- Resize from bottom-right.
- Click-through hole over Notepad/browser.
- Controls remain clickable.
- Capture rect matches hole.
- Record/Stop repeatedly.
- Preview fills hole.
- Cursor capture on/off.
- Cursor position and size correctness.
- Save GIF.
- Copy GIF to Discord.
- Missing FFmpeg path.
- Missing gifski path.
- Missing gifsicle path.
- 100%, 125%, 150% DPI.
- Mixed-DPI monitor move.
- Single-monitor capture on each monitor.
- Spanning-monitor behavior.
- Display sleep/wake or display configuration change.
- Long recording memory behavior.
- Export cancellation.
- Clipboard locked by another app.

## 10. Automated Test Targets

Create tests for:

- geometry intersection/clipping,
- DPI conversion,
- window region geometry,
- frame differ changed bounds,
- duplicate coalescing,
- bounded queue capacity,
- recorder stop/cancel semantics,
- export cache key invalidation,
- GIF candidate generation,
- GIF candidate ranking,
- no-hit target warning,
- FFmpeg argument building,
- gifski argument building,
- gifsicle optional optimizer behavior,
- missing encoder errors,
- clipboard `DROPFILES` memory layout,
- settings round trip,
- temp directory cleanup.

## 11. Fallback Matrix

| Risk | Primary approach | Fallback |
| --- | --- | --- |
| One-window hole is unreliable | `SetWindowRgn` cut-out | Multi-HWND border/control strips |
| DXGI capture spanning monitors is hard | Single-monitor v1 | Detect span and warn; add stitcher later |
| Cursor shapes are inconsistent | Full pointer shape conversion | Cursor capture optional with safe fallback |
| PNG materialization is slow | WIC PNG sequence | Raw BGRA pipe to FFmpeg |
| GIF cannot hit 10 MB | Planner degrades quality/FPS/size | Explain miss and offer MP4/WebP |
| Clipboard paste target rejects file drop | `CF_HDROP` temp GIF | Save file and copy path as last-resort helper |
| Exe exceeds 10 MB | Win32/D2D/D3D11 only | Audit dependencies; remove heavy libs |
| Unsigned exe warnings | Optional signing hook | Document expected warning for private builds |

## 12. MVP Definition

The MVP is complete when:

- one native `Gifler.exe` launches,
- window has a real click-through hole,
- window can record the hole with DXGI,
- user can stop and preview,
- user can export GIF,
- user can copy GIF to clipboard through `CF_HDROP`,
- default GIF target is 10 MB display / 9.5 MB internal,
- missing encoders produce clear messages,
- app remains responsive while recording/exporting.

The editor and modern video alternates can follow after this MVP unless they are needed for your first release promise.

## 13. Agent's First Concrete Task List

Start with these exact tasks:

1. Create `native/` scaffold with CMake, WinMain, logging, settings, and tests.
2. Create `docs/decision-log.md` and `docs/spike-outcomes.md`.
3. Implement S01 window-hole spike.
4. Record the S01 decision and screenshots/logs.
5. Implement S02 capture-alignment spike.
6. Record the S02 decision and sample captured images.
7. Implement S05 export-planner spike using synthetic frames.
8. Implement S06 clipboard-filedrop spike using a known GIF.
9. Only after S01/S02/S05/S06 are green, start product Milestone 1.
