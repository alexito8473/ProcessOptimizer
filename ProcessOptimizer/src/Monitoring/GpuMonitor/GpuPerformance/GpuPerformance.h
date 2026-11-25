
#pragma once

#include "../GpuInfo.h"
#include <Windows.h>
#include <Pdh.h>

#include <chrono>
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

    std::chrono::steady_clock::time_point lastRebuild_{};
    int consecutiveReadFailures_ = 0;

    // Los contadores de GPU Engine pueden no existir todavia cuando el
    // servicio arranca (sesion 0). RebuildQuery permite reintentarlo sin
    // duplicar contadores en la consulta PDH.
    void RebuildQuery();

    bool EnumerateCounters();

    static GpuEngineType ParseEngineType(
        const std::wstring& name
    );
};
