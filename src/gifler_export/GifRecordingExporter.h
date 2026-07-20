#pragma once

#include "gifler_core/Frame.h"
#include "gifler_export/EncoderDiscovery.h"
#include "gifler_export/GifExportPlanner.h"

#include <filesystem>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gifler::exporting {

using ExportProgressCallback = std::function<void(int)>;

struct GifExportSummary {
    bool success = false;
    std::filesystem::path outputPath{};
    std::uintmax_t fileSizeBytes = 0;
    GifCandidate candidate{};
    std::wstring message{};
};

enum class VideoExportFormat {
    Mp4H264,
    AnimatedWebP,
    WebMVP9
};

struct VideoExportRequest {
    VideoExportFormat format = VideoExportFormat::Mp4H264;
    std::filesystem::path outputPath{};
    std::optional<std::filesystem::path> ffmpegPath{};
    int requestedFps = 10;
    int width = 0;
    double targetMb = 10.0;
    int quality = 75;
    ExportProgressCallback progress{};
};

struct VideoExportSummary {
    bool success = false;
    std::filesystem::path outputPath{};
    std::uintmax_t fileSizeBytes = 0;
    VideoExportFormat format = VideoExportFormat::Mp4H264;
    std::wstring message{};
};

[[nodiscard]] GifExportSummary export_gif_from_frames(const std::vector<gifler::core::BgraFrame>& frames,
                                                      GifExportRequest request,
                                                      TargetSizeOptions target);

[[nodiscard]] VideoExportSummary export_video_from_frames(const std::vector<gifler::core::BgraFrame>& frames,
                                                          VideoExportRequest request);

[[nodiscard]] std::vector<gifler::core::BgraFrame> expand_frames_for_constant_fps(
    const std::vector<gifler::core::BgraFrame>& frames,
    int fps);
[[nodiscard]] int calculate_h264_bitrate_kbps(std::int64_t durationTicks, double targetMb);
[[nodiscard]] std::wstring video_format_name(VideoExportFormat format);
[[nodiscard]] std::wstring build_ffmpeg_h264_pass1_command(const std::filesystem::path& ffmpegPath,
                                                           const std::filesystem::path& framesPattern,
                                                           const std::filesystem::path& passLogPath,
                                                           int fps,
                                                           int width,
                                                           int bitrateKbps);
[[nodiscard]] std::wstring build_ffmpeg_h264_pass2_command(const std::filesystem::path& ffmpegPath,
                                                           const std::filesystem::path& framesPattern,
                                                           const std::filesystem::path& passLogPath,
                                                           const std::filesystem::path& outputPath,
                                                           int fps,
                                                           int width,
                                                           int bitrateKbps);
[[nodiscard]] std::wstring build_ffmpeg_webp_command(const std::filesystem::path& ffmpegPath,
                                                     const std::filesystem::path& framesPattern,
                                                     const std::filesystem::path& outputPath,
                                                     int fps,
                                                     int width,
                                                     int quality);
[[nodiscard]] std::wstring build_ffmpeg_webm_command(const std::filesystem::path& ffmpegPath,
                                                     const std::filesystem::path& framesPattern,
                                                     const std::filesystem::path& outputPath,
                                                     int fps,
                                                     int width,
                                                     int quality);

[[nodiscard]] std::filesystem::path default_temp_root();
[[nodiscard]] std::filesystem::path default_clipboard_cache_dir();
void cleanup_old_clipboard_cache_best_effort(const std::filesystem::path& directory, int maxAgeHours = 24);

} // namespace gifler::exporting
