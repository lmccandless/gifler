# Manual Test Plan

## S01 Window Hole

- App launches topmost.
- Center viewfinder passes clicks through to Notepad/browser behind it.
- Buttons remain clickable.
- Command strip drags the window except over controls.
- Bottom-right resize works.
- Hole remains aligned while resizing.
- Works at 100%, 125%, and 150% DPI.
- Moving between mixed-DPI monitors does not misalign the hole.

## S02 DXGI Capture Alignment

- Capture a static rectangle with known text.
- Saved PNG matches visible screen pixels.
- Gifler UI chrome is not in the image.
- Main `Gifler.exe` Rec/Stop captures the physical viewfinder rectangle, not a synthetic source.
- Capture rejects or clips invalid rectangles.
- Display changes produce clean errors.
- Record hardware-accelerated browser, video, and game windows, then stop and close Gifler.
- Confirm `dwm.exe` CPU returns to its pre-recording baseline and native window dragging remains smooth.
- Repeat Record/Stop ten times and confirm no cumulative compositor slowdown.

## S03 Cursor Recording

- Run `spike_s03_pointer_shape` and confirm `artifacts\s03_cursor_overlay.png` contains the cursor.
- Record with Cursor: On and confirm the cursor appears in preview/export frames.
- Record with Cursor: Off and confirm the cursor is absent.
- Confirm cursor hotspot alignment over known text or grid content.
- Repeat at 100%, 125%, and 150% DPI.

## S05 GIF Export Planner

- Missing encoders produce actionable messages.
- Candidate order reduces quality before FPS, then dimensions.
- Default visible target is 10 MB.
- Default internal target is 9.5 MB.
- Save GIF writes a selected `.gif` path through an external encoder.
- Copy GIF prepares a cache GIF without opening Save As.

## S06 Clipboard File Drop

- Copy a `.gif` path to clipboard using the spike.
- Copy from the main app creates its media file under `%LOCALAPPDATA%\Gifler\Clipboard`.
- Select each title-bar media format and use Copy; clipboard file-drop receives the matching `.gif`, `.mp4`, `.webp`, or `.webm` file.
- During Save and Copy, the bottom progress bar becomes visible and advances while the window continues repainting and can be moved or resized.
- Paste into Explorer.
- Paste into Discord.
- Paste into GitHub issue/comment upload.
- Clipboard lock failure shows a clean error.

## S09 Video Alternates

- Export MP4 H.264 from synthetic frames with `spike_s09_video_alternates`.
- Export Animated WebP from synthetic frames with `spike_s09_video_alternates`.
- Select MP4 in the main toolbar and Save a recording.
- Select WebP in the main toolbar and Save a recording.
- Verify MP4 duration, resolution, and size with `ffprobe`.
- Verify Animated WebP frame count in ImageMagick or a browser.
- Confirm GIF remains the default Save/Copy workflow.

## S08 Editor Timeline

- Open `Edit` after a recording.
- S08 spike shows 120 synthetic frames without creating per-frame controls.
- Click timeline frames and confirm selected preview updates.
- Use Set Start and Set End to change the included range.
- Use Delete and Delete Even to exclude frames non-destructively.
- Click Apply and confirm the main preview/export sequence changes.
- Scroll through the timeline and confirm cells paint only as needed.
- Reset restores all frames.

## MVP Smoke Test

- Launch one native `Gifler.exe`.
- Confirm Record, FPS, format, Play, Copy, Save, and More appear in the compact toolbar.
- Confirm accessible button names identify every command. Hover each toolbar button while idle and recording: no tooltip popup should appear, even after the usual hover delay. Inspect the exported recording for unwanted UI overlays.
- Confirm less-common commands are under `More`.
- Confirm the brand and all controls share one 34-pixel-high top strip. No bottom
  status bar or minimize/maximize buttons. Spare top-row space can show status.
- Dropdowns size to their actual rendered text with a 2-pixel text-to-arrow gap,
  a full arrow slot, and 3-pixel gaps between controls. Compact labels and More
  activate only as available width requires; below 192 pixels, Play/Copy also move there. Record and Save
  remain accessible down to 120 pixels wide without overlap.
- Hover/click-drag the client edges and bottom corners and confirm the resize cursor appears over a wider target.
- Right and bottom resize strips are 10 logical pixels wide. The bottom-right
  diagonal target extends 22 pixels along those strips; the true capture hole
  excludes all resize handles. Left/top borders retain their compact targets.
- Drag-resize repeatedly at mixed display scales. Owner-drawn controls must stay
  dark without white erases, text/arrow overlap, or painting into neighboring controls.
- Record/Stop repeatedly.
- Confirm idle, recording, stopped, and paused states keep the viewfinder visually pass-through.
- Confirm only active Play mode paints recorded frames into the viewfinder.
- Confirm playback/export frames show the desktop content under the viewfinder, not a solid black frame.
- Start recording, move the Gifler window across several screen positions, stop, then play back and confirm the captured content follows the new viewfinder positions without including Gifler UI.
- Toggle Cursor on/off and confirm recording behavior follows the button state.
- Play/Pause loops the captured frames without clipping toolbar controls.
- Save GIF writes a file.
- Copy GIF pastes as animated GIF file.
- Main toolbar Save writes GIF, MP4 H.264, and Animated WebP according to the selected format.
- Editor opens and supports trim/delete/apply operations.
- Missing encoder path is actionable.
- UI remains responsive during capture/export.

## Audio, Timing, and Export Reliability

- Run `scripts/run-export-smoke.ps1` after building Debug. It verifies 300 frames over
  10 seconds for MP4/WebM/GIF, WebP RIFF durations, audio streams, a 90-second held
  GIF, and recovery after encoder failure and cancellation.
- Run `scripts/run-ui-smoke.ps1` for isolated-settings hidden-window checks of
  menu persistence and asynchronous GIF completion/cancellation. It does not
  record the live desktop or modify the user's preferences or clipboard.
- Run `scripts/run-audio-smoke.ps1` to probe the default playback device for one
  second. Captured samples stay in memory and are discarded after the probe.
- Select format > Size target > 20 MB, restart, and confirm it remains checked.
- Repeat for No limit, then change the target after Save and confirm Copy re-encodes.
- Enable More > Record system audio before recording audible video. Export MP4
  and WebM and check A/V synchronization in a media player. GIF/WebP stay silent.
- Trim/delete frames in the editor and confirm exported audio follows the edit.
- Disconnect/change the audio endpoint during recording; Stop must show any audio
  failure while retaining the video. The next recording uses the new default device.
- Record moving content at 30 FPS. Check the measured capture rate in the stopped
  title and inspect motion, not just the output container's nominal FPS.
- While exporting, verify the thin progress line stays inside the top strip.
  Save becomes Cancel; cancel midway, then save the same recording successfully.
- Force an unwritable destination or a missing encoder. Gifler must retain the
  recording and report an error, with details in `%LOCALAPPDATA%\Gifler\last-export-error.txt`.
- Close Gifler during export and verify its own encoder child exits promptly.

## Custom Chrome and Frame Rates

- Verify dragging the brand or spare top-row area, double-click maximize/restore, Close,
  Alt+Space, Win+Arrow snapping, and all eight resize edges/corners.
- Maximize on monitors with different taskbar positions; controls must stay inside
  the work area. Restore and confirm the click-through hole is aligned.
- Move between 100%, 150%, and 200% monitors; text and icons must remain crisp,
  controls must not overlap, and physical capture coordinates must match the hole.
- Use Tab, Shift+Tab, Space, and Enter to operate controls and dropdown menus.
- Check Windows High Contrast without changing global theme or DWM settings in tests.
- FPS presets include 5, 10, 15, 24, 30, 48, 60, and 120. Custom accepts 1-240,
  rejects blank/non-numeric/out-of-range values, and Cancel preserves the old rate.
- Restart after selecting a custom rate; its label and check mark must persist.
- FPS selection must be disabled during recording/export. Changing the next FPS
  after Stop must not retime an existing recording.
- Record moving content at 60 FPS and inspect actual captured rate and playback.
  Synthetic export tests do not establish achievable live desktop capture rates.
- `run-ui-smoke.ps1` verifies actual controls, custom dialog apply/cancel/validation,
  persistence, non-overlapping layouts, and shaped-hole membership at multiple DPIs.
  Its PNGs are offscreen renders of the native UI with a neutral capture-area fill.
- `run-export-smoke.ps1` also verifies one-second MP4s at 60, 77, 120, and 240 FPS.
# Audio Packet Continuity Regression

2026-09-08: On the current 48 kHz stereo float endpoint, a three-second tone produced 256 packets with zero device-position gaps but 73 QPC jumps exceeding two samples (one was 928 samples). Capture now anchors QPC once and follows device frames. Production loopback capture plus WAV export retained the complete three-second 440 Hz tone: no internal silence at a -65 dB / 1 ms threshold, 2640 zero crossings, and maximum adjacent-sample difference 0.000824 at peak amplitude 0.014130. Debug unit tests passed, including 44.1/48 kHz synthetic timestamp jitter, real gaps, invalid timestamps, position resets, and pre-roll. Other physical endpoint rates and long-run A/V drift still require manual verification.

- Record continuous music/game audio with system audio enabled; export MP4 and WebM and listen for crackles, repeated snippets, and dropouts.
- Repeat with 44.1 kHz and 48 kHz output devices, including a Bluetooth device if available. Verify audio/video synchronization at the beginning and end of a longer recording.
- Include silent intervals, resume playback, and verify that silence does not shorten the audio timeline.
- Diagnostic: build Debug, then run `scripts/run-audio-packet-probe.ps1` during playback to compare clock jitter with device-frame gaps. This retains packet metadata only.
- With permission to play a test sound, run `scripts/run-audio-wave-probe.ps1 -TonePath <quiet-test.wav>` to capture four seconds through the production audio capture and WAV writer. Inspect the resulting waveform for internal gaps.
# Inside-Viewfinder Resizing

- Hover within 12 DIP of each viewfinder edge: confirm the matching resize cursor and two short parallel lines at that edge's midpoint.
- Hover near all four corners: confirm two nested right-angle marks oriented toward that corner and the matching diagonal cursor.
- Drag each edge outward and inward; verify the opposite edge stays fixed. Repeat all four corners and verify the diagonally opposite corner stays fixed.
- Verify ordinary clicks through the center still reach the application behind Gifler, and hover marks disappear after leaving the resize perimeter.
- Repeat in preview, during recording, on negative-coordinate monitors, and at 100%, 125%, 150%, and 200% scaling. Check minimum window size, moving between monitors, minimizing/restoring, and maximizing/restoring.
- Export a recording while hovering/dragging the handles: neither hover marks nor grab surfaces should appear in the recording.
- Automated coverage: eight edge/corner hit zones, exclusive center boundaries, small-window center clearance, native resize-message routing with negative coordinates, region-hole geometry, capture-exclusion affinity, hover rendering, and hover-leave clearing.
# Aspect Lock and Social MP4

- Select every preset in More > Capture aspect ratio. Resize all corners inward/outward and confirm the capture region (excluding chrome) retains the selected ratio and the opposite corner remains fixed.
- Check edge drags, snapping, maximizing/restoring, and DPI changes. Free restores unconstrained resizing; restart and verify the selection persists.
- Select MP4 Social / X and record a normal clip with audio. Verify the toolbar shows MP4 X, then export and upload manually to X and Discord without publishing unless authorized.
- Repeat with MP4 audio rate set to 44.1 kHz and 48 kHz; compare audio continuity and lip-sync. Capture must continue using the native device rate.
- Check a very wide capture, a portrait capture, and a short clip: padding must not crop content; short clips should last at least half a second. A clip over 140 seconds must show the preset limit rather than silently truncate.
- Verify regular MP4 still exports high/custom FPS and WebM/WebP continue working independently of the MP4 preset/audio setting.
