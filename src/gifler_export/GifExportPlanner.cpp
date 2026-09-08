#include "gifler_export/GifExportPlanner.h"

#include "gifler_win32/Win32Util.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <sstream>

namespace gifler::exporting {
namespace {

std::vector<int> unique_descending(std::vector<int> values) {
    values.erase(std::remove_if(values.begin(), values.end(), [](int v) { return v <= 0; }), values.end());
    std::sort(values.begin(), values.end(), std::greater<>());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

std::vector<int> fps_ladder(int requested, int minimum) {
    requested = std::max(1, requested);
    minimum = std::clamp(minimum, 1, requested);
    std::vector<int> values{requested, 120, 60, 48, 30, 24, 20, 15, 12, 10, 8, 6, minimum};
    values.erase(std::remove_if(values.begin(), values.end(), [&](int fps) { return fps > requested || fps < minimum; }), values.end());
    return unique_descending(values);
}

std::vector<int> width_ladder(int sourceWidth, int minimumWidth, bool aggressive) {
    minimumWidth = aggressive ? std::max(160, minimumWidth / 2) : minimumWidth;
    std::vector<int> values{sourceWidth,
                            static_cast<int>(std::round(sourceWidth * 0.85)),
                            static_cast<int>(std::round(sourceWidth * 0.75)),
                            static_cast<int>(std::round(sourceWidth * 0.66)),
                            static_cast<int>(std::round(sourceWidth * 0.50)),
                            minimumWidth};
    values.erase(std::remove_if(values.begin(), values.end(), [&](int width) { return width > sourceWidth || width < minimumWidth; }),
                 values.end());
    return unique_descending(values);
}

std::vector<int> color_ladder(int maxColors, bool allowVeryLow) {
    const int floor = allowVeryLow ? 16 : 32;
    std::vector<int> values{maxColors, 256, 192, 160, 128, 96, 64, 48, 32, floor};
    values.erase(std::remove_if(values.begin(), values.end(), [&](int colors) { return colors > maxColors || colors < floor; }),
                 values.end());
    return unique_descending(values);
}

std::wstring loop_arg(bool loop) {
    return loop ? L"--repeat" : L"--once";
}

} // namespace

GifPlan plan_gif_attempts(const GifExportRequest& request, const TargetSizeOptions& target) {
    GifPlan plan{};
    plan.target = target;

    const int sourceWidth = std::max(1, request.sourceWidth);
    const int minimumWidth = target.allowAggressiveDownscale ? std::max(160, target.minimumWidth / 2) : target.minimumWidth;
    const auto widths = width_ladder(sourceWidth, std::min(sourceWidth, std::max(1, minimumWidth)), target.allowAggressiveDownscale);
    const auto fpsValues = fps_ladder(std::max(1, request.requestedFps), std::max(1, target.minimumFps));
    const auto colors = color_ladder(std::clamp(request.maxColors, 2, 256), target.allowVeryLowColorGif);

    const bool hasGifski = request.gifskiPath.has_value();
    const bool hasFfmpeg = request.ffmpegPath.has_value();
    const bool hasGifsicle = request.gifsiclePath.has_value();

    if (!hasGifski && !hasFfmpeg) {
        plan.warning = L"No GIF encoder is configured. Set ffmpeg.exe or gifski.exe path.";
        return plan;
    }

    if (hasGifski && request.mode != GifExportMode::SmallestGif && request.mode != GifExportMode::CompatibilityGif) {
        const auto qualities = unique_descending({request.quality, 95, 90, 85, 80, 72, 65, 58, 50});
        for (int width : widths) {
            for (int fps : fpsValues) {
                for (int quality : qualities) {
                    if (quality <= 0 || quality > 100) {
                        continue;
                    }
                    plan.attempts.push_back(GifCandidate{GifEngine::Gifski, width, fps, quality, 256, hasGifsicle,
                                                         L"gifski first for high-quality Auto GIF"});
                }
            }
        }
    }

    if (hasFfmpeg) {
        for (int width : widths) {
            for (int fps : fpsValues) {
                for (int colorCount : colors) {
                    plan.attempts.push_back(GifCandidate{GifEngine::FfmpegPalette, width, fps, 0, colorCount, hasGifsicle,
                                                         L"FFmpeg palette fallback/compatibility path"});
                }
            }
        }
    }

    if (plan.attempts.empty()) {
        plan.warning = L"No viable GIF candidate attempts were generated.";
    }
    return plan;
}

std::wstring engine_name(GifEngine engine) {
    switch (engine) {
    case GifEngine::Gifski:
        return L"gifski";
    case GifEngine::FfmpegPalette:
        return L"FFmpeg palette";
    default:
        return L"unknown";
    }
}

std::wstring dither_name(GifDitherMode dither) {
    switch (dither) {
    case GifDitherMode::Sierra2_4A:
        return L"sierra2_4a";
    case GifDitherMode::Bayer:
        return L"bayer";
    case GifDitherMode::None:
        return L"none";
    default:
        return L"sierra2_4a";
    }
}

std::wstring build_ffmpeg_palettegen_command(const std::filesystem::path& ffmpegPath,
                                             const std::filesystem::path& framesPattern,
                                             const std::filesystem::path& palettePath, int fps, int width,
                                             int colors) {
    std::wstringstream filter;
    filter << L"fps=" << fps << L",scale=" << width
           << L":-2:flags=lanczos,palettegen=stats_mode=diff:max_colors=" << colors;

    std::wstringstream command;
    command << gifler::win32::quote_arg(ffmpegPath.wstring()) << L" -y -framerate " << fps << L" -i "
            << gifler::win32::quote_arg(framesPattern.wstring()) << L" -vf "
            << gifler::win32::quote_arg(filter.str()) << L" " << gifler::win32::quote_arg(palettePath.wstring());
    return command.str();
}

std::wstring build_ffmpeg_paletteuse_command(const std::filesystem::path& ffmpegPath,
                                             const std::filesystem::path& framesPattern,
                                             const std::filesystem::path& palettePath,
                                             const std::filesystem::path& outputPath, int fps, int width,
                                             GifDitherMode dither) {
    std::wstringstream filter;
    filter << L"fps=" << fps << L",scale=" << width << L":-2:flags=lanczos[x];[x][1:v]paletteuse=dither="
           << dither_name(dither) << L":diff_mode=rectangle";

    std::wstringstream command;
    command << gifler::win32::quote_arg(ffmpegPath.wstring()) << L" -y -framerate " << fps << L" -i "
            << gifler::win32::quote_arg(framesPattern.wstring()) << L" -i "
            << gifler::win32::quote_arg(palettePath.wstring()) << L" -lavfi " << gifler::win32::quote_arg(filter.str())
            << L" " << gifler::win32::quote_arg(outputPath.wstring());
    return command.str();
}

std::wstring build_gifski_command(const std::filesystem::path& gifskiPath, const std::filesystem::path& frameGlob,
                                  const std::filesystem::path& outputPath, int fps, int width, int quality) {
    std::wstringstream command;
    command << gifler::win32::quote_arg(gifskiPath.wstring()) << L" --fps " << fps << L" --width " << width
            << L" --quality " << quality << L" -o " << gifler::win32::quote_arg(outputPath.wstring()) << L" "
            << gifler::win32::quote_arg(frameGlob.wstring());
    return command.str();
}

std::wstring build_gifsicle_command(const std::filesystem::path& gifsiclePath, const std::filesystem::path& inputPath,
                                    const std::filesystem::path& outputPath) {
    std::wstringstream command;
    command << gifler::win32::quote_arg(gifsiclePath.wstring()) << L" -O3 " << gifler::win32::quote_arg(inputPath.wstring())
            << L" -o " << gifler::win32::quote_arg(outputPath.wstring());
    return command.str();
}

} // namespace gifler::exporting
