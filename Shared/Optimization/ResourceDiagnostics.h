#pragma once

#include "OptimizationTypes.h"
#include <Windows.h>

#include <cstdint>
#include <mutex>
#include <string>

namespace Optimization
{
    // Solo lectura. Ninguna de estas tecnicas se aplica desde fuera al juego:
    // large pages, NUMA, compresion de memoria, SysMain, BypassIO, HAGS o
    // afinidad de IRQ requieren permisos globales, reinicio o el handle del
    // archivo, y pueden empeorar la estabilidad.
    class ResourceDiagnostics
    {
    public:
        ResourceDiagnostics() = default;

        bool Refresh(const TopologySnapshot& topology);
        DiagnosticsSnapshot Snapshot() const;

    private:
        static bool HasLargePagePrivilege();
        static bool MemoryCompressionEnabled();
        static bool SysMainEnabled();
        static bool BypassIOLikely(std::string& note);
        static std::uint32_t HagsMode();

        mutable std::mutex mutex_;
        DiagnosticsSnapshot snapshot_;
    };
}
