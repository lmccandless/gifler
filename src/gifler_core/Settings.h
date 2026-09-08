#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace gifler::core {

struct EncoderSettings {
    std::optional<std::filesystem::path> ffmpegPath{};
    std::optional<std::filesystem::path> gifskiPath{};
    std::optional<std::filesystem::path> gifsiclePath{};
};

struct ExportTargetSettings {
    double displayTargetMb = 10.0;
    double internalSafetyTargetMb = 9.5;
    int minimumFps = 5;
    int minimumWidth = 320;
    bool allowAggressiveDownscale = false;
    bool allowVeryLowColorGif = false;
};

struct AppSettings {
    int defaultFps = 10;
    int lastExportFormat = 0;
    bool captureCursor = true;
    bool captureAudio = false;
    int captureAspectRatio = 0;
    bool socialMp4 = false;
    int mp4AudioSampleRate = 48000;
    EncoderSettings encoders{};
    ExportTargetSettings target{};
};

[[nodiscard]] std::filesystem::path default_settings_path();
[[nodiscard]] AppSettings load_settings_or_defaults(const std::filesystem::path& path);
void save_settings_best_effort(const std::filesystem::path& path, const AppSettings& settings);

} // namespace gifler::core
