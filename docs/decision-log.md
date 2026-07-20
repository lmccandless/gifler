# Decision Log

Record every technical decision that affects product architecture, portability, executable size, or user-visible behavior.

## Template

```text
Date:
Area:
Decision:
Reason:
Alternatives considered:
Impact on size/portability:
Follow-up:
```

## Initial Decisions

### Use Win32 instead of a large UI framework

Decision: Use direct Win32 for the main shell and editor v1.

Reason: The release target is a small portable native executable without .NET/WPF/Qt/Electron packaging overhead.

### Use external encoders initially

Decision: Discover FFmpeg/gifski/gifsicle at runtime instead of bundling them.

Reason: Bundling encoder binaries would dominate app size and complicate distribution. The app can remain small while still producing high-quality GIFs when encoders are configured.

## 2026-06-04

### Support Visual Studio Build Tools 2026 during bootstrap

Decision: Keep the Visual Studio 2022 presets, add Visual Studio 2026 presets, and make the PowerShell build/test/package scripts select the installed generation automatically.

Reason: The current development machine has Visual Studio Build Tools 2026 installed and no Visual Studio 2022 instance. The starter should still honor the original VS2022 path when present.

Alternatives considered: Installing VS2022 before any work; replacing Visual Studio generators with Ninja. Those would either block bootstrap or drift from the starter's intended Windows/MSVC workflow.

Impact on size/portability: No runtime impact.

Follow-up: If CI is added, pin the generator used there and keep local scripts flexible.

### Keep COM initialized until WIC objects are released

Decision: Use an RAII COM initialization guard in the WIC PNG writer so `CoUninitialize` runs after WIC `ComPtr` objects are destroyed.

Reason: S02 wrote its PNG and then exited with access violation `0xC0000005`; the writer was uninitializing COM before local WIC objects released.

Alternatives considered: Leaving S02 as an environment failure. The crash was deterministic after PNG write and independent of the black capture output, so fixing the lifetime bug was the correct scaffold fix.

Impact on size/portability: No meaningful size impact; improves correctness.

Follow-up: Add unit or smoke coverage around WIC writer lifetime once image-writing tests are introduced.

### Use a bounded two-stage recording pipeline

Decision: Shape `RecorderSession` around a bounded frame queue with a capture producer thread and a recording worker thread, even for the synthetic source.

Reason: The implementation plan requires recording to remain bounded and forbids encoding or heavy processing on the capture thread. The synthetic path now exercises the same architecture that live DXGI capture should feed.

Alternatives considered: Keeping the original single worker loop until DXGI recording is wired. That made the S04 spike too weak because it did not validate queue capacity, producer shutdown, or worker drain behavior.

Impact on size/portability: No runtime dependency impact.

Follow-up: Add a live DXGI producer, cancellation policy, memory limits, and long-recording measurements before promoting S04 as fully complete.

### Release size after synthetic recording pipeline work

Decision: Keep the current Win32/MSVC static module approach; no dependency change is needed for the recording pipeline slice.

Reason: `scripts/package-release.ps1` produced `Gifler.exe` at 225,280 bytes, well below the 10 MB target.

Alternatives considered: None needed for this slice.

Impact on size/portability: Current release executable remains 0.21 MB and uses no app-specific sibling DLLs.

Follow-up: Continue recording release executable size after product milestones and before adding any dependency.

### Render main-window preview with GDI until capture path is proven

Decision: Wire `Gifler.exe` Rec/Stop to the synthetic `RecorderSession`, switch the viewfinder from hole mode to solid preview mode after Stop, and render BGRA frames with `StretchDIBits` using cover-style scaling.

Reason: This advances the MVP preview workflow without promoting unproven DXGI capture alignment. GDI keeps the implementation small and deterministic while the S02 desktop-capture gate remains manual.

Alternatives considered: Implementing Direct2D/D3D preview immediately. That is still likely for final polish, but it would add complexity before live captured frames and preview timing are proven.

Impact on size/portability: `scripts/package-release.ps1` produced `Gifler.exe` at 240,640 bytes, still far below the 10 MB target and with no new dependencies.

Follow-up: Feed live DXGI frames into the recorder, validate playback resize/timing manually, then decide whether to keep GDI or switch to Direct2D/D3D for final preview rendering.

### Wire live DXGI capture into the product recorder

Decision: Add `RecorderSession::start_dxgi` and make the main `Rec` command capture the physical viewfinder rectangle through `DxgiCaptureProvider` into the bounded recording queue.

Reason: The MVP requires Record/Stop to capture the hole with DXGI, not only a synthetic source. This change keeps capture production separated from recording/coalescing and still avoids encoding on the capture thread.

Alternatives considered: Waiting for a long-lived DXGI duplication session before wiring the app. That would delay product integration; using the existing spike-grade `capture_one_frame` path exposes UI/status and pipeline issues earlier while keeping the later optimization localized.

Impact on size/portability: `scripts/package-release.ps1` produced `Gifler.exe` at 252,928 bytes, still far below the 10 MB target and with no new dependencies.

Follow-up: Replace per-frame duplication with a long-lived DXGI session, prove nonblack/aligned content on an interactive desktop, add access-loss recovery, and add cursor compositing.

### Add GIF materialization and clipboard copy workflow

Decision: Add a `gifler_export` recording exporter that writes BGRA frames to WIC PNGs, runs discovered external GIF encoders, and wires `Save`/`Copy GIF` through it. Copy GIF writes into `%LOCALAPPDATA%\Gifler\Clipboard` and publishes the file through `CF_HDROP`.

Reason: The MVP needs GIF-first export and copy without a Save As step. Keeping materialization and process execution in `gifler_export` preserves the UI/export boundary and keeps encoder binaries external.

Alternatives considered: Building a native GIF encoder now. External FFmpeg/gifski remains the planned v1 path and avoids a large or low-quality in-app encoder implementation.

Impact on size/portability: `scripts/package-release.ps1` produced `Gifler.exe` at 331,776 bytes. No encoder binaries are bundled.

Follow-up: Add actual output-size candidate ranking, cancellation, stdout/stderr capture, prepared-cache keys beyond frame count, animated/nonblack export smoke, and manual paste validation in Discord/GitHub/Explorer.

### Add FFmpeg video alternate exporters

Decision: Add MP4 H.264 and Animated WebP exporter APIs in `gifler_export`, backed by the same WIC PNG frame materialization path as GIF. MP4 H.264 uses a target-size bitrate calculation and two-pass FFmpeg command sequence; Animated WebP uses FFmpeg's `libwebp_anim` encoder.

Reason: The implementation plan requires MP4 H.264 and Animated WebP alternates while preserving GIF as the default workflow. Keeping these behind export APIs lets the UI expose alternates later without changing recorder or capture code.

Alternatives considered: Waiting for a completed export settings UI before adding alternates. Implementing the exporter first makes the alternates testable and keeps UI work separate.

Impact on size/portability: `scripts/package-release.ps1` produced `Gifler.exe` at 331,776 bytes. FFmpeg remains external and is not bundled.

Follow-up: Add UI selection for alternates, WebM/AV1 optional paths, cancellation/stdout capture, and manual target-app validation. FFmpeg 7.0.1 rejected decoding its own animated WebP artifact, while ImageMagick identified it as 24-frame WebP, so target-app compatibility must be checked.

### Add native editor timeline shell

Decision: Replace the editor placeholder with a custom-painted Win32 editor window backed by a small non-destructive `EditorModel`. The editor copies the current recording for inspection, renders a selected-frame preview, virtualizes timeline cells with a scrollbar, and supports Set Start, Set End, Delete, Delete Even, and Reset.

Reason: The implementation plan requires a native editor with timeline virtualization and non-destructive trim/delete operations. This keeps editor behavior in `gifler_editor` and avoids creating one HWND per frame.

Alternatives considered: Waiting until export/capture polish is complete. The editor model and shell can be built independently and tested now, while applying edits back to export remains a follow-up.

Impact on size/portability: `scripts/package-release.ps1` produced `Gifler.exe` at 340,992 bytes. No new dependencies were added.

Follow-up: Apply edited sequences to preview/export, add right-click context menu operations, green-screen/change visualization, and validate 1,000-frame timeline responsiveness.

### Apply editor changes to main preview and export

Decision: Add an editor Apply command that materializes the non-destructive edit model into a frame sequence and hands it back to the main window. The main preview, Save GIF, and Copy GIF paths now prefer the applied edited sequence until a new recording starts.

Reason: Editing needs to affect the user-visible recording, not only the editor window. Keeping materialization in `gifler_editor` and applying the result through a callback preserves the editor/app boundary without letting export code know about editor state.

Alternatives considered: Mutating the recorder frame store directly. That would blur recording and editor responsibilities and make Reset/new-record behavior harder to reason about.

Impact on size/portability: `scripts/package-release.ps1` produced `Gifler.exe` at 346,624 bytes. No new dependencies were added.

Follow-up: Add right-click context menu operations, green-screen/change visualization, stronger empty-edit UX, and validate 1,000-frame timeline responsiveness.

### Expose alternate export formats in the toolbar

Decision: Add a compact export format combo to the main toolbar with GIF as the default and route Save to GIF, MP4 H.264, or Animated WebP based on the selected format. Copy GIF remains GIF-only and continues to use the clipboard file-drop cache.

Reason: The video exporter APIs were implemented but unreachable from the product shell. A combo keeps the small toolbar model intact while making alternate exports available without changing recorder, editor, or clipboard behavior.

Alternatives considered: Adding separate Save MP4 and Save WebP buttons. That would crowd the default window and duplicate the same command flow.

Impact on size/portability: `scripts/package-release.ps1` produced `Gifler.exe` at 361,984 bytes. No dependency change; FFmpeg remains discovered externally.

Follow-up: Add export progress/cancel UI, explicit target-size/quality controls, and manual app compatibility validation for Animated WebP.

### Composite the Win32 cursor in captured frames

Decision: Add a `captureCursor` option to `DxgiCaptureProvider::capture_one_frame` and composite the currently visible Win32 cursor into the captured BGRA frame using hotspot-correct screen coordinates. The recorder passes the existing main-window Cursor toggle through to this option.

Reason: Optional cursor recording is part of the MVP, and the UI already exposed a toggle that did not affect capture. Win32 cursor overlay gives a usable product v1 path without waiting for full Desktop Duplication pointer-shape conversion.

Alternatives considered: Implementing DXGI `GetFramePointerShape` first. That remains the better long-term source for pointer shape metadata, but it is a larger conversion/test task and should not block the basic cursor-recording toggle.

Impact on size/portability: `scripts/package-release.ps1` produced `Gifler.exe` at 364,544 bytes. No dependency change; the capture module already depends on Win32/GDI-capable Windows APIs.

Follow-up: Validate cursor alignment against nonblack desktop content at 100%, 125%, 150%, and mixed DPI; add DXGI pointer-shape support and synthetic conversion tests for color, masked color, and monochrome cursors.

### Start as a normal themed window

Decision: Add the Common Controls v6 manifest dependency, initialize common controls before creating the main window, and replace the product top-level `SetWindowRgn` hole with a color-keyed layered window. Idle and recording paint the viewfinder with a transparent color key; preview mode paints normal frame pixels.

Reason: Applying or clearing `SetWindowRgn` on the top-level HWND changed nonclient frame behavior, so the titlebar appeared to toggle between decorated and undecorated states around Record/Stop. A layered color-key preserves normal themed DWM chrome while keeping the viewfinder visually transparent and click-through.

Alternatives considered: Deferring `SetWindowRgn` until after first show. That fixed the first-paint issue but still left the frame dependent on region state. Keeping the window rectangular until recording fixed chrome but regressed idle pass-through.

Impact on size/portability: `scripts/package-release.ps1` produced `Gifler.exe` at 365,568 bytes. No runtime dependency is bundled; `comctl32` is a Windows system library.

Follow-up: Validate layered color-key hit testing against real click targets at 100%, 125%, 150%, and mixed DPI. Keep the S01 `SetWindowRgn` spike as a fallback reference only.

### Keep the viewfinder pass-through except during playback

Decision: Stop no longer switches the main viewfinder into preview mode. Recorded frames are painted only while Play is actively running; stopping playback returns the viewfinder to pass-through mode.

Reason: The viewfinder is primarily a positioning/capture hole, so it should remain visually pass-through after recording completes. Showing the final captured frame at rest is confusing when the capture path returns black or stale content.

Alternatives considered: Automatically previewing the first/last frame after Stop. That makes saved content visible sooner, but it breaks the always-pass-through framing behavior expected from the recorder shell.

Impact on size/portability: No dependency or size impact.

Follow-up: Add a separate editor/preview surface if users need still-frame inspection without occupying the live viewfinder hole.

### Fall back to GDI when DXGI captures the layered hole as black

Decision: Keep the DXGI Desktop Duplication path as the first capture attempt, but if it returns an all-black BGR frame, capture the same physical rectangle with a GDI `BitBlt` desktop read and normalize alpha to opaque.

Reason: The layered color-key viewfinder preserves normal window chrome and pass-through behavior, but Desktop Duplication can see the layered transparent area as black instead of the visually exposed desktop pixels. GDI captures the composed screen pixels that the user actually sees through the viewfinder.

Alternatives considered: Using `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` on Gifler while recording. In practice that can still produce a black capture area for this path. Reverting to top-level `SetWindowRgn` restored capture, but reintroduced titlebar decoration toggling.

Impact on size/portability: No dependency change; GDI is already part of the Windows SDK/user32-gdi32 surface.

Follow-up: Promote a dedicated capture strategy selector once long-lived DXGI sessions are implemented, and validate GDI fallback throughput on high-FPS captures.

### Move command controls into a compact menu bar

Decision: Replace the child-control toolbar and bottom status strip with a compact native menu bar. `Rec`, `Play`, `FPS`, and `Copy GIF` are top-level menu-bar entries; cursor, export format, frame, edit, and save commands live under `More`. The status text is appended to the window title.

Reason: The toolbar consumed vertical space and forced a wide minimum window size. A native menu leaves the full client area as the viewfinder and allows much smaller free-form resizing.

Alternatives considered: Keeping every command under one top-level `Menu` entry. That conserved width but made the common recording workflow too hidden.

### Refresh the capture rectangle while recording

Decision: The live recorder now asks the app for the current physical viewfinder rectangle before every captured frame instead of freezing the rectangle selected at Record start.

Reason: Gifler is a movable frame; dragging or resizing it during recording should move the recorded area with the visible viewfinder. The app still reports only the inset viewfinder area, so the menu/title/chrome and opaque resize bands are outside the capture rectangle.

Alternatives considered: Stopping movement during recording or restarting the capture session after every move. Both make the frame less useful as an interactive recorder and are unnecessary while the capture provider can consume fresh rectangles per frame.

Impact on size/portability: No dependency impact. The release executable remains a single native Windows binary.

Follow-up: Validate mixed-monitor moves and decide whether long-lived Desktop Duplication sessions should switch outputs dynamically or prefer the composed-screen GDI strategy while the frame is moving.

### Use composed-screen capture for product recording

Decision: Product recording now captures the composed desktop pixels directly with GDI instead of running the spike-grade per-frame DXGI duplication helper.

Reason: The layered transparent viewfinder can appear black to Desktop Duplication, so the old path often paid the cost of creating a DXGI device/duplication session and then fell back to GDI for every frame. That made 30 FPS recording behave like a low-FPS capture with preserved timing. Direct composed capture matches the visible pixels under the viewfinder and removes the per-frame DXGI setup cost.

Alternatives considered: Keeping DXGI first for every frame. That preserves the spike path, but it is too slow for product recording with the layered window. A long-lived DXGI duplication session remains the better future capture backend once it can reliably exclude Gifler's own layered UI.

Impact on size/portability: No dependency impact; GDI is already part of the Windows API surface used by the app.

Follow-up: Add a capture-throughput smoke that records a moving target at 5, 10, 15, and 30 FPS and reports timeline duration plus stored-frame cadence.

Impact on size/portability: No dependency change.

Follow-up: Add keyboard accelerators for the most common commands once the command set stabilizes.

### Add wider client resize bands

Decision: Reserve an opaque client-margin resize band around the pass-through viewfinder and return explicit resize hit-test codes for the client edges and corners.

Reason: The layered color-key pass-through area does not provide a reliable target for resizing. A wider opaque band makes the resize cursor and click target available without bringing back the toolbar.

Alternatives considered: Relying only on the native nonclient border. That proved too narrow and difficult to hit in practice.

Impact on size/portability: No dependency change.

Follow-up: Tune the band width after manual use across DPI values.

### Stream exports off the UI thread

Decision: Save and Copy run through one worker-thread export path and stream BGRA recording frames directly to FFmpeg over standard input. A native progress bar reports frame-feed progress while the window remains responsive. Frames whose dimensions change during a recording are normalized to the initial output dimensions while streaming.

Reason: Expanding frame pixels in memory, writing every frame as PNG, rereading those files, and blocking the window thread made ordinary saves take tens of seconds and caused the viewfinder to stop repainting. Copy also bypassed the selected format and always synchronously encoded GIF. Direct streaming removes that intermediate work, and one export path keeps Save and Copy behavior consistent. MP4 now uses single-pass veryfast H.264, WebM uses realtime VP9, and animated WebP uses a lower compression effort.

Alternatives considered: Keeping synchronous export and adding only an indeterminate indicator. That would describe the delay without fixing the freeze or the dominant PNG conversion cost.

Impact on size/portability: No new dependency. The implementation uses Win32 pipes and the existing external FFmpeg adapter.

Follow-up: Add cancellation and FFmpeg machine-readable progress if exports gain settings that perform substantial work after all input frames have been consumed.
