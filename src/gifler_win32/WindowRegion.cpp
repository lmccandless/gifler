#include "gifler_win32/WindowRegion.h"

#include "gifler_win32/Win32Util.h"

namespace gifler::win32 {

bool apply_viewfinder_hole_region(HWND hwnd, gifler::core::PixelRect clientHoleRect, std::wstring* error) {
    RECT windowRect{};
    RECT clientRect{};
    if (!GetWindowRect(hwnd, &windowRect) || !GetClientRect(hwnd, &clientRect)) {
        if (error != nullptr) {
            *error = last_error_message();
        }
        return false;
    }

    POINT clientOrigin{0, 0};
    ClientToScreen(hwnd, &clientOrigin);

    const int windowWidth = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;
    const int clientOffsetX = clientOrigin.x - windowRect.left;
    const int clientOffsetY = clientOrigin.y - windowRect.top;

    HRGN outer = CreateRectRgn(0, 0, windowWidth, windowHeight);
    HRGN hole = CreateRectRgn(clientOffsetX + clientHoleRect.x, clientOffsetY + clientHoleRect.y,
                              clientOffsetX + clientHoleRect.right(), clientOffsetY + clientHoleRect.bottom());
    if (outer == nullptr || hole == nullptr) {
        if (outer != nullptr) {
            DeleteObject(outer);
        }
        if (hole != nullptr) {
            DeleteObject(hole);
        }
        if (error != nullptr) {
            *error = last_error_message();
        }
        return false;
    }

    CombineRgn(outer, outer, hole, RGN_DIFF);
    DeleteObject(hole);

    // On success, the system owns the region handle.
    if (!SetWindowRgn(hwnd, outer, TRUE)) {
        DeleteObject(outer);
        if (error != nullptr) {
            *error = last_error_message();
        }
        return false;
    }
    return true;
}

bool clear_window_region(HWND hwnd, std::wstring* error) {
    if (!SetWindowRgn(hwnd, nullptr, TRUE)) {
        if (error != nullptr) {
            *error = last_error_message();
        }
        return false;
    }
    return true;
}

bool enable_colorkey_transparency(HWND hwnd, COLORREF transparentColor, std::wstring* error) {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (style == 0 && GetLastError() != ERROR_SUCCESS) {
        if (error != nullptr) {
            *error = last_error_message();
        }
        return false;
    }

    if ((style & WS_EX_LAYERED) == 0) {
        SetLastError(ERROR_SUCCESS);
        if (SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED) == 0 && GetLastError() != ERROR_SUCCESS) {
            if (error != nullptr) {
                *error = last_error_message();
            }
            return false;
        }
    }

    if (!SetLayeredWindowAttributes(hwnd, transparentColor, 0, LWA_COLORKEY)) {
        if (error != nullptr) {
            *error = last_error_message();
        }
        return false;
    }
    return true;
}

bool set_window_excluded_from_capture(HWND hwnd, bool excluded, std::wstring* error) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        if (error != nullptr) {
            *error = L"user32.dll is not loaded.";
        }
        return false;
    }

    using SetWindowDisplayAffinityFn = BOOL(WINAPI*)(HWND, DWORD);
    auto fn = reinterpret_cast<SetWindowDisplayAffinityFn>(GetProcAddress(user32, "SetWindowDisplayAffinity"));
    if (fn == nullptr) {
        if (error != nullptr) {
            *error = L"SetWindowDisplayAffinity is not available on this Windows version.";
        }
        return false;
    }

    constexpr DWORD noAffinity = 0x00000000;
    constexpr DWORD excludeFromCapture = 0x00000011;
    if (!fn(hwnd, excluded ? excludeFromCapture : noAffinity)) {
        if (error != nullptr) {
            *error = last_error_message();
        }
        return false;
    }
    return true;
}

gifler::core::PixelRect client_rect_to_screen_pixels(HWND hwnd, gifler::core::PixelRect clientRect) {
    POINT topLeft{clientRect.x, clientRect.y};
    ClientToScreen(hwnd, &topLeft);
    return {topLeft.x, topLeft.y, clientRect.width, clientRect.height};
}

} // namespace gifler::win32
