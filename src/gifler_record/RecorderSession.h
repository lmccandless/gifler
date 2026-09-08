#pragma once

#include "gifler_core/Frame.h"
#include "gifler_core/Geometry.h"
#include "gifler_record/BoundedFrameQueue.h"
#include "gifler_record/InMemoryFrameStore.h"
#include "gifler_record/LoopbackAudio.h"

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
    bool captureAudio = false;
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
    const gifler::core::AudioRecording& audio() const { return audio_.recording(); }
    const std::wstring& audio_error() const { return audio_.error(); }
    int recorded_fps() const { return recordedFps_; }
    std::size_t captured_frames() const { return capturedFrames_; }

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
    LoopbackAudio audio_;
    int recordedFps_ = 10;
    std::atomic<std::size_t> capturedFrames_ = 0;
};

} // namespace gifler::record
