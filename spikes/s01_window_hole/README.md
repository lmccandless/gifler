# S01 Window Hole Spike

Validates the critical UI question: one top-level Win32 window with a real click-through region-cut viewfinder.

Run:

```powershell
./build/vs2022-x64-debug/Debug/spike_s01_window_hole.exe
```

Manual checks:

- Put Notepad or a browser behind the window.
- Click through the central viewfinder.
- Confirm controls still work.
- Drag from the toolbar blank area.
- Resize from the bottom-right grip.
- Test at multiple DPI settings.
