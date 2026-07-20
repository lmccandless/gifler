#pragma once

#include "gifler_core/Frame.h"

#include <optional>

namespace gifler::core {

struct FrameDiffResult {
    bool identical = false;
    std::optional<PixelRect> changedBounds{};
};

[[nodiscard]] FrameDiffResult diff_bgra_frames(const BgraFrame& previous, const BgraFrame& current);

} // namespace gifler::core
