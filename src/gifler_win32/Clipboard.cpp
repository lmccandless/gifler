#include "gifler_win32/Clipboard.h"

#include "gifler_win32/Win32Util.h"

#include <ShlObj_core.h>

#include <cstring>

namespace gifler::win32 {

bool set_clipboard_file_drop(HWND owner, const std::filesystem::path& filePath, std::wstring* error) {
    const std::wstring path = std::filesystem::absolute(filePath).wstring();
    if (path.empty()) {
        if (error != nullptr) {
            *error = L"Clipboard file path is empty.";
        }
        return false;
    }

    const std::size_t pathBytes = (path.size() + 2) * sizeof(wchar_t); // double-NUL terminated file list
    const SIZE_T totalBytes = sizeof(DROPFILES) + pathBytes;

    unique_hglobal global(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalBytes));
    if (!global) {
        if (error != nullptr) {
            *error = last_error_message();
        }
        return false;
    }

    void* locked = GlobalLock(global.get());
    if (locked == nullptr) {
        if (error != nullptr) {
            *error = last_error_message();
        }
        return false;
    }

    auto* drop = static_cast<DROPFILES*>(locked);
    drop->pFiles = sizeof(DROPFILES);
    drop->pt = POINT{0, 0};
    drop->fNC = FALSE;
    drop->fWide = TRUE;

    auto* fileList = reinterpret_cast<wchar_t*>(static_cast<unsigned char*>(locked) + sizeof(DROPFILES));
    std::memcpy(fileList, path.c_str(), pathBytes - sizeof(wchar_t));
    fileList[path.size()] = L'\0';
    fileList[path.size() + 1] = L'\0';
    GlobalUnlock(global.get());

    if (!OpenClipboard(owner)) {
        if (error != nullptr) {
            *error = L"Clipboard is unavailable: " + last_error_message();
        }
        return false;
    }

    struct ClipboardCloseGuard {
        ~ClipboardCloseGuard() { CloseClipboard(); }
    } guard;

    if (!EmptyClipboard()) {
        if (error != nullptr) {
            *error = L"Could not clear clipboard: " + last_error_message();
        }
        return false;
    }

    if (SetClipboardData(CF_HDROP, global.get()) == nullptr) {
        if (error != nullptr) {
            *error = L"Could not set CF_HDROP data: " + last_error_message();
        }
        return false;
    }

    // Clipboard owns the global memory now.
    global.release();
    return true;
}

} // namespace gifler::win32
