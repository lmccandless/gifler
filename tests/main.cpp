#include "gifler_core/FrameDiffer.h"
#include "gifler_core/Geometry.h"
#include "gifler_core/Settings.h"
#include "gifler_export/GifExportPlanner.h"
#include "gifler_export/GifRecordingExporter.h"
#include "gifler_editor/EditorModel.h"
#include "gifler_record/BoundedFrameQueue.h"
#include "gifler_record/DuplicateFrameCoalescer.h"
#include "gifler_record/InMemoryFrameStore.h"
#include "gifler_record/RecorderSession.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
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
    PixelRect b{50, 70, 100, 100};
    auto i = intersection(a, b);
    check(i == PixelRect{50, 70, 60, 40}, "intersection should be exact");
    check(contains(a, PixelPoint{10, 10}), "contains top-left");
    check(!contains(a, PixelPoint{110, 110}), "contains excludes bottom-right edge");
    check(scale_for_dpi(96, 144) == 144, "DPI scale 150%");
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

    req.ffmpegPath.reset();
    plan = gifler::exporting::plan_gif_attempts(req, target);
    check(plan.attempts.empty(), "planner should have no attempts without encoders");
    check(!plan.warning.empty(), "planner should warn without encoders");
}

void settings_tests() {
    const auto path = std::filesystem::temp_directory_path() / L"gifler_settings_roundtrip_test.ini";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    gifler::core::AppSettings saved{};
    saved.defaultFps = 30;
    saved.lastExportFormat = 3;
    gifler::core::save_settings_best_effort(path, saved);

    const auto loaded = gifler::core::load_settings_or_defaults(path);
    check(loaded.defaultFps == 30, "settings preserve selected FPS");
    check(loaded.lastExportFormat == 3, "settings preserve selected export format");

    saved.defaultFps = 27;
    saved.lastExportFormat = 99;
    gifler::core::save_settings_best_effort(path, saved);
    const auto invalid = gifler::core::load_settings_or_defaults(path);
    check(invalid.defaultFps == 10, "settings reject unsupported FPS values");
    check(invalid.lastExportFormat == 0, "settings reject unsupported export formats");
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

} // namespace

int main() {
    geometry_tests();
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

    if (failures != 0) {
        std::cerr << failures << " test failure(s).\n";
        return EXIT_FAILURE;
    }
    std::cout << "All gifler_tests passed.\n";
    return EXIT_SUCCESS;
}
