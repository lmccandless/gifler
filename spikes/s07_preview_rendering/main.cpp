#include <Windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    MessageBoxW(nullptr,
                L"S07 preview rendering placeholder. Use S01/MainWindow for region-switch validation; add Direct2D playback here.",
                L"S07 Preview Rendering", MB_OK | MB_ICONINFORMATION);
    return 0;
}
