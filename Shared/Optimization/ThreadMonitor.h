#pragma once

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Optimization
{
    struct ThreadLoad
    {
        DWORD processId = 0;
        DWORD threadId = 0;
        // GetThreadTimes creation time:distinguye un TID reutilizado.
        std::uint64_t start100ns = 0;
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

        struct SampleKey
        {
            DWORD processId = 0;
            DWORD threadId = 0;
            std::uint64_t start100ns = 0;

            bool operator==(const SampleKey& other) const
            {
                return processId == other.processId &&
                       threadId == other.threadId &&
                       start100ns == other.start100ns;
            }
        };

        struct SampleKeyHash
        {
            std::size_t operator()(const SampleKey& key) const
            {
                return std::hash<std::uint64_t>()(
                    (static_cast<std::uint64_t>(key.processId)
                         << 32) ^
                    key.threadId ^
                    (key.start100ns ^
                     (key.start100ns >> 32)));
            }
        };

        std::unordered_map<
            SampleKey, PreviousSample, SampleKeyHash> previous_;
        std::uint64_t sequence_ = 0;
        bool hasLastSample_ = false;
        std::chrono::steady_clock::time_point lastSample_;
    };
}
