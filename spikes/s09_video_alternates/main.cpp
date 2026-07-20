#include "gifler_core/Frame.h"
#include "gifler_export/GifRecordingExporter.h"

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <vector>

namespace {

gifler::core::BgraFrame make_frame(int width, int height, int index) {
    gifler::core::BgraFrame frame{};
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.durationTicks = 1'000'000;
    frame.timestampTicks = static_cast<std::int64_t>(index) * frame.durationTicks;
    frame.pixels.resize(static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(height));

    auto* p = reinterpret_cast<unsigned char*>(frame.pixels.data());
    const int boxX = (index * 8) % width;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int offset = y * frame.stride + x * 4;
            const bool box = x >= boxX && x < boxX + 24 && y >= 20 && y < 52;
            p[offset + 0] = box ? 20 : 35;
            p[offset + 1] = box ? 180 : 35;
            p[offset + 2] = box ? 245 : 35;
            p[offset + 3] = 255;
        }
    }
    return frame;
}

std::filesystem::path find_ffmpeg() {
    wchar_t buffer[MAX_PATH]{};
    DWORD found = SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, buffer, nullptr);
    if (found > 0 && found < MAX_PATH) {
        return buffer;
    }
    return {};
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::filesystem::path ffmpeg = argc >= 2 ? argv[1] : find_ffmpeg();
    std::filesystem::path outDir = argc >= 3 ? argv[2] : std::filesystem::path(L"artifacts") / L"video";
    if (ffmpeg.empty() || !std::filesystem::exists(ffmpeg)) {
        std::wcerr << L"ffmpeg.exe not found.\n";
        return 2;
    }

    std::filesystem::create_directories(outDir);

    std::vector<gifler::core::BgraFrame> frames;
    for (int i = 0; i < 24; ++i) {
        frames.push_back(i == 12 ? make_frame(173, 97, i) : make_frame(161, 91, i));
    }

    gifler::exporting::VideoExportRequest mp4{};
    mp4.format = gifler::exporting::VideoExportFormat::Mp4H264;
    mp4.ffmpegPath = ffmpeg;
    mp4.outputPath = outDir / L"synthetic.mp4";
    mp4.requestedFps = 10;
    mp4.width = 161;
    mp4.targetMb = 2.0;
    int mp4Progress = 0;
    mp4.progress = [&](int value) { mp4Progress = value; };

    auto mp4Result = gifler::exporting::export_video_from_frames(frames, mp4);
    std::wcout << mp4Result.message << L"\n";
    if (!mp4Result.success || mp4Progress != 100) {
        return 3;
    }

    gifler::exporting::VideoExportRequest webp{};
    webp.format = gifler::exporting::VideoExportFormat::AnimatedWebP;
    webp.ffmpegPath = ffmpeg;
    webp.outputPath = outDir / L"synthetic.webp";
    webp.requestedFps = 10;
    webp.width = 161;
    webp.quality = 75;
    int webpProgress = 0;
    webp.progress = [&](int value) { webpProgress = value; };

    auto webpResult = gifler::exporting::export_video_from_frames(frames, webp);
    std::wcout << webpResult.message << L"\n";
    if (!webpResult.success || webpProgress != 100) {
        return 4;
    }

    gifler::exporting::VideoExportRequest webm{};
    webm.format = gifler::exporting::VideoExportFormat::WebMVP9;
    webm.ffmpegPath = ffmpeg;
    webm.outputPath = outDir / L"synthetic.webm";
    webm.requestedFps = 10;
    webm.width = 161;
    webm.quality = 75;
    int webmProgress = 0;
    webm.progress = [&](int value) { webmProgress = value; };

    auto webmResult = gifler::exporting::export_video_from_frames(frames, webm);
    std::wcout << webmResult.message << L"\n";
    if (!webmResult.success || webmProgress != 100) {
        return 5;
    }

    gifler::exporting::GifExportRequest gif{};
    gif.ffmpegPath = ffmpeg;
    gif.outputPath = outDir / L"synthetic.gif";
    gif.requestedFps = 10;
    int gifProgress = 0;
    gif.progress = [&](int value) { gifProgress = value; };
    gifler::exporting::TargetSizeOptions gifTarget{};
    gifTarget.enabled = false;

    auto gifResult = gifler::exporting::export_gif_from_frames(frames, gif, gifTarget);
    std::wcout << gifResult.message << L"\n";
    if (!gifResult.success || gifProgress != 100) {
        return 6;
    }

    return 0;
}
