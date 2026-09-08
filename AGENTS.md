# Agent Instructions for Gifler

## Mission

Build Gifler as a native Windows C++ replacement for a GifCam-style recorder. Start with proof-of-concept spikes for the high-risk details, then implement the product using only approaches that passed.

## Hard Product Constraints

- Native C++ Windows application.
- One portable `Gifler.exe` target.
- No .NET, WPF, WinUI, Electron, Qt, wxWidgets, or runtime extraction.
- Keep release executable under 10 MB unless a dependency is explicitly justified in `docs/decision-log.md`.
- Do not bundle FFmpeg/gifski/gifsicle binaries.
- GIF is the default export workflow; MP4, animated WebP, and WebM are also supported.
- Clipboard copy must follow the selected media format and expose a real file through `CF_HDROP`.
- Capture coordinates below the UI layer are physical pixels.

## Current Product State

The application includes:

- a working Win32 application with a region-cut click-through viewfinder,
- a window-hole spike,
- a DXGI capture-once spike,
- an asynchronous recording and export pipeline,
- a GIF planner spike,
- a clipboard file-drop spike,
- unit and native smoke coverage.

Treat spike programs as diagnostics; product behavior belongs under `src/`.

## Required First Step

Run and validate these critical spikes before expanding the final app:

1. `spike_s01_window_hole`
2. `spike_s02_dxgi_capture_alignment`
3. `spike_s05_gif_export_planner`
4. `spike_s06_clipboard_filedrop`

Update `docs/spike-outcomes.md` after each one.

## Coding Rules

- Keep UI, Win32 interop, capture, recording, export, and editor code separated.
- Never encode on the capture thread.
- Never allow unbounded frame queues.
- Use RAII for Win32 handles, COM objects, mapped textures, global memory, process handles, temp directories, and clipboard resources.
- Prefer small standard-library code and Windows SDK APIs over third-party libraries.
- Use WIC for PNG frame materialization until profiling proves it is too slow.
- Put every external process command behind tested argument builders.
- Never mutate the source recording during export planning.
- Log raw details, but show user-friendly errors in the UI.
- Keep missing encoder behavior actionable, not fatal to the app.

## Spike Gate

Do not proceed to product milestone work until `docs/spike-outcomes.md` records a pass or explicit fallback decision for:

- S01 real click-through window hole,
- S02 DXGI capture alignment,
- S05 GIF export planning,
- S06 clipboard file drop.

## Promotion Rule

Spike code may be copied into `src/` only after:

- the manual test outcome is recorded,
- the fallback decision is recorded,
- the code has been stripped of throwaway diagnostics,
- ownership/lifetime rules are cleaned up,
- a test or manual smoke checklist exists.

## Definition of Done for MVP

- `Gifler.exe` launches without .NET.
- The viewfinder is genuinely click-through in capture mode.
- Record/Stop captures the visible hole.
- Preview fills the viewfinder after Stop.
- Save exports GIF by default.
- Copy follows the selected GIF, MP4, WebP, or WebM format.
- Default target is 10 MB display / 9.5 MB internal.
- Missing encoders produce clear setup messages.
- Unit tests and manual smoke tests pass.
