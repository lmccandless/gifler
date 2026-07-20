#include "gifler_capture_dxgi/DxgiCaptureProvider.h"
#include "gifler_core/Geometry.h"
#include "gifler_win32/WicImageWriter.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace {

gifler::core::PixelRect clamp_capture_rect(gifler::core::PixelPoint cursor, gifler::core::PixelRect monitor) {
    constexpr int width = 320;
    constexpr int height = 240;
    const int maxX = monitor.x + std::max(0, monitor.width - width);
    const int maxY = monitor.y + std::max(0, monitor.height - height);
    return {std::clamp(cursor.x - width / 2, monitor.x, maxX), std::clamp(cursor.y - height / 2, monitor.y, maxY),
            std::min(width, monitor.width), std::min(height, monitor.height)};
}

} // namespace

int wmain() {
    gifler::capture_dxgi::DxgiCaptureProvider capture;
    std::wstring error;
    auto monitors = capture.enumerate_monitors(&error);
    if (!error.empty()) {
        std::wcerr << L"Monitor enumeration warning: " << error << L"\n";
    }

    CURSORINFO cursorInfo{};
    cursorInfo.cbSize = sizeof(cursorInfo);
    if (!GetCursorInfo(&cursorInfo)) {
        std::wcerr << L"GetCursorInfo failed.\n";
        return 2;
    }

    const gifler::core::PixelPoint cursor{cursorInfo.ptScreenPos.x, cursorInfo.ptScreenPos.y};
    auto monitor = std::find_if(monitors.begin(), monitors.end(), [&](const auto& info) {
        return info.attachedToDesktop && gifler::core::contains(info.desktopRect, cursor);
    });
    if (monitor == monitors.end()) {
        monitor = std::find_if(monitors.begin(), monitors.end(), [](const auto& info) {
            return info.attachedToDesktop && !info.desktopRect.empty();
        });
    }
    if (monitor == monitors.end()) {
        std::wcerr << L"No attached DXGI output found.\n";
        return 3;
    }

    const auto rect = clamp_capture_rect(cursor, monitor->desktopRect);
    std::filesystem::create_directories(L"artifacts");
    const std::filesystem::path outputPath = L"artifacts/s03_cursor_overlay.png";

    gifler::core::BgraFrame frame;
    error.clear();
    if (!capture.capture_one_frame(rect, frame, &error, true)) {
        std::wcerr << L"Cursor capture failed: " << error << L"\n";
        return 4;
    }

    error.clear();
    if (!gifler::win32::save_bgra_frame_as_png(frame, outputPath, &error)) {
        std::wcerr << L"PNG write failed: " << error << L"\n";
        return 5;
    }

    std::wcout << L"S03 cursor overlay smoke\n";
    std::wcout << L"Captured rect: " << rect.x << L"," << rect.y << L" " << rect.width << L"x" << rect.height << L"\n";
    if (frame.cursor.has_value()) {
        const auto& cursorShape = *frame.cursor;
        std::wcout << L"Cursor visible: " << (cursorShape.visible ? L"yes" : L"no") << L"\n";
        std::wcout << L"Cursor position in frame: " << cursorShape.position.x << L"," << cursorShape.position.y << L"\n";
        std::wcout << L"Cursor hotspot: " << cursorShape.hotSpot.x << L"," << cursorShape.hotSpot.y << L"\n";
        std::wcout << L"Cursor size: " << cursorShape.size.width << L"x" << cursorShape.size.height << L"\n";
    } else {
        std::wcout << L"Cursor visible: no\n";
    }
    std::wcout << L"Wrote " << outputPath.wstring() << L"\n";
    return 0;
}
