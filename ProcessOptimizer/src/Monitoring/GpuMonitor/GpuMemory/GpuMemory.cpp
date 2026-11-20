
#include "GpuMemory.h"

#include <algorithm>

bool GpuMemory::Update(
    Microsoft::WRL::ComPtr<IDXGIAdapter1>& adapter,
    GpuAdapterInfo& info
)
{
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;

    HRESULT result = adapter.As(&adapter3);

    if (FAILED(result))
        return false;

    DXGI_QUERY_VIDEO_MEMORY_INFO localMemory{};

    result = adapter3->QueryVideoMemoryInfo(
        0,
        DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
        &localMemory
    );

    if (FAILED(result))
        return false;

    DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalMemory{};

    result = adapter3->QueryVideoMemoryInfo(
        0,
        DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
        &nonLocalMemory
    );

    if (FAILED(result))
        return false;

    // Memoria dedicada.
    info.processDedicatedMemoryUsedBytes =
        localMemory.CurrentUsage;

    info.processDedicatedMemoryBudgetBytes =
        localMemory.Budget;

    info.processDedicatedMemoryUsagePercent =
        localMemory.Budget > 0
        ? 100.0 * double(localMemory.CurrentUsage) /
        double(localMemory.Budget)
        : 0.0;

    info.processSharedMemoryUsedBytes =
        nonLocalMemory.CurrentUsage;

    info.processSharedMemoryBudgetBytes =
        nonLocalMemory.Budget;

    info.processSharedMemoryUsagePercent =
        nonLocalMemory.Budget > 0
        ? 100.0 * double(nonLocalMemory.CurrentUsage) /
        double(nonLocalMemory.Budget)
        : 0.0;

    return true;
}
