#include "gifler_editor/EditorWindow.h"

#include <Windows.h>

#include <cstddef>
#include <vector>

namespace {

gifler::core::BgraFrame make_frame(int width, int height, int index) {
    gifler::core::BgraFrame frame{};
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.durationTicks = 1'000'000;
    frame.pixels.resize(static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(height));
    auto* p = reinterpret_cast<unsigned char*>(frame.pixels.data());
    const int boxX = (index * 9) % width;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int offset = y * frame.stride + x * 4;
            const bool box = x >= boxX && x < boxX + 24 && y >= 12 && y < 42;
            p[offset + 0] = box ? 30 : 45;
            p[offset + 1] = box ? 170 : 45;
            p[offset + 2] = box ? 250 : 45;
            p[offset + 3] = 255;
        }
    }
    return frame;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::vector<gifler::core::BgraFrame> frames;
    for (int i = 0; i < 120; ++i) {
        frames.push_back(make_frame(160, 90, i));
    }
    gifler::editor::EditorWindow::show(nullptr, frames);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
