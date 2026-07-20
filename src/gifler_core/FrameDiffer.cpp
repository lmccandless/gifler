#include "gifler_core/FrameDiffer.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace gifler::core {

FrameDiffResult diff_bgra_frames(const BgraFrame& previous, const BgraFrame& current) {
    if (previous.width != current.width || previous.height != current.height || previous.width <= 0 || previous.height <= 0 ||
        previous.stride < previous.width * 4 || current.stride < current.width * 4) {
        throw std::invalid_argument("diff_bgra_frames requires same-size non-empty BGRA frames");
    }

    int minX = current.width;
    int minY = current.height;
    int maxX = -1;
    int maxY = -1;

    const auto* prev = reinterpret_cast<const unsigned char*>(previous.pixels.data());
    const auto* cur = reinterpret_cast<const unsigned char*>(current.pixels.data());

    for (int y = 0; y < current.height; ++y) {
        const unsigned char* prevRow = prev + static_cast<std::size_t>(y) * static_cast<std::size_t>(previous.stride);
        const unsigned char* curRow = cur + static_cast<std::size_t>(y) * static_cast<std::size_t>(current.stride);

        if (std::memcmp(prevRow, curRow, static_cast<std::size_t>(current.width) * 4u) == 0) {
            continue;
        }

        for (int x = 0; x < current.width; ++x) {
            const int byteX = x * 4;
            if (prevRow[byteX + 0] != curRow[byteX + 0] || prevRow[byteX + 1] != curRow[byteX + 1] ||
                prevRow[byteX + 2] != curRow[byteX + 2] || prevRow[byteX + 3] != curRow[byteX + 3]) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (maxX < minX || maxY < minY) {
        return {true, std::nullopt};
    }

    return {false, PixelRect{minX, minY, maxX - minX + 1, maxY - minY + 1}};
}

} // namespace gifler::core
