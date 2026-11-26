#include "TrayApp.h"

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int)
{
    // El instalador usa AppMutex=ProcessOptimizerSetup para cerrar el Tray
    // antes de reemplazar los binarios.
    HANDLE installerMutex = CreateMutexW(
        nullptr, TRUE, L"ProcessOptimizerSetup");
    (void)installerMutex;

    Tray::TrayApp app;
    return app.Run(instance) ? 0 : 1;
}
