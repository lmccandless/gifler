#include "gifler_win32/ResizeOverlay.h"
#include "gifler_win32/WindowRegion.h"
#include <windowsx.h>
#include <algorithm>
#include <cstdint>

namespace gifler::win32 {
ResizeOverlay::~ResizeOverlay() { close(); }
void ResizeOverlay::close() {
    if (marks_) DestroyWindow(marks_);
    marks_ = nullptr;
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

int ResizeOverlay::hit(core::PixelPoint p, int width, int height, int band, int corner) {
    if (p.x < 0 || p.y < 0 || p.x >= width || p.y >= height) return HTNOWHERE;
    band = std::max(1, std::min({band, width / 3, height / 3}));
    corner = std::max(band, std::min({corner, width / 3, height / 3}));
    const bool left = p.x < band, right = p.x >= width - band;
    const bool top = p.y < band, bottom = p.y >= height - band;
    if (!(left || right || top || bottom)) return HTNOWHERE;
    if (p.x < corner && p.y < corner) return HTTOPLEFT;
    if (p.x >= width - corner && p.y < corner) return HTTOPRIGHT;
    if (p.x < corner && p.y >= height - corner) return HTBOTTOMLEFT;
    if (p.x >= width - corner && p.y >= height - corner) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    return top ? HTTOP : HTBOTTOM;
}

bool ResizeOverlay::create(HWND owner) {
    owner_ = owner;
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = procedure;
    wc.hInstance = instance;
    wc.lpszClassName = L"GiflerResizeOverlay";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, this);
    if (!hwnd_) return false;
    SetLayeredWindowAttributes(hwnd_, 0, 1, LWA_ALPHA);
    marks_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, hwnd_, nullptr, instance, nullptr);
    if (!marks_) { close(); return false; }
    SetWindowLongPtrW(marks_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetLayeredWindowAttributes(marks_, RGB(0, 0, 0), 255, LWA_COLORKEY);
    // Hover marks must never appear in the recording, including during a resize.
    if (!set_window_excluded_from_capture(hwnd_, true) || !set_window_excluded_from_capture(marks_, true)) {
        close();
        return false;
    }
    return true;
}

void ResizeOverlay::update(core::PixelRect rect, int band, int corner, bool visible) {
    if (!hwnd_) return;
    if (!visible || rect.width < 3 || rect.height < 3) {
        ShowWindow(hwnd_, SW_HIDE);
        ShowWindow(marks_, SW_HIDE);
        hot_ = HTNOWHERE;
        return;
    }
    const bool changed = rect.width != rect_.width || rect.height != rect_.height || band != band_ || corner != corner_;
    rect_ = rect;
    band_ = band;
    corner_ = corner;
    if (changed || !IsWindowVisible(hwnd_)) {
        const int inset = std::max(1, std::min({band_, rect.width / 3, rect.height / 3}));
        HRGN region = CreateRectRgn(0, 0, rect.width, rect.height);
        HRGN hole = CreateRectRgn(inset, inset, rect.width - inset, rect.height - inset);
        if (region && hole) {
            CombineRgn(region, region, hole, RGN_DIFF);
            if (!SetWindowRgn(hwnd_, region, FALSE)) DeleteObject(region);
        } else if (region) DeleteObject(region);
        if (hole) DeleteObject(hole);
        SetWindowPos(hwnd_, HWND_TOP, rect.x, rect.y, rect.width, rect.height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetWindowPos(marks_, HWND_TOP, rect.x, rect.y, rect.width, rect.height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(hwnd_, nullptr, FALSE);
        render();
    } else {
        SetWindowPos(hwnd_, nullptr, rect.x, rect.y, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
        SetWindowPos(marks_, nullptr, rect.x, rect.y, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
    }
}

void ResizeOverlay::hover(int value) {
    if (hot_ == value) return;
    hot_ = value;
    render();
}

void ResizeOverlay::render(HDC print) {
    if (!hwnd_ || rect_.width <= 0 || rect_.height <= 0) return;
    if (!print) {
        InvalidateRect(marks_, nullptr, FALSE);
        UpdateWindow(marks_);
        return;
    }
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), rect_.width, -rect_.height, 1, 32, BI_RGB};
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap) { DeleteDC(dc); return; }
    const auto old = SelectObject(dc, bitmap);
    auto* data = static_cast<std::uint32_t*>(pixels);
    // Black is the marks window's transparency key; input lives in the separate ring.
    std::fill_n(data, static_cast<std::size_t>(rect_.width) * rect_.height, 0x01000000u);
    const auto scaled = [&](int value) { return std::max(1, MulDiv(value, band_, 12)); };
    const int scale = scaled(1);
    const auto line = [&](int x1, int y1, int x2, int y2) {
        for (int y = std::max(0, y1 - scale); y < std::min(rect_.height, y2 + scale); ++y)
            for (int x = std::max(0, x1 - scale); x < std::min(rect_.width, x2 + scale); ++x)
                data[static_cast<std::size_t>(y) * rect_.width + x] = 0xff151617u;
        for (int y = std::max(0, y1); y < std::min(rect_.height, y2); ++y)
            for (int x = std::max(0, x1); x < std::min(rect_.width, x2); ++x)
                data[static_cast<std::size_t>(y) * rect_.width + x] = 0xffe5e6e2u;
    };
    if (hot_ >= HTLEFT && hot_ <= HTBOTTOMRIGHT) {
        const bool left = hot_ == HTTOPLEFT || hot_ == HTBOTTOMLEFT;
        const bool right = hot_ == HTTOPRIGHT || hot_ == HTBOTTOMRIGHT;
        const bool bottom = hot_ == HTBOTTOMLEFT || hot_ == HTBOTTOMRIGHT;
        for (int i : {3, 6}) {
            const int inset = scaled(i);
            const int length = std::min(scaled(18), corner_ - 2 * scale);
            if (left || right) {
                const int x = left ? inset : rect_.width - inset - scale;
                const int y = bottom ? rect_.height - inset - scale : inset;
                line(left ? x : x - length + scale, y, left ? x + length : x + scale, y + scale);
                line(x, bottom ? y - length + scale : y, x + scale, bottom ? y + scale : y + length);
            } else if (hot_ == HTTOP || hot_ == HTBOTTOM) {
                const int y = hot_ == HTTOP ? inset : rect_.height - inset - scale;
                line(rect_.width / 2 - scaled(11), y, rect_.width / 2 + scaled(11), y + scale);
            } else {
                const int x = hot_ == HTLEFT ? inset : rect_.width - inset - scale;
                line(x, rect_.height / 2 - scaled(11), x + scale, rect_.height / 2 + scaled(11));
            }
        }
    }
    BitBlt(print, 0, 0, rect_.width, rect_.height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

LRESULT CALLBACK ResizeOverlay::procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<ResizeOverlay*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<ResizeOverlay*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        if (!self) return DefWindowProcW(window, message, wp, lp);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = window;
    }
    if (!self) return DefWindowProcW(window, message, wp, lp);
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        if (window == self->marks_) self->render(dc);
        else {
            RECT client{};
            GetClientRect(window, &client);
            FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        EndPaint(window, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PRINTCLIENT: self->render(reinterpret_cast<HDC>(wp)); return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        self->hover(hit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, self->rect_.width,
            self->rect_.height, self->band_, self->corner_));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE: self->hover(HTNOWHERE); return 0;
    case WM_SETCURSOR: {
        POINT p{};
        GetCursorPos(&p);
        ScreenToClient(window, &p);
        const int h = hit({p.x, p.y}, self->rect_.width, self->rect_.height, self->band_, self->corner_);
        LPCWSTR cursor = IDC_ARROW;
        if (h == HTLEFT || h == HTRIGHT) cursor = IDC_SIZEWE;
        if (h == HTTOP || h == HTBOTTOM) cursor = IDC_SIZENS;
        if (h == HTTOPLEFT || h == HTBOTTOMRIGHT) cursor = IDC_SIZENWSE;
        if (h == HTTOPRIGHT || h == HTBOTTOMLEFT) cursor = IDC_SIZENESW;
        SetCursor(LoadCursorW(nullptr, cursor));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        if (!IsWindowEnabled(self->owner_) || IsZoomed(self->owner_)) return 0;
        const int h = hit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, self->rect_.width,
            self->rect_.height, self->band_, self->corner_);
        if (h == HTNOWHERE) return 0;
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(window, &p);
        ReleaseCapture();
        SendMessageW(self->owner_, WM_NCLBUTTONDOWN, h, MAKELPARAM(p.x, p.y));
        self->hover(HTNOWHERE);
        return 0;
    }
    case WM_NCDESTROY:
        if (window == self->marks_) self->marks_ = nullptr;
        else self->hwnd_ = nullptr;
        break;
    }
    return DefWindowProcW(window, message, wp, lp);
}
}
