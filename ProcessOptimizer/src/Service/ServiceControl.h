#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

namespace Service
{
    inline constexpr wchar_t ServiceName[] =
        L"ProcessOptimizerService";
    inline constexpr wchar_t ServiceDisplayName[] =
        L"ProcessOptimizer Monitoring Service";
    inline constexpr wchar_t ServiceDescription[] =
        L"Recopila CPU, RAM, GPU y procesos para ProcessOptimizer.";

    bool IsInstalled();

    // Devuelve SERVICE_STOPPED / SERVICE_RUNNING / etc., o 0 si no
    // se puede consultar.
    std::uint32_t QueryStatus();

    bool Start();
    bool Stop();

    // Codigo de error de la ultima accion Start/Stop/Install/Uninstall.
    DWORD LastError();

    bool Install(const std::wstring& binaryPath, bool startAfter);
    bool Uninstall();

    // Reaplica la DACL al servicio. El propio servicio (LocalSystem) la
    // llama al arrancar para reparar instalaciones anteriores.
    bool RepairPermissions();
}
