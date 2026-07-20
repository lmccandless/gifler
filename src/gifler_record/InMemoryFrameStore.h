#pragma once

#include "gifler_core/Frame.h"

#include <cstdint>
#include <vector>

namespace gifler::record {

class InMemoryFrameStore {
public:
    void clear() { frames_.clear(); }
    void add(gifler::core::BgraFrame frame) { frames_.push_back(std::move(frame)); }

    [[nodiscard]] const std::vector<gifler::core::BgraFrame>& frames() const noexcept { return frames_; }
    [[nodiscard]] std::vector<gifler::core::BgraFrame>& frames() noexcept { return frames_; }
    [[nodiscard]] std::size_t frame_count() const noexcept { return frames_.size(); }
    [[nodiscard]] std::int64_t total_duration_ticks() const noexcept {
        std::int64_t total = 0;
        for (const auto& frame : frames_) {
            total += frame.durationTicks;
        }
        return total;
    }
    [[nodiscard]] std::size_t total_pixel_bytes() const noexcept {
        std::size_t total = 0;
        for (const auto& frame : frames_) {
            total += frame.pixels.size();
        }
        return total;
    }

private:
    std::vector<gifler::core::BgraFrame> frames_{};
};

} // namespace gifler::record
