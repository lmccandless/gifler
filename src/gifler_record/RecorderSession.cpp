#include "gifler_record/RecorderSession.h"

#include "gifler_capture_dxgi/DxgiCaptureProvider.h"
#include "gifler_record/DuplicateFrameCoalescer.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace gifler::record {
namespace {

gifler::core::BgraFrame make_synthetic_frame(int width, int height, int frameIndex, std::int64_t durationTicks) {
    gifler::core::BgraFrame frame{};
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.durationTicks = durationTicks;
    frame.timestampTicks = frameIndex * durationTicks;
    frame.pixels.resize(static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(height));

    auto* p = reinterpret_cast<unsigned char*>(frame.pixels.data());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int offset = y * frame.stride + x * 4;
            const bool movingBox = x >= frameIndex % width && x < (frameIndex % width) + 20 && y >= 10 && y < 30;
            p[offset + 0] = movingBox ? 0 : 40;    // B
            p[offset + 1] = movingBox ? 180 : 40;  // G
            p[offset + 2] = movingBox ? 255 : 40;  // R
            p[offset + 3] = 255;                   // A
        }
    }
    return frame;
}

} // namespace

RecorderSession::~RecorderSession() {
    stop();
}

void RecorderSession::prepare_start(RecorderSettings settings) {
    stop();
    recordedFps_ = settings.fps;
    capturedFrames_ = 0;
    audio_.clear();
    store_.clear();
    set_last_error({});
    stopRequested_ = false;
    state_ = RecorderState::Recording;
    queue_ = std::make_shared<BoundedFrameQueue<gifler::core::BgraFrame>>(settings.queueCapacity);
}

void RecorderSession::start_recording_worker(std::shared_ptr<BoundedFrameQueue<gifler::core::BgraFrame>> queue) {
    try {
    recordingThread_ = std::thread([this, queue = std::move(queue)] {
      try {
        DuplicateFrameCoalescer coalescer;
        while (auto frame = queue->pop()) {
            coalescer.push(std::move(*frame));
        }
        auto frames = coalescer.finish();
        for (auto& frame : frames) {
            store_.add(std::move(frame));
        }
        if (state_.load() != RecorderState::Failed) {
            state_ = RecorderState::Completed;
        }
      } catch (...) {
        state_ = RecorderState::Failed;
        stopRequested_ = true;
        set_last_error(L"Recording storage failed. Try a smaller region or shorter recording.");
        queue->close();
      }
    });
    } catch (...) {
        stopRequested_ = true;
        queue_->close();
        if (captureThread_.joinable()) captureThread_.join();
        state_ = RecorderState::Failed;
        set_last_error(L"Could not start recording storage worker.");
    }
}

void RecorderSession::start_synthetic(RecorderSettings settings, int width, int height) {
    prepare_start(settings);

    captureThread_ = std::thread([this, settings, width, height, queue = queue_] {
        const auto frameDelay = std::chrono::milliseconds(1000 / (settings.fps <= 0 ? 10 : settings.fps));
        const std::int64_t fakeDurationTicks = 10'000'000LL / (settings.fps <= 0 ? 10 : settings.fps);
        int index = 0;
        while (!stopRequested_.load()) {
            if (!queue->push(make_synthetic_frame(width, height, index++, fakeDurationTicks))) {
                break;
            }
            std::this_thread::sleep_for(frameDelay);
        }
        queue->close();
    });

    start_recording_worker(queue_);
}

void RecorderSession::start_dxgi(RecorderSettings settings, gifler::core::PixelRect captureRect) {
    start_dxgi(settings, [captureRect] { return captureRect; });
}

void RecorderSession::start_dxgi(RecorderSettings settings, std::function<gifler::core::PixelRect()> captureRectProvider) {
    prepare_start(settings);

    captureThread_ = std::thread([this, settings, captureRectProvider = std::move(captureRectProvider), queue = queue_] {
      try {
        const int fps = settings.fps <= 0 ? 10 : settings.fps;
        const auto frameDelay = std::chrono::nanoseconds(1'000'000'000LL / fps);
        gifler::capture_dxgi::DxgiCaptureProvider capture;
        int consecutiveFailures = 0;
        const auto epoch = clock_100ns();
        auto deadline = std::chrono::steady_clock::now();
        if (settings.captureAudio) audio_.start(epoch);
        gifler::core::BgraFrame pending;

        while (!stopRequested_.load()) {
            gifler::core::BgraFrame frame;
            std::wstring error;
            const auto captureRect = captureRectProvider();
            if (captureRect.empty()) {
                set_last_error(L"Capture rectangle is empty.");
                std::this_thread::sleep_for(frameDelay);
                continue;
            }
            if (capture.capture_composed_frame(captureRect, frame, &error, settings.captureCursor)) {
                ++capturedFrames_;
                const auto capturedAt = clock_100ns() - epoch;
                frame.timestampTicks = capturedAt;
                if (!pending.empty()) {
                    pending.durationTicks = std::max<std::int64_t>(1, capturedAt - pending.timestampTicks);
                    if (!queue->push(std::move(pending))) break;
                } else {
                    frame.timestampTicks = 0;
                }
                pending = std::move(frame);
                consecutiveFailures = 0;
            } else {
                ++consecutiveFailures;
                set_last_error(std::move(error));
                if (consecutiveFailures >= 3) {
                    state_ = RecorderState::Failed;
                    break;
                }
            }

            deadline += frameDelay;
            const auto now = std::chrono::steady_clock::now();
            if (deadline < now) deadline = now;
            std::this_thread::sleep_until(deadline);
        }
        if (!pending.empty()) {
            pending.durationTicks = std::max<std::int64_t>(1, clock_100ns() - epoch - pending.timestampTicks);
            queue->push(std::move(pending));
        }
      } catch (...) {
        set_last_error(L"Recording stopped because capture resources could not be allocated.");
        state_ = RecorderState::Failed;
      }
        audio_.stop();
        queue->close();
    });

    start_recording_worker(queue_);
}

void RecorderSession::stop() {
    stopRequested_ = true;
    if ((captureThread_.joinable() || recordingThread_.joinable()) && state_.load() != RecorderState::Failed) {
        state_ = RecorderState::Stopping;
    }
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    audio_.stop();
    if (queue_) queue_->close();
    if (recordingThread_.joinable()) {
        recordingThread_.join();
    }
    if (state_.load() == RecorderState::Stopping) {
        state_ = RecorderState::Completed;
    }
    queue_.reset();
}

std::wstring RecorderSession::last_error() const {
    std::lock_guard lock(errorMutex_);
    return lastError_;
}

void RecorderSession::set_last_error(std::wstring error) {
    std::lock_guard lock(errorMutex_);
    lastError_ = std::move(error);
}

} // namespace gifler::record
