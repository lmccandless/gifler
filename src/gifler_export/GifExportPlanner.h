#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gifler::exporting {

enum class GifExportMode {
    AutoGif,
    BestGifQuality,
    SmallestGif,
    CompatibilityGif
};

enum class GifEngine {
    Gifski,
    FfmpegPalette
};

enum class GifDitherMode {
    Sierra2_4A,
    Bayer,
    None
};

struct GifExportRequest {
    GifExportMode mode = GifExportMode::AutoGif;
    int sourceWidth = 640;
    int sourceHeight = 360;
    int requestedFps = 10;
    int quality = 90;
    bool loop = true;
    std::filesystem::path outputPath{};
    std::optional<std::filesystem::path> ffmpegPath{};
    std::optional<std::filesystem::path> gifskiPath{};
    std::optional<std::filesystem::path> gifsiclePath{};
    int maxColors = 256;
    GifDitherMode dither = GifDitherMode::Sierra2_4A;
    std::function<void(int)> progress{};
    std::function<bool()> canceled{};
};

struct TargetSizeOptions {
    bool enabled = true;
    double displayTargetMb = 10.0;
    double internalSafetyTargetMb = 9.5;
    int minimumFps = 5;
    int minimumWidth = 320;
    bool allowAggressiveDownscale = false;
    bool allowVeryLowColorGif = false;
};

struct GifCandidate {
    GifEngine engine = GifEngine::FfmpegPalette;
    int width = 0;
    int fps = 0;
    int quality = 0;
    int colors = 256;
    bool useGifsicle = false;
    std::wstring note{};
};

struct GifPlan {
    TargetSizeOptions target{};
    std::vector<GifCandidate> attempts{};
    std::wstring warning{};
};

[[nodiscard]] GifPlan plan_gif_attempts(const GifExportRequest& request, const TargetSizeOptions& target);
[[nodiscard]] std::wstring engine_name(GifEngine engine);
[[nodiscard]] std::wstring dither_name(GifDitherMode dither);

[[nodiscard]] std::wstring build_ffmpeg_palettegen_command(const std::filesystem::path& ffmpegPath,
                                                           const std::filesystem::path& framesPattern,
                                                           const std::filesystem::path& palettePath, int fps,
                                                           int width, int colors);

[[nodiscard]] std::wstring build_ffmpeg_paletteuse_command(const std::filesystem::path& ffmpegPath,
                                                           const std::filesystem::path& framesPattern,
                                                           const std::filesystem::path& palettePath,
                                                           const std::filesystem::path& outputPath, int fps,
                                                           int width, GifDitherMode dither);

[[nodiscard]] std::wstring build_gifski_command(const std::filesystem::path& gifskiPath,
                                                const std::filesystem::path& frameGlob,
                                                const std::filesystem::path& outputPath, int fps, int width,
                                                int quality);

[[nodiscard]] std::wstring build_gifsicle_command(const std::filesystem::path& gifsiclePath,
                                                  const std::filesystem::path& inputPath,
                                                  const std::filesystem::path& outputPath);

} // namespace gifler::exporting
