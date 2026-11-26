
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
    // Evita que Windows escale la ventana por DPI: en pantallas completas
    // esa virtualizacion deformaba la UI.
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Inno Setup cierra la UI durante una actualizacion si encuentra este
    // mutex; sin el, el EXE puede quedar bloqueado y la instalacion fallar.
    HANDLE installerMutex = CreateMutexW(
        nullptr, TRUE, L"ProcessOptimizerSetup");
    (void)installerMutex;

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
