#include "gifler_export/GifRecordingExporter.h"
#include "gifler_win32/Process.h"
#include <Windows.h>
#include <mmreg.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const std::filesystem::path encoder = argv[1], directory = argv[2];
    std::filesystem::create_directories(directory);
    std::vector<gifler::core::BgraFrame> frames;
    for (int i = 0; i < 300; ++i) {
        gifler::core::BgraFrame frame;
        frame.width = 320; frame.height = 180; frame.stride = 1280;
        frame.timestampTicks = i * 100'000'000LL / 300;
        frame.durationTicks = (i + 1) * 100'000'000LL / 300 - frame.timestampTicks;
        frame.pixels.resize(frame.stride * frame.height);
        for (int y = 0; y < frame.height; ++y) for (int x = 0; x < frame.width; ++x) {
            auto* p = reinterpret_cast<unsigned char*>(frame.pixels.data()) + y * frame.stride + x * 4;
            p[0] = static_cast<unsigned char>(x + i);
            p[1] = static_cast<unsigned char>(y + i * 3);
            p[2] = (x / 10 == i % 32) ? 255 : 0; p[3] = 255;
        }
        frames.push_back(std::move(frame));
    }
    gifler::core::AudioRecording audio;
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, 48000, 96000, 2, 16, 0};
    const auto* fmt = reinterpret_cast<const std::byte*>(&format);
    audio.waveFormat.assign(fmt, fmt + sizeof(format));
    audio.sampleRate = 48000; audio.blockAlign = 2; audio.samples.resize(960000);
    for (int i = 0; i < 480000; ++i) {
        const auto sample = static_cast<std::int16_t>(12000 * std::sin(i * 6.283185307179586 * 440 / 48000));
        std::memcpy(audio.samples.data() + i * 2, &sample, 2);
    }
    gifler::exporting::GifExportRequest gif;
    gif.ffmpegPath = encoder; gif.requestedFps = 30; gif.outputPath = directory / L"ten-seconds.gif";
    gifler::exporting::TargetSizeOptions target;
    target.displayTargetMb = 20; target.internalSafetyTargetMb = 19;
    int progress = 0;
    gif.progress = [&](int value) { progress = value; };
    auto gifResult = gifler::exporting::export_gif_from_frames(frames, gif, target);
    std::wcout << gifResult.message << L"\n";
    if (!gifResult.success || progress != 100) return 3;
    for (int index = 0; index < 3; ++index) {
        gifler::exporting::VideoExportRequest video;
        video.ffmpegPath = encoder; video.requestedFps = 30; video.audio = &audio;
        video.targetMb = 1;
        video.format = static_cast<gifler::exporting::VideoExportFormat>(index);
        video.outputPath = directory / (index == 0 ? L"ten-seconds.mp4" : index == 1 ? L"ten-seconds.webp" : L"ten-seconds.webm");
        auto result = gifler::exporting::export_video_from_frames(frames, video);
        std::wcout << result.message << L"\n";
        if (!result.success || result.fileSizeBytes > 1024 * 1024) return 4 + index;
    }
    for (const int fps : {60, 77, 120, 240}) {
        std::vector<gifler::core::BgraFrame> fast(frames.begin(), frames.begin() + fps);
        for (int i = 0; i < fps; ++i) {
            fast[i].timestampTicks = i * 10'000'000LL / fps;
            fast[i].durationTicks = (i + 1) * 10'000'000LL / fps - fast[i].timestampTicks;
        }
        gifler::exporting::VideoExportRequest video;
        video.ffmpegPath = encoder;
        video.requestedFps = fps;
        video.format = gifler::exporting::VideoExportFormat::Mp4H264;
        video.outputPath = directory / (L"fps-" + std::to_wstring(fps) + L".mp4");
        const auto result = gifler::exporting::export_video_from_frames(fast, video);
        std::wcout << result.message << L"\n";
        if (!result.success) return 11;
    }
    for (const int rate : {48000, 44100}) {
        gifler::exporting::VideoExportRequest video;
        video.ffmpegPath = encoder;
        video.requestedFps = 120;
        video.socialCompatibility = true;
        video.audioSampleRate = rate;
        video.audio = &audio;
        video.targetMb = 0;
        video.outputPath = directory / (L"social-" + std::to_wstring(rate) + L".mp4");
        std::vector<gifler::core::BgraFrame> shortClip(frames.begin(), frames.begin() + 30);
        const auto result = gifler::exporting::export_video_from_frames(shortClip, video);
        if (!result.success) { std::wcout << result.message << L"\n"; return 12; }
    }
    {
        auto wide = frames.front();
        wide.width = 1000; wide.height = 100; wide.stride = 4000;
        wide.durationTicks = 1'000'000;
        wide.pixels.assign(static_cast<std::size_t>(wide.stride) * wide.height, std::byte{127});
        gifler::exporting::VideoExportRequest video;
        video.ffmpegPath = encoder;
        video.socialCompatibility = true;
        video.audio = &audio;
        video.outputPath = directory / L"social-wide-short.mp4";
        const auto result = gifler::exporting::export_video_from_frames({wide}, video);
        if (!result.success) { std::wcout << result.message << L"\n"; return 13; }
        wide.durationTicks = 141ll * 10'000'000;
        video.outputPath = directory / L"social-too-long.mp4";
        const auto rejected = gifler::exporting::export_video_from_frames({wide}, video);
        if (rejected.success || rejected.message.find(L"140 seconds") == std::wstring::npos || std::filesystem::exists(video.outputPath)) return 14;
    }
    auto failure = gifler::win32::run_process_with_input(encoder,
        {L"-hide_banner", L"-loglevel", L"error", L"-this-option-does-not-exist"}, [](HANDLE) { return true; });
    if (!failure.launched || !failure.exitCode || failure.errorMessage.empty()) return 7;
    std::atomic_bool canceled = false;
    std::jthread canceler([&] { std::this_thread::sleep_for(std::chrono::milliseconds(200)); canceled = true; });
    const auto started = std::chrono::steady_clock::now();
    auto cancellation = gifler::win32::run_process_with_input(encoder,
        {L"-v", L"error", L"-re", L"-f", L"lavfi", L"-i", L"color=size=16x16:rate=1", L"-f", L"null", L"-"},
        [](HANDLE) { return true; }, {}, [&] { return canceled.load(); });
    if (cancellation.exitCode != ERROR_CANCELLED || std::chrono::steady_clock::now() - started > std::chrono::seconds(3)) return 10;
    // Encoder failure must not prevent a subsequent export in the same process.
    gif.outputPath = directory / L"retry.gif";
    if (!gifler::exporting::export_gif_from_frames(frames, gif, target).success) return 8;
    std::vector<gifler::core::BgraFrame> held{frames.front()};
    held.front().durationTicks = 900'000'000;
    gif.outputPath = directory / L"ninety-seconds.gif";
    if (!gifler::exporting::export_gif_from_frames(held, gif, target).success) return 9;
    std::cout << "Export smoke passed.\n";
    return 0;
}
