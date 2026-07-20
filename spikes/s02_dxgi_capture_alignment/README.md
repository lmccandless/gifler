# S02 DXGI Capture Alignment Spike

Validates that DXGI Desktop Duplication can capture a physical desktop rectangle and write it to PNG.

Run with explicit physical pixels:

```powershell
spike_s02_dxgi_capture_alignment.exe 100 100 500 300 out.png
```

Run with defaults:

```powershell
spike_s02_dxgi_capture_alignment.exe
```

The default captures a 480x270 rectangle near the first attached DXGI output.
