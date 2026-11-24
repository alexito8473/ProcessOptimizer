
#include <Windows.h>

#include "src/UI/Windows/Window.h"
#include "src/Monitoring/SystemMonitor/SystemMonitor.h"
#include "src/Core/Events/EventSystem.h"

#include <iostream>

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    PWSTR,
    int nCmdShow)
{
    Window window;

    if (!window.Create(hInstance))
    {
        MessageBoxW(
            nullptr,
            L"No se pudo crear la ventana.",
            L"Process Optimizer",
            MB_OK | MB_ICONERROR
        );

        return 1;
    }

    ShowWindow(
        GetActiveWindow(),
        nCmdShow
    );

    window.Run();

    return 0;
}
