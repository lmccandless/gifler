# Gifler Native Architecture

## Module Boundaries

```text
gifler_app            HWND ownership, state wiring, user commands.
gifler_core           Values, geometry, frame model, settings contracts.
gifler_win32          Thin wrappers around raw Windows calls.
gifler_capture_dxgi   D3D11/DXGI Desktop Duplication capture.
gifler_record         Threading, queueing, coalescing, frame storage.
gifler_export         Encoder discovery, command building, GIF planning.
gifler_editor         Timeline/editor UI and non-destructive edit model.
```

## Coordinate Rule

The UI may lay out in client pixels, but recorder/capture/export modules use physical pixels. Convert at HWND boundaries and after `WM_DPICHANGED`.

## Threading Model

```text
UI thread            Win32 messages, painting, status updates.
Capture thread       DXGI frame acquisition and BGRA crop production.
Recording worker     duplicate coalescing, changed-bounds detection, frame storage.
Export worker        materializes frames and runs encoder processes.
```

The capture thread must never encode. The recording queue must be bounded.

## Windowing Approach

The preferred approach is one top-level HWND with the viewfinder cut out using `SetWindowRgn`. The region is switched back to a full region during preview playback so the app can paint into the viewfinder.

Fallback: multiple coordinated HWND strips around a physically empty center.

## Export Approach

GIF-first export uses external encoders discovered at runtime. The default target is Discord/GitHub Free: 10 MB displayed target, 9.5 MB internal safety target.

`CF_HDROP` is used for clipboard copy because animated GIF clipboard bitmap formats often paste only the first frame.
