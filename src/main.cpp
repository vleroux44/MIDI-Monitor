#include <windows.h>

#include "MainWindow.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    MainWindow window(instance);
    if (!window.create(showCommand)) {
        MessageBoxW(nullptr, L"Impossible de créer la fenêtre principale.", L"MIDI Monitor",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
