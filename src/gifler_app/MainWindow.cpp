#include "gifler_app/MainWindow.h"
#include "gifler_app/FpsDialog.h"
#include "gifler_core/AspectRatio.h"

#include "gifler_editor/EditorWindow.h"
#include "gifler_win32/Clipboard.h"
#include "gifler_win32/Dpi.h"
#include "gifler_win32/WindowRegion.h"

#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <fstream>

namespace gifler::app {
namespace {

constexpr wchar_t WindowClassName[] = L"GiflerNativeMainWindow";
constexpr UINT_PTR PreviewTimerId = 3001;
constexpr UINT_PTR RecorderTimerId = 3002;
constexpr UINT SaveProgressMessage = WM_APP + 1;
constexpr UINT SaveCompleteMessage = WM_APP + 2;

int duration_ticks_to_timer_ms(std::int64_t durationTicks) {
    if (durationTicks <= 0) {
        return 100;
    }
    const auto ms = static_cast<int>(durationTicks / 10'000);
    return std::clamp(ms, 16, 1000);
}

std::wstring media_format_name(int format) {
    if (format == 0) {
        return L"GIF";
    }
    if (format == 2) {
        return L"WebP";
    }
    if (format == 3) {
        return L"WebM";
    }
    return L"MP4";
}

std::wstring media_extension(int format) {
    if (format == 0) {
        return L"gif";
    }
    if (format == 2) {
        return L"webp";
    }
    if (format == 3) {
        return L"webm";
    }
    return L"mp4";
}

} // namespace

bool MainWindow::create(HINSTANCE instance, int showCommand) {
    instance_ = instance;
    dpi_ = win32::dpi_for_window_or_default(nullptr);
    settingsPath_ = gifler::core::default_settings_path();
    settings_ = gifler::core::load_settings_or_defaults(settingsPath_);
    selectedFps_ = settings_.defaultFps;
    selectedExportFormat_ = settings_.lastExportFormat;
    captureCursor_ = settings_.captureCursor;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWindow::static_window_proc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = WindowClassName;

    RegisterClassExW(&wc);

    const int initialWidth = scaled(660);
    const int initialHeight = scaled(390);
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST, WindowClassName, L"Gifler",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, initialWidth, initialHeight,
                            nullptr, nullptr, instance_, this);
    if (hwnd_ == nullptr) {
        return false;
    }
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    fit_window_aspect();
    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    apply_capture_or_preview_region();
    update_resize_overlay();
    return true;
}

int MainWindow::run_message_loop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(hwnd_, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::static_window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->window_proc(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT MainWindow::window_proc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        dpi_ = win32::dpi_for_window_or_default(hwnd_);
        gifler::exporting::cleanup_old_clipboard_cache_best_effort(gifler::exporting::default_clipboard_cache_dir());
        create_menu();
        create_chrome();
        if (!resizeOverlay_.create(hwnd_)) return -1;
        saveProgress_ = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | PBS_SMOOTH, 0, 0, 1, 1, hwnd_, nullptr,
                                        instance_, nullptr);
        SendMessageW(saveProgress_, PBM_SETRANGE32, 0, 100);
        SendMessageW(saveProgress_, PBM_SETSTATE, PBST_NORMAL, 0);
        layout();
        return 0;
    case WM_SIZE:
        layout();
        return 0;
    case WM_WINDOWPOSCHANGING: {
        auto& position = *reinterpret_cast<WINDOWPOS*>(lParam);
        if (settings_.captureAspectRatio && !(position.flags & SWP_NOSIZE) && !IsIconic(hwnd_) && position.cx > 0 && position.cy > 0) {
            const auto size = gifler::core::aspect_window_size({position.cx, position.cy},
                gifler::core::CaptureAspectRatios[settings_.captureAspectRatio], capture_chrome_size(),
                {scaled(120), scaled(65)}, gifler::core::ResizeSide::BottomRight, true);
            position.cx = size.width;
            position.cy = size.height;
        }
        break;
    }
    case WM_MOVE:
        update_status_rect_text();
        update_resize_overlay();
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        refresh_fonts();
        const auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        fit_window_aspect();
        return 0;
    }
    case WM_SIZING: {
        if (!settings_.captureAspectRatio || wParam < WMSZ_LEFT || wParam > WMSZ_BOTTOMRIGHT) break;
        auto& rect = *reinterpret_cast<RECT*>(lParam);
        const auto adjusted = gifler::core::constrain_capture_aspect(
            {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top},
            gifler::core::CaptureAspectRatios[settings_.captureAspectRatio], capture_chrome_size(),
            {scaled(120), scaled(65)}, static_cast<gifler::core::ResizeSide>(wParam));
        rect = {adjusted.x, adjusted.y, adjusted.right(), adjusted.bottom()};
        return TRUE;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = scaled(120);
        info->ptMinTrackSize.y = scaled(65);
        if (settings_.captureAspectRatio) {
            const auto size = gifler::core::aspect_window_size({scaled(120), scaled(65)},
                gifler::core::CaptureAspectRatios[settings_.captureAspectRatio], capture_chrome_size(),
                {scaled(120), scaled(65)}, gifler::core::ResizeSide::BottomRight, true);
            info->ptMinTrackSize = {size.width, size.height};
        }
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) {
            info->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left, monitor.rcWork.top - monitor.rcMonitor.top};
            info->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left, monitor.rcWork.bottom - monitor.rcWork.top};
            if (settings_.captureAspectRatio) {
                const auto size = gifler::core::aspect_window_size({info->ptMaxSize.x, info->ptMaxSize.y},
                    gifler::core::CaptureAspectRatios[settings_.captureAspectRatio], capture_chrome_size(),
                    {scaled(120), scaled(65)}, gifler::core::ResizeSide::BottomRight, true);
                info->ptMaxPosition.x += (info->ptMaxSize.x - size.width) / 2;
                info->ptMaxPosition.y += (info->ptMaxSize.y - size.height) / 2;
                info->ptMaxSize = {size.width, size.height};
            }
        }
        return 0;
    }
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCPAINT:
        return 0;
    case WM_NCACTIVATE:
        InvalidateRect(hwnd_, nullptr, FALSE);
        return TRUE;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        refresh_fonts();
        InvalidateRect(hwnd_, nullptr, TRUE);
        refresh_chrome();
        return 0;
    case WM_DRAWITEM:
        draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_NCHITTEST:
        return hit_test(lParam);
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == CloseButton) { SendMessageW(hwnd_, WM_CLOSE, 0, 0); return 0; }
        if (id == MinimizeButton) { ShowWindow(hwnd_, SW_MINIMIZE); return 0; }
        if (id == MaximizeButton) { ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE); return 0; }
        if (id == FpsButton) { if (!recording_ && !saving_) show_popup(fpsMenu_, id); return 0; }
        if (id == FormatButton) { if (!saving_) show_popup(exportMenu_, id); return 0; }
        if (id == MoreButton) { if (!saving_) show_popup(commandMenu_, id); return 0; }
        if (saving_) {
            if (id == SaveButton) {
                cancelExport_ = true;
                set_status(L"Canceling export...");
                return 0;
            }
            set_status(saveProgressLabel_ + L" in progress");
            return 0;
        }
        if (id >= AspectFree && id < AspectFree + static_cast<int>(gifler::core::CaptureAspectRatios.size())) {
            select_aspect_ratio(id - AspectFree);
            return 0;
        }
        if (id == Audio48k || id == Audio441k) {
            settings_.mp4AudioSampleRate = id == Audio48k ? 48000 : 44100;
            preparedMediaPath_.clear();
            save_preferences();
            update_menu_state();
            return 0;
        }
        if (id == AudioButton && !recording_) {
            settings_.captureAudio = !settings_.captureAudio;
            save_preferences();
            update_menu_state();
            set_status(settings_.captureAudio ? L"System audio enabled for the next recording" : L"System audio off");
        } else if (id == TargetNone || id == Target5 || id == Target10 || id == Target20 || id == Target50 || id == Target100) {
            settings_.target.displayTargetMb = id - TargetNone;
            settings_.target.internalSafetyTargetMb = settings_.target.displayTargetMb * 0.95;
            preparedMediaPath_.clear();
            save_preferences();
            update_menu_state();
            set_status(id == TargetNone ? L"Export size: no limit" : std::format(L"Export target: {} MB", id - TargetNone));
        } else if (id == RecButton) {
            if (recording_) {
                stop_recording();
            } else {
                start_recording();
            }
        } else if (id == CursorButton) {
            captureCursor_ = !captureCursor_;
            save_preferences();
            update_menu_state();
            set_status(captureCursor_ ? L"Cursor capture enabled" : L"Cursor capture disabled");
        } else if (id == PlayButton && !recording_) {
            toggle_preview_playback();
        } else if (id == CopyGifButton) {
            copy_media();
        } else if (id == SaveButton) {
            save_recording();
        } else if (id == EditButton && !recording_) {
            const auto& frames = active_frames();
            const auto revision = recordingRevision_;
            gifler::editor::EditorWindow::show(hwnd_, frames, [this, revision](std::vector<gifler::core::BgraFrame> editedFrames) {
                if (revision != recordingRevision_) {
                    set_status(L"This editor belongs to an earlier recording");
                    return;
                }
                apply_edited_frames(std::move(editedFrames));
            });
            set_status(frames.empty() ? L"No recording to edit | GIF: --" : L"Editor opened");
        } else if (id == FrameButton) {
            set_status(L"Frame menu stub");
        } else if (!recording_ && id == FpsCustom) {
            const auto fps = prompt_custom_fps(hwnd_, selectedFps_);
            if (fps) select_fps(*fps);
        } else if (!recording_ && (id == Fps5 || id == Fps10 || id == Fps15 || id == Fps24 ||
                   id == Fps30 || id == Fps48 || id == Fps60 || id == Fps120)) {
            select_fps(id - 1100);
        } else if (id == ExportGif || id == ExportMp4 || id == ExportSocialMp4 || id == ExportWebP || id == ExportWebM) {
            selectedExportFormat_ = id == ExportGif ? 0 : (id == ExportMp4 || id == ExportSocialMp4) ? 1 : id == ExportWebP ? 2 : 3;
            if (selectedExportFormat_ == 1) settings_.socialMp4 = id == ExportSocialMp4;
            preparedMediaPath_.clear();
            save_preferences();
            update_menu_state();
            set_status(selectedExportFormat_ == 0 ? L"Export format: GIF | GIF: --"
                                                  : selectedExportFormat_ == 1 ? (settings_.socialMp4 ? L"Export format: MP4 Social / X" : L"Export format: MP4 | GIF: --")
                                                  : selectedExportFormat_ == 2 ? L"Export format: WebP | GIF: --"
                                                                              : L"Export format: WebM | GIF: --");
        }
        return 0;
    }
    case SaveProgressMessage: {
        const int progress = std::clamp(static_cast<int>(wParam), 0, 100);
        SendMessageW(saveProgress_, PBM_SETPOS, progress, 0);
        set_status(std::format(L"{}: {}%", saveProgressLabel_, progress));
        return 0;
    }
    case SaveCompleteMessage:
        finish_save();
        return 0;
    case WM_TIMER:
        if (wParam == RecorderTimerId) {
            if (recording_ && recorder_.state() == gifler::record::RecorderState::Failed) {
                const auto error = recorder_.last_error();
                stop_recording();
                set_status(L"Recording stopped: " + error);
            }
            return 0;
        }
        if (wParam == PreviewTimerId) {
            const auto& frames = active_frames();
            if (!previewMode_ || !previewPlaying_ || frames.empty()) {
                KillTimer(hwnd_, PreviewTimerId);
                return 0;
            }
            previewFrameIndex_ = (previewFrameIndex_ + 1) % frames.size();
            InvalidateRect(hwnd_, nullptr, FALSE);
            update_preview_timer();
            return 0;
        }
        return 0;
    case WM_PAINT:
        paint();
        return 0;
    case WM_PRINTCLIENT: {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const auto dc = reinterpret_cast<HDC>(wParam);
        paint_chrome(dc, client);
        if (previewMode_) paint_preview_frame(dc);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORBTN:
        SetDCBrushColor(reinterpret_cast<HDC>(wParam), highContrast_ ? GetSysColor(COLOR_BTNFACE) : RGB(28, 29, 30));
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    case WM_DESTROY:
        resizeOverlay_.close();
        cancelExport_ = true;
        KillTimer(hwnd_, PreviewTimerId);
        KillTimer(hwnd_, RecorderTimerId);
        recorder_.stop();
        win32::clear_window_region(hwnd_);
        if (saveThread_.joinable()) {
            saveThread_.join();
        }
        save_preferences();
        if (commandMenu_) { DestroyMenu(commandMenu_); commandMenu_ = nullptr; }
        if (uiFont_) { DeleteObject(uiFont_); uiFont_ = nullptr; }
        if (titleFont_) { DeleteObject(titleFont_); titleFont_ = nullptr; }
        if (iconFont_) { DeleteObject(iconFont_); iconFont_ = nullptr; }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void MainWindow::save_preferences() {
    settings_.defaultFps = selectedFps_;
    settings_.lastExportFormat = selectedExportFormat_;
    settings_.captureCursor = captureCursor_;
    gifler::core::save_settings_best_effort(settingsPath_, settings_);
}

void MainWindow::create_menu() {
    commandMenu_ = CreatePopupMenu();
    fpsMenu_ = CreatePopupMenu();
    exportMenu_ = CreatePopupMenu();
    targetMenu_ = CreatePopupMenu();
    aspectMenu_ = CreatePopupMenu();
    audioRateMenu_ = CreatePopupMenu();
    int aspectIndex = 0;
    for (const auto* label : {L"Free", L"16:9  Landscape", L"9:16  Portrait", L"1:1  Square", L"4:5  Portrait", L"4:3  Landscape", L"3:4  Portrait"})
        AppendMenuW(aspectMenu_, MF_STRING, AspectFree + aspectIndex++, label);
    AppendMenuW(audioRateMenu_, MF_STRING, Audio48k, L"48 kHz");
    AppendMenuW(audioRateMenu_, MF_STRING, Audio441k, L"44.1 kHz");
    AppendMenuW(targetMenu_, MF_STRING, TargetNone, L"No limit");
    for (const int mb : {5, 10, 20, 50, 100}) {
        const auto label = std::format(L"{} MB", mb);
        AppendMenuW(targetMenu_, MF_STRING, TargetNone + mb, label.c_str());
    }

    AppendMenuW(fpsMenu_, MF_STRING, Fps5, L"5 FPS");
    AppendMenuW(fpsMenu_, MF_STRING, Fps10, L"10 FPS");
    AppendMenuW(fpsMenu_, MF_STRING, Fps15, L"15 FPS");
    AppendMenuW(fpsMenu_, MF_STRING, Fps24, L"24 FPS");
    AppendMenuW(fpsMenu_, MF_STRING, Fps30, L"30 FPS");
    AppendMenuW(fpsMenu_, MF_STRING, Fps48, L"48 FPS");
    AppendMenuW(fpsMenu_, MF_STRING, Fps60, L"60 FPS");
    AppendMenuW(fpsMenu_, MF_STRING, Fps120, L"120 FPS");
    AppendMenuW(fpsMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fpsMenu_, MF_STRING, FpsCustom, L"Custom...");

    AppendMenuW(exportMenu_, MF_STRING, ExportGif, L"GIF");
    AppendMenuW(exportMenu_, MF_STRING, ExportMp4, L"MP4 H.264");
    AppendMenuW(exportMenu_, MF_STRING, ExportSocialMp4, L"MP4 Social / X");
    AppendMenuW(exportMenu_, MF_STRING, ExportWebP, L"Animated WebP");
    AppendMenuW(exportMenu_, MF_STRING, ExportWebM, L"WebM VP9");
    AppendMenuW(exportMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(exportMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(targetMenu_), L"Size target");
    AppendMenuW(exportMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(audioRateMenu_), L"MP4 audio rate");

    AppendMenuW(commandMenu_, MF_STRING, PlayButton, L"Play");
    AppendMenuW(commandMenu_, MF_STRING, CursorButton, L"Record Cursor");
    AppendMenuW(commandMenu_, MF_STRING, AudioButton, L"Record system audio");
    AppendMenuW(commandMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(aspectMenu_), L"Capture aspect ratio");
    AppendMenuW(commandMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(commandMenu_, MF_STRING, EditButton, L"Edit");
    AppendMenuW(commandMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(commandMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fpsMenu_), L"Frame rate");
    AppendMenuW(commandMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(exportMenu_), L"Export format");
    AppendMenuW(commandMenu_, MF_STRING, CopyGifButton, L"Copy recording");
    AppendMenuW(commandMenu_, MF_STRING, SaveButton, L"Save recording...");
    update_menu_state();
}

void MainWindow::update_menu_state() {
    if (commandMenu_ == nullptr) {
        return;
    }
    ModifyMenuW(commandMenu_, PlayButton, MF_BYCOMMAND | MF_STRING, PlayButton, previewPlaying_ ? L"Pause" : L"Play");
    CheckMenuItem(commandMenu_, CursorButton, MF_BYCOMMAND | (captureCursor_ ? MF_CHECKED : MF_UNCHECKED));
    const UINT enabled = saving_ ? MF_GRAYED : MF_ENABLED;
    CheckMenuRadioItem(aspectMenu_, AspectFree, AspectFree + 6, AspectFree + settings_.captureAspectRatio, MF_BYCOMMAND);
    for (int i = 0; i < static_cast<int>(gifler::core::CaptureAspectRatios.size()); ++i)
        EnableMenuItem(aspectMenu_, AspectFree + i, MF_BYCOMMAND | ((saving_ || recording_) ? MF_GRAYED : MF_ENABLED));
    CheckMenuRadioItem(audioRateMenu_, Audio48k, Audio441k,
        settings_.mp4AudioSampleRate == 44100 ? Audio441k : Audio48k, MF_BYCOMMAND);
    CheckMenuItem(commandMenu_, AudioButton, MF_BYCOMMAND | (settings_.captureAudio ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(commandMenu_, AudioButton, MF_BYCOMMAND | ((saving_ || recording_) ? MF_GRAYED : MF_ENABLED));
    for (const int mb : {0, 5, 10, 20, 50, 100}) {
        CheckMenuItem(targetMenu_, TargetNone + mb, MF_BYCOMMAND |
            (settings_.target.displayTargetMb == mb ? MF_CHECKED : MF_UNCHECKED));
    }
    const bool hasFrames = !recording_ && !active_frames().empty();
    EnableMenuItem(commandMenu_, CopyGifButton, MF_BYCOMMAND | ((!saving_ && hasFrames) ? MF_ENABLED : MF_GRAYED));
    ModifyMenuW(commandMenu_, SaveButton, MF_BYCOMMAND | MF_STRING, SaveButton, saving_ ? L"Cancel export" : L"Save recording...");
    EnableMenuItem(commandMenu_, SaveButton, MF_BYCOMMAND | ((saving_ || hasFrames) ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(commandMenu_, PlayButton, MF_BYCOMMAND | ((saving_ || recording_) ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(commandMenu_, CursorButton, MF_BYCOMMAND | enabled);
    EnableMenuItem(commandMenu_, FrameButton, MF_BYCOMMAND | enabled);
    EnableMenuItem(commandMenu_, EditButton, MF_BYCOMMAND | ((saving_ || recording_) ? MF_GRAYED : MF_ENABLED));

    if (fpsMenu_ != nullptr) {
        const UINT fpsEnabled = (saving_ || recording_) ? MF_GRAYED : MF_ENABLED;
        bool preset = false;
        for (const int fps : {5, 10, 15, 24, 30, 48, 60, 120}) {
            CheckMenuItem(fpsMenu_, 1100 + fps, MF_BYCOMMAND | (selectedFps_ == fps ? MF_CHECKED : MF_UNCHECKED));
            EnableMenuItem(fpsMenu_, 1100 + fps, MF_BYCOMMAND | fpsEnabled);
            preset = preset || selectedFps_ == fps;
        }
        const auto customLabel = preset ? std::wstring(L"Custom...") : std::format(L"Custom... ({} FPS)", selectedFps_);
        ModifyMenuW(fpsMenu_, FpsCustom, MF_BYCOMMAND | MF_STRING | (preset ? MF_UNCHECKED : MF_CHECKED), FpsCustom, customLabel.c_str());
        EnableMenuItem(fpsMenu_, FpsCustom, MF_BYCOMMAND | fpsEnabled);
    }

    if (exportMenu_ != nullptr) {
        CheckMenuItem(exportMenu_, ExportGif, MF_BYCOMMAND | (selectedExportFormat_ == 0 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(exportMenu_, ExportMp4, MF_BYCOMMAND | (selectedExportFormat_ == 1 && !settings_.socialMp4 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(exportMenu_, ExportSocialMp4, MF_BYCOMMAND | (selectedExportFormat_ == 1 && settings_.socialMp4 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(exportMenu_, ExportWebP, MF_BYCOMMAND | (selectedExportFormat_ == 2 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(exportMenu_, ExportWebM, MF_BYCOMMAND | (selectedExportFormat_ == 3 ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(exportMenu_, ExportGif, MF_BYCOMMAND | enabled);
        EnableMenuItem(exportMenu_, ExportMp4, MF_BYCOMMAND | enabled);
        EnableMenuItem(exportMenu_, ExportSocialMp4, MF_BYCOMMAND | enabled);
        EnableMenuItem(exportMenu_, ExportWebP, MF_BYCOMMAND | enabled);
        EnableMenuItem(exportMenu_, ExportWebM, MF_BYCOMMAND | enabled);
        EnableMenuItem(audioRateMenu_, Audio48k, MF_BYCOMMAND | enabled);
        EnableMenuItem(audioRateMenu_, Audio441k, MF_BYCOMMAND | enabled);

    }
    refresh_chrome();
}

void MainWindow::update_window_title() {
    const std::wstring title = statusText_.empty() ? L"Gifler" : L"Gifler - " + statusText_;
    SetWindowTextW(hwnd_, title.c_str());
    RECT client{};
    GetClientRect(hwnd_, &client);
    RECT header{0, 0, client.right, scaled(34)};
    InvalidateRect(hwnd_, &header, FALSE);
}

gifler::core::PixelRect MainWindow::viewfinder_client_rect() const {
    std::lock_guard lock(geometryMutex_);
    return viewfinderClientRect_;
}

gifler::core::PixelRect MainWindow::capture_rect_snapshot() const {
    std::lock_guard lock(geometryMutex_);
    return captureRectScreen_;
}

gifler::core::PixelRect MainWindow::current_capture_rect() const {
    return win32::client_rect_to_screen_pixels(hwnd_, viewfinder_client_rect());
}

int MainWindow::scaled(int value) const {
    return win32::scale_for_dpi(value, dpi_);
}

gifler::core::PixelSize MainWindow::capture_chrome_size() const {
    return {scaled(3) + scaled(10), scaled(34) + scaled(10)};
}

void MainWindow::fit_window_aspect() {
    if (!hwnd_ || !settings_.captureAspectRatio) return;
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    const auto size = gifler::core::aspect_window_size({rect.right - rect.left, rect.bottom - rect.top},
        gifler::core::CaptureAspectRatios[settings_.captureAspectRatio], capture_chrome_size(),
        {scaled(120), scaled(65)}, gifler::core::ResizeSide::BottomRight, true);
    SetWindowPos(hwnd_, nullptr, 0, 0, size.width, size.height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::select_aspect_ratio(int index) {
    if (recording_ || saving_ || index < 0 || index >= static_cast<int>(gifler::core::CaptureAspectRatios.size())) return;
    if (IsZoomed(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    settings_.captureAspectRatio = index;
    fit_window_aspect();
    save_preferences();
    update_menu_state();
    const auto ratio = gifler::core::CaptureAspectRatios[index];
    set_status(index ? std::format(L"Capture ratio locked to {}:{}", ratio.width, ratio.height) : L"Ready");
}

void MainWindow::layout() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    layout_chrome(width, height);
    const int grip = scaled(3);
    const int inset = (width > grip * 2 && height > grip * 2) ? grip : 0;
    if (saveProgress_ != nullptr) {
        const int progressHeight = scaled(2);
        MoveWindow(saveProgress_, inset, scaled(31),
                   std::max(1, width - inset * 2), progressHeight, TRUE);
    }
    {
        std::lock_guard lock(geometryMutex_);
        viewfinderClientRect_ = {inset, scaled(34), std::max(1, width - inset - scaled(10)),
                                std::max(1, height - scaled(34) - scaled(10))};
        captureRectScreen_ = win32::client_rect_to_screen_pixels(hwnd_, viewfinderClientRect_);
    }

    update_status_rect_text();
    apply_capture_or_preview_region();
    update_resize_overlay();
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
}

void MainWindow::update_resize_overlay() {
    resizeOverlay_.update(current_capture_rect(), scaled(12), scaled(26),
        IsWindowVisible(hwnd_) && !IsIconic(hwnd_) && !IsZoomed(hwnd_));
}

void MainWindow::paint() {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);

    RECT client{};
    GetClientRect(hwnd_, &client);

    paint_chrome(dc, client);

    const auto viewfinder = viewfinder_client_rect();
    RECT vf{viewfinder.left(), viewfinder.top(), viewfinder.right(), viewfinder.bottom()};
    if (previewMode_) {
        FillRect(dc, &vf, GetSysColorBrush(COLOR_WINDOW));
        paint_preview_frame(dc);
    }

    EndPaint(hwnd_, &ps);
}

void MainWindow::apply_capture_or_preview_region() {
    if (hwnd_ == nullptr || applyingRegion_) {
        return;
    }

    const auto hole = previewMode_ ? std::optional<gifler::core::PixelRect>{} : std::make_optional(viewfinder_client_rect());
    if (hole == appliedHole_) return;
    applyingRegion_ = true;
    std::wstring error;
    const bool applied = previewMode_ ? win32::clear_window_region(hwnd_, &error)
                                      : win32::apply_viewfinder_hole_region(hwnd_, viewfinder_client_rect(), &error);
    applyingRegion_ = false;
    if (applied) appliedHole_ = hole;
    if (!applied && !error.empty()) {
        statusText_ = L"Viewfinder error: " + error;
        update_window_title();
    }
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

void MainWindow::update_status_rect_text() {
    const auto screen = current_capture_rect();
    {
        std::lock_guard lock(geometryMutex_);
        captureRectScreen_ = screen;
    }
    statusText_ = std::format(L"{}x{} @ {},{} | GIF: --", screen.width, screen.height, screen.x, screen.y);
    update_window_title();
}

void MainWindow::set_status(std::wstring text) {
    statusText_ = std::move(text);
    update_window_title();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::start_recording() {
    ++recordingRevision_;
    KillTimer(hwnd_, PreviewTimerId);
    previewMode_ = false;
    previewPlaying_ = false;
    previewFrameIndex_ = 0;
    editedFrames_.clear();
    preparedMediaPath_.clear();
    preparedMediaFrameCount_ = 0;
    preparedMediaFormat_ = -1;
    recording_ = true;
    update_menu_state();

    gifler::record::RecorderSettings settings{};
    settings.fps = selected_fps();
    settings.queueCapacity = 8;
    settings.captureCursor = captureCursor_;
    settings.captureAudio = settings_.captureAudio;

    const auto captureRect = current_capture_rect();
    try {
        recorder_.start_dxgi(settings, [this] { return capture_rect_snapshot(); });
    } catch (...) {
        recording_ = false;
        update_menu_state();
        set_status(L"Could not allocate recording resources.");
        return;
    }
    SetTimer(hwnd_, RecorderTimerId, 250, nullptr);
    apply_capture_or_preview_region();
    set_status(std::format(L"Recording {}x{} @ {},{} target {} FPS | GIF: --", captureRect.width, captureRect.height,
                           captureRect.x, captureRect.y, settings.fps));
}

void MainWindow::stop_recording() {
    KillTimer(hwnd_, RecorderTimerId);
    recorder_.stop();
    recording_ = false;
    update_menu_state();

    const auto& frames = recorder_.frame_store().frames();
    previewFrameIndex_ = 0;
    previewMode_ = false;
    previewPlaying_ = false;
    update_menu_state();
    apply_capture_or_preview_region();

    if (frames.empty()) {
        const auto error = recorder_.last_error();
        if (!error.empty()) {
            set_status(L"Stopped | no frames captured: " + error + L" | GIF: --");
        } else {
            set_status(L"Stopped | no frames captured | GIF: --");
        }
        return;
    }
    const double seconds = static_cast<double>(recorder_.frame_store().total_duration_ticks()) / 10'000'000.0;
    set_status(std::format(L"Stopped | preview ready: {} frames, {:.2f}s, {:.2f} MB raw | GIF: --", frames.size(), seconds,
                           static_cast<double>(recorder_.frame_store().total_pixel_bytes()) / (1024.0 * 1024.0)));
    if (!recorder_.audio_error().empty()) set_status(statusText_ + L" | " + recorder_.audio_error());
    else if (seconds > 0) set_status(std::format(L"Stopped | {:.2f}s | {:.1f} FPS captured | {} frames | {}",
        seconds, recorder_.captured_frames() / seconds, frames.size(),
        recorder_.audio().sampleRate ? L"System audio" : L"Silent"));
}

void MainWindow::toggle_preview_playback() {
    const auto& frames = active_frames();
    if (frames.empty()) {
        set_status(L"No recording to preview | GIF: --");
        return;
    }

    if (previewPlaying_) {
        KillTimer(hwnd_, PreviewTimerId);
        previewMode_ = false;
        previewPlaying_ = false;
        update_menu_state();
        apply_capture_or_preview_region();
        set_status(std::format(L"Playback stopped | {} frames | GIF: --", frames.size()));
        return;
    }

    previewMode_ = true;
    previewPlaying_ = true;
    previewFrameIndex_ = std::min(previewFrameIndex_, frames.size() - 1);
    update_menu_state();
    apply_capture_or_preview_region();
    update_preview_timer();
    set_status(std::format(L"Playback running | {} frames | GIF: --", frames.size()));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::save_recording() {
    if (recording_) {
        set_status(L"Stop recording before saving");
        return;
    }
    const auto& frames = active_frames();
    if (frames.empty()) {
        set_status(L"No recording to save | GIF: --");
        return;
    }

    auto outputPath = prompt_save_path();
    if (outputPath.empty()) {
        set_status(L"Save canceled | GIF: --");
        return;
    }

    start_export(std::move(outputPath), false);
}

void MainWindow::start_export(std::filesystem::path outputPath, bool copyOperation) {
    const auto& frames = active_frames();
    if (frames.empty() || saving_) {
        return;
    }

    if (saveThread_.joinable()) {
        saveThread_.join();
    }

    try {
    const int exportFormat = selectedExportFormat_;
    const bool gif = exportFormat == 0;
    const auto* framesToSave = &frames;
    const std::size_t frameCount = frames.size();
    cancelExport_ = false;
    saveProgressLabel_ = (copyOperation ? L"Copying " : L"Saving ") + media_format_name(exportFormat);
    saving_ = true;
    layout();
    SendMessageW(saveProgress_, PBM_SETPOS, 0, 0);
    ShowWindow(saveProgress_, SW_SHOWNA);
    BringWindowToTop(saveProgress_);
    RedrawWindow(saveProgress_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    update_menu_state();
    set_status(saveProgressLabel_ + L": 0%");

    const HWND progressWindow = hwnd_;
    if (gif) {
        auto request = make_gif_request(outputPath);
        request.canceled = [this] { return cancelExport_.load(); };
        gifler::exporting::TargetSizeOptions target{};
        target.enabled = settings_.target.displayTargetMb > 0;
        target.displayTargetMb = settings_.target.displayTargetMb;
        target.internalSafetyTargetMb = settings_.target.internalSafetyTargetMb;
        request.progress = [progressWindow](int value) { PostMessageW(progressWindow, SaveProgressMessage, value, 0); };
        saveThread_ = std::thread([this, framesToSave, frameCount, exportFormat, copyOperation,
                                   target, request = std::move(request)]() mutable {
            gifler::exporting::GifExportSummary summary{};
            try {
                summary = gifler::exporting::export_gif_from_frames(*framesToSave, std::move(request), target);
            } catch (const std::exception& error) {
                const std::string text = error.what();
                summary.message = L"Export failed: " + std::wstring(text.begin(), text.end());
            } catch (...) {
                summary.message = L"Export failed unexpectedly. The recording is still available.";
            }
            {
                std::lock_guard lock(saveResultMutex_);
                saveResultSuccess_ = summary.success;
                saveResultCopyOperation_ = copyOperation;
                saveResultFormat_ = exportFormat;
                saveResultMessage_ = summary.message;
                saveResultPath_ = summary.outputPath;
                saveResultFrameCount_ = frameCount;
            }
            PostMessageW(hwnd_, SaveCompleteMessage, 0, 0);
        });
    } else {
        auto request = make_video_request(outputPath);
        request.canceled = [this] { return cancelExport_.load(); };
        request.progress = [progressWindow](int value) { PostMessageW(progressWindow, SaveProgressMessage, value, 0); };
        saveThread_ = std::thread([this, framesToSave, frameCount, exportFormat, copyOperation,
                                   request = std::move(request)]() mutable {
            gifler::exporting::VideoExportSummary summary{};
            try {
                summary = gifler::exporting::export_video_from_frames(*framesToSave, std::move(request));
            } catch (const std::exception& error) {
                const std::string text = error.what();
                summary.message = L"Export failed: " + std::wstring(text.begin(), text.end());
            } catch (...) {
                summary.message = L"Export failed unexpectedly. The recording is still available.";
            }
            {
                std::lock_guard lock(saveResultMutex_);
                saveResultSuccess_ = summary.success;
                saveResultCopyOperation_ = copyOperation;
                saveResultFormat_ = exportFormat;
                saveResultMessage_ = summary.message;
                saveResultPath_ = summary.outputPath;
                saveResultFrameCount_ = frameCount;
            }
            PostMessageW(hwnd_, SaveCompleteMessage, 0, 0);
        });
    }
    } catch (...) {
        saving_ = false;
        ShowWindow(saveProgress_, SW_HIDE);
        layout();
        update_menu_state();
        set_status(L"Could not start export. The recording is still available; retry or choose a smaller recording.");
    }
}

void MainWindow::finish_save() {
    if (saveThread_.joinable()) {
        saveThread_.join();
    }

    bool success = false;
    bool copyOperation = false;
    int exportFormat = 0;
    std::wstring message;
    std::filesystem::path outputPath;
    std::size_t frameCount = 0;
    {
        std::lock_guard lock(saveResultMutex_);
        success = saveResultSuccess_;
        copyOperation = saveResultCopyOperation_;
        exportFormat = saveResultFormat_;
        message = saveResultMessage_;
        outputPath = saveResultPath_;
        frameCount = saveResultFrameCount_;
    }

    saving_ = false;
    layout();
    ShowWindow(saveProgress_, SW_HIDE);
    update_menu_state();
    if (success) {
        preparedMediaPath_ = outputPath;
        preparedMediaFrameCount_ = frameCount;
        preparedMediaFormat_ = exportFormat;
    }

    const auto formatName = media_format_name(exportFormat);
    if (!success && cancelExport_) {
        set_status(L"Export canceled | Recording retained");
        return;
    }
    if (success && copyOperation) {
        std::wstring clipboardError;
        if (!gifler::win32::set_clipboard_file_drop(hwnd_, outputPath, &clipboardError)) {
            success = false;
            message = clipboardError;
        } else {
            std::error_code ec;
            const auto size = std::filesystem::file_size(outputPath, ec);
            const double mb = ec ? 0.0 : static_cast<double>(size) / (1024.0 * 1024.0);
            set_status(std::format(L"Copied {} | {:.2f} MB | {}", formatName, mb, outputPath.filename().wstring()));
        }
    } else if (success) {
        set_status(L"Saved | " + message);
    }

    if (!success) {
        const auto failure = (copyOperation ? L"Copy " : L"Save ") + formatName + L" failed | " + message;
        try {
            const auto path = gifler::exporting::default_temp_root().parent_path() / L"last-export-error.txt";
            std::wofstream log(path, std::ios::trunc);
            log << failure;
        } catch (...) {}
        set_status(failure);
        MessageBoxW(hwnd_, failure.c_str(), copyOperation ? L"Gifler Copy Failed" : L"Gifler Save Failed",
                    MB_OK | MB_ICONERROR);
    }
}

void MainWindow::copy_media() {
    if (recording_) {
        set_status(L"Stop recording before copying");
        return;
    }
    const auto& frames = active_frames();
    if (frames.empty()) {
        set_status(L"No recording to copy | GIF: --");
        return;
    }

    auto clipboardDir = gifler::exporting::default_clipboard_cache_dir();
    std::error_code ec;
    std::filesystem::create_directories(clipboardDir, ec);
    if (ec) {
        set_status(L"Copy failed | could not create clipboard cache");
        return;
    }

    if (!preparedMediaPath_.empty() && preparedMediaFrameCount_ == frames.size() &&
        preparedMediaFormat_ == selectedExportFormat_ && std::filesystem::exists(preparedMediaPath_)) {
        std::wstring error;
        if (!gifler::win32::set_clipboard_file_drop(hwnd_, preparedMediaPath_, &error)) {
            set_status(L"Copy failed | " + error);
            return;
        }
        const auto size = std::filesystem::file_size(preparedMediaPath_, ec);
        const double mb = ec ? 0.0 : static_cast<double>(size) / (1024.0 * 1024.0);
        set_status(std::format(L"Copied {} | {:.2f} MB | {}", media_format_name(selectedExportFormat_), mb,
                               preparedMediaPath_.filename().wstring()));
        return;
    }

    const auto extension = media_extension(selectedExportFormat_);
    auto outputPath = clipboardDir /
                      std::format(L"gifler_clipboard_{}_{}.{}", GetCurrentProcessId(), GetTickCount64(), extension);
    start_export(std::move(outputPath), true);
}

void MainWindow::apply_edited_frames(std::vector<gifler::core::BgraFrame> frames) {
    if (saving_ || recording_) {
        set_status(L"Finish the current operation before applying edits");
        return;
    }
    if (frames.empty()) {
        set_status(L"Edit produced no frames | GIF: --");
        return;
    }
    KillTimer(hwnd_, PreviewTimerId);
    editedFrames_ = std::move(frames);
    previewFrameIndex_ = 0;
    previewMode_ = false;
    previewPlaying_ = false;
    preparedMediaPath_.clear();
    preparedMediaFrameCount_ = 0;
    preparedMediaFormat_ = -1;
    update_menu_state();
    apply_capture_or_preview_region();
    set_status(std::format(L"Applied edit | {} frames | GIF: --", editedFrames_.size()));
}

const std::vector<gifler::core::BgraFrame>& MainWindow::active_frames() const {
    if (!editedFrames_.empty()) {
        return editedFrames_;
    }
    return recorder_.frame_store().frames();
}

gifler::exporting::VideoExportFormat MainWindow::selected_video_format() const {
    if (selectedExportFormat_ == 2) {
        return gifler::exporting::VideoExportFormat::AnimatedWebP;
    }
    if (selectedExportFormat_ == 3) {
        return gifler::exporting::VideoExportFormat::WebMVP9;
    }
    return gifler::exporting::VideoExportFormat::Mp4H264;
}

bool MainWindow::selected_export_is_gif() const {
    return selectedExportFormat_ == 0;
}

std::filesystem::path MainWindow::prompt_save_path() const {
    wchar_t fileName[MAX_PATH]{};
    const bool gif = selected_export_is_gif();
    const auto videoFormat = selected_video_format();
    const wchar_t* filter = L"GIF files (*.gif)\0*.gif\0All files (*.*)\0*.*\0";
    const wchar_t* defaultExt = L"gif";
    if (!gif && videoFormat == gifler::exporting::VideoExportFormat::Mp4H264) {
        filter = L"MP4 files (*.mp4)\0*.mp4\0All files (*.*)\0*.*\0";
        defaultExt = L"mp4";
    } else if (!gif && videoFormat == gifler::exporting::VideoExportFormat::AnimatedWebP) {
        filter = L"WebP files (*.webp)\0*.webp\0All files (*.*)\0*.*\0";
        defaultExt = L"webp";
    } else if (!gif) {
        filter = L"WebM files (*.webm)\0*.webm\0All files (*.*)\0*.*\0";
        defaultExt = L"webm";
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defaultExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) {
        return {};
    }
    return std::filesystem::path(fileName);
}

std::filesystem::path MainWindow::app_directory() const {
    wchar_t path[MAX_PATH]{};
    DWORD chars = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (chars == 0 || chars >= MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(path).parent_path();
}

gifler::exporting::GifExportRequest MainWindow::make_gif_request(std::filesystem::path outputPath) const {
    gifler::exporting::EncoderPaths configured{};
    auto encoders = gifler::exporting::discover_encoders(configured, app_directory());

    gifler::exporting::GifExportRequest request{};
    request.outputPath = std::move(outputPath);
    request.requestedFps = recorder_.recorded_fps();
    request.ffmpegPath = encoders.ffmpeg;
    request.gifskiPath = encoders.gifski;
    request.gifsiclePath = encoders.gifsicle;
    return request;
}

gifler::exporting::VideoExportRequest MainWindow::make_video_request(std::filesystem::path outputPath) const {
    gifler::exporting::EncoderPaths configured{};
    auto encoders = gifler::exporting::discover_encoders(configured, app_directory());

    gifler::exporting::VideoExportRequest request{};
    request.format = selected_video_format();
    request.outputPath = std::move(outputPath);
    request.requestedFps = recorder_.recorded_fps();
    request.ffmpegPath = encoders.ffmpeg;
    request.targetMb = settings_.target.displayTargetMb;
    if (recorder_.audio().sampleRate) request.audio = &recorder_.audio();
    request.quality = 75;
    request.socialCompatibility = settings_.socialMp4;
    request.audioSampleRate = settings_.mp4AudioSampleRate;
    return request;
}

void MainWindow::update_preview_timer() {
    KillTimer(hwnd_, PreviewTimerId);
    const auto& frames = active_frames();
    if (!previewMode_ || !previewPlaying_ || frames.empty()) {
        return;
    }
    previewFrameIndex_ = std::min(previewFrameIndex_, frames.size() - 1);
    SetTimer(hwnd_, PreviewTimerId, static_cast<UINT>(duration_ticks_to_timer_ms(frames[previewFrameIndex_].durationTicks)), nullptr);
}

void MainWindow::paint_preview_frame(HDC dc) {
    const auto viewfinder = viewfinder_client_rect();
    const auto& frames = active_frames();
    if (frames.empty()) {
        RECT vf{viewfinder.left(), viewfinder.top(), viewfinder.right(), viewfinder.bottom()};
        InflateRect(&vf, -scaled(12), -scaled(12));
        const std::wstring text = L"No recording";
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &vf, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        return;
    }

    previewFrameIndex_ = std::min(previewFrameIndex_, frames.size() - 1);
    const auto& frame = frames[previewFrameIndex_];
    if (frame.empty()) {
        return;
    }

    RECT vf{viewfinder.left(), viewfinder.top(), viewfinder.right(), viewfinder.bottom()};
    const int destW = vf.right - vf.left;
    const int destH = vf.bottom - vf.top;
    const double scale = std::max(static_cast<double>(destW) / static_cast<double>(frame.width),
                                  static_cast<double>(destH) / static_cast<double>(frame.height));
    const int drawW = std::max(1, static_cast<int>(static_cast<double>(frame.width) * scale));
    const int drawH = std::max(1, static_cast<int>(static_cast<double>(frame.height) * scale));
    const int drawX = vf.left + (destW - drawW) / 2;
    const int drawY = vf.top + (destH - drawH) / 2;

    SaveDC(dc);
    IntersectClipRect(dc, vf.left, vf.top, vf.right, vf.bottom);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = frame.width;
    info.bmiHeader.biHeight = -frame.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, drawX, drawY, drawW, drawH, 0, 0, frame.width, frame.height, frame.pixels.data(), &info, DIB_RGB_COLORS,
                  SRCCOPY);
    RestoreDC(dc, -1);
}

int MainWindow::selected_fps() const {
    return selectedFps_ > 0 ? selectedFps_ : 10;
}

LRESULT MainWindow::hit_test(LPARAM lParam) const {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd_, &point);

    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int grip = IsZoomed(hwnd_) ? 0 : scaled(3);

    const bool left = point.x >= 0 && point.x < grip;
    const int easyGrip = IsZoomed(hwnd_) ? 0 : scaled(10);
    const bool right = point.x >= width - easyGrip && point.x < width;
    const bool top = point.y >= 0 && point.y < grip;
    const bool bottom = point.y >= height - easyGrip && point.y < height;

    if ((right || bottom) && point.x >= width - scaled(22) && point.y >= height - scaled(22)) return HTBOTTOMRIGHT;

    if (top && left) {
        return HTTOPLEFT;
    }
    if (top && right) {
        return HTTOPRIGHT;
    }
    if (bottom && left) {
        return HTBOTTOMLEFT;
    }
    if (bottom && right) {
        return HTBOTTOMRIGHT;
    }
    if (left) {
        return HTLEFT;
    }
    if (right) {
        return HTRIGHT;
    }
    if (top) {
        return HTTOP;
    }
    if (bottom) {
        return HTBOTTOM;
    }

    if (point.y < scaled(31)) return HTCAPTION;
    return HTCLIENT;
}

} // namespace gifler::app
