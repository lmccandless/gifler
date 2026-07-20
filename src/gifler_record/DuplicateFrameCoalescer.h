#pragma once

#include "gifler_core/FrameDiffer.h"
#include "gifler_core/Frame.h"

#include <optional>
#include <vector>

namespace gifler::record {

class DuplicateFrameCoalescer {
public:
    void push(gifler::core::BgraFrame frame) {
        if (!previous_) {
            frame.changedBounds = gifler::core::PixelRect{0, 0, frame.width, frame.height};
            previous_ = std::move(frame);
            return;
        }

        if (previous_->width != frame.width || previous_->height != frame.height || previous_->stride != frame.stride) {
            completed_.push_back(std::move(*previous_));
            frame.changedBounds = gifler::core::PixelRect{0, 0, frame.width, frame.height};
            previous_ = std::move(frame);
            return;
        }

        auto diff = gifler::core::diff_bgra_frames(*previous_, frame);
        if (diff.identical) {
            previous_->durationTicks += frame.durationTicks;
            return;
        }

        completed_.push_back(std::move(*previous_));
        frame.changedBounds = diff.changedBounds;
        previous_ = std::move(frame);
    }

    std::vector<gifler::core::BgraFrame> finish() {
        if (previous_) {
            completed_.push_back(std::move(*previous_));
            previous_.reset();
        }
        return std::move(completed_);
    }

private:
    std::optional<gifler::core::BgraFrame> previous_{};
    std::vector<gifler::core::BgraFrame> completed_{};
};

} // namespace gifler::record
