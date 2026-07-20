# S03 Pointer Shape Spike

Cursor overlay smoke for the product capture path.

Run from the repository root after building:

```powershell
.\build\vs2026-x64-debug\Debug\spike_s03_pointer_shape.exe
```

Expected output:

- captures a 320x240 physical-pixel rectangle around the current cursor,
- calls `DxgiCaptureProvider::capture_one_frame(..., captureCursor=true)`,
- composites the visible Win32 cursor into the BGRA frame,
- writes `artifacts\s03_cursor_overlay.png`,
- prints cursor position, hotspot, and size metadata when the cursor is visible.

Remaining Desktop Duplication pointer-shape work:

- add `GetFramePointerShape` support for DXGI-provided pointer metadata,
- cache the latest pointer shape across frames,
- support color, masked color, and monochrome shape conversion tests,
- compare Win32 cursor overlay behavior with DXGI pointer-shape behavior on high-DPI and mixed-DPI desktops.
