#include "TrayApp.h"

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int)
{
    Tray::TrayApp app;
    return app.Run(instance) ? 0 : 1;
}
