#pragma once

#include "gifler_core/Geometry.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gifler::core {

enum class PixelFormat {
    Bgra32
};

struct CursorShape {
    bool visible = false;
    PixelPoint position{};
    PixelPoint hotSpot{};
    PixelSize size{};
    std::vector<std::byte> bgraPixels{};
};

struct BgraFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::int64_t timestampTicks = 0;
    std::int64_t durationTicks = 0;
    std::vector<std::byte> pixels{};
    std::optional<PixelRect> changedBounds{};
    std::optional<CursorShape> cursor{};

    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0 || stride <= 0 || pixels.empty(); }
    [[nodiscard]] std::size_t expectedByteSize() const noexcept {
        if (width <= 0 || height <= 0 || stride <= 0) {
            return 0;
        }
        return static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
    }
};

} // namespace gifler::core
