#include "gifler_win32/Win32Util.h"

#include <Shlwapi.h>

#include <sstream>

namespace gifler::win32 {

std::wstring last_error_message(DWORD error) {
    if (error == 0) {
        return L"No error";
    }
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD chars = FormatMessageW(flags, nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring message = chars > 0 && buffer != nullptr ? std::wstring(buffer, chars) : L"Unknown Win32 error";
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return message;
}

std::wstring hresult_message(HRESULT hr) {
    return last_error_message(static_cast<DWORD>(hr));
}

std::wstring utf8_to_wide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), needed);
    return result;
}

std::string wide_to_utf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), needed, nullptr, nullptr);
    return result;
}

std::wstring quote_arg(std::wstring_view arg) {
    if (arg.empty()) {
        return L"\"\"";
    }

    bool needsQuotes = false;
    for (wchar_t ch : arg) {
        if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\"') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return std::wstring(arg);
    }

    std::wstring result;
    result.push_back(L'\"');
    unsigned backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(ch);
            backslashes = 0;
        } else {
            result.append(backslashes, L'\\');
            backslashes = 0;
            result.push_back(ch);
        }
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::vector<std::wstring> split_path_env() {
    DWORD needed = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (needed == 0) {
        return {};
    }
    std::wstring path(needed, L'\0');
    GetEnvironmentVariableW(L"PATH", path.data(), needed);
    if (!path.empty() && path.back() == L'\0') {
        path.pop_back();
    }

    std::vector<std::wstring> parts;
    std::wstringstream stream(path);
    std::wstring item;
    while (std::getline(stream, item, L';')) {
        if (!item.empty()) {
            parts.push_back(item);
        }
    }
    return parts;
}

} // namespace gifler::win32
