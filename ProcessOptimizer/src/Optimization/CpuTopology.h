#pragma once

#include "OptimizationTypes.h"

#include "../Monitoring/CpuMonitor/CpuInfo.h"

namespace Optimization
{
    // Lee la topologia real expuesta por Windows. No inventa clases P/E:
    // solo las usa cuando el sistema informa de mas de una EfficiencyClass.
    class CpuTopology
    {
    public:
        static TopologySnapshot Detect();

        static void RefreshUsage(
            TopologySnapshot& topology,
            const CpuSnapshot& snapshot);

        // Actualiza solo frecuencia real, limites y estado de aparcado.
        // Es una lectura barata (una llamada NtPowerInformation) y permite
        // mostrar y usar frecuencias vivas sin reconstruir la topologia.
        static void RefreshFrequencies(
            TopologySnapshot& topology);

        static const TopologyProcessor* FindProcessor(
            const TopologySnapshot& topology,
            WORD group,
            BYTE logicalIndex);
    };
}
