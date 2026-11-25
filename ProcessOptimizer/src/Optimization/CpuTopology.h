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

        static const TopologyProcessor* FindProcessor(
            const TopologySnapshot& topology,
            WORD group,
            BYTE logicalIndex);
    };
}
