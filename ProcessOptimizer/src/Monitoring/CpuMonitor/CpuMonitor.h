
#pragma once

#include "CpuInfo.h"

#include <Windows.h>
#include <Pdh.h>

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <chrono>
class CpuMonitor
{
public:
    CpuMonitor() = default;
    ~CpuMonitor();

    CpuMonitor(const CpuMonitor&) = delete;
    CpuMonitor& operator=(const CpuMonitor&) = delete;

    bool Initialize();
    bool Update();
    void Shutdown();

    bool IsInitialized() const;
    const CpuSnapshot& GetSnapshot() const;

private:
    // Información del hardware
    bool DiscoverProcessorInfo();
    bool DiscoverTopology();
    bool DiscoverCacheInfo();
    bool DiscoverVirtualization();

    // Frecuencias
    bool UpdateFrequencyInfo();
    bool UpdatePerformanceCounter();
    bool UpdateMaximumFrequencyCounter();
    // Actividad de CPU
    bool UpdateCpuUsage();
    bool UpdateSystemActivity();

    // Procesos y subprocesos
    bool UpdateProcessInformation();

    // Contadores PDH
    bool InitializeCounters();

    // Identificación de procesadores
    bool ParseProcessorInstance(
        const wchar_t* name,
        WORD& group,
        BYTE& processor
    ) const;

    static std::uint32_t MakeProcessorKey(
        WORD group,
        BYTE processor
    );

    CpuSnapshot snapshot_;

    bool initialized_ = false;

    // Recursos PDH
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER counter_ = nullptr;
    PDH_HCOUNTER frequencyCounter_ = nullptr;
    PDH_HCOUNTER performanceCounter_ = nullptr;
    PDH_HCOUNTER maximumFrequencyCounter_ = nullptr;
    // Índice rápido de procesadores lógicos
    std::unordered_map<std::uint32_t, std::size_t>
        processorLookup_;

    // Búfer reutilizable de PDH
    std::vector<BYTE> formattedBuffer_;
    std::unordered_map<std::uint32_t, PDH_HCOUNTER> processorPerformanceCounters_;
    std::chrono::steady_clock::time_point nextActivityUpdate_{};
    std::chrono::steady_clock::time_point nextProcessUpdate_{};
};
