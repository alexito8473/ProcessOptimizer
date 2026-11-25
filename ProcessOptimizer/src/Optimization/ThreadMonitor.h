#pragma once

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Optimization
{
    struct ThreadLoad
    {
        DWORD processId = 0;
        DWORD threadId = 0;
        std::uint64_t cpuTime100ns = 0;
        double usagePercent = 0.0;
    };

    // Muestreo ligero de hilos. Solo enumera los PIDs que el orquestador
    // ha marcado como candidatos, y calcula carga por diferencia de tiempos.
    class ThreadMonitor
    {
    public:
        std::vector<ThreadLoad> Sample(
            const std::unordered_set<DWORD>& processIds,
            std::size_t logicalProcessorCount);

    private:
        struct PreviousSample
        {
            std::uint64_t cpuTime100ns = 0;
            std::uint64_t sequence = 0;
        };

        std::unordered_map<std::uint64_t, PreviousSample> previous_;
        std::uint64_t sequence_ = 0;
        bool hasLastSample_ = false;
        std::chrono::steady_clock::time_point lastSample_;
    };
}
