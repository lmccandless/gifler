#pragma once

#include "gifler_core/Frame.h"
#include "gifler_core/Geometry.h"
#include "gifler_record/BoundedFrameQueue.h"
#include "gifler_record/InMemoryFrameStore.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace gifler::record {

enum class RecorderState {
    Idle,
    Recording,
    Paused,
    Stopping,
    Completed,
    Failed
};

struct RecorderSettings {
    int fps = 10;
    std::size_t queueCapacity = 8;
    bool captureCursor = true;
};

class RecorderSession {
public:
    RecorderSession() = default;
    RecorderSession(const RecorderSession&) = delete;
    RecorderSession& operator=(const RecorderSession&) = delete;
    ~RecorderSession();

    void start_synthetic(RecorderSettings settings, int width, int height);
    void start_dxgi(RecorderSettings settings, gifler::core::PixelRect captureRect);
    void start_dxgi(RecorderSettings settings, std::function<gifler::core::PixelRect()> captureRectProvider);
    void stop();

    [[nodiscard]] RecorderState state() const noexcept { return state_.load(); }
    [[nodiscard]] const InMemoryFrameStore& frame_store() const noexcept { return store_; }
    [[nodiscard]] std::wstring last_error() const;

private:
    void prepare_start(RecorderSettings settings);
    void start_recording_worker(std::shared_ptr<BoundedFrameQueue<gifler::core::BgraFrame>> queue);
    void set_last_error(std::wstring error);

    std::atomic<RecorderState> state_ = RecorderState::Idle;
    std::atomic<bool> stopRequested_ = false;
    std::shared_ptr<BoundedFrameQueue<gifler::core::BgraFrame>> queue_{};
    std::thread captureThread_{};
    std::thread recordingThread_{};
    InMemoryFrameStore store_{};
    mutable std::mutex errorMutex_{};
    std::wstring lastError_{};
};

} // namespace gifler::record
