#include "gifler_app/MainWindow.h"
#include "gifler_core/AspectRatio.h"

#include <CommCtrl.h>
#include <algorithm>
#include <format>

namespace gifler::app {
namespace {
constexpr COLORREF Surface = RGB(28, 29, 30);
constexpr COLORREF Ink = RGB(229, 230, 226);
constexpr COLORREF Muted = RGB(154, 158, 154);
constexpr COLORREF Line = RGB(65, 67, 67);
constexpr COLORREF Accent = RGB(224, 66, 66);

void fill(HDC dc, const RECT& rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

LRESULT CALLBACK button_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_MOUSEMOVE) {
        if (!GetPropW(hwnd, L"Gifler.Hot")) {
            SetPropW(hwnd, L"Gifler.Hot", reinterpret_cast<HANDLE>(1));
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    } else if (message == WM_MOUSELEAVE) {
        RemovePropW(hwnd, L"Gifler.Hot");
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (message == WM_NCDESTROY) {
        RemovePropW(hwnd, L"Gifler.Hot");
        RemoveWindowSubclass(hwnd, button_proc, 1);
    }
    return DefSubclassProc(hwnd, message, wp, lp);
}
}

void MainWindow::refresh_fonts() {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    highContrast_ = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    const auto font = [&](const wchar_t* face, int size, int weight) {
        return CreateFontW(-scaled(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    };
    if (uiFont_) DeleteObject(uiFont_);
    if (titleFont_) DeleteObject(titleFont_);
    if (iconFont_) DeleteObject(iconFont_);
    uiFont_ = font(L"Segoe UI", 11, FW_NORMAL);
    titleFont_ = font(L"Segoe UI", 12, FW_SEMIBOLD);
    iconFont_ = font(L"Segoe MDL2 Assets", 11, FW_NORMAL);
    if (tooltips_) SendMessageW(tooltips_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
}

void MainWindow::create_chrome() {
    refresh_fonts();
    tooltips_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               hwnd_, nullptr, instance_, nullptr);
    SendMessageW(tooltips_, TTM_SETMAXTIPWIDTH, 0, scaled(300));
    struct Button { int id; const wchar_t* name; };
    for (const auto& button : {Button{RecButton, L"Record"}, {FpsButton, L"Frame rate"},
            {FormatButton, L"Export format and size target"}, {PlayButton, L"Play preview"},
            {CopyGifButton, L"Copy recording"}, {SaveButton, L"Save recording"}, {MoreButton, L"More options"},
            {CloseButton, L"Close"}}) {
        HWND child = CreateWindowExW(0, L"BUTTON", button.name,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1,
            hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(button.id)), instance_, nullptr);
        SetWindowSubclass(child, button_proc, 1, 0);
        TOOLINFOW tip{sizeof(tip)};
        tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tip.hwnd = hwnd_;
        tip.uId = reinterpret_cast<UINT_PTR>(child);
        tip.lpszText = const_cast<wchar_t*>(button.name);
        SendMessageW(tooltips_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tip));
    }
    refresh_chrome();
}

std::wstring MainWindow::dropdown_label(int control) const {
    if (control == FpsButton && compactFps_) return std::to_wstring(selectedFps_);
    wchar_t label[64]{};
    GetWindowTextW(GetDlgItem(hwnd_, control), label, 64);
    return label;
}

int MainWindow::dropdown_width(int control) const {
    const auto label = dropdown_label(control);
    HDC dc = GetDC(hwnd_);
    if (!dc) return scaled(62);
    const auto old = SelectObject(dc, uiFont_);
    SIZE extent{};
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &extent);
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    // Label padding, a small gap, the complete arrow glyph, and its outer padding.
    return extent.cx + scaled(4) + scaled(2) + scaled(11) + scaled(2);
}

void MainWindow::layout_chrome(int width, int) {
    compactFps_ = false;
    const int fixedWidth = scaled(44) + 5 * scaled(20) + 6 * scaled(3) + scaled(28);
    int fpsWidth = dropdown_width(FpsButton);
    const int formatWidth = dropdown_width(FormatButton);
    if (fixedWidth + fpsWidth + formatWidth > width) {
        compactFps_ = true;
        fpsWidth = dropdown_width(FpsButton);
    }
    const bool overflow = fixedWidth + fpsWidth + formatWidth > width;
    const bool tiny = width < scaled(192);
    HDWP positions = BeginDeferWindowPos(8);
    const auto place = [&](HWND child, int x, int w, bool hidden) {
        const bool visible = (GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) != 0;
        RECT old{};
        GetWindowRect(child, &old);
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&old), 2);
        const RECT next{x, scaled(5), x + w, scaled(27)};
        if (visible == !hidden && (hidden || EqualRect(&old, &next))) return;
        const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS |
                           (hidden ? SWP_HIDEWINDOW : SWP_SHOWWINDOW);
        if (positions) positions = DeferWindowPos(positions, child, nullptr, x, scaled(5), w, scaled(22), flags);
        else SetWindowPos(child, nullptr, x, scaled(5), w, scaled(22), flags);
    };
    int x = scaled(tiny ? 25 : 44);
    for (const auto& [id, pixels] : {std::pair{RecButton, scaled(20)}, {FpsButton, fpsWidth},
             {FormatButton, formatWidth}, {PlayButton, scaled(20)}, {CopyGifButton, scaled(20)},
             {SaveButton, scaled(20)}, {MoreButton, scaled(20)}}) {
        const bool hidden = (overflow && (id == FpsButton || id == FormatButton)) ||
                            (tiny && (id == PlayButton || id == CopyGifButton));
        HWND child = GetDlgItem(hwnd_, id);
        if (!child) continue;
        place(child, x, pixels, hidden);
        if (!hidden) x += pixels + scaled(tiny ? 2 : 3);
    }
    if (HWND close = GetDlgItem(hwnd_, CloseButton))
        place(close, width - scaled(25), scaled(20), false);
    if (positions) EndDeferWindowPos(positions);
}

void MainWindow::refresh_chrome() {
    if (!GetDlgItem(hwnd_, RecButton)) return;
    const auto label = [&](int id, const wchar_t* text) {
        HWND child = GetDlgItem(hwnd_, id);
        SetWindowTextW(child, text);
        TOOLINFOW tip{sizeof(tip)};
        tip.hwnd = hwnd_;
        tip.uId = reinterpret_cast<UINT_PTR>(child);
        tip.lpszText = const_cast<wchar_t*>(text);
        SendMessageW(tooltips_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tip));
    };
    label(RecButton, recording_ ? L"Stop recording" : L"Record");
    label(SaveButton, saving_ ? L"Cancel export" : L"Save recording");
    label(PlayButton, previewPlaying_ ? L"Pause preview" : L"Play preview");
    const auto fps = std::format(L"{} FPS", selectedFps_);
    SetWindowTextW(GetDlgItem(hwnd_, FpsButton), fps.c_str());
    const wchar_t* format = selectedExportFormat_ == 0 ? L"GIF" : selectedExportFormat_ == 1 ? (settings_.socialMp4 ? L"MP4 X" : L"MP4") : selectedExportFormat_ == 2 ? L"WebP" : L"WebM";
    SetWindowTextW(GetDlgItem(hwnd_, FormatButton), format);
    EnableWindow(GetDlgItem(hwnd_, RecButton), !saving_);
    EnableWindow(GetDlgItem(hwnd_, FpsButton), !saving_ && !recording_);
    EnableWindow(GetDlgItem(hwnd_, FormatButton), !saving_);
    EnableWindow(GetDlgItem(hwnd_, MoreButton), !saving_);
    EnableWindow(GetDlgItem(hwnd_, PlayButton), !saving_ && !recording_ && !active_frames().empty());
    EnableWindow(GetDlgItem(hwnd_, CopyGifButton), !saving_ && !recording_ && !active_frames().empty());
    EnableWindow(GetDlgItem(hwnd_, SaveButton), saving_ || (!recording_ && !active_frames().empty()));
    RECT client{};
    GetClientRect(hwnd_, &client);
    layout_chrome(client.right, client.bottom);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void MainWindow::show_popup(HMENU menu, int control) {
    RECT rect{};
    GetWindowRect(GetDlgItem(hwnd_, control), &rect);
    const int command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                         rect.left, rect.bottom + scaled(4), hwnd_, nullptr);
    if (command) SendMessageW(hwnd_, WM_COMMAND, command, 0);
}

void MainWindow::select_fps(int fps) {
    if (fps < 1 || fps > 240 || recording_ || saving_) return;
    selectedFps_ = fps;
    save_preferences();
    update_menu_state();
    set_status(std::format(L"Frame rate set to {} FPS", fps));
}

void MainWindow::paint_chrome(HDC dc, const RECT& client) {
    const int saved = SaveDC(dc);
    const COLORREF surface = highContrast_ ? GetSysColor(COLOR_BTNFACE) : Surface;
    const COLORREF ink = highContrast_ ? GetSysColor(COLOR_BTNTEXT) : Ink;
    const COLORREF line = highContrast_ ? GetSysColor(COLOR_WINDOWTEXT) : Line;
    fill(dc, client, surface);
    SetDCBrushColor(dc, line);
    FrameRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ink);
    SelectObject(dc, titleFont_);
    RECT title{scaled(5), scaled(4), scaled(client.right < scaled(192) ? 23 : 40), scaled(28)};
    DrawTextW(dc, client.right < scaled(192) ? L"G" : L"Gifler", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT divider{scaled(3), scaled(31), client.right - scaled(3), scaled(32)};
    fill(dc, divider, highContrast_ ? line : RGB(13, 14, 15));
    RECT highlight{1, 1, client.right - 1, 2};
    fill(dc, highlight, highContrast_ ? line : RGB(76, 78, 78));
    SelectObject(dc, uiFont_);
    SetTextColor(dc, highContrast_ ? ink : Muted);
    const auto capture = viewfinder_client_rect();
    RECT more{};
    GetWindowRect(GetDlgItem(hwnd_, MoreButton), &more);
    MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&more), 2);
    RECT statusRect{more.right + scaled(8), scaled(5), client.right - scaled(33), scaled(27)};
    std::wstring status = statusText_;
    if (!recording_ && !saving_ && !previewMode_ &&
        (status.starts_with(L"Ready") || status.find(L" @ ") != std::wstring::npos)) {
        status = std::format(L"{} x {}{}", capture.width, capture.height, settings_.captureAudio ? L"  |  Audio" : L"");
        if (settings_.captureAspectRatio) {
            const auto ratio = gifler::core::CaptureAspectRatios[settings_.captureAspectRatio];
            status += std::format(L"  |  {}:{}", ratio.width, ratio.height);
        }
    }
    if (recording_) status = settings_.captureAudio ? L"Recording  |  System audio on" : L"Recording";
    SetTextColor(dc, recording_ && !highContrast_ ? Accent : (highContrast_ ? ink : Muted));
    if (statusRect.right - statusRect.left > scaled(90))
        DrawTextW(dc, status.c_str(), -1, &statusRect, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS | DT_NOPREFIX);
    RestoreDC(dc, saved);
}

void MainWindow::draw_button(const DRAWITEMSTRUCT& source) {
    DRAWITEMSTRUCT item = source;
    const int width = source.rcItem.right - source.rcItem.left;
    const int height = source.rcItem.bottom - source.rcItem.top;
    HDC buffer = CreateCompatibleDC(source.hDC);
    HBITMAP bitmap = buffer ? CreateCompatibleBitmap(source.hDC, width, height) : nullptr;
    HGDIOBJ oldBitmap = nullptr;
    if (bitmap) {
        oldBitmap = SelectObject(buffer, bitmap);
        item.hDC = buffer;
        item.rcItem = {0, 0, width, height};
    }
    const int saved = SaveDC(item.hDC);
    IntersectClipRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom);
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hot = GetPropW(item.hwndItem, L"Gifler.Hot") != nullptr;
    const bool record = item.CtlID == RecButton;
    COLORREF background = highContrast_ ? GetSysColor(COLOR_BTNFACE) : RGB(38, 40, 41);
    COLORREF foreground = highContrast_ ? GetSysColor(COLOR_BTNTEXT) : Ink;
    if (record && !highContrast_) foreground = Accent;
    if (item.CtlID == FpsButton && !highContrast_) { background = RGB(22, 28, 24); foreground = RGB(192, 211, 179); }
    if (item.CtlID == FormatButton && !highContrast_) foreground = RGB(218, 201, 151);
    if (!disabled && (hot || pressed)) {
        background = highContrast_ ? GetSysColor(COLOR_HIGHLIGHT) : (pressed ? RGB(18, 20, 21) : RGB(58, 61, 62));
        if (highContrast_) foreground = GetSysColor(COLOR_HIGHLIGHTTEXT);
        if (item.CtlID == CloseButton && !highContrast_) { background = Accent; foreground = RGB(255, 255, 255); }
    }
    if (disabled) foreground = highContrast_ ? GetSysColor(COLOR_GRAYTEXT) : RGB(101, 106, 106);
    fill(item.hDC, item.rcItem, highContrast_ ? GetSysColor(COLOR_BTNFACE) : Surface);
    SetDCPenColor(item.hDC, background);
    SetDCBrushColor(item.hDC, background);
    SelectObject(item.hDC, GetStockObject(DC_PEN));
    SelectObject(item.hDC, GetStockObject(DC_BRUSH));
    RoundRect(item.hDC, 0, 0, item.rcItem.right, item.rcItem.bottom, scaled(3), scaled(3));
    if (!highContrast_) {
        RECT edge{scaled(2), 0, item.rcItem.right - scaled(2), 1};
        fill(item.hDC, edge, pressed ? RGB(12, 13, 14) : RGB(66, 69, 70));
        edge.top = item.rcItem.bottom - 1;
        edge.bottom = item.rcItem.bottom;
        fill(item.hDC, edge, RGB(12, 13, 14));
    }
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, foreground);
    SelectObject(item.hDC, iconFont_);
    RECT rect = item.rcItem;
    const wchar_t* glyph = L"";
    switch (item.CtlID) {
    case RecButton: glyph = recording_ ? L"\uE71A" : L"\uE7C8"; break;
    case PlayButton: glyph = previewPlaying_ ? L"\uE769" : L"\uE768"; break;
    case CopyGifButton: glyph = L"\uE8C8"; break;
    case SaveButton: glyph = saving_ ? L"\uE711" : L"\uE74E"; break;
    case MoreButton: glyph = L"\uE712"; break;
    case CloseButton: glyph = L"\uE8BB"; break;
    default: break;
    }
    if (record) {
        const int size = scaled(recording_ ? 7 : 6);
        const int x = (rect.right - size) / 2;
        const int y = (rect.bottom - size) / 2;
        SetDCPenColor(item.hDC, foreground);
        SetDCBrushColor(item.hDC, foreground);
        if (recording_) Rectangle(item.hDC, x, y, x + size, y + size);
        else Ellipse(item.hDC, x, y, x + size, y + size);
    } else if (item.CtlID == FpsButton || item.CtlID == FormatButton) {
        rect.left += scaled(4);
        const auto label = dropdown_label(item.CtlID);
        SelectObject(item.hDC, uiFont_);
        SIZE extent{};
        GetTextExtentPoint32W(item.hDC, label.c_str(), static_cast<int>(label.size()), &extent);
        rect.right = rect.left + extent.cx;
        DrawTextW(item.hDC, label.c_str(), -1, &rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        rect.left = rect.right + scaled(2);
        rect.right = rect.left + scaled(11);
        SelectObject(item.hDC, iconFont_);
        DrawTextW(item.hDC, L"\uE70D", -1, &rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    } else {
        DrawTextW(item.hDC, glyph, -1, &rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    if (item.itemState & ODS_FOCUS) {
        rect = item.rcItem;
        InflateRect(&rect, -scaled(3), -scaled(3));
        DrawFocusRect(item.hDC, &rect);
    }
    RestoreDC(item.hDC, saved);
    if (bitmap) {
        BitBlt(source.hDC, source.rcItem.left, source.rcItem.top, width, height, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, oldBitmap);
        DeleteObject(bitmap);
    }
    if (buffer) DeleteDC(buffer);
}
}
