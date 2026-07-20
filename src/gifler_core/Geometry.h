#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace gifler::core {

struct PixelPoint {
    int x = 0;
    int y = 0;
};

struct PixelSize {
    int width = 0;
    int height = 0;

    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
};

struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] constexpr int left() const noexcept { return x; }
    [[nodiscard]] constexpr int top() const noexcept { return y; }
    [[nodiscard]] constexpr int right() const noexcept { return x + width; }
    [[nodiscard]] constexpr int bottom() const noexcept { return y + height; }
    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
};

[[nodiscard]] constexpr bool operator==(const PixelPoint& a, const PixelPoint& b) noexcept {
    return a.x == b.x && a.y == b.y;
}

[[nodiscard]] constexpr bool operator==(const PixelSize& a, const PixelSize& b) noexcept {
    return a.width == b.width && a.height == b.height;
}

[[nodiscard]] constexpr bool operator==(const PixelRect& a, const PixelRect& b) noexcept {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

[[nodiscard]] constexpr bool contains(const PixelRect& r, PixelPoint p) noexcept {
    return p.x >= r.left() && p.x < r.right() && p.y >= r.top() && p.y < r.bottom();
}

[[nodiscard]] constexpr bool intersects(const PixelRect& a, const PixelRect& b) noexcept {
    return !a.empty() && !b.empty() && a.left() < b.right() && b.left() < a.right() && a.top() < b.bottom() &&
           b.top() < a.bottom();
}

[[nodiscard]] constexpr PixelRect intersection(const PixelRect& a, const PixelRect& b) noexcept {
    const int l = (a.left() > b.left()) ? a.left() : b.left();
    const int t = (a.top() > b.top()) ? a.top() : b.top();
    const int r = (a.right() < b.right()) ? a.right() : b.right();
    const int bt = (a.bottom() < b.bottom()) ? a.bottom() : b.bottom();
    if (r <= l || bt <= t) {
        return {};
    }
    return {l, t, r - l, bt - t};
}

[[nodiscard]] constexpr PixelRect offset(const PixelRect& r, int dx, int dy) noexcept {
    return {r.x + dx, r.y + dy, r.width, r.height};
}

[[nodiscard]] inline int scale_for_dpi(int value, unsigned dpi) noexcept {
    return static_cast<int>((static_cast<std::int64_t>(value) * static_cast<std::int64_t>(dpi) + 48) / 96);
}

[[nodiscard]] inline PixelRect scale_rect_for_dpi(const PixelRect& value, unsigned dpi) noexcept {
    return {scale_for_dpi(value.x, dpi), scale_for_dpi(value.y, dpi), scale_for_dpi(value.width, dpi),
            scale_for_dpi(value.height, dpi)};
}

[[nodiscard]] inline PixelRect union_bounds(const PixelRect& a, const PixelRect& b) noexcept {
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    const int l = std::min(a.left(), b.left());
    const int t = std::min(a.top(), b.top());
    const int r = std::max(a.right(), b.right());
    const int bt = std::max(a.bottom(), b.bottom());
    return {l, t, r - l, bt - t};
}

} // namespace gifler::core
