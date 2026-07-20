#include "gifler_app/MainWindow.h"

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

namespace gifler::app {
namespace {

constexpr wchar_t WindowClassName[] = L"GiflerNativeMainWindow";
constexpr UINT_PTR PreviewTimerId = 3001;
constexpr UINT SaveProgressMessage = WM_APP + 1;
constexpr UINT SaveCompleteMessage = WM_APP + 2;
constexpr COLORREF TransparentViewfinderColor = RGB(1, 0, 1);

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
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = WindowClassName;

    RegisterClassExW(&wc);

    const int initialWidth = scaled(660);
    const int initialHeight = scaled(390);
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED, WindowClassName, L"Gifler",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, initialWidth, initialHeight,
                            nullptr, nullptr, instance_, this);
    if (hwnd_ == nullptr) {
        return false;
    }
    win32::enable_colorkey_transparency(hwnd_, TransparentViewfinderColor);

    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    apply_capture_or_preview_region();
    return true;
}

int MainWindow::run_message_loop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
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
        saveProgress_ = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | PBS_SMOOTH, 0, 0, 1, 1, hwnd_, nullptr,
                                        instance_, nullptr);
        SendMessageW(saveProgress_, PBM_SETRANGE32, 0, 100);
        SendMessageW(saveProgress_, PBM_SETSTATE, PBST_NORMAL, 0);
        layout();
        return 0;
    case WM_SIZE:
        layout();
        return 0;
    case WM_MOVE:
        update_status_rect_text();
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        const auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = scaled(120);
        info->ptMinTrackSize.y = scaled(90);
        return 0;
    }
    case WM_NCHITTEST:
        return hit_test(lParam);
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (saving_) {
            set_status(saveProgressLabel_ + L" in progress");
            return 0;
        }
        if (id == RecButton) {
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
        } else if (id == PlayButton) {
            toggle_preview_playback();
        } else if (id == CopyGifButton) {
            copy_media();
        } else if (id == SaveButton) {
            save_recording();
        } else if (id == EditButton) {
            const auto& frames = active_frames();
            gifler::editor::EditorWindow::show(hwnd_, frames, [this](std::vector<gifler::core::BgraFrame> editedFrames) {
                apply_edited_frames(std::move(editedFrames));
            });
            set_status(frames.empty() ? L"No recording to edit | GIF: --" : L"Editor opened");
        } else if (id == FrameButton) {
            set_status(L"Frame menu stub");
        } else if (id == Fps5 || id == Fps10 || id == Fps15 || id == Fps30) {
            selectedFps_ = id == Fps5 ? 5 : id == Fps10 ? 10 : id == Fps15 ? 15 : 30;
            save_preferences();
            update_menu_state();
            set_status(std::format(L"FPS set to {} | GIF: --", selectedFps_));
        } else if (id == ExportGif || id == ExportMp4 || id == ExportWebP || id == ExportWebM) {
            selectedExportFormat_ = id == ExportGif ? 0 : id == ExportMp4 ? 1 : id == ExportWebP ? 2 : 3;
            save_preferences();
            update_menu_state();
            set_status(selectedExportFormat_ == 0 ? L"Export format: GIF | GIF: --"
                                                  : selectedExportFormat_ == 1 ? L"Export format: MP4 | GIF: --"
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
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        KillTimer(hwnd_, PreviewTimerId);
        recorder_.stop();
        if (saveThread_.joinable()) {
            saveThread_.join();
        }
        save_preferences();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
}

void MainWindow::save_preferences() {
    settings_.defaultFps = selectedFps_;
    settings_.lastExportFormat = selectedExportFormat_;
    settings_.captureCursor = captureCursor_;
    gifler::core::save_settings_best_effort(settingsPath_, settings_);
}

void MainWindow::create_menu() {
    menuBar_ = CreateMenu();
    commandMenu_ = CreatePopupMenu();
    fpsMenu_ = CreatePopupMenu();
    exportMenu_ = CreatePopupMenu();

    AppendMenuW(fpsMenu_, MF_STRING, Fps5, L"5 FPS");
    AppendMenuW(fpsMenu_, MF_STRING, Fps10, L"10 FPS");
    AppendMenuW(fpsMenu_, MF_STRING, Fps15, L"15 FPS");
    AppendMenuW(fpsMenu_, MF_STRING, Fps30, L"30 FPS");

    AppendMenuW(exportMenu_, MF_STRING, ExportGif, L"GIF");
    AppendMenuW(exportMenu_, MF_STRING, ExportMp4, L"MP4 H.264");
    AppendMenuW(exportMenu_, MF_STRING, ExportWebP, L"Animated WebP");
    AppendMenuW(exportMenu_, MF_STRING, ExportWebM, L"WebM VP9");

    AppendMenuW(commandMenu_, MF_STRING, PlayButton, L"Play");
    AppendMenuW(commandMenu_, MF_STRING, CursorButton, L"Record Cursor");
    AppendMenuW(commandMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(commandMenu_, MF_STRING, FrameButton, L"Frame");
    AppendMenuW(commandMenu_, MF_STRING, EditButton, L"Edit");

    AppendMenuW(menuBar_, MF_STRING, RecButton, L"Rec");
    AppendMenuW(menuBar_, MF_POPUP, reinterpret_cast<UINT_PTR>(fpsMenu_), L"FPS");
    AppendMenuW(menuBar_, MF_STRING, CopyGifButton, L"Copy");
    exportMenuPosition_ = GetMenuItemCount(menuBar_);
    AppendMenuW(menuBar_, MF_POPUP, reinterpret_cast<UINT_PTR>(exportMenu_), L"GIF");
    AppendMenuW(menuBar_, MF_STRING, SaveButton, L"Save");
    AppendMenuW(menuBar_, MF_POPUP, reinterpret_cast<UINT_PTR>(commandMenu_), L"More");
    SetMenu(hwnd_, menuBar_);
    update_menu_state();
}

void MainWindow::update_menu_state() {
    if (menuBar_ == nullptr || commandMenu_ == nullptr) {
        return;
    }
    ModifyMenuW(menuBar_, RecButton, MF_BYCOMMAND | MF_STRING, RecButton, recording_ ? L"Stop" : L"Rec");
    ModifyMenuW(commandMenu_, PlayButton, MF_BYCOMMAND | MF_STRING, PlayButton, previewPlaying_ ? L"Pause" : L"Play");
    CheckMenuItem(commandMenu_, CursorButton, MF_BYCOMMAND | (captureCursor_ ? MF_CHECKED : MF_UNCHECKED));
    const UINT enabled = saving_ ? MF_GRAYED : MF_ENABLED;
    EnableMenuItem(menuBar_, RecButton, MF_BYCOMMAND | enabled);
    EnableMenuItem(menuBar_, CopyGifButton, MF_BYCOMMAND | enabled);
    EnableMenuItem(menuBar_, SaveButton, MF_BYCOMMAND | ((saving_ || recording_) ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(commandMenu_, PlayButton, MF_BYCOMMAND | enabled);
    EnableMenuItem(commandMenu_, CursorButton, MF_BYCOMMAND | enabled);
    EnableMenuItem(commandMenu_, FrameButton, MF_BYCOMMAND | enabled);
    EnableMenuItem(commandMenu_, EditButton, MF_BYCOMMAND | enabled);

    if (fpsMenu_ != nullptr) {
        CheckMenuItem(fpsMenu_, Fps5, MF_BYCOMMAND | (selectedFps_ == 5 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(fpsMenu_, Fps10, MF_BYCOMMAND | (selectedFps_ == 10 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(fpsMenu_, Fps15, MF_BYCOMMAND | (selectedFps_ == 15 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(fpsMenu_, Fps30, MF_BYCOMMAND | (selectedFps_ == 30 ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(fpsMenu_, Fps5, MF_BYCOMMAND | enabled);
        EnableMenuItem(fpsMenu_, Fps10, MF_BYCOMMAND | enabled);
        EnableMenuItem(fpsMenu_, Fps15, MF_BYCOMMAND | enabled);
        EnableMenuItem(fpsMenu_, Fps30, MF_BYCOMMAND | enabled);
    }

    if (exportMenu_ != nullptr) {
        CheckMenuItem(exportMenu_, ExportGif, MF_BYCOMMAND | (selectedExportFormat_ == 0 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(exportMenu_, ExportMp4, MF_BYCOMMAND | (selectedExportFormat_ == 1 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(exportMenu_, ExportWebP, MF_BYCOMMAND | (selectedExportFormat_ == 2 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(exportMenu_, ExportWebM, MF_BYCOMMAND | (selectedExportFormat_ == 3 ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(exportMenu_, ExportGif, MF_BYCOMMAND | enabled);
        EnableMenuItem(exportMenu_, ExportMp4, MF_BYCOMMAND | enabled);
        EnableMenuItem(exportMenu_, ExportWebP, MF_BYCOMMAND | enabled);
        EnableMenuItem(exportMenu_, ExportWebM, MF_BYCOMMAND | enabled);

        const wchar_t* formatLabel = selectedExportFormat_ == 0 ? L"GIF"
                                     : selectedExportFormat_ == 1 ? L"MP4"
                                     : selectedExportFormat_ == 2 ? L"WebP"
                                                                  : L"WebM";
        ModifyMenuW(menuBar_, exportMenuPosition_, MF_BYPOSITION | MF_POPUP | MF_STRING,
                    reinterpret_cast<UINT_PTR>(exportMenu_), formatLabel);
    }
    DrawMenuBar(hwnd_);
}

void MainWindow::update_window_title() {
    const std::wstring title = statusText_.empty() ? L"Gifler" : L"Gifler - " + statusText_;
    SetWindowTextW(hwnd_, title.c_str());
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

void MainWindow::layout() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    const int grip = scaled(10);
    const int inset = (width > grip * 2 && height > grip * 2) ? grip : 0;
    if (saveProgress_ != nullptr) {
        const int progressHeight = scaled(16);
        MoveWindow(saveProgress_, inset, std::max(inset, height - inset - progressHeight),
                   std::max(1, width - inset * 2), progressHeight, TRUE);
    }
    {
        std::lock_guard lock(geometryMutex_);
        viewfinderClientRect_ = {inset, inset, std::max(1, width - inset * 2), std::max(1, height - inset * 2)};
        captureRectScreen_ = win32::client_rect_to_screen_pixels(hwnd_, viewfinderClientRect_);
    }

    update_status_rect_text();
    apply_capture_or_preview_region();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::paint() {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);

    RECT client{};
    GetClientRect(hwnd_, &client);

    HBRUSH faceBrush = GetSysColorBrush(COLOR_BTNFACE);
    FillRect(dc, &client, faceBrush);

    const auto viewfinder = viewfinder_client_rect();
    RECT vf{viewfinder.left(), viewfinder.top(), viewfinder.right(), viewfinder.bottom()};
    if (previewMode_) {
        FillRect(dc, &vf, GetSysColorBrush(COLOR_WINDOW));
        paint_preview_frame(dc);
    } else {
        HBRUSH transparentBrush = CreateSolidBrush(TransparentViewfinderColor);
        if (transparentBrush != nullptr) {
            FillRect(dc, &vf, transparentBrush);
            DeleteObject(transparentBrush);
        }
    }

    EndPaint(hwnd_, &ps);
}

void MainWindow::apply_capture_or_preview_region() {
    if (hwnd_ == nullptr) {
        return;
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
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

    const auto captureRect = current_capture_rect();
    recorder_.start_dxgi(settings, [this] { return capture_rect_snapshot(); });
    apply_capture_or_preview_region();
    set_status(std::format(L"Recording {}x{} @ {},{} target {} FPS | GIF: --", captureRect.width, captureRect.height,
                           captureRect.x, captureRect.y, settings.fps));
}

void MainWindow::stop_recording() {
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

    const int exportFormat = selectedExportFormat_;
    const bool gif = exportFormat == 0;
    const auto* framesToSave = &frames;
    const std::size_t frameCount = frames.size();
    saveProgressLabel_ = (copyOperation ? L"Copying " : L"Saving ") + media_format_name(exportFormat);
    saving_ = true;
    SendMessageW(saveProgress_, PBM_SETPOS, 0, 0);
    ShowWindow(saveProgress_, SW_SHOWNA);
    BringWindowToTop(saveProgress_);
    RedrawWindow(saveProgress_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    update_menu_state();
    set_status(saveProgressLabel_ + L": 0%");

    const HWND progressWindow = hwnd_;
    if (gif) {
        auto request = make_gif_request(outputPath);
        request.progress = [progressWindow](int value) { PostMessageW(progressWindow, SaveProgressMessage, value, 0); };
        saveThread_ = std::thread([this, framesToSave, frameCount, exportFormat, copyOperation,
                                   request = std::move(request)]() mutable {
            auto summary = gifler::exporting::export_gif_from_frames(*framesToSave, std::move(request),
                                                                     gifler::exporting::TargetSizeOptions{});
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
        request.progress = [progressWindow](int value) { PostMessageW(progressWindow, SaveProgressMessage, value, 0); };
        saveThread_ = std::thread([this, framesToSave, frameCount, exportFormat, copyOperation,
                                   request = std::move(request)]() mutable {
            auto summary = gifler::exporting::export_video_from_frames(*framesToSave, std::move(request));
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
    ShowWindow(saveProgress_, SW_HIDE);
    update_menu_state();
    if (success) {
        preparedMediaPath_ = outputPath;
        preparedMediaFrameCount_ = frameCount;
        preparedMediaFormat_ = exportFormat;
    }

    const auto formatName = media_format_name(exportFormat);
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
    request.requestedFps = selected_fps();
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
    request.requestedFps = selected_fps();
    request.ffmpegPath = encoders.ffmpeg;
    request.targetMb = 10.0;
    request.quality = 75;
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
    const LRESULT defaultHit = DefWindowProcW(hwnd_, WM_NCHITTEST, 0, lParam);
    if (defaultHit != HTCLIENT) {
        return defaultHit;
    }

    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd_, &point);

    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int grip = scaled(12);

    const bool left = point.x >= 0 && point.x < grip;
    const bool right = point.x >= width - grip && point.x < width;
    const bool top = point.y >= 0 && point.y < grip;
    const bool bottom = point.y >= height - grip && point.y < height;

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

    return HTCLIENT;
}

} // namespace gifler::app
