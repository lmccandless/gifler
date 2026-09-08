#include "gifler_export/GifRecordingExporter.h"
#include "gifler_core/FrameTiming.h"
#include "gifler_export/AudioWave.h"

#include "gifler_win32/Process.h"
#include "gifler_win32/Win32Util.h"
#include "gifler_win32/WicImageWriter.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <numeric>
#include <sstream>
#include <set>

namespace gifler::exporting {
namespace {

struct ExportDirectoryGuard {
    std::filesystem::path path;
    ~ExportDirectoryGuard() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

std::filesystem::path local_app_data_root() {
    wchar_t buffer[MAX_PATH]{};
    DWORD chars = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (chars > 0 && chars < MAX_PATH) {
        return std::filesystem::path(buffer) / L"Gifler";
    }
    return std::filesystem::temp_directory_path() / L"Gifler";
}

std::wstring widen_message(const std::string& message) {
    return std::wstring(message.begin(), message.end());
}

std::filesystem::path unique_export_dir() {
    auto root = default_temp_root();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    const auto tick = GetTickCount64();
    const auto pid = GetCurrentProcessId();
    auto dir = root / std::format(L"export_{}_{}", pid, tick);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path frame_path(const std::filesystem::path& framesDir, std::size_t index) {
    return framesDir / std::format(L"frame_{:06}.png", index + 1);
}

bool materialize_png_frames(const std::vector<gifler::core::BgraFrame>& frames, const std::filesystem::path& framesDir,
                            std::wstring* error, const ExportProgressCallback& progress = {}, int progressStart = 0,
                            int progressEnd = 60) {
    std::error_code ec;
    std::filesystem::create_directories(framesDir, ec);
    if (ec) {
        if (error != nullptr) {
            *error = L"Could not create frame directory: " + widen_message(ec.message());
        }
        return false;
    }

    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (!gifler::win32::save_bgra_frame_as_png(frames[i], frame_path(framesDir, i), error)) {
            return false;
        }
        if (progress) {
            progress(progressStart + static_cast<int>((i + 1) * (progressEnd - progressStart) / frames.size()));
        }
    }
    return true;
}

bool frames_are_streamable(const std::vector<gifler::core::BgraFrame>& frames) {
    if (frames.empty() || frames.front().empty()) {
        return false;
    }
    return std::all_of(frames.begin(), frames.end(), [](const auto& frame) {
        return !frame.empty() && frame.stride >= frame.width * 4 &&
            frame.pixels.size() >= static_cast<std::size_t>(frame.stride) * frame.height;
    });
}

bool write_all(HANDLE output, const std::byte* data, std::size_t size) {
    while (size > 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size, 16 * 1024 * 1024));
        DWORD written = 0;
        if (!WriteFile(output, data, chunk, &written, nullptr) || written == 0) {
            return false;
        }
        data += written;
        size -= written;
    }
    return true;
}

bool stream_frames(HANDLE output, const std::vector<gifler::core::BgraFrame>& frames, int fps,
                   const ExportProgressCallback& progress, int progressStart = 5, int progressEnd = 90) {
    const int outputWidth = frames.front().width;
    const int outputHeight = frames.front().height;
    const std::size_t outputRowBytes = static_cast<std::size_t>(outputWidth) * 4;
    const auto repeatCounts = gifler::core::frame_repeats(frames, fps);
    const auto totalFrames = std::accumulate(repeatCounts.begin(), repeatCounts.end(), std::size_t{});

    std::size_t completed = 0;
    int lastProgress = -1;
    std::vector<std::byte> normalized;
    for (std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
        const auto& frame = frames[frameIndex];
        const bool dimensionsMatch = frame.width == outputWidth && frame.height == outputHeight;
        if (!dimensionsMatch) {
            normalized.resize(outputRowBytes * static_cast<std::size_t>(outputHeight));
            for (int y = 0; y < outputHeight; ++y) {
                const int sourceY = std::min(frame.height - 1, y * frame.height / outputHeight);
                for (int x = 0; x < outputWidth; ++x) {
                    const int sourceX = std::min(frame.width - 1, x * frame.width / outputWidth);
                    const auto* source = frame.pixels.data() + static_cast<std::size_t>(sourceY) * frame.stride +
                                         static_cast<std::size_t>(sourceX) * 4;
                    auto* destination = normalized.data() + static_cast<std::size_t>(y) * outputRowBytes +
                                        static_cast<std::size_t>(x) * 4;
                    std::memcpy(destination, source, 4);
                }
            }
        }

        const std::size_t repeats = repeatCounts[frameIndex];
        for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
            if (!dimensionsMatch) {
                if (!write_all(output, normalized.data(), normalized.size())) {
                    return false;
                }
            } else if (frame.stride == static_cast<int>(outputRowBytes)) {
                if (!write_all(output, frame.pixels.data(), outputRowBytes * static_cast<std::size_t>(outputHeight))) {
                    return false;
                }
            } else {
                for (int row = 0; row < outputHeight; ++row) {
                    if (!write_all(output, frame.pixels.data() + static_cast<std::size_t>(row) * frame.stride,
                                   outputRowBytes)) {
                        return false;
                    }
                }
            }
            ++completed;
            const int value = progressStart + static_cast<int>(completed * (progressEnd - progressStart) / totalFrames);
            if (progress && value != lastProgress) {
                progress(value);
                lastProgress = value;
            }
        }
    }
    return true;
}

std::vector<std::wstring> raw_video_input_args(const gifler::core::BgraFrame& frame, int fps) {
    return {L"-y", L"-hide_banner", L"-loglevel", L"error", L"-threads", L"2", L"-filter_threads", L"2", L"-filter_complex_threads", L"2", L"-f", L"rawvideo", L"-pix_fmt", L"bgra", L"-video_size",
            std::format(L"{}x{}", frame.width, frame.height), L"-framerate", std::to_wstring(fps), L"-i", L"pipe:0"};
}

std::wstring ffmpeg_filter(int fps, int width, int colors) {
    std::wstringstream filter;
    filter << L"fps=" << fps << L",scale=" << width
           << L":-2:flags=lanczos,palettegen=stats_mode=diff:max_colors=" << colors;
    return filter.str();
}

std::wstring ffmpeg_paletteuse_filter(int fps, int width, GifDitherMode dither) {
    std::wstringstream filter;
    filter << L"fps=" << fps << L",scale=" << width << L":-2:flags=lanczos[x];[x][1:v]paletteuse=dither="
           << dither_name(dither) << L":diff_mode=rectangle";
    return filter.str();
}

std::wstring ffmpeg_video_scale_filter(int fps, int width) {
    std::wstringstream filter;
    const int evenWidth = std::max(2, width - (width % 2));
    filter << L"fps=" << fps << L",scale=" << evenWidth << L":-2:flags=lanczos,setsar=1";
    return filter.str();
}

gifler::win32::ProcessResult run_ffmpeg_palette(const std::filesystem::path& ffmpegPath,
                                                const std::filesystem::path& framesPattern,
                                                const std::filesystem::path& palettePath,
                                                const std::filesystem::path& outputPath,
                                                const GifCandidate& candidate,
                                                GifDitherMode dither, const std::function<bool()>& canceled) {
    auto first = gifler::win32::run_process_wait(
        ffmpegPath,
        {L"-y", L"-framerate", std::to_wstring(candidate.fps), L"-i", framesPattern.wstring(), L"-vf",
         ffmpeg_filter(candidate.fps, candidate.width, candidate.colors), palettePath.wstring()}, {}, canceled);
    if (!first.launched || first.exitCode != 0) {
        return first;
    }

    return gifler::win32::run_process_wait(
        ffmpegPath,
        {L"-y", L"-framerate", std::to_wstring(candidate.fps), L"-i", framesPattern.wstring(), L"-i",
         palettePath.wstring(), L"-lavfi", ffmpeg_paletteuse_filter(candidate.fps, candidate.width, dither),
         outputPath.wstring()}, {}, canceled);
}

gifler::win32::ProcessResult run_ffmpeg_palette_streamed(const std::filesystem::path& ffmpegPath,
                                                         const std::vector<gifler::core::BgraFrame>& frames,
                                                         const std::filesystem::path& outputPath,
                                                         const GifCandidate& candidate,
                                                         int inputFps,
                                                         GifDitherMode dither,
                                                         const ExportProgressCallback& progress,
                                                         const std::function<bool()>& canceled) {
    const auto palette = outputPath.parent_path() / L"palette.png";
    auto args = raw_video_input_args(frames.front(), inputFps);
    args.insert(args.end(), {L"-vf", ffmpeg_filter(candidate.fps, candidate.width, candidate.colors),
                            L"-frames:v", L"1", L"-threads", L"1", palette.wstring()});
    auto first = gifler::win32::run_process_with_input(ffmpegPath, args,
        [&](HANDLE input) { return stream_frames(input, frames, inputFps, progress, 2, 40); }, {}, canceled);
    if (!first.launched || first.exitCode != 0) return first;
    // A separate pass avoids retaining the entire split stream until palettegen reaches EOF.
    args = raw_video_input_args(frames.front(), inputFps);
    args.insert(args.end(), {L"-i", palette.wstring(), L"-lavfi",
        ffmpeg_paletteuse_filter(candidate.fps, candidate.width, dither), L"-loop", L"0", outputPath.wstring()});
    return gifler::win32::run_process_with_input(
        ffmpegPath, args, [&](HANDLE input) { return stream_frames(input, frames, inputFps, progress, 42, 95); }, {}, canceled);
}

gifler::win32::ProcessResult run_gifski(const std::filesystem::path& gifskiPath,
                                        const std::filesystem::path& frameGlob,
                                        const std::filesystem::path& outputPath,
                                        const GifCandidate& candidate, const std::function<bool()>& canceled) {
    return gifler::win32::run_process_wait(gifskiPath,
                                           {L"--fps", std::to_wstring(candidate.fps), L"--width",
                                            std::to_wstring(candidate.width), L"--quality",
                                            std::to_wstring(candidate.quality), L"-o", outputPath.wstring(),
                                            frameGlob.wstring()}, {}, canceled);
}

std::wstring process_failure_message(const std::wstring& engine, const gifler::win32::ProcessResult& result) {
    if (!result.launched) {
        return engine + L" did not launch: " + result.errorMessage;
    }
    return engine + L" exited with code " + std::to_wstring(result.exitCode) + L". " + result.errorMessage;
}

std::int64_t total_duration_ticks(const std::vector<gifler::core::BgraFrame>& frames, int fallbackFps) {
    std::int64_t total = 0;
    for (const auto& frame : frames) {
        total += frame.durationTicks;
    }
    if (total > 0) {
        return total;
    }
    const int fps = fallbackFps <= 0 ? 10 : fallbackFps;
    return static_cast<std::int64_t>(frames.size()) * (10'000'000LL / fps);
}

std::filesystem::path resolved_output_path(const std::filesystem::path& outputPath) {
    return outputPath.empty() ? outputPath : std::filesystem::absolute(outputPath);
}

bool copy_export_output(const std::filesystem::path& source, const std::filesystem::path& destination, std::wstring* error) {
    std::error_code ec;
    const auto outputParent = destination.parent_path();
    if (!outputParent.empty()) {
        std::filesystem::create_directories(outputParent, ec);
    }
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        if (error != nullptr) {
            *error = L"Could not write output: " + widen_message(ec.message());
        }
        return false;
    }
    return true;
}

} // namespace

std::vector<gifler::core::BgraFrame> expand_frames_for_constant_fps(const std::vector<gifler::core::BgraFrame>& frames, int fps) {
    if (frames.empty()) {
        return {};
    }

    const int resolvedFps = fps <= 0 ? 10 : fps;
    const std::int64_t frameDurationTicks = std::max<std::int64_t>(1, 10'000'000LL / resolvedFps);

    std::vector<gifler::core::BgraFrame> expanded;
    expanded.reserve(frames.size());

    const auto repeats = gifler::core::frame_repeats(frames, resolvedFps);
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto& frame = frames[index];
        const auto repeatCount = repeats[index];
        for (std::size_t i = 0; i < repeatCount; ++i) {
            auto expandedFrame = frame;
            expandedFrame.durationTicks = frameDurationTicks;
            expanded.push_back(std::move(expandedFrame));
        }
    }

    return expanded;
}

std::filesystem::path default_temp_root() {
    return local_app_data_root() / L"Temp";
}

std::filesystem::path default_clipboard_cache_dir() {
    return local_app_data_root() / L"Clipboard";
}

void cleanup_old_clipboard_cache_best_effort(const std::filesystem::path& directory, int maxAgeHours) {
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec)) {
        return;
    }
    const auto now = std::filesystem::file_time_type::clock::now();
    const auto maxAge = std::chrono::hours(std::max(1, maxAgeHours));
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        const auto modified = entry.last_write_time(ec);
        if (!ec && now - modified > maxAge) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

GifExportSummary export_gif_from_frames(const std::vector<gifler::core::BgraFrame>& frames,
                                        GifExportRequest request,
                                        TargetSizeOptions target) {
    GifExportSummary summary{};
    summary.outputPath = request.outputPath;

    if (frames.empty()) {
        summary.message = L"No recording frames are available to export.";
        return summary;
    }
    if (request.outputPath.empty()) {
        summary.message = L"GIF output path is empty.";
        return summary;
    }

    request.sourceWidth = frames.front().width;
    if (!frames_are_streamable(frames)) {
        summary.message = L"Recording contains invalid frame data.";
        return summary;
    }
    request.sourceHeight = frames.front().height;
    auto plan = plan_gif_attempts(request, target);
    if (plan.attempts.empty()) {
        summary.message = plan.warning.empty() ? L"No GIF encoder is configured." : plan.warning;
        return summary;
    }

    const auto workDir = unique_export_dir();
    ExportDirectoryGuard workGuard{workDir};
    const auto framesDir = workDir / L"frames";
    const auto framesPattern = framesDir / L"frame_%06d.png";
    const auto frameGlob = framesDir / L"frame_*.png";
    std::wstring error;
    int materializedFps = 0;
    int lastProgress = 0;
    auto reportProgress = [&](int value) {
        lastProgress = std::max(lastProgress, std::clamp(value, 0, 100));
        if (request.progress) {
            request.progress(lastProgress);
        }
    };
    reportProgress(1);
    auto ensureMaterialized = [&](int fps) {
        if (materializedFps == fps) {
            return true;
        }
        std::error_code ec;
        std::filesystem::remove_all(framesDir, ec);
        std::filesystem::create_directories(framesDir);
        const auto repeats = gifler::core::frame_repeats(frames, fps);
        std::size_t index = 0;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            for (std::size_t repeat = 0; repeat < repeats[i]; ++repeat) {
                if (request.canceled && request.canceled()) { error = L"Export canceled."; return false; }
                if (!gifler::win32::save_bgra_frame_as_png(frames[i], frame_path(framesDir, index++), &error)) return false;
            }
            reportProgress(5 + static_cast<int>((i + 1) * 55 / frames.size()));
        }
        materializedFps = fps;
        return true;
    };

    std::wstring lastFailure;
    const auto internalTargetBytes =
        static_cast<std::uintmax_t>(std::max(0.0, target.internalSafetyTargetMb) * 1024.0 * 1024.0);

    std::set<GifEngine> failedEngines;
    std::size_t attempted = 0;
    for (const auto& candidate : plan.attempts) {
        if (request.canceled && request.canceled()) { lastFailure = L"Export canceled."; break; }
        if (failedEngines.contains(candidate.engine)) continue;
        if (++attempted > 12) {
            lastFailure += L" Choose a larger size target or No limit.";
            break;
        }
        // Show the current attempt's two-pass progress, including compression retries.
        lastProgress = 0;
        reportProgress(0);
        const auto rawOutput = workDir / L"candidate.gif";
        const auto optimizedOutput = workDir / L"candidate_optimized.gif";
        std::error_code ec;
        std::filesystem::remove(rawOutput, ec);
        std::filesystem::remove(optimizedOutput, ec);

        gifler::win32::ProcessResult result{};
        if (candidate.engine == GifEngine::Gifski) {
            if (!ensureMaterialized(candidate.fps)) {
                summary.message = L"Could not prepare PNG frames: " + error;
                return summary;
            }
            result = run_gifski(*request.gifskiPath, frameGlob, rawOutput, candidate, request.canceled);
        } else if (frames_are_streamable(frames)) {
            result = run_ffmpeg_palette_streamed(*request.ffmpegPath, frames, rawOutput, candidate,
                                                 std::max(1, request.requestedFps), request.dither, reportProgress, request.canceled);
        } else {
            if (!ensureMaterialized(candidate.fps)) {
                summary.message = L"Could not prepare PNG frames: " + error;
                return summary;
            }
            result = run_ffmpeg_palette(*request.ffmpegPath, framesPattern, workDir / L"palette.png", rawOutput, candidate,
                                        request.dither, request.canceled);
        }

        if (!result.launched || result.exitCode != 0 || !std::filesystem::exists(rawOutput)) {
            lastFailure = process_failure_message(engine_name(candidate.engine), result);
            failedEngines.insert(candidate.engine);
            continue;
        }

        std::filesystem::path finalCandidate = rawOutput;
        if (candidate.useGifsicle && request.gifsiclePath.has_value()) {
            auto optimize = gifler::win32::run_process_wait(*request.gifsiclePath,
                                                            {L"-O3", rawOutput.wstring(), L"-o", optimizedOutput.wstring()}, {}, request.canceled);
            if (optimize.launched && optimize.exitCode == 0 && std::filesystem::exists(optimizedOutput)) {
                finalCandidate = optimizedOutput;
            }
        }

        const auto size = std::filesystem::file_size(finalCandidate, ec);
        if (ec) {
            lastFailure = L"Could not read encoded GIF size.";
            continue;
        }
        if (target.enabled && internalTargetBytes > 0 && size > internalTargetBytes) {
            lastFailure = std::format(L"{} produced {:.2f} MB, above the {:.2f} MB internal target.",
                                      engine_name(candidate.engine), static_cast<double>(size) / (1024.0 * 1024.0),
                                      target.internalSafetyTargetMb);
            continue;
        }

        std::wstring copyError;
        if (request.canceled && request.canceled()) { lastFailure = L"Export canceled."; break; }
        if (!copy_export_output(finalCandidate, request.outputPath, &copyError)) {
            summary.message = copyError;
            return summary;
        }

        summary.success = true;
        summary.fileSizeBytes = size;
        summary.candidate = candidate;
        summary.message = std::format(L"GIF ready | {:.2f} MB | {} | {} px | {} FPS",
                                      static_cast<double>(size) / (1024.0 * 1024.0), engine_name(candidate.engine),
                                      candidate.width, candidate.fps);
        reportProgress(100);
        std::filesystem::remove_all(workDir, ec);
        return summary;
    }

    summary.message = lastFailure.empty() ? L"No GIF candidate produced an output file." : lastFailure;
    std::error_code ec;
    std::filesystem::remove_all(workDir, ec);
    return summary;
}

VideoExportSummary export_video_from_frames(const std::vector<gifler::core::BgraFrame>& frames,
                                            VideoExportRequest request) {
    VideoExportSummary summary{};
    summary.outputPath = resolved_output_path(request.outputPath);
    summary.format = request.format;

    if (frames.empty()) {
        summary.message = L"No recording frames are available to export.";
        return summary;
    }
    if (!request.ffmpegPath.has_value()) {
        summary.message = L"No video encoder is configured. Set ffmpeg.exe path.";
        return summary;
    }
    if (request.outputPath.empty()) {
        summary.message = L"Video output path is empty.";
        return summary;
    }
    const bool social = request.socialCompatibility && request.format == VideoExportFormat::Mp4H264;
    if (social && total_duration_ticks(frames, request.requestedFps) > 140ll * 10'000'000) {
        summary.message = L"The Social / X preset supports clips up to 140 seconds. Trim the recording or choose regular MP4.";
        return summary;
    }

    const auto workDir = unique_export_dir();
    ExportDirectoryGuard workGuard{workDir};
    const auto framesDir = workDir / L"frames";
    const auto framesPattern = framesDir / L"frame_%06d.png";
    const int fps = social ? 30 : (request.requestedFps <= 0 ? 10 : request.requestedFps);
    int lastProgress = 0;
    auto reportProgress = [&](int value) {
        lastProgress = std::max(lastProgress, std::clamp(value, 0, 100));
        if (request.progress) {
            request.progress(lastProgress);
        }
    };
    reportProgress(1);

    const bool streamedInput = frames_are_streamable(frames);
    if (!streamedInput) {
        summary.message = L"Recording contains invalid frame data.";
        return summary;
    }
    std::wstring error;
    if (!streamedInput) {
        const auto expandedFrames = expand_frames_for_constant_fps(frames, fps);
        if (!materialize_png_frames(expandedFrames, framesDir, &error, reportProgress, 2, 60)) {
            summary.message = L"Could not prepare PNG frames: " + error;
            return summary;
        }
    }

    const int requestedWidth = request.width > 0 ? request.width : frames.front().width;
    const int width = std::max(2, requestedWidth - (requestedWidth % 2));
    const auto encodedPath = workDir / (request.format == VideoExportFormat::Mp4H264
                                            ? L"candidate.mp4"
                                            : request.format == VideoExportFormat::AnimatedWebP ? L"candidate.webp"
                                                                                                : L"candidate.webm");

    gifler::win32::ProcessResult result{};
    std::vector<std::wstring> args = streamedInput
                                         ? raw_video_input_args(frames.front(), fps)
                                         : std::vector<std::wstring>{L"-y", L"-framerate", std::to_wstring(fps), L"-i",
                                                                     framesPattern.wstring()};
    const bool includeAudio = request.audio && request.audio->sampleRate && request.format != VideoExportFormat::AnimatedWebP;
    if (includeAudio) {
        const auto wavePath = workDir / L"audio.wav";
        write_audio_wave(wavePath, *request.audio, frames);
        args.insert(args.end(), {L"-i", wavePath.wstring(), L"-map", L"0:v:0", L"-map", L"1:a:0"});
        const auto audioArgs = video_audio_arguments(request.format, request.audioSampleRate);
        args.insert(args.end(), audioArgs.begin(), audioArgs.end());
    }
    const auto runEncoder = [&](std::vector<std::wstring> encoderArgs) {
        auto command = args;
        command.insert(command.end(), encoderArgs.begin(), encoderArgs.end());
        if (streamedInput) {
            return gifler::win32::run_process_with_input(
                *request.ffmpegPath, command, [&](HANDLE input) { return stream_frames(input, frames, fps, reportProgress); }, {}, request.canceled);
        }
        reportProgress(70);
        return gifler::win32::run_process_wait(*request.ffmpegPath, command, {}, request.canceled);
    };

    int bitrate = std::max(64, calculate_h264_bitrate_kbps(total_duration_ticks(frames, fps), request.targetMb) - (includeAudio ? 128 : 0));
    if (social) bitrate = std::min(bitrate, 8000);
    bool withinTarget = request.targetMb <= 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
    lastProgress = 0;
    reportProgress(0);
    std::vector<std::wstring> rateArgs;
    if (request.targetMb > 0 && request.format != VideoExportFormat::AnimatedWebP) {
        rateArgs = {L"-b:v", std::to_wstring(bitrate) + L"k", L"-maxrate", std::to_wstring(bitrate) + L"k",
                    L"-bufsize", std::to_wstring(bitrate * 2) + L"k"};
    }
    if (social && request.targetMb <= 0)
        rateArgs = {L"-maxrate", L"8000k", L"-bufsize", L"16000k"};
    auto encode = [&](std::vector<std::wstring> options) {
        options.insert(options.end(), rateArgs.begin(), rateArgs.end());
        if (!includeAudio) options.push_back(L"-an");
        const auto repeats = gifler::core::frame_repeats(frames, fps);
        const auto count = std::accumulate(repeats.begin(), repeats.end(), std::size_t{});
        const auto duration = std::max(social ? 0.5 : 0.0, static_cast<double>(count) / fps);
        options.insert(options.end(), {L"-t", std::format(L"{:.9f}", duration)});
        options.push_back(encodedPath.wstring());
        return runEncoder(std::move(options));
    };
    if (request.format == VideoExportFormat::Mp4H264) {
        const int crf = std::clamp(40 - request.quality * 24 / 100, 16, 34);
        std::vector<std::wstring> options{L"-vf", social ? build_social_video_filter({frames.front().width, frames.front().height}, width)
                                                                       : ffmpeg_video_scale_filter(fps, width), L"-c:v", L"libx264", L"-pix_fmt",
                             L"yuv420p", L"-preset", L"veryfast", L"-threads", L"2", L"-crf", std::to_wstring(crf),
                             L"-movflags", L"+faststart"};
        if (social) options.insert(options.end(), {L"-profile:v", L"high", L"-level:v", L"4.1",
            L"-x264-params", L"open-gop=0", L"-g", L"60", L"-field_order", L"progressive"});
        result = encode(std::move(options));
    } else if (request.format == VideoExportFormat::AnimatedWebP) {
        result = encode({L"-vf", ffmpeg_video_scale_filter(fps, std::max(2, width >> attempt)), L"-loop", L"0", L"-c:v", L"libwebp_anim",
                             L"-lossless", L"0", L"-compression_level", L"2", L"-q:v",
                             std::to_wstring(std::clamp(request.quality - attempt * 15, 1, 100))});
    } else {
        const int crf = std::clamp(63 - (request.quality * 48 / 100), 15, 63);
        result = encode({L"-vf", ffmpeg_video_scale_filter(fps, width), L"-c:v", L"libvpx-vp9", L"-pix_fmt",
                             L"yuv420p", L"-deadline", L"realtime", L"-cpu-used", L"7", L"-row-mt", L"1", L"-crf",
                             std::to_wstring(crf), L"-threads", L"2", L"-b:v", L"0"});
    }
        if (!result.launched || result.exitCode != 0) break;
        std::error_code sizeError;
        const auto bytes = std::filesystem::file_size(encodedPath, sizeError);
        if (sizeError) break;
        const double limit = request.targetMb * 1024.0 * 1024.0 * 0.95;
        withinTarget = request.targetMb <= 0 || bytes <= limit;
        if (withinTarget) break;
        bitrate = std::max(32, static_cast<int>(bitrate * limit / bytes * 0.85));
    }

    if (result.launched && result.exitCode == 0 && !withinTarget) {
        summary.message = L"Could not meet the selected size target. Choose a larger target or No limit.";
        std::error_code ec;
        std::filesystem::remove_all(workDir, ec);
        return summary;
    }

    if (!result.launched || result.exitCode != 0 || !std::filesystem::exists(encodedPath)) {
        summary.message = process_failure_message(video_format_name(request.format), result);
        std::error_code ec;
        std::filesystem::remove_all(workDir, ec);
        return summary;
    }

    std::wstring copyError;
    if (request.canceled && request.canceled()) { summary.message = L"Export canceled."; return summary; }
    if (!copy_export_output(encodedPath, summary.outputPath, &copyError)) {
        summary.message = copyError;
        return summary;
    }

    std::error_code ec;
    summary.fileSizeBytes = std::filesystem::file_size(summary.outputPath, ec);
    if (ec || summary.fileSizeBytes == 0) {
        summary.message = L"Encoder finished, but the output file could not be verified at " +
                          summary.outputPath.wstring();
        std::filesystem::remove_all(workDir, ec);
        return summary;
    }
    summary.success = true;
    summary.message = std::format(L"{} ready | {:.2f} MB | {} px | {} FPS | {}", video_format_name(request.format),
                                  static_cast<double>(summary.fileSizeBytes) / (1024.0 * 1024.0), width, fps,
                                  summary.outputPath.wstring());
    if (social) summary.message = std::format(L"MP4 Social / X ready | {:.2f} MB | 30 FPS | {}",
        static_cast<double>(summary.fileSizeBytes) / (1024.0 * 1024.0), summary.outputPath.wstring());
    reportProgress(100);
    std::filesystem::remove_all(workDir, ec);
    return summary;
}

int calculate_h264_bitrate_kbps(std::int64_t durationTicks, double targetMb) {
    const double seconds = std::max(0.1, static_cast<double>(durationTicks) / 10'000'000.0);
    const double videoBudgetBits = std::max(0.25, targetMb) * 1024.0 * 1024.0 * 8.0 * 0.92;
    return std::max(128, static_cast<int>(videoBudgetBits / seconds / 1000.0));
}

std::wstring build_social_video_filter(gifler::core::PixelSize source, int requestedWidth) {
    if (source.empty()) return {};
    const int maxWidth = source.width > source.height ? 1280 : 720;
    const int maxHeight = source.height > source.width ? 1280 : 720;
    const double scale = std::min({1.0, static_cast<double>(maxWidth) / source.width,
        static_cast<double>(maxHeight) / source.height,
        requestedWidth > 0 ? static_cast<double>(requestedWidth) / source.width : 1.0});
    int width = std::max(2, static_cast<int>(source.width * scale) / 2 * 2);
    int height = std::max(2, static_cast<int>(source.height * scale) / 2 * 2);
    // Keep common locked ratios exact while producing even YUV 4:2:0 dimensions.
    const int divisor = std::gcd(source.width, source.height);
    const int units = static_cast<int>(std::floor((divisor * scale + 1e-8) / 2)) * 2;
    if (units >= 2) {
        width = source.width / divisor * units;
        height = source.height / divisor * units;
    }
    const auto evenCeiling = [](double value) { return static_cast<int>(std::ceil(value / 2)) * 2; };
    const int paddedWidth = std::max({32, width, evenCeiling(height / 2.39)});
    const int paddedHeight = std::max({32, height, evenCeiling(width / 2.39)});
    return std::format(L"fps=30,scale={}:{}:flags=lanczos,setsar=1,pad={}:{}:(ow-iw)/2:(oh-ih)/2:black,tpad=stop_mode=clone:stop_duration=0.5",
        width, height, paddedWidth, paddedHeight);
}

std::vector<std::wstring> video_audio_arguments(VideoExportFormat format, int sampleRate) {
    if (format == VideoExportFormat::AnimatedWebP) return {L"-an"};
    std::vector<std::wstring> args;
    if (format == VideoExportFormat::Mp4H264) {
        args = {L"-c:a", L"aac", L"-profile:a", L"aac_low", L"-ar", sampleRate == 44100 ? L"44100" : L"48000"};
    } else args = {L"-c:a", L"libopus", L"-ar", L"48000"};
    args.insert(args.end(), {L"-ac", L"2", L"-b:a", L"128k", L"-af", L"apad", L"-shortest"});
    return args;
}

std::wstring video_format_name(VideoExportFormat format) {
    switch (format) {
    case VideoExportFormat::Mp4H264:
        return L"MP4 H.264";
    case VideoExportFormat::AnimatedWebP:
        return L"Animated WebP";
    case VideoExportFormat::WebMVP9:
        return L"WebM VP9";
    default:
        return L"Video";
    }
}

std::wstring build_ffmpeg_h264_pass1_command(const std::filesystem::path& ffmpegPath,
                                             const std::filesystem::path& framesPattern,
                                             const std::filesystem::path& passLogPath,
                                             int fps,
                                             int width,
                                             int bitrateKbps) {
    std::wstringstream command;
    command << gifler::win32::quote_arg(ffmpegPath.wstring()) << L" -y -framerate " << fps << L" -i "
            << gifler::win32::quote_arg(framesPattern.wstring()) << L" -vf "
            << gifler::win32::quote_arg(ffmpeg_video_scale_filter(fps, width))
            << L" -c:v libx264 -pix_fmt yuv420p -preset slow -b:v " << bitrateKbps
            << L"k -pass 1 -passlogfile " << gifler::win32::quote_arg(passLogPath.wstring()) << L" -an -f mp4 NUL";
    return command.str();
}

std::wstring build_ffmpeg_h264_pass2_command(const std::filesystem::path& ffmpegPath,
                                             const std::filesystem::path& framesPattern,
                                             const std::filesystem::path& passLogPath,
                                             const std::filesystem::path& outputPath,
                                             int fps,
                                             int width,
                                             int bitrateKbps) {
    std::wstringstream command;
    command << gifler::win32::quote_arg(ffmpegPath.wstring()) << L" -y -framerate " << fps << L" -i "
            << gifler::win32::quote_arg(framesPattern.wstring()) << L" -vf "
            << gifler::win32::quote_arg(ffmpeg_video_scale_filter(fps, width))
            << L" -c:v libx264 -pix_fmt yuv420p -preset slow -b:v " << bitrateKbps
            << L"k -pass 2 -passlogfile " << gifler::win32::quote_arg(passLogPath.wstring())
            << L" -an -movflags +faststart " << gifler::win32::quote_arg(outputPath.wstring());
    return command.str();
}

std::wstring build_ffmpeg_webp_command(const std::filesystem::path& ffmpegPath,
                                       const std::filesystem::path& framesPattern,
                                       const std::filesystem::path& outputPath,
                                       int fps,
                                       int width,
                                       int quality) {
    std::wstringstream command;
    command << gifler::win32::quote_arg(ffmpegPath.wstring()) << L" -y -framerate " << fps << L" -i "
            << gifler::win32::quote_arg(framesPattern.wstring()) << L" -vf "
            << gifler::win32::quote_arg(ffmpeg_video_scale_filter(fps, width))
            << L" -loop 0 -c:v libwebp_anim -lossless 0 -q:v " << std::clamp(quality, 1, 100) << L" "
            << gifler::win32::quote_arg(outputPath.wstring());
    return command.str();
}

std::wstring build_ffmpeg_webm_command(const std::filesystem::path& ffmpegPath,
                                       const std::filesystem::path& framesPattern,
                                       const std::filesystem::path& outputPath,
                                       int fps,
                                       int width,
                                       int quality) {
    const int crf = std::clamp(63 - (quality * 48 / 100), 15, 63);
    std::wstringstream command;
    command << gifler::win32::quote_arg(ffmpegPath.wstring()) << L" -y -framerate " << fps << L" -i "
            << gifler::win32::quote_arg(framesPattern.wstring()) << L" -vf "
            << gifler::win32::quote_arg(ffmpeg_video_scale_filter(fps, width))
            << L" -c:v libvpx-vp9 -pix_fmt yuv420p -crf " << crf << L" -b:v 0 -an "
            << gifler::win32::quote_arg(outputPath.wstring());
    return command.str();
}

} // namespace gifler::exporting
