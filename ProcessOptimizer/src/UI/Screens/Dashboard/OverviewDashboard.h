#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Monitoring/CpuMonitor/CpuInfo.h"
#include "Monitoring/GpuMonitor/GpuInfo.h"
#include "Monitoring/MemoryMonitor/MemoryInfo.h"
#include "Monitoring/ProcessMonitor/ProcessInfo.h"
#include "../../Localization/Localization.h"
#include "../../Theme/Theme.h"

namespace UI::Dashboard::Overview
{
    inline constexpr std::size_t HistorySize = 120;

    // Serie de uso de un adaptador GPU. Se identifica por LUID para que no
    // se mezclen muestras si cambia el orden de los adaptadores.
    struct GpuSeries
    {
        std::uint32_t luidLowPart = 0;
        std::int32_t luidHighPart = 0;
        std::string name;

        std::array<float, HistorySize> usage{};
    };

    // Todas las series comparten índice temporal: cada muestra contiene
    // CPU, RAM y el uso de cada GPU presente en ese instante.
    struct CombinedHistory
    {
        std::array<float, HistorySize> cpu{};
        std::array<float, HistorySize> memory{};

        std::vector<GpuSeries> gpus;

        std::size_t index = 0;
        std::size_t count = 0;
    };

    class OverviewDashboard
    {
    public:
        static void Render(
            const CpuSnapshot* cpu,
            const MemorySnapshot* memory,
            const GpuSnapshot* gpu,
            const ProcessSnapshot* processes,
            const CombinedHistory& history,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language);
    };
}
