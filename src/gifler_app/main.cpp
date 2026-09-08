#include "gifler_app/MainWindow.h"
#include "gifler_win32/Dpi.h"

#include <CommCtrl.h>
#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_UPDOWN_CLASS;
    InitCommonControlsEx(&controls);

    gifler::win32::enable_per_monitor_dpi_awareness_best_effort();
    gifler::app::MainWindow window;
    if (!window.create(instance, showCommand)) {
        MessageBoxW(nullptr, L"Could not create Gifler main window.", L"Gifler", MB_ICONERROR | MB_OK);
        return 1;
    }
    return window.run_message_loop();
}
