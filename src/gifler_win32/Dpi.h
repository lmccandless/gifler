#pragma once

#include <Windows.h>

namespace gifler::win32 {

void enable_per_monitor_dpi_awareness_best_effort();
[[nodiscard]] unsigned dpi_for_window_or_default(HWND hwnd);
[[nodiscard]] int scale_for_dpi(int value, unsigned dpi);

} // namespace gifler::win32
