# Encoder Setup

Gifler should not bundle encoders in the main portable executable.

Search order for each encoder:

1. User-configured absolute path.
2. App-local `encoders/` folder beside `Gifler.exe`.
3. `PATH`.

Supported tools:

- `ffmpeg.exe` for palette GIFs, H.264 MP4, animated WebP, and VP9 WebM.
- `gifski.exe` for high-quality GIFs.
- `gifsicle.exe` for optional GIF optimization.

Missing tool behavior:

- Missing `ffmpeg.exe`: GIF fallback may be unavailable unless `gifski.exe` exists.
- Missing `gifski.exe`: Auto GIF falls back to FFmpeg palette mode.
- Missing `gifsicle.exe`: skip optimizer pass, do not fail the export.

The app should show a concise setup message and log detailed paths/commands.
