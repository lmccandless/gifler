#pragma once

#include "gifler_core/Geometry.h"

#include <Windows.h>

#include <string>

namespace gifler::win32 {

bool apply_viewfinder_hole_region(HWND hwnd, gifler::core::PixelRect clientHoleRect, std::wstring* error = nullptr);
bool clear_window_region(HWND hwnd, std::wstring* error = nullptr);
bool enable_colorkey_transparency(HWND hwnd, COLORREF transparentColor, std::wstring* error = nullptr);
bool set_window_excluded_from_capture(HWND hwnd, bool excluded, std::wstring* error = nullptr);

gifler::core::PixelRect client_rect_to_screen_pixels(HWND hwnd, gifler::core::PixelRect clientRect);

} // namespace gifler::win32
