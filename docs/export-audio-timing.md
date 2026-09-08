# Export, Audio, and Timing Decisions

## GIF Export

The old streamed palette filter split the recording and waited until EOF to
produce a global palette. Its other branch could retain an entire decoded
recording, increasing memory pressure near the end of input. The new FFmpeg path
streams two separate passes from the existing recording: palette generation,
then palette application. PNG fallback for gifski writes repeated frames directly
without allocating an expanded recording. Candidate retries are bounded.

Export threads catch failures, retain the recording, and report completion back
to the UI. Encoder stderr is retained in the failure message, and failed exports
write a local diagnostic. Cancellation closes only the encoder process created
for the active operation; no DWM or display-driver recovery action is used.

## Frame Timing

The persistent DXGI session polls without waiting once a frame is cached; the
previous 250 ms wait could exceed a 30 FPS recording's 33.3 ms interval. Capture
uses absolute sampling deadlines and assigns each image the interval until its
successor. Stop drains the final image before closing the queue. Exports round
cumulative time boundaries, so rounding jitter cannot accumulate speed errors.
Exports use the recording's FPS even when the next-recording preference changes.

## System Audio

System audio uses Windows WASAPI shared-mode loopback on the default playback
endpoint, with a worker-owned COM session and deterministic Stop/Release. Native
mix-format samples use packet QPC timestamps aligned with the video clock.
Silence fills gaps. The audio buffer has a 512 MiB ceiling; capture errors are
reported at Stop. No new third-party dependency or bundled encoder is required.

MP4 uses AAC and WebM uses Opus. An intermediate WAV selects the intervals attached
to the exported frames so trim/delete edits also edit the audio. The video
duration limits the muxed result. GIF/WebP and the built-in visual preview have
no audio playback. Microphone capture is not included.

References: [Microsoft loopback recording](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording)
and [IAudioCaptureClient::GetBuffer timestamps](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer).

## Size Targets

The format dropdown owns remembered 5/10/20/50/100 MiB targets and No limit.
Targets reserve a 5% safety margin. GIF uses its candidate planner; MP4/WebM use
duration-based bitrate budgets including audio, verify actual file size, and
retry with reduced bitrate up to three times. WebP retries lower quality and
resolution while retaining FPS. Failure to meet the target reports an explicit
error; the user can choose a larger target or No limit. Changing a target
invalidates the prepared Copy cache.
