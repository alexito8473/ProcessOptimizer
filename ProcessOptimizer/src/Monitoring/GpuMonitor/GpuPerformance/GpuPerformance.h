
#pragma once

#include "../GpuInfo.h"
#include <Windows.h>
#include <Pdh.h>

#include <string>
#include <vector>

class GpuPerformance
{
public:
    GpuPerformance() = default;
    ~GpuPerformance();

    GpuPerformance(const GpuPerformance&) = delete;
    GpuPerformance& operator=(const GpuPerformance&) = delete;

    bool Initialize();

    bool Update(std::vector<GpuAdapterInfo>& adapters);

    void Shutdown();

    bool IsInitialized() const;

private:
    struct CounterEntry
    {
        PDH_HCOUNTER counter = nullptr;

        DWORD luidLow = 0;
        DWORD luidHigh = 0;

        DWORD physicalEngine = 0;
        DWORD engineIndex = 0;

        GpuEngineType type = GpuEngineType::Unknown;
        std::wstring name;
    };

    PDH_HQUERY query_ = nullptr;

    std::vector<CounterEntry> counters_;

    bool initialized_ = false;
    bool firstSample_ = true;

    bool EnumerateCounters();

    static GpuEngineType ParseEngineType(
        const std::wstring& name
    );
};
