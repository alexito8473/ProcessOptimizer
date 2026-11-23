#pragma once

#include "ProcessInfo.h"
#include "ProcessPerformance/ProcessPerformance.h"
#include "ProcessGpu/ProcessGpu.h"

class ProcessMonitor
{
public:
    ProcessMonitor() = default;
    ~ProcessMonitor();

    bool Update();

    const ProcessSnapshot& GetSnapshot() const;

    void Shutdown();

private:
    ProcessPerformance m_performance;
    ProcessGpu m_processGpu;

    ProcessSnapshot m_snapshot;
};