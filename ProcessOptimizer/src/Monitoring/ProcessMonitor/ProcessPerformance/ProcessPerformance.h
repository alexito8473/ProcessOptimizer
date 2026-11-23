
#pragma once

#include "../ProcessInfo.h"

#include <Windows.h>

#include <unordered_map>

class ProcessPerformance
{
public:
    void Update(ProcessSnapshot& snapshot);

private:
    struct ProcessCpuSample
    {
        std::uint64_t lastKernelTime = 0;
        std::uint64_t lastUserTime = 0;
    };

    std::unordered_map<DWORD, ProcessCpuSample> m_previousSamples;

    std::uint64_t m_lastSystemTotalTime = 0;
};
