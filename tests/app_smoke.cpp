#include "gifler_app/MainWindow.h"
#include "gifler_core/AspectRatio.h"
#include "gifler_win32/WicImageWriter.h"
#include <CommCtrl.h>
#include <chrono>
#include <iostream>
#include <cstring>
#include <atomic>
#include <cstdlib>

namespace gifler::app {
struct MainWindowTestAccess {
    static bool aspect_test(MainWindow& app) {
        bool ok = true;
        for (const unsigned dpi : {96u, 120u, 144u, 192u}) {
            app.dpi_ = dpi;
            app.refresh_fonts();
            for (int index = 1; index < static_cast<int>(gifler::core::CaptureAspectRatios.size()); ++index) {
                SetWindowPos(app.hwnd_, nullptr, 100, 100, 600, 400, SWP_NOZORDER | SWP_NOACTIVATE);
                SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::AspectFree + index, 0);
                const auto ratio = gifler::core::CaptureAspectRatios[index];
                const auto hole = app.viewfinder_client_rect();
                ok = hole.width * ratio.height == hole.height * ratio.width && ok;
                SetWindowPos(app.hwnd_, nullptr, 100, 100, 523, 387, SWP_NOZORDER | SWP_NOACTIVATE);
                const auto snapped = app.viewfinder_client_rect();
                ok = snapped.width * ratio.height == snapped.height * ratio.width && ok;
                ok = (GetMenuState(app.aspectMenu_, MainWindow::AspectFree + index, MF_BYCOMMAND) & MF_CHECKED) && ok;
                for (WPARAM side = WMSZ_LEFT; side <= WMSZ_BOTTOMRIGHT; ++side) {
                    RECT proposed{-600, -300, 101, 203};
                    const auto before = proposed;
                    ok = SendMessageW(app.hwnd_, WM_SIZING, side, reinterpret_cast<LPARAM>(&proposed)) == TRUE && ok;
                    const auto chrome = app.capture_chrome_size();
                    ok = (proposed.right - proposed.left - chrome.width) * ratio.height ==
                         (proposed.bottom - proposed.top - chrome.height) * ratio.width && ok;
                    if (side == WMSZ_TOPLEFT) ok = proposed.right == before.right && proposed.bottom == before.bottom && ok;
                    if (side == WMSZ_TOPRIGHT) ok = proposed.left == before.left && proposed.bottom == before.bottom && ok;
                    if (side == WMSZ_BOTTOMLEFT) ok = proposed.right == before.right && proposed.top == before.top && ok;
                    if (side == WMSZ_BOTTOMRIGHT) ok = proposed.left == before.left && proposed.top == before.top && ok;
                }
            }
        }
        app.recording_ = true;
        app.select_aspect_ratio(0);
        ok = app.settings_.captureAspectRatio == 6 && ok;
        app.recording_ = false;
        app.select_aspect_ratio(0);
        app.dpi_ = 96;
        app.refresh_fonts();
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::ExportSocialMp4, 0);
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::Audio441k, 0);
        const auto request = app.make_video_request(L"unused-social-test.mp4");
        ok = app.selectedExportFormat_ == 1 && request.socialCompatibility && request.audioSampleRate == 44100 && ok;
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::ExportMp4, 0);
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::Audio48k, 0);
        ok = !app.settings_.socialMp4 && app.settings_.mp4AudioSampleRate == 48000 && ok;
        std::cout << (ok ? "Aspect menus, native sizing, anchoring, DPI, and social/audio selections passed.\n" : "Aspect/social checks FAILED.\n");
        return ok;
    }
    static bool resize_test(MainWindow& app, const std::filesystem::path& directory) {
        constexpr int width = 240, height = 140;
        auto& overlay = app.resizeOverlay_;
        overlay.update({-2000, -2000, width, height}, 12, 26, true);
        const HWND window = overlay.handle();
        DWORD affinity = 0;
        bool ok = window && GetWindowDisplayAffinity(window, &affinity) && affinity == WDA_EXCLUDEFROMCAPTURE;
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        ok = GetWindowRgn(window, region) != ERROR && !PtInRegion(region, width / 2, height / 2) && ok;
        struct Route { WPARAM hit = 0; LPARAM position = 0; } route;
        const SUBCLASSPROC routeProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) -> LRESULT {
            if (msg == WM_NCLBUTTONDOWN) {
                auto& r = *reinterpret_cast<Route*>(data);
                r = {wp, lp};
                return 0;
            }
            return DefSubclassProc(hwnd, msg, wp, lp);
        };
        SetWindowSubclass(app.hwnd_, routeProc, 89, reinterpret_cast<DWORD_PTR>(&route));
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        void* bits = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap) {
            RemoveWindowSubclass(app.hwnd_, routeProc, 89);
            DeleteObject(region);
            DeleteDC(dc);
            return false;
        }
        const auto old = SelectObject(dc, bitmap);
        gifler::core::BgraFrame sheet;
        sheet.width = width * 4;
        sheet.height = height * 2;
        sheet.stride = sheet.width * 4;
        sheet.pixels.resize(static_cast<std::size_t>(sheet.stride) * sheet.height);
        struct Zone { int x, y, hit; };
        int index = 0;
        for (const auto& zone : {Zone{3, 3, HTTOPLEFT}, {width / 2, 3, HTTOP}, {width - 3, 3, HTTOPRIGHT},
                {width - 3, height / 2, HTRIGHT}, {width - 3, height - 3, HTBOTTOMRIGHT},
                {width / 2, height - 3, HTBOTTOM}, {3, height - 3, HTBOTTOMLEFT}, {3, height / 2, HTLEFT}}) {
            ok = PtInRegion(region, zone.x, zone.y) && ok;
            SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(zone.x, zone.y));
            SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), 0);
            const auto* source = static_cast<const std::uint32_t*>(bits);
            int marks = 0;
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                const auto pixel = source[y * width + x];
                const bool mark = (pixel >> 24) == 255;
                marks += pixel == 0xffe5e6e2u;
                const auto offset = static_cast<std::size_t>((index / 4 * height + y) * sheet.stride + (index % 4 * width + x) * 4);
                const auto background = (x == 0 || y == 0 || x == width - 1 || y == height - 1) ? 75u : 35u;
                const std::uint32_t color = mark ? pixel : 0xff000000u | background | (background << 8) | (background << 16);
                std::memcpy(sheet.pixels.data() + offset, &color, 4);
            }
            ok = marks >= 40 && marks <= 150 && ok;
            SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(zone.x, zone.y));
            ok = route.hit == static_cast<WPARAM>(zone.hit) && route.position == MAKELPARAM(-2000 + zone.x, -2000 + zone.y) && ok;
            ++index;
        }
        ok = gifler::win32::save_bgra_frame_as_png(sheet, directory / L"resize-hover-eight-directions.png") && ok;
        SendMessageW(window, WM_MOUSELEAVE, 0, 0);
        SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), 0);
        const auto* source = static_cast<const std::uint32_t*>(bits);
        for (int i = 0; i < width * height; ++i) ok = (source[i] >> 24) == 1 && ok;
        RemoveWindowSubclass(app.hwnd_, routeProc, 89);
        overlay.update({}, 12, 26, false);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        DeleteObject(region);
        std::cout << (ok ? "Eight resize routes, center hole, capture exclusion, and hover rendering passed.\n" : "Resize checks FAILED.\n");
        return ok;
    }
    static bool dialog_test(MainWindow& app, bool cancel) {
        const auto threadId = GetCurrentThreadId();
        std::atomic_bool done = false;
        bool valid = false;
        std::jthread input([&] {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            HWND dialog = nullptr;
            while (!done && !dialog && std::chrono::steady_clock::now() < until) {
                EnumThreadWindows(threadId, [](HWND window, LPARAM state) -> BOOL {
                    wchar_t title[64]{};
                    GetWindowTextW(window, title, 64);
                    if (std::wstring(title) == L"Custom frame rate") {
                        *reinterpret_cast<HWND*>(state) = window;
                        return FALSE;
                    }
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&dialog));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!dialog) {
                EnumThreadWindows(threadId, [](HWND window, LPARAM owner) -> BOOL {
                    if (GetWindow(window, GW_OWNER) == reinterpret_cast<HWND>(owner)) PostMessageW(window, WM_CLOSE, 0, 0);
                    return TRUE;
                }, reinterpret_cast<LPARAM>(app.hwnd_));
                return;
            }
            while (!GetDlgItem(dialog, 1701) && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (cancel) {
                SetDlgItemTextW(dialog, 1701, L"42");
                SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
                valid = true;
            } else {
                valid = true;
                for (const auto* invalid : {L"0", L"241", L"7a", L""}) {
                    SetDlgItemTextW(dialog, 1701, invalid);
                    SendMessageW(dialog, WM_COMMAND, IDOK, 0);
                    wchar_t error[128]{};
                    GetDlgItemTextW(dialog, 1702, error, 128);
                    valid = IsWindow(dialog) && error[0] != L'\0' && valid;
                }
                SetDlgItemTextW(dialog, 1701, L"77");
                SendMessageW(dialog, WM_COMMAND, IDOK, 0);
            }
        });
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::FpsCustom, 0);
        done = true;
        input.join();
        std::cout << "Dialog " << (cancel ? "cancel" : "apply") << ": validation=" << valid << ", selected=" << app.selectedFps_ << "\n";
        return valid && app.selectedFps_ == 77;
    }

    static bool render(MainWindow& app, const std::filesystem::path& path, int width, int height, unsigned dpi) {
        app.dpi_ = dpi;
        app.refresh_fonts();
        SetWindowPos(app.hwnd_, nullptr, 100, 100, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        app.layout();
        const auto hole = app.viewfinder_client_rect();
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        bool success = GetWindowRgn(app.hwnd_, region) != ERROR &&
                       !PtInRegion(region, hole.x + hole.width / 2, hole.y + hole.height / 2) &&
                       PtInRegion(region, width / 2, app.scaled(20)) &&
                       hole.y == app.scaled(34) && height - hole.bottom() == app.scaled(10) &&
                       width - hole.right() == app.scaled(10) &&
                       !GetDlgItem(app.hwnd_, MainWindow::MinimizeButton) && !GetDlgItem(app.hwnd_, MainWindow::MaximizeButton);
        const auto hit = [&](int x, int y, LRESULT expected) {
            return PtInRegion(region, x, y) && app.hit_test(MAKELPARAM(100 + x, 100 + y)) == expected;
        };
        success = hit(width - app.scaled(9), height / 2, HTRIGHT) &&
                  hit(width / 2, height - app.scaled(9), HTBOTTOM) &&
                  hit(width - app.scaled(18), height - app.scaled(4), HTBOTTOMRIGHT) && success;
        DeleteObject(region);
        RECT previous{};
        for (int id : {MainWindow::RecButton, MainWindow::FpsButton, MainWindow::FormatButton,
                       MainWindow::PlayButton, MainWindow::CopyGifButton, MainWindow::SaveButton, MainWindow::MoreButton}) {
            if (!(GetWindowLongPtrW(GetDlgItem(app.hwnd_, id), GWL_STYLE) & WS_VISIBLE)) continue;
            RECT rect{};
            GetWindowRect(GetDlgItem(app.hwnd_, id), &rect);
            success = success && rect.left >= previous.right + (previous.right ? app.scaled(2) : 0) && rect.right <= 100 + width - app.scaled(25) &&
                      rect.top == 100 + app.scaled(5) && rect.bottom == 100 + app.scaled(27);
            previous = rect;
        }
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        void* bits = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap) { DeleteDC(dc); return false; }
        HGDIOBJ old = SelectObject(dc, bitmap);
        const HGDIOBJ oldFont = SelectObject(dc, app.uiFont_);
        for (int id : {MainWindow::FpsButton, MainWindow::FormatButton}) {
            HWND control = GetDlgItem(app.hwnd_, id);
            if (!(GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE)) continue;
            RECT rect{};
            GetClientRect(control, &rect);
            wchar_t label[64]{};
            GetWindowTextW(control, label, 64);
            if (id == MainWindow::FpsButton && app.compactFps_)
                wcscpy_s(label, std::to_wstring(app.selectedFps_).c_str());
            SIZE extent{};
            GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &extent);
            const int requiredWidth = extent.cx + app.scaled(4) + app.scaled(2) + app.scaled(11) + app.scaled(2);
            if (rect.right != requiredWidth) std::wcout << L"Dropdown does not fit label exactly: " << label << L"\n";
            success = success && rect.right == requiredWidth;
        }
        SelectObject(dc, oldFont);
        SetPixelV(dc, 1, 1, RGB(51, 52, 53));
        success = SendMessageW(GetDlgItem(app.hwnd_, MainWindow::RecButton), WM_ERASEBKGND,
                               reinterpret_cast<WPARAM>(dc), 0) == 1 && GetPixel(dc, 1, 1) == RGB(51, 52, 53) && success;
        SendMessageW(app.hwnd_, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
        gifler::core::BgraFrame frame;
        frame.width = width;
        frame.height = height;
        frame.stride = width * 4;
        frame.pixels.resize(static_cast<std::size_t>(frame.stride) * height);
        std::memcpy(frame.pixels.data(), bits, frame.pixels.size());
        std::size_t light = 0;
        std::size_t red = 0;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const auto offset = static_cast<std::size_t>(y * frame.stride + x * 4);
            frame.pixels[offset + 3] = std::byte{255};
            if (x >= hole.x && x < hole.right() && y >= hole.y && y < hole.bottom()) {
                frame.pixels[offset] = std::byte{242};
                frame.pixels[offset + 1] = std::byte{240};
                frame.pixels[offset + 2] = std::byte{237};
            } else {
                if (std::to_integer<int>(frame.pixels[offset]) > 160) ++light;
                if (std::to_integer<int>(frame.pixels[offset + 2]) > 180 &&
                    std::to_integer<int>(frame.pixels[offset]) < 100) ++red;
            }
        }
        success = light > 10 && red > 5 && gifler::win32::save_bgra_frame_as_png(frame, path) && success;
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        if (!success) std::wcout << L"Render/geometry failed: " << path << L"\n";
        return success;
    }

    static bool chrome_test(MainWindow& app, const std::filesystem::path& directory) {
        bool ok = GetMenu(app.hwnd_) == nullptr;
        for (int fps : {5, 10, 15, 24, 30, 48, 60, 120}) {
            SendMessageW(app.hwnd_, WM_COMMAND, 1100 + fps, 0);
            ok = app.selectedFps_ == fps && (GetMenuState(app.fpsMenu_, 1100 + fps, MF_BYCOMMAND) & MF_CHECKED) && ok;
        }
        app.select_fps(77);
        ok = app.selectedFps_ == 77 && (GetMenuState(app.fpsMenu_, MainWindow::FpsCustom, MF_BYCOMMAND) & MF_CHECKED) && ok;
        app.select_fps(241);
        ok = app.selectedFps_ == 77 && ok;
        ok = dialog_test(app, false) && ok;
        ok = dialog_test(app, true) && ok;
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::Target20, 0);
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::AudioButton, 0);
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::ExportWebM, 0);
        const auto loaded = gifler::core::load_settings_or_defaults(app.settingsPath_);
        ok = loaded.defaultFps == 77 && loaded.target.displayTargetMb == 20 && loaded.captureAudio && loaded.lastExportFormat == 3 && ok;
        app.recording_ = true;
        app.refresh_chrome();
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::Fps60, 0);
        ok = app.selectedFps_ == 77 && !IsWindowEnabled(GetDlgItem(app.hwnd_, MainWindow::FpsButton)) && ok;
        app.recording_ = false;
        app.select_fps(60);
        ok = render(app, directory / L"chrome-660-100.png", 660, 390, 96) && ok;
        ok = render(app, directory / L"chrome-360-100.png", 360, 220, 96) && ok;
        app.select_fps(240);
        ok = render(app, directory / L"chrome-248-100.png", 248, 140, 96) && ok;
        ok = render(app, directory / L"chrome-248-150.png", 372, 210, 144) && ok;
        ok = render(app, directory / L"chrome-290-100.png", 290, 170, 96) && ok;
        ok = render(app, directory / L"chrome-290-125.png", 363, 212, 120) && ok;
        ok = render(app, directory / L"chrome-290-150.png", 435, 255, 144) && ok;
        ok = render(app, directory / L"chrome-120-100.png", 120, 100, 96) && ok;
        ok = render(app, directory / L"chrome-660-150.png", 990, 585, 144) && ok;
        ok = render(app, directory / L"chrome-360-200.png", 720, 440, 192) && ok;
        app.dpi_ = 96;
        app.refresh_fonts();
        app.select_fps(30);
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::ExportMp4, 0);
        ok = render(app, directory / L"chrome-30-mp4.png", 500, 350, 96) && ok;
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::ExportSocialMp4, 0);
        ok = render(app, directory / L"chrome-social-320-100.png", 320, 220, 96) && ok;
        ok = render(app, directory / L"chrome-social-248-150.png", 372, 330, 144) && ok;
        app.select_fps(5);
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::ExportGif, 0);
        ok = render(app, directory / L"chrome-5-gif.png", 500, 200, 96) && ok;
        return ok;
    }

    static bool run(const std::filesystem::path& directory) {
        MainWindow app;
        if (!app.create(GetModuleHandleW(nullptr), SW_HIDE)) {
            std::cout << "App create failed: " << GetLastError() << "\n";
            return false;
        }
        const bool chromeOk = aspect_test(app) && chrome_test(app, directory) && resize_test(app, directory);
        std::cout << (chromeOk ? "Chrome, geometry, DPI, FPS, and settings checks passed.\n" : "Chrome checks FAILED.\n");
        gifler::record::RecorderSettings settings;
        settings.fps = 30;
        app.recorder_.start_synthetic(settings, 320, 180);
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        app.recorder_.stop();
        const auto frames = app.active_frames().size();
        app.selectedExportFormat_ = 0;
        app.start_export(directory / L"window-export.gif", false);
        const auto pump = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (app.saving_ && std::chrono::steady_clock::now() < deadline) {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return !app.saving_;
        };
        bool success = pump() && app.saveResultSuccess_ && !app.preparedMediaPath_.empty() &&
                       std::filesystem::file_size(app.preparedMediaPath_) > 0;
        app.start_export(directory / L"canceled-window-export.gif", false);
        SendMessageW(app.hwnd_, WM_COMMAND, MainWindow::SaveButton, 0);
        success = pump() && app.cancelExport_ && app.active_frames().size() == frames && success;
        DestroyWindow(app.hwnd_);
        return success && chromeOk;
    }
};
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path root = argv[1];
    std::filesystem::create_directories(root);
    if (_wputenv_s(L"APPDATA", root.c_str()) || _wputenv_s(L"LOCALAPPDATA", root.c_str())) return 2;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_UPDOWN_CLASS};
    InitCommonControlsEx(&controls);
    const bool ok = gifler::app::MainWindowTestAccess::run(root);
    std::cout << (ok ? "App export and cancel smoke passed.\n" : "App smoke failed.\n");
    return ok ? 0 : 1;
}
