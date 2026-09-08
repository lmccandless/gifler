#include "gifler_core/FrameDiffer.h"
#include "gifler_core/Geometry.h"
#include "gifler_core/AspectRatio.h"
#include "gifler_core/Settings.h"
#include "gifler_core/FrameTiming.h"
#include "gifler_export/AudioWave.h"
#include "gifler_export/GifExportPlanner.h"
#include "gifler_export/GifRecordingExporter.h"
#include "gifler_editor/EditorModel.h"
#include "gifler_record/BoundedFrameQueue.h"
#include "gifler_record/AudioPacketTimeline.h"
#include "gifler_win32/ResizeOverlay.h"
#include "gifler_record/DuplicateFrameCoalescer.h"
#include "gifler_record/InMemoryFrameStore.h"
#include "gifler_record/RecorderSession.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <numeric>
#include <cstring>
#include <Windows.h>
#include <mmreg.h>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << "\n";
    }
}

gifler::core::BgraFrame make_frame(int width, int height, unsigned char value) {
    gifler::core::BgraFrame frame{};
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.pixels.resize(static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < frame.pixels.size(); ++i) {
        frame.pixels[i] = static_cast<std::byte>(value);
    }
    return frame;
}

gifler::core::BgraFrame make_timed_frame(int width, int height, unsigned char value, std::int64_t durationTicks) {
    auto frame = make_frame(width, height, value);
    frame.durationTicks = durationTicks;
    return frame;
}

void geometry_tests() {
    using namespace gifler::core;
    PixelRect a{10, 10, 100, 100};
    PixelRect other{50, 70, 100, 100};
    auto i = intersection(a, other);
    check(i == PixelRect{50, 70, 60, 40}, "intersection should be exact");
    check(contains(a, PixelPoint{10, 10}), "contains top-left");
    check(!contains(a, PixelPoint{110, 110}), "contains excludes bottom-right edge");
    check(scale_for_dpi(96, 144) == 144, "DPI scale 150%");
    const auto hit = gifler::win32::ResizeOverlay::hit;
    for (const auto scale : {1, 2}) {
        const int w = 600 * scale, h = 400 * scale, b = 12 * scale, c = 26 * scale;
        check(hit({2, 2}, w, h, b, c) == HTTOPLEFT, "inner top-left resize zone");
        check(hit({w - 2, 2}, w, h, b, c) == HTTOPRIGHT, "inner top-right resize zone");
        check(hit({2, h - 2}, w, h, b, c) == HTBOTTOMLEFT, "inner bottom-left resize zone");
        check(hit({w - 2, h - 2}, w, h, b, c) == HTBOTTOMRIGHT, "inner bottom-right resize zone");
        check(hit({w / 2, 2}, w, h, b, c) == HTTOP, "inner top resize zone");
        check(hit({w / 2, h - 2}, w, h, b, c) == HTBOTTOM, "inner bottom resize zone");
        check(hit({2, h / 2}, w, h, b, c) == HTLEFT, "inner left resize zone");
        check(hit({w - 2, h / 2}, w, h, b, c) == HTRIGHT, "inner right resize zone");
        check(hit({w / 2, h / 2}, w, h, b, c) == HTNOWHERE, "viewfinder center remains click-through");
        check(hit({b, c + 1}, w, h, b, c) == HTNOWHERE, "inner edge boundary is exclusive");
        check(hit({-1, 0}, w, h, b, c) == HTNOWHERE, "negative coordinates excluded");
    }
    check(hit({5, 5}, 12, 12, 12, 26) == HTNOWHERE, "small viewfinder keeps center open");
}

void frame_differ_tests() {
    auto a = make_frame(4, 4, 0);
    auto b = make_frame(4, 4, 0);
    auto same = gifler::core::diff_bgra_frames(a, b);
    check(same.identical, "identical frame should be identical");
    check(!same.changedBounds.has_value(), "identical frame has no changed bounds");

    auto* pixels = reinterpret_cast<unsigned char*>(b.pixels.data());
    pixels[(2 * b.stride) + 1 * 4 + 0] = 1;
    pixels[(3 * b.stride) + 3 * 4 + 0] = 1;
    auto diff = gifler::core::diff_bgra_frames(a, b);
    check(!diff.identical, "modified frame should differ");
    check(diff.changedBounds == gifler::core::PixelRect{1, 2, 3, 2}, "changed bounds should cover modified pixels");
}

void planner_tests() {
    gifler::exporting::GifExportRequest req{};
    req.sourceWidth = 640;
    req.sourceHeight = 360;
    req.requestedFps = 15;
    req.ffmpegPath = L"ffmpeg.exe";
    req.gifskiPath = L"gifski.exe";

    gifler::exporting::TargetSizeOptions target{};
    auto plan = gifler::exporting::plan_gif_attempts(req, target);
    check(!plan.attempts.empty(), "planner should create attempts with encoders configured");
    check(plan.target.displayTargetMb == 10.0, "default display target is 10 MB");
    check(plan.target.internalSafetyTargetMb == 9.5, "default internal target is 9.5 MB");
    check(plan.attempts.front().engine == gifler::exporting::GifEngine::Gifski, "Auto GIF should try gifski first when configured");

    req.gifskiPath.reset();
    plan = gifler::exporting::plan_gif_attempts(req, target);
    check(!plan.attempts.empty(), "planner should fall back to ffmpeg");
    check(plan.attempts.front().engine == gifler::exporting::GifEngine::FfmpegPalette, "ffmpeg fallback first when gifski missing");

    req.requestedFps = 1;
    plan = gifler::exporting::plan_gif_attempts(req, target);
    check(!plan.attempts.empty() && plan.attempts.front().fps == 1, "custom 1 FPS remains exportable below the default minimum");
    req.requestedFps = 60;
    plan = gifler::exporting::plan_gif_attempts(req, target);
    check(!plan.attempts.empty() && plan.attempts.front().fps == 60, "GIF planner starts at selected 60 FPS");

    req.ffmpegPath.reset();
    plan = gifler::exporting::plan_gif_attempts(req, target);
    check(plan.attempts.empty(), "planner should have no attempts without encoders");
    check(!plan.warning.empty(), "planner should warn without encoders");
}

void aspect_ratio_tests() {
    using namespace gifler::core;
    for (unsigned dpi : {96u, 120u, 144u, 192u}) {
        const PixelSize chrome{scale_for_dpi(3, dpi) + scale_for_dpi(10, dpi), scale_for_dpi(34, dpi) + scale_for_dpi(10, dpi)};
        const PixelSize minimum{scale_for_dpi(120, dpi), scale_for_dpi(65, dpi)};
        for (const auto ratio : CaptureAspectRatios) {
            const PixelRect proposed{-1500, -500, 573, 379};
            for (int edge = 1; edge <= 8; ++edge) {
                const auto side = static_cast<ResizeSide>(edge);
                const auto rect = constrain_capture_aspect(proposed, ratio, chrome, minimum, side);
                if (ratio.empty()) { check(rect == proposed, "free aspect does not constrain resizing"); continue; }
                check((rect.width - chrome.width) * ratio.height == (rect.height - chrome.height) * ratio.width,
                      "all sizing directions preserve the exact capture aspect, excluding chrome");
                check(rect.width >= minimum.width && rect.height >= minimum.height, "ratio respects minimum window size");
                if (side == ResizeSide::Left || side == ResizeSide::TopLeft || side == ResizeSide::BottomLeft)
                    check(rect.right() == proposed.right(), "left resize anchors right edge");
                if (side == ResizeSide::Right || side == ResizeSide::TopRight || side == ResizeSide::BottomRight)
                    check(rect.left() == proposed.left(), "right resize anchors left edge");
                if (side == ResizeSide::Top || side == ResizeSide::TopLeft || side == ResizeSide::TopRight)
                    check(rect.bottom() == proposed.bottom(), "top resize anchors bottom edge");
                if (side == ResizeSide::Bottom || side == ResizeSide::BottomLeft || side == ResizeSide::BottomRight)
                    check(rect.top() == proposed.top(), "bottom resize anchors top edge");
                const auto small = constrain_capture_aspect({0, 0, 2, 2}, ratio, chrome, minimum, side);
                check(small.width >= minimum.width && small.height >= minimum.height, "shrinking never collapses a ratio-locked window");
            }
            if (!ratio.empty()) {
                const auto fit = aspect_window_size({1600, 900}, ratio, chrome, minimum, ResizeSide::BottomRight, true);
                check(fit.width <= 1600 && fit.height <= 900, "ratio fits within monitor/workspace bounds");
            }
        }
    }
}

void settings_tests() {
    const auto path = std::filesystem::temp_directory_path() / L"gifler_settings_roundtrip_test.ini";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    gifler::core::AppSettings saved{};
    saved.defaultFps = 30;
    saved.lastExportFormat = 3;
    saved.captureAudio = true;
    saved.captureAspectRatio = 2;
    saved.socialMp4 = true;
    saved.mp4AudioSampleRate = 44100;
    saved.target.displayTargetMb = 20;
    gifler::core::save_settings_best_effort(path, saved);

    const auto loaded = gifler::core::load_settings_or_defaults(path);
    check(loaded.defaultFps == 30, "settings preserve selected FPS");
    check(loaded.lastExportFormat == 3, "settings preserve selected export format");
    check(loaded.captureAudio, "settings preserve system audio selection");
    check(loaded.captureAspectRatio == 2 && loaded.socialMp4 && loaded.mp4AudioSampleRate == 44100,
          "settings preserve aspect lock, social profile, and MP4 audio rate");
    check(loaded.target.displayTargetMb == 20 && loaded.target.internalSafetyTargetMb == 19,
          "20 MB target restores its matching safety margin");

    for (const int fps : {1, 24, 27, 48, 60, 77, 120, 240}) {
        saved.defaultFps = fps;
        gifler::core::save_settings_best_effort(path, saved);
        check(gifler::core::load_settings_or_defaults(path).defaultFps == fps, "preset and custom FPS persist");
    }
    for (const int fps : {-1, 0, 241, 1000}) {
        saved.defaultFps = fps;
        gifler::core::save_settings_best_effort(path, saved);
        check(gifler::core::load_settings_or_defaults(path).defaultFps == 10, "out-of-range FPS rejected");
    }
    saved.defaultFps = 241;
    saved.lastExportFormat = 99;
    saved.captureAspectRatio = 99;
    saved.mp4AudioSampleRate = 96000;
    gifler::core::save_settings_best_effort(path, saved);
    const auto invalid = gifler::core::load_settings_or_defaults(path);
    check(invalid.defaultFps == 10, "settings reject unsupported FPS values");
    check(invalid.lastExportFormat == 0, "settings reject unsupported export formats");
    check(invalid.captureAspectRatio == 0 && invalid.mp4AudioSampleRate == 48000, "invalid ratio/rate settings restore safe defaults");
    std::filesystem::remove(path, ec);
}

void command_builder_tests() {
    auto cmd = gifler::exporting::build_ffmpeg_palettegen_command(L"C:\\Program Files\\ffmpeg\\ffmpeg.exe",
                                                                  L"frames\\frame_%06d.png", L"palette.png", 10, 640, 256);
    check(cmd.find(L"palettegen") != std::wstring::npos, "palettegen command contains palettegen");
    check(cmd.find(L"\"C:\\Program Files\\ffmpeg\\ffmpeg.exe\"") != std::wstring::npos,
          "command quotes spaced executable path");
}

void gif_exporter_tests() {
    std::vector<gifler::core::BgraFrame> frames;
    frames.push_back(make_timed_frame(2, 2, 1, 10));

    gifler::exporting::GifExportRequest request{};
    request.outputPath = L"missing-encoder-test.gif";
    request.ffmpegPath.reset();
    request.gifskiPath.reset();

    auto summary = gifler::exporting::export_gif_from_frames(frames, request, gifler::exporting::TargetSizeOptions{});
    check(!summary.success, "GIF exporter fails cleanly without encoders");
    check(summary.message.find(L"encoder") != std::wstring::npos, "GIF exporter reports missing encoder");
}

void video_exporter_tests() {
    using namespace gifler::exporting;
    const auto option = [](const std::vector<std::wstring>& args, const std::wstring& name) {
        const auto found = std::find(args.begin(), args.end(), name);
        return found != args.end() && std::next(found) != args.end() ? *std::next(found) : std::wstring{};
    };
    for (const int rate : {44100, 48000}) {
        const auto args = video_audio_arguments(VideoExportFormat::Mp4H264, rate);
        check(option(args, L"-ar") == std::to_wstring(rate), "MP4 resamples only at export to the selected rate");
        check(option(args, L"-profile:a") == L"aac_low" && option(args, L"-ac") == L"2", "MP4 uses AAC-LC stereo");
    }
    check(option(video_audio_arguments(VideoExportFormat::WebMVP9, 44100), L"-ar") == L"48000", "WebM retains Opus-compatible 48 kHz");
    check(build_social_video_filter({1920, 1080}).find(L"scale=1280:720:") != std::wstring::npos, "social landscape fits 720p");
    check(build_social_video_filter({1080, 1920}).find(L"scale=720:1280:") != std::wstring::npos, "social portrait fits 720p");
    check(build_social_video_filter({1000, 1000}).find(L"scale=720:720:") != std::wstring::npos, "social square fits 720 pixels");
    check(build_social_video_filter({1000, 100}).find(L"pad=1000:420:") != std::wstring::npos, "social extreme aspect is padded without cropping");
    check(build_social_video_filter({10, 10}).find(L"pad=32:32:") != std::wstring::npos, "social tiny clips meet minimum dimensions");
    check(build_social_video_filter({320, 180}).find(L"scale=320:180:") != std::wstring::npos, "social profile does not upscale normal clips");
    check(build_social_video_filter({320, 180}).find(L"setsar=1") != std::wstring::npos, "social profile guarantees square pixels");
    check(build_social_video_filter({528, 297}).find(L"scale=512:288:") != std::wstring::npos, "social export preserves exact locked ratio when making dimensions even");
    std::vector<gifler::core::BgraFrame> jitter;
    for (int i = 0; i < 200; ++i) jitter.push_back(make_timed_frame(2, 2, 0, 500'000));
    auto repeats = gifler::core::frame_repeats(jitter, 30);
    check(std::accumulate(repeats.begin(), repeats.end(), std::size_t{}) == 300,
          "200 samples spanning 10 seconds produce exactly 300 frames at 30 FPS");
    jitter.assign(1000, make_timed_frame(2, 2, 0, 10'000));
    repeats = gifler::core::frame_repeats(jitter, 30);
    check(std::accumulate(repeats.begin(), repeats.end(), std::size_t{}) == 30,
          "sub-frame durations do not stretch the recording");
    const int bitrate = gifler::exporting::calculate_h264_bitrate_kbps(10'000'000, 10.0);
    check(bitrate > 1000, "H.264 bitrate calculation uses target size and duration");

    std::vector<gifler::core::BgraFrame> timedFrames;
    timedFrames.push_back(make_timed_frame(2, 2, 1, 1'000'000));
    timedFrames.push_back(make_timed_frame(2, 2, 2, 3'000'000));
    auto expanded = gifler::exporting::expand_frames_for_constant_fps(timedFrames, 10);
    check(expanded.size() == 4, "timed frames expand to preserve playback length at constant FPS");
    check(expanded[0].durationTicks == 1'000'000, "expanded frames use constant frame duration");
    check(reinterpret_cast<const unsigned char*>(expanded[3].pixels.data())[0] == 2,
          "expanded sequence preserves later frame content");

    std::vector<gifler::core::BgraFrame> tenSecondCapture;
    tenSecondCapture.push_back(make_timed_frame(2, 2, 3, 100'000'000));
    auto tenSecondExpanded = gifler::exporting::expand_frames_for_constant_fps(tenSecondCapture, 15);
    check(tenSecondExpanded.size() == 150, "10 seconds at 15 FPS exports 150 constant-FPS frames");

    auto pass1 = gifler::exporting::build_ffmpeg_h264_pass1_command(L"C:\\Program Files\\ffmpeg\\ffmpeg.exe",
                                                                    L"frames\\frame_%06d.png", L"passlog", 10, 640, 1200);
    check(pass1.find(L"libx264") != std::wstring::npos, "H.264 pass1 command selects libx264");
    check(pass1.find(L"-pass 1") != std::wstring::npos, "H.264 pass1 command includes pass marker");
    check(pass1.find(L"\"C:\\Program Files\\ffmpeg\\ffmpeg.exe\"") != std::wstring::npos,
          "H.264 command quotes spaced executable path");

    auto pass2 = gifler::exporting::build_ffmpeg_h264_pass2_command(L"ffmpeg.exe", L"frames\\frame_%06d.png", L"passlog",
                                                                    L"out.mp4", 10, 640, 1200);
    check(pass2.find(L"+faststart") != std::wstring::npos, "H.264 pass2 command enables faststart");

    auto webp = gifler::exporting::build_ffmpeg_webp_command(L"ffmpeg.exe", L"frames\\frame_%06d.png", L"out.webp", 12,
                                                             480, 75);
    check(webp.find(L"libwebp_anim") != std::wstring::npos, "WebP command selects animated WebP encoder");
    check(webp.find(L"-loop 0") != std::wstring::npos, "WebP command loops animation");

    auto webm = gifler::exporting::build_ffmpeg_webm_command(L"ffmpeg.exe", L"frames\\frame_%06d.png", L"out.webm", 30,
                                                             481, 75);
    check(webm.find(L"libvpx-vp9") != std::wstring::npos, "WebM command selects VP9 encoder");
    check(webm.find(L"scale=480:-2") != std::wstring::npos, "video command forces an even output width");

    std::vector<gifler::core::BgraFrame> frames;
    frames.push_back(make_timed_frame(2, 2, 1, 10));
    gifler::exporting::VideoExportRequest request{};
    request.outputPath = L"missing-ffmpeg-test.mp4";
    request.ffmpegPath.reset();
    auto summary = gifler::exporting::export_video_from_frames(frames, request);
    check(!summary.success, "video exporter fails cleanly without ffmpeg");
    check(summary.message.find(L"ffmpeg") != std::wstring::npos || summary.message.find(L"encoder") != std::wstring::npos,
          "video exporter reports missing ffmpeg");
}

void editor_model_tests() {
    gifler::editor::EditorModel model(10);
    check(model.included_indices().size() == 10, "editor starts with all frames included");
    model.set_trim_start(2);
    model.set_trim_end(7);
    auto trimmed = model.included_indices();
    check(trimmed.size() == 6 && trimmed.front() == 2 && trimmed.back() == 7, "editor trim range is inclusive");
    model.delete_frame(4);
    check(!model.included(4), "editor delete frame excludes frame non-destructively");
    model.delete_even_frames();
    check(!model.included(2) && model.included(3), "editor delete even frames only removes even indices");
    model.reset(3);
    check(model.included_indices().size() == 3, "editor reset restores frame count");
}

void bounded_queue_tests() {
    gifler::record::BoundedFrameQueue<int> queue(2);
    check(queue.capacity() == 2, "queue preserves non-zero capacity");
    check(queue.try_push(1), "try_push accepts first item");
    check(queue.try_push(2), "try_push accepts up to capacity");
    check(!queue.try_push(3), "try_push rejects above capacity");
    check(queue.size() == 2, "queue size never exceeds capacity");

    auto first = queue.pop();
    check(first.has_value() && *first == 1, "queue pops FIFO item");
    check(queue.try_push(3), "try_push accepts after pop frees capacity");

    queue.close();
    check(queue.closed(), "queue reports closed");
    check(!queue.push(4), "push fails after close");
    check(queue.pop().has_value(), "pop drains first queued item after close");
    check(queue.pop().has_value(), "pop drains second queued item after close");
    check(!queue.pop().has_value(), "closed empty queue returns nullopt");
}

void duplicate_coalescer_tests() {
    gifler::record::DuplicateFrameCoalescer coalescer;
    coalescer.push(make_timed_frame(3, 2, 7, 10));
    coalescer.push(make_timed_frame(3, 2, 7, 20));
    coalescer.push(make_timed_frame(3, 2, 9, 30));

    auto frames = coalescer.finish();
    check(frames.size() == 2, "duplicate frames coalesce");
    check(frames[0].durationTicks == 30, "duplicate duration is aggregated");
    check(frames[0].changedBounds == gifler::core::PixelRect{0, 0, 3, 2}, "first frame changed bounds is full frame");
    check(frames[1].durationTicks == 30, "non-duplicate frame keeps own duration");
    check(frames[1].changedBounds == gifler::core::PixelRect{0, 0, 3, 2}, "changed frame bounds cover changed pixels");

    gifler::record::DuplicateFrameCoalescer resized;
    resized.push(make_timed_frame(3, 2, 7, 10));
    resized.push(make_timed_frame(4, 2, 7, 20));
    auto resizedFrames = resized.finish();
    check(resizedFrames.size() == 2, "resized frames do not enter same-size diffing");
    check(resizedFrames[1].changedBounds == gifler::core::PixelRect{0, 0, 4, 2}, "resized frame changed bounds is full frame");
}

void frame_store_tests() {
    gifler::record::InMemoryFrameStore store;
    store.add(make_timed_frame(2, 2, 1, 10));
    store.add(make_timed_frame(2, 2, 2, 15));
    check(store.frame_count() == 2, "frame store counts frames");
    check(store.total_duration_ticks() == 25, "frame store sums duration");
    check(store.total_pixel_bytes() == 32, "frame store sums pixel bytes");
    store.clear();
    check(store.frame_count() == 0, "frame store clears frames");
}

void recorder_session_tests() {
    gifler::record::RecorderSession session;
    gifler::record::RecorderSettings settings{};
    settings.fps = 30;
    settings.queueCapacity = 2;

    session.start_synthetic(settings, 24, 16);
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    session.stop();

    check(session.state() == gifler::record::RecorderState::Completed, "synthetic recorder reaches completed state after stop");
    check(session.frame_store().frame_count() > 0, "synthetic recorder stores frames");
    check(session.frame_store().total_duration_ticks() > 0, "synthetic recorder preserves frame durations");
    check(session.frame_store().total_pixel_bytes() > 0, "synthetic recorder stores pixel data");
}

void audio_packet_timeline_tests() {
    for (const auto rate : {44100, 48000}) {
        gifler::record::AudioPacketTimeline timeline;
        const auto frames = static_cast<std::uint32_t>(rate / 100);
        const auto initial = rate / 4;
        for (std::uint32_t i = 0; i < 300; ++i) {
            const auto expected = initial + static_cast<std::int64_t>(i) * frames;
            const auto jitter = i == 0 ? 0 : (i % 2 ? 928 : -25);
            check(timeline.place(7000 + static_cast<std::uint64_t>(i) * frames, frames,
                                 expected + jitter, true) == expected,
                  "audio remains sample-continuous despite QPC jitter");
        }
        const auto expected = initial + static_cast<std::int64_t>(302) * frames;
        check(timeline.place(7000 + 302ull * frames, frames, expected, true) == expected,
              "real missing device frames preserve their silence interval");
        check(timeline.place(0, frames, 0, false) == expected + frames,
              "invalid timestamps append instead of using arrival jitter");
        check(timeline.place(900000, frames, 0, true) == expected + 2 * frames,
              "valid timestamps resume continuously after an invalid packet");
        check(timeline.place(0, frames, 0, true) == expected + 3 * frames,
              "device position reset never overwrites earlier samples");
    }
    gifler::record::AudioPacketTimeline preroll;
    check(preroll.place(0, 480, -240, true) == -240, "pre-epoch samples retain their position for trimming");
    check(preroll.place(480, 480, 999, true) == 240, "pre-roll trimming does not shift later packets");
}

void audio_wave_tests() {
    gifler::core::AudioRecording audio;
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, 1000, 2000, 2, 16, 0};
    const auto* data = reinterpret_cast<const std::byte*>(&format);
    audio.waveFormat.assign(data, data + sizeof(format));
    audio.sampleRate = 1000;
    audio.blockAlign = 2;
    audio.samples.resize(4000);
    for (int i = 0; i < 2000; ++i) {
        const auto value = static_cast<std::int16_t>(i);
        std::memcpy(audio.samples.data() + i * 2, &value, 2);
    }
    auto first = make_timed_frame(2, 2, 0, 5'000'000);
    first.timestampTicks = 10'000'000;
    auto second = first;
    second.timestampTicks = 0;
    const auto path = std::filesystem::temp_directory_path() / L"gifler_audio_timeline_test.wav";
    gifler::exporting::write_audio_wave(path, audio, {first, second});
    std::ifstream input(path, std::ios::binary);
    const auto header = 28 + sizeof(format);
    input.seekg(header);
    std::int16_t sample = 0;
    input.read(reinterpret_cast<char*>(&sample), 2);
    check(sample == 1000, "audio follows edited frame timestamp");
    input.seekg(header + 1000);
    input.read(reinterpret_cast<char*>(&sample), 2);
    check(sample == 0, "audio splices selected frame intervals in timeline order");
    input.close();
    check(std::filesystem::file_size(path) == header + 2000, "audio duration matches selected video duration");
    std::filesystem::remove(path);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--audio-wave-probe") {
        gifler::record::LoopbackAudio audio;
        audio.start(gifler::record::clock_100ns());
        std::this_thread::sleep_for(std::chrono::seconds(4));
        audio.stop();
        if (!audio.error().empty() || audio.recording().samples.empty()) return 1;
        gifler::exporting::write_audio_wave(argv[2], audio.recording(),
            {make_timed_frame(2, 2, 0, 40'000'000)});
        std::cout << "Captured " << audio.recording().samples.size() << " bytes\n";
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--audio-probe") {
        gifler::record::LoopbackAudio audio;
        audio.start(gifler::record::clock_100ns());
        std::this_thread::sleep_for(std::chrono::seconds(1));
        audio.stop();
        std::wcout << L"WASAPI loopback: " << audio.recording().sampleRate << L" Hz, "
                   << audio.recording().samples.size() << L" bytes, " << audio.error() << L"\n";
        return audio.recording().sampleRate && audio.error().empty() ? 0 : 1;
    }
    geometry_tests();
    aspect_ratio_tests();
    frame_differ_tests();
    planner_tests();
    settings_tests();
    command_builder_tests();
    gif_exporter_tests();
    video_exporter_tests();
    editor_model_tests();
    bounded_queue_tests();
    duplicate_coalescer_tests();
    frame_store_tests();
    recorder_session_tests();
    audio_wave_tests();
    audio_packet_timeline_tests();

    if (failures != 0) {
        std::cerr << failures << " test failure(s).\n";
        return EXIT_FAILURE;
    }
    std::cout << "All gifler_tests passed.\n";
    return EXIT_SUCCESS;
}
