#pragma once

#include "gifler_core/Frame.h"
#include "gifler_core/Geometry.h"

#include <string>
#include <vector>

namespace gifler::capture_dxgi {

struct MonitorInfo {
    std::wstring deviceName{};
    gifler::core::PixelRect desktopRect{};
    bool attachedToDesktop = false;
};

class DxgiCaptureProvider {
public:
    [[nodiscard]] bool is_available(std::wstring* error = nullptr) const;
    [[nodiscard]] std::vector<MonitorInfo> enumerate_monitors(std::wstring* error = nullptr) const;

    // Spike-grade helper: creates a fresh D3D11 device/duplication session, captures one BGRA frame, then tears down.
    // Product code should replace this with a long-lived duplication session.
    [[nodiscard]] bool capture_one_frame(gifler::core::PixelRect desktopRect, gifler::core::BgraFrame& output,
                                         std::wstring* error = nullptr, bool captureCursor = false) const;

    // Captures the composed desktop pixels that are visible through Gifler's layered viewfinder.
    [[nodiscard]] bool capture_composed_frame(gifler::core::PixelRect desktopRect, gifler::core::BgraFrame& output,
                                              std::wstring* error = nullptr, bool captureCursor = false) const;
};

} // namespace gifler::capture_dxgi
