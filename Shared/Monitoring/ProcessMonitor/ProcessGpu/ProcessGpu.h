
#pragma once

#include <Windows.h>
#include <Pdh.h>

#include <string>
#include <vector>

struct ProcessGpuEngineUsage
{
    DWORD processId = 0;

    DWORD luidLow = 0;
    DWORD luidHigh = 0;

    DWORD physicalEngine = 0;
    DWORD engineIndex = 0;

    std::wstring engineName;

    double usagePercent = 0.0;
};

class ProcessGpu
{
public:
    ProcessGpu() = default;
    ~ProcessGpu();

    ProcessGpu(const ProcessGpu&) = delete;
    ProcessGpu& operator=(const ProcessGpu&) = delete;

    bool Initialize();
    bool Update();
    void Shutdown();

    bool IsInitialized() const;

    const std::vector<ProcessGpuEngineUsage>& GetUsage() const;

private:
    struct CounterEntry
    {
        PDH_HCOUNTER counter = nullptr;

        DWORD processId = 0;
        DWORD luidLow = 0;
        DWORD luidHigh = 0;

        DWORD physicalEngine = 0;
        DWORD engineIndex = 0;

        std::wstring engineName;
    };

    PDH_HQUERY query_ = nullptr;

    std::vector<CounterEntry> counters_;
    std::vector<ProcessGpuEngineUsage> usage_;

    bool initialized_ = false;
    bool firstSample_ = true;

    bool EnumerateCounters();
};
