#pragma once
#include "gifler_core/Frame.h"
#include <algorithm>
#include <vector>

namespace gifler::core {
// Round cumulative boundaries, so frame jitter cannot accumulate playback drift.
inline std::vector<std::size_t> frame_repeats(const std::vector<BgraFrame>& frames, int fps) {
    fps = std::clamp(fps, 1, 240);
    std::vector<std::size_t> repeats;
    std::int64_t ticks = 0;
    std::int64_t emitted = 0;
    for (const auto& frame : frames) {
        ticks += frame.durationTicks > 0 ? frame.durationTicks : 10'000'000LL / fps;
        const auto boundary = (ticks * fps + 5'000'000) / 10'000'000;
        repeats.push_back(static_cast<std::size_t>(std::max<std::int64_t>(0, boundary - emitted)));
        emitted = boundary;
    }
    if (!repeats.empty() && emitted == 0) repeats.back() = 1;
    return repeats;
}
}
