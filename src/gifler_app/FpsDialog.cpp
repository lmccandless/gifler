#include "gifler_app/FpsDialog.h"
#include "gifler_win32/Dpi.h"
#include <CommCtrl.h>
#include <string>

namespace gifler::app {
namespace {
constexpr int InputId = 1701;
constexpr int ErrorId = 1702;
struct DialogState { int value; HFONT font = nullptr; };

INT_PTR CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<DialogState*>(lp);
        SetWindowLongPtrW(hwnd, DWLP_USER, lp);
        const unsigned dpi = win32::dpi_for_window_or_default(hwnd);
        const auto px = [dpi](int value) { return win32::scale_for_dpi(value, dpi); };
        state->font = CreateFontW(-px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        RECT size{0, 0, px(300), px(162)};
        AdjustWindowRectExForDpi(&size, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), dpi);
        RECT owner{};
        GetWindowRect(GetParent(hwnd), &owner);
        SetWindowPos(hwnd, nullptr, owner.left + ((owner.right - owner.left) - (size.right - size.left)) / 2,
                     owner.top + ((owner.bottom - owner.top) - (size.bottom - size.top)) / 2,
                     size.right - size.left, size.bottom - size.top, SWP_NOZORDER);
        const auto child = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w, int h) {
            HWND control = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0,
                cls, text, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            return control;
        };
        child(L"STATIC", L"Frames per second (1-240)", 0, -1, 18, 16, 264, 22);
        HWND input = child(L"EDIT", std::to_wstring(state->value).c_str(), WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, InputId, 18, 44, 264, 28);
        SendMessageW(input, EM_SETLIMITTEXT, 3, 0);
        HWND spin = child(UPDOWN_CLASSW, L"", UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_SETBUDDYINT, 1703, 0, 0, 0, 0);
        SendMessageW(spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(input), 0);
        SendMessageW(spin, UDM_SETRANGE32, 1, 240);
        SendMessageW(spin, UDM_SETPOS32, 0, state->value);
        child(L"STATIC", L"", 0, ErrorId, 18, 80, 264, 22);
        child(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL, 110, 116, 80, 28);
        child(L"BUTTON", L"Apply", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, 200, 116, 82, 28);
        SendMessageW(hwnd, DM_SETDEFID, IDOK, 0);
        SetFocus(input);
        SendMessageW(input, EM_SETSEL, 0, -1);
        return FALSE;
    }
    if (message == WM_COMMAND && LOWORD(wp) == IDOK) {
        wchar_t text[16]{};
        const int length = GetDlgItemTextW(hwnd, InputId, text, 16);
        bool valid = length > 0 && length <= 3;
        UINT fps = 0;
        for (int i = 0; i < length && valid; ++i) {
            valid = text[i] >= L'0' && text[i] <= L'9';
            if (valid) fps = fps * 10 + static_cast<UINT>(text[i] - L'0');
        }
        if (!valid || fps < 1 || fps > 240) {
            SetDlgItemTextW(hwnd, ErrorId, L"Enter a whole number from 1 to 240.");
            SetFocus(GetDlgItem(hwnd, InputId));
        } else {
            state->value = static_cast<int>(fps);
            EndDialog(hwnd, IDOK);
        }
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wp) == IDCANCEL)) {
        EndDialog(hwnd, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
}

std::optional<int> prompt_custom_fps(HWND owner, int current) {
    struct Template {
        DLGTEMPLATE dialog{};
        WORD menu = 0;
        WORD windowClass = 0;
        wchar_t title[18] = L"Custom frame rate";
    } layout;
    layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    layout.dialog.cx = 200;
    layout.dialog.cy = 110;
    DialogState state{current};
    const auto result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &layout.dialog, owner, dialog_proc,
                                               reinterpret_cast<LPARAM>(&state));
    if (state.font) DeleteObject(state.font);
    return result == IDOK ? std::make_optional(state.value) : std::nullopt;
}
}
