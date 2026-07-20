#include "gifler_win32/Clipboard.h"

#include <filesystem>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::wcerr << L"Usage: spike_s06_clipboard_filedrop.exe C:\\path\\to\\animated.gif\n";
        return 2;
    }
    std::filesystem::path path = argv[1];
    if (!std::filesystem::exists(path)) {
        std::wcerr << L"File does not exist: " << path.wstring() << L"\n";
        return 3;
    }

    std::wstring error;
    if (!gifler::win32::set_clipboard_file_drop(nullptr, path, &error)) {
        std::wcerr << L"Clipboard set failed: " << error << L"\n";
        return 4;
    }

    std::wcout << L"Copied file drop to clipboard: " << std::filesystem::absolute(path).wstring() << L"\n";
    std::wcout << L"Now paste into Explorer/Discord/GitHub to validate behavior.\n";
    return 0;
}
