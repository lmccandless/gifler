#pragma once
#include "gifler_core/Geometry.h"
#include <Windows.h>

namespace gifler::win32 {
class ResizeOverlay {
public:
    ResizeOverlay() = default;
    ~ResizeOverlay();
    ResizeOverlay(const ResizeOverlay&) = delete;
    ResizeOverlay& operator=(const ResizeOverlay&) = delete;
    bool create(HWND owner);
    void update(core::PixelRect screenRect, int band, int corner, bool visible);
    void close();
    [[nodiscard]] HWND handle() const { return hwnd_; }
    [[nodiscard]] static int hit(core::PixelPoint point, int width, int height, int band, int corner);
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp);
    void render(HDC print = nullptr);
    void hover(int hit);
    HWND hwnd_ = nullptr;
    HWND marks_ = nullptr;
    HWND owner_ = nullptr;
    core::PixelRect rect_{};
    int band_ = 12;
    int corner_ = 24;
    int hot_ = HTNOWHERE;
};
}
