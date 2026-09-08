# Spike Outcomes

Fill this in as soon as each spike is run.

## 2026-09-08 Chrome and FPS Follow-up

- Dropdown-width follow-up: native font measurement now determines FPS/format
  widths and arrow position, removing unused space after short labels. Exact-width
  assertions cover 5 FPS/GIF, 30 FPS/MP4, 240 FPS/WebM, and mixed display scaling.
- VSync investigation is recorded in `display-sync-exploration.md`. No capture
  pacing or desktop/compositor behavior was changed by this UI revision.

- Spacing/resize follow-up: separated dropdown arrow/text bounds, clipped and
  double-buffered button painting, suppressed native white erases, and batched
  child positioning without intermediate repainting. Full-row threshold is now
  290 logical pixels to retain readable spacing.
- Right/bottom handles widened to 10 logical pixels with an extended bottom-right
  diagonal target. Native hit-test and region-membership assertions passed.
- Re-ran Debug build, unit tests, custom-dialog/save/cancel UI smoke, and native
  renders/text-fit assertions at 100/125/150/200% scaling. Live resize animation
  remains a manual confirmation rather than an offscreen-test guarantee.

- Subsequent compact revision: charcoal camera-style single 34-pixel top strip,
  no bottom bar, no minimize/maximize buttons, 18/20-pixel icon controls, and a
  6-pixel record dot. Full controls fit at 248 logical pixels; smaller windows
  retain Record/Save/More/Close with secondary actions in More.
- Revalidated control bounds, label text extents (including 240 FPS/WebM), native
  rendered pixels, capture-hole geometry, settings, custom FPS, and GIF save/cancel.

- Kept `SetWindowRgn` for the actual capture hole. Replaced the OS-drawn caption
  and menu bar with custom-painted chrome and accessible native buttons; no DWM,
  compatibility, or global theme settings are changed.
- Native offscreen renders inspected at 120/360/660 logical-pixel widths and
  100/150/200% scaling. Bounds checks and region membership checks passed.
- Custom FPS dialog rejects empty, nonnumeric, 0, and 241; applying 77 succeeds,
  cancellation preserves it, and presets/custom preferences round-trip.
- Unit and UI smoke passed, including asynchronous GIF saving/cancellation.
- FFmpeg/ffprobe verified one-second MP4s at 60, 77, 120, and 240 FPS with the
  expected frame counts. Existing audio and 10/90-second export tests also pass.
- Live capture cadence, mixed-monitor interaction, window snapping, high-contrast
  mode, and assistive-technology behavior remain manual checks.

Icon glyphs use the Windows-provided
[Segoe MDL2 Assets font](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-ui-symbol-font),
so no production dependency was added.

## 2026-09-08 Regression Validation

- Debug unit tests passed, including cumulative frame timing, 20 MB/audio setting
  persistence, and audio interval splicing for edits.
- Export smoke passed with external FFmpeg: GIF and MP4 10.000 s, WebM 10.008 s,
  WebP 9.999 s; all contain 300 video frames. MP4/WebM contain audio. A coalesced
  90-second GIF preserved 90.000 s. Encoder error/cancel/retry checks passed.
- Hidden-window UI smoke passed: compact menus, restart persistence, asynchronous
  GIF export completion, and cancellation preserving the recording. Isolated
  settings were used; no live desktop capture or clipboard replacement occurred.
- WASAPI loopback probe passed on the default device: 48 kHz, 400632 captured bytes
  over the one-second probe, no reported error. No audio file was retained.
- Manual moving-content capture cadence, audible A/V sync, actual clipboard paste,
  and hardware-accelerated-window compositor behavior remain manual checks.

| Spike | Result | Final Decision | Product Impact | Follow-up Tasks |
| --- | --- | --- | --- | --- |
| S01 Window Hole | Launch smoke passed 2026-06-04; manual click-through not yet validated | Keep single HWND + `SetWindowRgn` candidate pending manual click-through and DPI proof | Product can keep viewfinder-hole prototype as preferred approach, but must not promote until manual pass | Run click-through against Notepad/browser at 100%, 125%, 150%, mixed DPI |
| S02 DXGI Capture Alignment | Partial 2026-06-04: executable captures and writes 480x270 PNG, and main app live-DXGI Rec/Stop smoke produced 3 preview frames at `artifacts\app_dxgi_record_smoke.png`; automation session output still black/indirect | DXGI path is wired into product recording, but final alignment/pass decision still needs real interactive desktop validation | Product now records the physical viewfinder rect through DXGI into the bounded recorder; do not mark capture accuracy proven yet | Re-run against visible text/color target in an interactive desktop; compare with S01 hole rect; replace spike-grade per-frame duplication with long-lived session |
| S03 Pointer Shape | Product partial 2026-06-04: `spike_s03_pointer_shape` captured `artifacts\s03_cursor_overlay.png` with visible cursor metadata at position 240,144 and hotspot 0,0; automation desktop background remains black like S02 | Use Win32 cursor overlay as the product v1 cursor-recording path while DXGI pointer-shape conversion remains a follow-up | Cursor toggle now reaches DXGI recording and composites the visible cursor into BGRA frames before recording/export | Validate cursor alignment on real desktop content at multiple DPI values; add DXGI `GetFramePointerShape` support and color/masked/monochrome conversion tests |
| S04 Recording Pipeline | Synthetic pass 2026-06-04: 2s at 10 FPS stored 19 coalesced 160x90 frames; app live-DXGI smoke records into the same queue/worker; unit tests cover bounded queue, duplicate coalescing, frame-store totals, and stop completion | Use bounded capture queue plus separate recording worker for product recording pipeline | Product `RecorderSession` has synthetic and DXGI producers feeding the same recording worker; no encoding on capture thread | Replace spike-grade capture helper with long-lived DXGI session, add long-run memory measurement and stop/cancel drain tests |
| S05 GIF Export Planner | Product partial 2026-06-04: default 10 MB display / 9.5 MB internal target, planner attempts, PNG materialization, FFmpeg palette export, Save wiring, and missing-encoder tests pass | Use planner/materializer/process-adapter approach for GIF v1; gifski/gifsicle remain optional when discovered | Save/Copy can encode GIFs from recorded frames through external encoders without bundling binaries | Add size-based candidate ranking across actual output sizes, export cancellation, stdout/stderr capture, and synthetic animated export smoke |
| S06 Clipboard File Drop | Product partial 2026-06-04: `CF_HDROP` set successfully for `artifacts\s06_sample.gif`; main app Copy GIF encoded via FFmpeg and cached `gifler_clipboard_12104_9171687.gif` (832 bytes), then published file drop; paste targets not manually tested | Use `CF_HDROP` plus app-managed clipboard cache for Copy GIF | Copy GIF no longer requires Save As and reuses prepared GIF when valid; Discord/GitHub/Explorer paste behavior is not yet proven | Paste into Explorer, Discord, and GitHub issue/comment upload with an animated GIF from a nonblack/motion recording |
| S07 Preview Rendering | Product smoke partial 2026-06-04: main app Rec/Stop synthetic path switches from click-through capture mode to solid preview mode and paints recorded BGRA frames; screenshot at `artifacts\app_preview_smoke.png` | Use solid-region preview mode with cover-scaled frame painting as the product path; Direct2D/D3D optimization remains TBD | Main window can now preview synthetic recordings after Stop and Play/Pause loops frames by duration | Replace synthetic source with live DXGI frames, validate resize timing, and decide GDI vs Direct2D/D3D for final playback |
| S08 Editor Timeline | Product partial 2026-06-04: `Edit` opens a native editor; S08 synthetic smoke rendered 120-frame virtualized timeline at `artifacts\editor_timeline_smoke.png`; Apply smoke updated main preview/export frames at `artifacts\editor_apply_smoke.png`; unit tests cover trim/delete model | Use custom-painted Win32 editor with non-destructive edit model and virtualized timeline | Editor is no longer a placeholder and supports selection, Set Start, Set End, Delete, Delete Even, Reset, Apply to main preview/export, preview, and scrollbar navigation | Add context menu operations, green-screen/change visualization, and large 1,000-frame responsiveness validation |
| S09 Video Alternates | Product partial 2026-06-04: `spike_s09_video_alternates` exported `artifacts\video\synthetic.mp4` (2,900 bytes, H.264, 160x90, 2.4s) and `artifacts\video\synthetic.webp` (2,914 bytes, 24 frames identified by ImageMagick) through FFmpeg; main toolbar format selector smoke at `artifacts\app_toolbar_export_format_smoke.png` | Use FFmpeg-backed MP4 H.264 and Animated WebP exporter APIs as alternates; GIF remains default | Export module supports target-size H.264 bitrate calculation, two-pass MP4, and Animated WebP without bundling FFmpeg; product Save can route to GIF, MP4, or WebP from the toolbar selector | Add WebM/AV1 optional paths, cancel/progress UI, and validate WebP in target apps because FFmpeg 7.0.1 decoder rejected its own animated WebP artifact |
