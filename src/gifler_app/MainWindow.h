#pragma once

#include "gifler_core/Geometry.h"
#include "gifler_core/Settings.h"
#include "gifler_export/GifRecordingExporter.h"
#include "gifler_record/RecorderSession.h"

#include <Windows.h>

#include <cstddef>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gifler::app {

class MainWindow {
public:
    MainWindow() = default;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool create(HINSTANCE instance, int showCommand);
    int run_message_loop();

private:
    enum ControlId : int {
        RecButton = 1001,
        CursorButton = 1002,
        FrameButton = 1004,
        EditButton = 1005,
        SaveButton = 1006,
        CopyGifButton = 1007,
        PlayButton = 1008,
        Fps5 = 1105,
        Fps10 = 1110,
        Fps15 = 1115,
        Fps30 = 1130,
        ExportGif = 1201,
        ExportMp4 = 1202,
        ExportWebP = 1203,
        ExportWebM = 1204,
    };

    static LRESULT CALLBACK static_window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT window_proc(UINT message, WPARAM wParam, LPARAM lParam);

    void create_menu();
    void save_preferences();
    void update_menu_state();
    void update_window_title();
    [[nodiscard]] gifler::core::PixelRect viewfinder_client_rect() const;
    [[nodiscard]] gifler::core::PixelRect capture_rect_snapshot() const;
    [[nodiscard]] gifler::core::PixelRect current_capture_rect() const;
    void layout();
    void paint();
    void apply_capture_or_preview_region();
    void update_status_rect_text();
    void set_status(std::wstring text);
    void start_recording();
    void stop_recording();
    void toggle_preview_playback();
    void update_preview_timer();
    void paint_preview_frame(HDC dc);
    void save_recording();
    void start_export(std::filesystem::path outputPath, bool copyOperation);
    void finish_save();
    void copy_media();
    void apply_edited_frames(std::vector<gifler::core::BgraFrame> frames);
    [[nodiscard]] const std::vector<gifler::core::BgraFrame>& active_frames() const;
    [[nodiscard]] gifler::exporting::VideoExportFormat selected_video_format() const;
    [[nodiscard]] bool selected_export_is_gif() const;
    std::filesystem::path prompt_save_path() const;
    std::filesystem::path app_directory() const;
    gifler::exporting::GifExportRequest make_gif_request(std::filesystem::path outputPath) const;
    gifler::exporting::VideoExportRequest make_video_request(std::filesystem::path outputPath) const;
    int selected_fps() const;
    LRESULT hit_test(LPARAM lParam) const;

    int scaled(int value) const;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    unsigned dpi_ = 96;
    bool previewMode_ = false;
    bool recording_ = false;
    bool previewPlaying_ = false;
    bool captureCursor_ = true;
    int selectedFps_ = 10;
    int selectedExportFormat_ = 0;
    std::size_t previewFrameIndex_ = 0;
    gifler::record::RecorderSession recorder_{};
    std::vector<gifler::core::BgraFrame> editedFrames_{};
    std::filesystem::path preparedMediaPath_{};
    std::size_t preparedMediaFrameCount_ = 0;
    int preparedMediaFormat_ = -1;
    mutable std::mutex geometryMutex_{};
    gifler::core::PixelRect viewfinderClientRect_{};
    gifler::core::PixelRect captureRectScreen_{};
    std::wstring statusText_ = L"Ready | GIF: --";
    gifler::core::AppSettings settings_{};
    std::filesystem::path settingsPath_{};
    std::atomic_bool saving_ = false;
    std::thread saveThread_{};
    std::mutex saveResultMutex_{};
    bool saveResultSuccess_ = false;
    bool saveResultCopyOperation_ = false;
    int saveResultFormat_ = 0;
    std::wstring saveResultMessage_{};
    std::filesystem::path saveResultPath_{};
    std::size_t saveResultFrameCount_ = 0;
    std::wstring saveProgressLabel_{};

    HMENU menuBar_ = nullptr;
    HMENU commandMenu_ = nullptr;
    HMENU fpsMenu_ = nullptr;
    HMENU exportMenu_ = nullptr;
    UINT exportMenuPosition_ = 0;
    HWND saveProgress_ = nullptr;
};

} // namespace gifler::app
