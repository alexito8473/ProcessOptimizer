
#pragma once

#include <array>
#include <cstddef>

#include "../../../Monitoring/GpuMonitor/GpuInfo.h"

namespace UI::Dashboard::Gpu
{
    inline constexpr std::size_t GpuHistorySize = 120;

    struct GpuAdapterHistory
    {
        std::uint32_t luidLowPart = 0;
        std::int32_t luidHighPart = 0;

        std::array<float, GpuHistorySize> usage{};
        std::array<float, GpuHistorySize> usage3D{};
        std::array<float, GpuHistorySize> usageCopy{};
        std::array<float, GpuHistorySize> usageCompute{};
        std::array<float, GpuHistorySize> usageVideoDecode{};
        std::array<float, GpuHistorySize> usageVideoEncode{};
        std::array<float, GpuHistorySize> usageVideoProcessing{};
        std::array<float, GpuHistorySize> dedicatedMemory{};
        std::array<float, GpuHistorySize> sharedMemory{};

        std::size_t historyIndex = 0;
        std::size_t historyCount = 0;
    };

    struct GpuDashboardHistory
    {
        std::vector<GpuAdapterHistory> adapters;
    };

    class GpuDashboard
    {
    public:
        static void Render(
            const GpuSnapshot& snapshot,
            const GpuDashboardHistory& history
        );
    };
}
