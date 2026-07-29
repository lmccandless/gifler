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
- Confirm `Rec`, `Play`, `FPS`, and `Copy GIF` are top-level menu-bar entries.
- Confirm less-common commands are under `More`.
- Confirm status text appears in the window title, not in a bottom status strip.
- Resize the window narrow/small and confirm there is no toolbar-imposed width limit.
- Hover/click-drag the client edges and bottom corners and confirm the resize cursor appears over a wider target.
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
