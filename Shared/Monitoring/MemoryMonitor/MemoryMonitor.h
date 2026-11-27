
#pragma once

#include "MemoryInfo.h"
#include <chrono>

class MemoryMonitor
{
public:
    MemoryMonitor() = default;
    ~MemoryMonitor();

    // Evitamos copiar el monitor.
    MemoryMonitor(const MemoryMonitor&) = delete;
    MemoryMonitor& operator=(const MemoryMonitor&) = delete;

    // Inicialización y ciclo de actualización.
    bool Initialize();
    bool Update();
    void Shutdown();

    // Estado del monitor.
    bool IsInitialized() const;

    // Acceso a la última información recopilada.
    const MemorySnapshot& GetSnapshot() const;

private:

    // Recopilación de información.
    bool UpdatePhysicalMemory();
    bool UpdatePerformanceInfo();

    // Información de los módulos físicos mediante WMI.
    bool DiscoverMemoryModules();
    bool DiscoverMemorySpeed();

    MemorySnapshot snapshot_;

    bool initialized_ = false;

    // Controla cuándo volver a consultar los módulos físicos.
    std::chrono::steady_clock::time_point lastModuleRefresh_{};

    std::chrono::seconds moduleRefreshInterval_{ 30 };


};
