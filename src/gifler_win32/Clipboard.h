#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>

namespace gifler::win32 {

bool set_clipboard_file_drop(HWND owner, const std::filesystem::path& filePath, std::wstring* error = nullptr);

} // namespace gifler::win32
