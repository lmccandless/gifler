#include "gifler_core/Geometry.h"
#include "gifler_win32/Dpi.h"
#include "gifler_win32/WindowRegion.h"

#include <Windows.h>
#include <windowsx.h>

#include <array>
#include <format>
#include <string>

namespace {

constexpr wchar_t ClassName[] = L"GiflerSpikeWindowHole";
constexpr int IdTogglePreview = 2001;
constexpr int IdRec = 2002;

struct SpikeWindow {
    HWND hwnd = nullptr;
    HWND previewButton = nullptr;
    HWND recButton = nullptr;
    unsigned dpi = 96;
    bool previewMode = false;
    gifler::core::PixelRect hole{};
    std::wstring status = L"S01 ready";

    int scaled(int v) const { return gifler::win32::scale_for_dpi(v, dpi); }

    void create_controls(HINSTANCE instance) {
        previewButton = CreateWindowW(L"BUTTON", L"Preview", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 1, 1, hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdTogglePreview)), instance, nullptr);
        recButton = CreateWindowW(L"BUTTON", L"Rec", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 1, 1, hwnd,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdRec)), instance, nullptr);
        HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(previewButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(recButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    bool point_hits_control(POINT p) const {
        for (HWND control : std::array{previewButton, recButton}) {
            RECT r{};
            if (control != nullptr && GetWindowRect(control, &r)) {
                POINT tl{r.left, r.top};
                POINT br{r.right, r.bottom};
                ScreenToClient(hwnd, &tl);
                ScreenToClient(hwnd, &br);
                RECT cr{tl.x, tl.y, br.x, br.y};
                if (PtInRect(&cr, p)) {
                    return true;
                }
            }
        }
        return false;
    }

    void layout() {
        RECT client{};
        GetClientRect(hwnd, &client);
        const int pad = scaled(8);
        const int toolbarH = scaled(42);
        const int statusH = scaled(24);
        MoveWindow(recButton, pad, pad, scaled(58), scaled(28), TRUE);
        MoveWindow(previewButton, pad + scaled(64), pad, scaled(84), scaled(28), TRUE);

        hole = {pad, toolbarH + pad, client.right - client.left - pad * 2,
                client.bottom - client.top - toolbarH - statusH - pad * 2};
        if (hole.width < scaled(120)) {
            hole.width = scaled(120);
        }
        if (hole.height < scaled(80)) {
            hole.height = scaled(80);
        }

        const auto screen = gifler::win32::client_rect_to_screen_pixels(hwnd, hole);
        status = std::format(L"Hole: {}x{} at {},{} | {}", screen.width, screen.height, screen.x, screen.y,
                             previewMode ? L"preview solid" : L"click-through");
        if (previewMode) {
            gifler::win32::clear_window_region(hwnd);
        } else {
            gifler::win32::apply_viewfinder_hole_region(hwnd, hole);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
    }

    void paint() {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
        SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dc, TRANSPARENT);
        RECT st{scaled(8), client.bottom - scaled(24), client.right - scaled(8), client.bottom};
        DrawTextW(dc, status.c_str(), static_cast<int>(status.size()), &st, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        if (previewMode) {
            RECT vf{hole.left(), hole.top(), hole.right(), hole.bottom()};
            FillRect(dc, &vf, GetSysColorBrush(COLOR_WINDOW));
            FrameRect(dc, &vf, GetSysColorBrush(COLOR_HIGHLIGHT));
            DrawTextW(dc, L"Preview paint mode", -1, &vf, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        EndPaint(hwnd, &ps);
    }

    LRESULT hit_test(LPARAM lp) {
        LRESULT def = DefWindowProcW(hwnd, WM_NCHITTEST, 0, lp);
        if (def != HTCLIENT) {
            return def;
        }
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        RECT client{};
        GetClientRect(hwnd, &client);
        const int grip = scaled(20);
        if (p.x >= client.right - grip && p.y >= client.bottom - grip) {
            return HTBOTTOMRIGHT;
        }
        if (p.y < scaled(42) && !point_hits_control(p)) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
};

SpikeWindow* self(HWND hwnd) {
    return reinterpret_cast<SpikeWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* window = static_cast<SpikeWindow*>(cs->lpCreateParams);
        window->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    auto* window = self(hwnd);
    if (window == nullptr) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    switch (msg) {
    case WM_CREATE:
        window->dpi = gifler::win32::dpi_for_window_or_default(hwnd);
        window->create_controls(reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance);
        window->layout();
        return 0;
    case WM_SIZE:
        window->layout();
        return 0;
    case WM_DPICHANGED: {
        window->dpi = HIWORD(wp);
        const auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        window->layout();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize.x = window->scaled(300);
        info->ptMinTrackSize.y = window->scaled(220);
        return 0;
    }
    case WM_NCHITTEST:
        return window->hit_test(lp);
    case WM_COMMAND:
        if (LOWORD(wp) == IdTogglePreview) {
            window->previewMode = !window->previewMode;
            SetWindowTextW(window->previewButton, window->previewMode ? L"Capture" : L"Preview");
            window->layout();
        }
        return 0;
    case WM_PAINT:
        window->paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    gifler::win32::enable_per_monitor_dpi_awareness_best_effort();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = ClassName;
    RegisterClassExW(&wc);

    SpikeWindow window{};
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, ClassName, L"S01 Gifler Window Hole Spike", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 520, 360, nullptr, nullptr, instance, &window);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Could not create S01 window.", L"S01", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
