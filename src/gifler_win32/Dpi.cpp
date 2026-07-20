#include "gifler_win32/Dpi.h"

namespace gifler::win32 {

void enable_per_monitor_dpi_awareness_best_effort() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto fn = reinterpret_cast<SetProcessDpiAwarenessContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (fn != nullptr) {
            if (fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                return;
            }
        }
    }
    SetProcessDPIAware();
}

unsigned dpi_for_window_or_default(HWND hwnd) {
    if (hwnd != nullptr) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 != nullptr) {
            using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
            auto fn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
            if (fn != nullptr) {
                const UINT dpi = fn(hwnd);
                if (dpi != 0) {
                    return dpi;
                }
            }
        }
    }
    HDC dc = GetDC(nullptr);
    const unsigned dpi = dc != nullptr ? static_cast<unsigned>(GetDeviceCaps(dc, LOGPIXELSX)) : 96u;
    if (dc != nullptr) {
        ReleaseDC(nullptr, dc);
    }
    return dpi == 0 ? 96u : dpi;
}

int scale_for_dpi(int value, unsigned dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

} // namespace gifler::win32
