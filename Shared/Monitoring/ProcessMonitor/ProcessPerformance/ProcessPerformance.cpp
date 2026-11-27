
#include "ProcessPerformance.h"
#include <thread>
#include <Psapi.h>

#include <algorithm>
#include <cstdint>

#pragma comment(lib, "Psapi.lib")

namespace
{
    std::uint64_t FileTimeToUInt64(const FILETIME& time)
    {
        ULARGE_INTEGER value{};
        value.LowPart = time.dwLowDateTime;
        value.HighPart = time.dwHighDateTime;
        return value.QuadPart;
    }

    std::uint64_t GetSystemTotalTime()
    {
        FILETIME idleTime{};
        FILETIME kernelTime{};
        FILETIME userTime{};

        if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
            return 0;

        return FileTimeToUInt64(kernelTime) +
            FileTimeToUInt64(userTime);
    }
}

void ProcessPerformance::Update(ProcessSnapshot& snapshot)
{
    const std::uint64_t systemTotalTime = GetSystemTotalTime();

    const std::uint64_t systemDelta =
        systemTotalTime >= m_lastSystemTotalTime
        ? systemTotalTime - m_lastSystemTotalTime
        : 0;

    for (ProcessInfo& process : snapshot.processes)
    {
        HANDLE handle = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
            FALSE,
            process.processId
        );

        if (!handle)
            continue;

        // CPU usage
        FILETIME creationTime{};
        FILETIME exitTime{};
        FILETIME kernelTime{};
        FILETIME userTime{};

        if (GetProcessTimes(
            handle,
            &creationTime,
            &exitTime,
            &kernelTime,
            &userTime))
        {
            const std::uint64_t currentKernel =
                FileTimeToUInt64(kernelTime);

            const std::uint64_t currentUser =
                FileTimeToUInt64(userTime);

            process.cpuTime100ns = currentKernel + currentUser;
            process.startTime100ns = FileTimeToUInt64(creationTime);

            auto& previous = m_previousSamples[process.processId];

            if (previous.lastKernelTime != 0 &&
                previous.lastUserTime != 0 &&
                systemDelta > 0)
            {
                const std::uint64_t processDelta =
                    (currentKernel - previous.lastKernelTime) +
                    (currentUser - previous.lastUserTime);

                const double cpu =
                    static_cast<double>(processDelta) /
                    static_cast<double>(systemDelta) * 100.0;

                const unsigned int logicalProcessors =
                    (std::thread::hardware_concurrency() == 0)
                    ? 1u
                    : std::thread::hardware_concurrency();

                process.cpuUsagePercent =
                    std::clamp(cpu * logicalProcessors, 0.0, 100.0);
            }

            previous.lastKernelTime = currentKernel;
            previous.lastUserTime = currentUser;
        }

        // Memory usage
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);

        if (GetProcessMemoryInfo(
            handle,
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
            sizeof(memory)))
        {
            process.workingSetBytes =
                memory.WorkingSetSize;

            process.privateMemoryBytes =
                memory.PrivateUsage;

            process.peakWorkingSetBytes =
                memory.PeakWorkingSetSize;
        }

        CloseHandle(handle);
    }

    m_lastSystemTotalTime = systemTotalTime;
}
