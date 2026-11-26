#include "TrayApp.h"

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int)
{
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // El instalador usa AppMutex=ProcessOptimizerSetup para cerrar el Tray
    // antes de reemplazar los binarios.
    HANDLE installerMutex = CreateMutexW(
        nullptr, TRUE, L"ProcessOptimizerSetup");
    (void)installerMutex;

    Tray::TrayApp app;
    return app.Run(instance) ? 0 : 1;
}
