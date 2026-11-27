
#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <span>

#include "Monitoring/CpuMonitor/CpuInfo.h"
#include "../../Localization/Localization.h"
#include "../../Theme/Theme.h"

namespace UI::Dashboard::Cpu
{
    inline constexpr std::size_t HistorySize = 300;

    struct ProcessorChartHistory
    {
        WORD group = 0;
        BYTE logicalIndex = 0;

        std::array<float, HistorySize> values{};

        std::size_t historyIndex = 0;
        std::size_t historyCount = 0;
    };

    class CpuDashboard
    {
    public:
        static void Render(
            const CpuSnapshot& snapshot,
            std::span<const float> cpuHistory,
            std::size_t cpuHistoryIndex,
            std::size_t cpuHistoryCount,
            std::span<const ProcessorChartHistory> processorHistories,
            bool& showProcessorCharts,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language
        );
    };
}
