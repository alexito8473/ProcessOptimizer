
#include "GpuEnumerator.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

bool GpuEnumerator::Enumerate(
    std::vector<GpuAdapterInfo>& adapters
)
{
    adapters.clear();

    ComPtr<IDXGIFactory1> factory;

    HRESULT result = CreateDXGIFactory1(
        IID_PPV_ARGS(&factory)
    );

    if (FAILED(result))
        return false;

    for (UINT i = 0; ; ++i)
    {
        ComPtr<IDXGIAdapter1> adapter;

        result = factory->EnumAdapters1(
            i,
            &adapter
        );

        if (result == DXGI_ERROR_NOT_FOUND)
            break;

        if (FAILED(result))
            return false;

        DXGI_ADAPTER_DESC1 description{};

        result = adapter->GetDesc1(&description);

        if (FAILED(result))
            continue;

        GpuAdapterInfo info{};

        info.index = static_cast<std::uint32_t>(
            adapters.size()
            );

        info.name = description.Description;

        info.vendorId = description.VendorId;
        info.deviceId = description.DeviceId;
        info.subsystemId = description.SubSysId;
        info.revision = description.Revision;

        info.dedicatedVideoMemoryBytes =
            static_cast<std::uint64_t>(
                description.DedicatedVideoMemory
                );

        info.dedicatedSystemMemoryBytes =
            static_cast<std::uint64_t>(
                description.DedicatedSystemMemory
                );

        info.sharedSystemMemoryBytes =
            static_cast<std::uint64_t>(
                description.SharedSystemMemory
                );

        info.luidLowPart =
            description.AdapterLuid.LowPart;

        info.luidHighPart =
            description.AdapterLuid.HighPart;

        info.isSoftware =
            (description.Flags &
                DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

        // Excluimos adaptadores de software.
        if (info.isSoftware)
            continue;

        adapters.push_back(std::move(info));
    }

    return true;
}
