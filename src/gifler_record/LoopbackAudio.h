#pragma once
#include "gifler_core/Audio.h"
#include <atomic>
#include <thread>
#include <string>

namespace gifler::record {
class LoopbackAudio {
public:
    ~LoopbackAudio() { stop(); }
    void start(std::int64_t epoch100ns);
    void stop();
    void clear() { stop(); recording_ = {}; error_.clear(); }
    const gifler::core::AudioRecording& recording() const { return recording_; }
    const std::wstring& error() const { return error_; } // Read only after stop.
private:
    std::thread worker_;
    std::atomic_bool stopping_ = false;
    gifler::core::AudioRecording recording_;
    std::wstring error_;
};
std::int64_t clock_100ns();
}
