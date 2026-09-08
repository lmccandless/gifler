#pragma once
#include "gifler_core/Geometry.h"
#include <array>
#include <cmath>

namespace gifler::core {
inline constexpr std::array<PixelSize, 7> CaptureAspectRatios{{
    {0, 0}, {16, 9}, {9, 16}, {1, 1}, {4, 5}, {4, 3}, {3, 4}
}};

// Values match the native sizing directions, without exposing Win32 to core code.
enum class ResizeSide { Left = 1, Right, Top, TopLeft, TopRight, Bottom, BottomLeft, BottomRight };

[[nodiscard]] inline PixelSize aspect_window_size(PixelSize proposed, PixelSize ratio,
    PixelSize chrome, PixelSize minimum, ResizeSide side, bool fitInside = false) {
    if (ratio.empty()) return proposed;
    const int w = std::max(1, proposed.width - chrome.width);
    const int h = std::max(1, proposed.height - chrome.height);
    const int minUnits = std::max({1,
        (std::max(1, minimum.width - chrome.width) + ratio.width - 1) / ratio.width,
        (std::max(1, minimum.height - chrome.height) + ratio.height - 1) / ratio.height});
    double units = 0;
    if (fitInside) units = std::floor(std::min(static_cast<double>(w) / ratio.width, static_cast<double>(h) / ratio.height));
    else if (side == ResizeSide::Left || side == ResizeSide::Right) units = static_cast<double>(w) / ratio.width;
    else if (side == ResizeSide::Top || side == ResizeSide::Bottom) units = static_cast<double>(h) / ratio.height;
    else units = (static_cast<double>(w) * ratio.width + static_cast<double>(h) * ratio.height) /
                 (ratio.width * ratio.width + ratio.height * ratio.height);
    const int count = std::max(minUnits, static_cast<int>(std::round(units)));
    return {count * ratio.width + chrome.width, count * ratio.height + chrome.height};
}

[[nodiscard]] inline PixelRect constrain_capture_aspect(PixelRect proposed, PixelSize ratio,
    PixelSize chrome, PixelSize minimum, ResizeSide side) {
    if (ratio.empty()) return proposed;
    const auto size = aspect_window_size({proposed.width, proposed.height}, ratio, chrome, minimum, side);
    const bool left = side == ResizeSide::Left || side == ResizeSide::TopLeft || side == ResizeSide::BottomLeft;
    const bool top = side == ResizeSide::Top || side == ResizeSide::TopLeft || side == ResizeSide::TopRight;
    const bool verticalEdge = side == ResizeSide::Top || side == ResizeSide::Bottom;
    const bool horizontalEdge = side == ResizeSide::Left || side == ResizeSide::Right;
    return {left ? proposed.right() - size.width : proposed.x + (verticalEdge ? (proposed.width - size.width) / 2 : 0),
        top ? proposed.bottom() - size.height : proposed.y + (horizontalEdge ? (proposed.height - size.height) / 2 : 0),
        size.width, size.height};
}
}
