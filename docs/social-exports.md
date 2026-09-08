# Capture Ratios and Social Exports

## Capture aspect ratio

Open **More > Capture aspect ratio** and select Free, 16:9, 9:16, 1:1, 4:5, 4:3, or 3:4.

The ratio applies to the recorded area, not the toolbar and borders. All corner handles remain available and keep the opposite corner fixed. Edge resizing adjusts the other dimension around its center. Sizes use whole-pixel multiples of the selected ratio. The selection persists between launches and can be changed before/after recording.

## MP4 Social / X

Select **MP4 Social / X** from the format dropdown. The toolbar shows **MP4 X** while selected. This is a conservative upload preset, not a guarantee that a particular account or upload path will accept a file.

- MP4 with H.264 High, progressive YUV 4:2:0, square pixels, closed GOP, and fast-start metadata.
- Constant 30 FPS. Capture FPS remains unchanged; conversion happens during export.
- Landscape fits within 1280x720, portrait within 720x1280, and square within 720x720. Normal-sized recordings are not upscaled.
- Output dimensions are even and at least 32x32. Extremely wide/tall captures get black padding to stay within a 2.39:1 or 1:2.39 aspect range; content is not cropped.
- Maximum video bitrate is 8 Mbps. A selected file-size target can reduce it further.
- AAC-LC stereo at 128 kbps when audio was recorded.
- Clips shorter than 0.5 seconds hold the last frame to reach 0.5 seconds. Clips over 140 seconds are rejected with a trim/regular-MP4 message; they are never silently truncated.

Regular MP4 remains available for unrestricted source FPS and longer clips. WebM (VP9/Opus) and animated WebP remain available for destinations that support them, including the user's tested Discord workflow. Changing a filename extension does not convert a format.

## Audio rate

**Format dropdown > MP4 audio rate** selects 48 kHz or 44.1 kHz. Default: 48 kHz. Both regular and social MP4 use this setting. Resampling occurs only during export; system audio is captured at the endpoint's native mix rate. WebM uses 48 kHz Opus. GIF and animated WebP do not contain audio.

48 kHz is appropriate for video uploads; YouTube explicitly recommends it. Resampling to 44.1 kHz does not make WebM or animated WebP into X-compatible video containers.

## Sources and verification

Checked 2026-09-08:

- [X video help](https://help.x.com/en/using-x/x-videos): web upload and account limits. Its documented 40 FPS web limit differs from the API's 60 FPS limit; this preset uses 30 FPS.
- [X Media Studio](https://help.x.com/en/using-twitter/media-studio-faqs.html): MP4/MOV with H.264 and AAC-LC.
- [X media upload best practices](https://docs.x.com/x-api/media/quickstart/best-practices): H.264, AAC-LC mono/stereo, square pixels, YUV 4:2:0, progressive scan, and closed GOP. WebP in the image API list is not a promise of animated WebP support in every uploader.
- [YouTube upload encoding settings](https://support.google.com/youtube/answer/1722171?hl=en): 48 kHz audio recommendation.

Automated tests cover ratio geometry/native sizing, settings, export arguments, and actual FFmpeg/ffprobe validation of both audio rates, 30 FPS, AAC-LC stereo, pixel format, sample aspect ratio, duration, and extreme-ratio padding. Actual X/Discord account uploads remain manual tests.
