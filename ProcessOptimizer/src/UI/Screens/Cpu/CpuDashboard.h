
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include "../../../Monitoring/CpuMonitor/CpuInfo.h"
#include <atomic>
#include <mutex>
static constexpr std::size_t HistorySize = 300;

// Historial individual de cada procesador lógico.
// Lo utiliza Window para almacenar muestras y CpuDashboard para dibujarlas.
struct ProcessorChartHistory
{

    WORD group = 0;
    BYTE logicalIndex = 0;

    std::array<float, HistorySize> values{};

    std::size_t historyIndex = 0;
    std::size_t historyCount = 0;
};
struct SharedState
{
    std::mutex mutex;

    CpuSnapshot snapshot;
    bool hasSnapshot = false;

    HWND hwnd = nullptr;

    // Historial circular de CPU.


    std::array<float, HistorySize> cpuHistory{};

    std::size_t historyIndex = 0;
    std::size_t historyCount = 0;

    // Indica si ya hay una notificación pendiente.
    std::atomic<bool> updatePending{ false };
    std::vector<ProcessorChartHistory> processorHistories;
};
namespace UI::Dashboard::Cpu
{
    class CpuDashboard
    {
    public:
        static void Render(
            const CpuSnapshot& snapshot,
            std::span<const float> cpuHistory,
            std::size_t cpuHistoryIndex,
            std::size_t cpuHistoryCount,
            std::span<const ProcessorChartHistory> processorHistories,
            bool& showProcessorCharts
        );
    };
}
