# Display-Synchronized Capture Exploration

Status: design investigation only. This build changes dropdown spacing, not
recording cadence, monitor configuration, or compositor settings.

## What Exists

`RecorderSession::start_dxgi` uses a steady-clock deadline based on an integer
FPS setting. `DxgiCaptureProvider::Impl::capture` polls `AcquireNextFrame` with
a zero timeout after initialization and returns a cached image on timeout.
The recorder timestamps each successful sample rather than retaining DXGI's
desktop-presentation timestamp. Capture and export currently share an integer
`recorded_fps()` value. These observations come from the local implementation.

## Three Different Options

1. **Auto (monitor Hz):** read the selected monitor's refresh rate and use it for
   the existing timer. This is automatic rate selection, not phase-locked VSync.
   Keep fractional rates such as 60000/1001 rather than silently rounding them;
   Windows represents display frequencies as rational values.
   [Display frequency representation](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_rational).

2. **Auto (display updates):** wait for actual desktop updates, keep their
   timestamps, and hold unchanged frames for their elapsed duration. This is my
   recommended first prototype for a recorder, but it must not be advertised as
   one capture per physical refresh. Desktop Duplication also wakes for mouse
   changes, and a static display need not produce a new desktop bitmap each cycle.
   Use short, finite waits so Stop remains responsive.
   [Desktop frame acquisition](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutputduplication-acquirenextframe).

3. **VSync (vertical blank):** pace a worker using the selected output's
   `WaitForVBlank`, then acquire the newest available desktop image. That API
   blocks until vertical blank and provides no timeout/cancellation parameter.
   It needs a separate lifecycle/cancellation design and hardware validation;
   simply inserting it into the existing Stop-and-join loop is not sufficient.
   [Vertical-blank wait](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgioutput-waitforvblank).

## Recommended Prototype

- Leave existing fixed-FPS behavior as the default and add an opt-in capture mode,
  separate from the numeric rate. Keep it within the existing FPS menu.
- Have the capture provider distinguish NewFrame, NoChange, and Error. Idle
  timeouts must not trigger the current three-failures stop policy.
- Return presentation/cursor metadata and monitor identity from the provider.
  Convert QPC timestamps into the recording's 100-nanosecond timebase, preserving
  the audio epoch. `LastPresentTime` is zero for pointer-only updates; account for
  `LastMouseUpdateTime` separately and use `AccumulatedFrames` to report missed
  desktop updates. These fields are documented in
  [frame metadata](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info).
- Retain the pending-frame duration and final Stop flush. Do not create extra
  image copies merely to fill display intervals. Apply a documented capture cap
  for high-refresh displays and preserve the existing bounded worker queue.
- Store the export cadence with the recording, independently of later menu
  selections. Supporting fractional monitor rates requires extending the current
  integer-rate timing/export contracts; do not reinterpret a special integer as
  both a capture mode and an encoder frame rate.
- Specify behavior when dragging across monitors or changing display modes.
  The current provider selects a single output, so cross-monitor synchronized
  composition would be separate work.
- Do not alter display refresh, disable VRR, restart DWM, or change system themes.

## Validation Before Shipping

Deterministic tests should cover idle holds, pointer-only updates, timestamp
conversion, missed frames, finite-wait cancellation, and fractional export timing.
Hardware checks should cover 59.94/60/120/144 Hz, VRR, mixed-refresh monitors,
monitor disconnect/sleep, and audio synchronization. Compare distinct captured
images and measured cadence, not only the FPS field reported by an exported file.

The expected benefit is less redundant capture work and better alignment with
available desktop updates. Neither mode can create motion frames the source did
not present, and hardware-specific gains have not yet been measured.
