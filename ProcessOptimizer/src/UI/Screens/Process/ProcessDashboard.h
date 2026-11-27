
#pragma once
#include "Monitoring/ProcessMonitor/ProcessInfo.h"


class ProcessDashboard
{
public:
    void Render(const ProcessSnapshot& snapshot);

private:
    char searchBuffer_[128] = {};

    void RenderHeader(const ProcessSnapshot& snapshot);
    void RenderProcessTable(const ProcessSnapshot& snapshot);

    static const char* GetPowerLevelText(ProcessPowerLevel level);
    static const char* GetPowerTrendText(ProcessPowerLevel level);

};
