
#pragma once

#include "Monitoring/MemoryMonitor/MemoryInfo.h"
#include <cstddef>
#include <span>

namespace UI::Dashboard::Memory
{
    class MemoryDashboard
    {
    public:
        static void Render(
            const MemorySnapshot& snapshot,
            std::span<const float> memoryHistory,
            std::size_t historyIndex,
            std::size_t historyCount);
    };
}
