#include "gifler_capture_dxgi/DxgiCaptureProvider.h"
#include "gifler_win32/WicImageWriter.h"
#include "gifler_win32/Win32Util.h"

#include <Windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    gifler::capture_dxgi::DxgiCaptureProvider capture;
    std::wstring error;

    auto monitors = capture.enumerate_monitors(&error);
    if (!error.empty()) {
        std::wcerr << L"Monitor enumeration warning: " << error << L"\n";
    }
    std::wcout << L"DXGI outputs:\n";
    for (const auto& m : monitors) {
        std::wcout << L"  " << m.deviceName << L" rect=" << m.desktopRect.x << L"," << m.desktopRect.y << L" "
                   << m.desktopRect.width << L"x" << m.desktopRect.height
                   << (m.attachedToDesktop ? L" attached" : L" detached") << L"\n";
    }

    gifler::core::PixelRect rect{};
    std::filesystem::path output = L"s02_capture.png";
    if (argc >= 6) {
        rect.x = _wtoi(argv[1]);
        rect.y = _wtoi(argv[2]);
        rect.width = _wtoi(argv[3]);
        rect.height = _wtoi(argv[4]);
        output = argv[5];
    } else {
        auto first = std::find_if(monitors.begin(), monitors.end(), [](const auto& m) { return m.attachedToDesktop && !m.desktopRect.empty(); });
        if (first == monitors.end()) {
            std::wcerr << L"No attached DXGI output found.\n";
            return 2;
        }
        rect = {first->desktopRect.x + 80, first->desktopRect.y + 80, 480, 270};
    }

    std::wcout << L"Capturing physical rect " << rect.x << L"," << rect.y << L" " << rect.width << L"x" << rect.height
               << L" -> " << output.wstring() << L"\n";

    gifler::core::BgraFrame frame;
    error.clear();
    if (!capture.capture_one_frame(rect, frame, &error)) {
        std::wcerr << L"Capture failed: " << error << L"\n";
        return 3;
    }

    error.clear();
    if (!gifler::win32::save_bgra_frame_as_png(frame, output, &error)) {
        std::wcerr << L"PNG write failed: " << error << L"\n";
        return 4;
    }

    std::wcout << L"Wrote " << output.wstring() << L"\n";
    return 0;
}
