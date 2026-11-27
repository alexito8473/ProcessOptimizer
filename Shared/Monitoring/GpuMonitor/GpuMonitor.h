#pragma once

#include "GpuInfo.h"
#include <chrono>
#include <vector>
#include "GpuPerformance/GpuPerformance.h"
#include <dxgi1_2.h>
#include <wrl/client.h>
class GpuMonitor
{
public:
    GpuMonitor() = default;
    ~GpuMonitor();

    GpuMonitor(const GpuMonitor&) = delete;
    GpuMonitor& operator=(const GpuMonitor&) = delete;

    bool Initialize();
    bool Update();
    void Shutdown();

    bool IsInitialized() const;

    const GpuSnapshot& GetSnapshot() const;

private:
    GpuSnapshot snapshot_;

    std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> adapters_;

    GpuPerformance performance_;

    bool initialized_ = false;

    std::chrono::steady_clock::time_point lastStaticRefresh_{};
    std::chrono::seconds staticRefreshInterval_{ 30 };

    bool EnumerateAdapters();
    void UpdateStaticInformation();
};