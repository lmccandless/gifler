#pragma once
#include <Windows.h>
#include <optional>

namespace gifler::app {
std::optional<int> prompt_custom_fps(HWND owner, int current);
}
