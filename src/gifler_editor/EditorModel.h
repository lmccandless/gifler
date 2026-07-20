#pragma once

#include <cstddef>
#include <vector>

namespace gifler::editor {

class EditorModel {
public:
    explicit EditorModel(std::size_t frameCount = 0) { reset(frameCount); }

    void reset(std::size_t frameCount) {
        frameCount_ = frameCount;
        deleted_.assign(frameCount, false);
        trimStart_ = 0;
        trimEnd_ = frameCount == 0 ? 0 : frameCount - 1;
    }

    [[nodiscard]] std::size_t frame_count() const noexcept { return frameCount_; }
    [[nodiscard]] std::size_t trim_start() const noexcept { return trimStart_; }
    [[nodiscard]] std::size_t trim_end() const noexcept { return trimEnd_; }

    void set_trim_start(std::size_t index) {
        if (frameCount_ == 0) {
            return;
        }
        trimStart_ = index >= frameCount_ ? frameCount_ - 1 : index;
        if (trimStart_ > trimEnd_) {
            trimEnd_ = trimStart_;
        }
    }

    void set_trim_end(std::size_t index) {
        if (frameCount_ == 0) {
            return;
        }
        trimEnd_ = index >= frameCount_ ? frameCount_ - 1 : index;
        if (trimEnd_ < trimStart_) {
            trimStart_ = trimEnd_;
        }
    }

    void delete_frame(std::size_t index) {
        if (index < deleted_.size()) {
            deleted_[index] = true;
        }
    }

    void delete_to_start(std::size_t index) {
        if (frameCount_ == 0) {
            return;
        }
        const auto last = index >= frameCount_ ? frameCount_ - 1 : index;
        for (std::size_t i = 0; i <= last; ++i) {
            deleted_[i] = true;
        }
    }

    void delete_to_end(std::size_t index) {
        if (frameCount_ == 0) {
            return;
        }
        const auto first = index >= frameCount_ ? frameCount_ - 1 : index;
        for (std::size_t i = first; i < frameCount_; ++i) {
            deleted_[i] = true;
        }
    }

    void delete_even_frames() {
        for (std::size_t i = 0; i < frameCount_; i += 2) {
            deleted_[i] = true;
        }
    }

    [[nodiscard]] bool deleted(std::size_t index) const noexcept {
        return index < deleted_.size() && deleted_[index];
    }

    [[nodiscard]] bool included(std::size_t index) const noexcept {
        return index < frameCount_ && index >= trimStart_ && index <= trimEnd_ && !deleted(index);
    }

    [[nodiscard]] std::vector<std::size_t> included_indices() const {
        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < frameCount_; ++i) {
            if (included(i)) {
                indices.push_back(i);
            }
        }
        return indices;
    }

private:
    std::size_t frameCount_ = 0;
    std::size_t trimStart_ = 0;
    std::size_t trimEnd_ = 0;
    std::vector<bool> deleted_{};
};

} // namespace gifler::editor
