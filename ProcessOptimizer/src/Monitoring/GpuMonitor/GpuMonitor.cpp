#include "GpuMonitor.h"

#include <dxgi1_2.h>
#include <wrl/client.h>

#include <utility>
#include "GpuDriverInfo/GpuDriverInfo.h"
#include "GpuCapabilities/GpuCapabilities.h"
#include "GpuPciInfo/GpuPciInfo.h"
#include "GpuWddmInfo/GpuWddmInfo.h"
#include "GpuMemory/GpuMemory.h"

using Microsoft::WRL::ComPtr;

GpuMonitor::~GpuMonitor()
{
    Shutdown();
}

bool GpuMonitor::Initialize()
{
    Shutdown();

    if (!EnumerateAdapters())
        return false;

    // La lectura de rendimiento es opcional.
    // Si PDH no está disponible, el resto del monitor sigue funcionando.
    performance_.Initialize();

    UpdateStaticInformation();

    lastStaticRefresh_ = std::chrono::steady_clock::now();

    initialized_ = true;

    return true;
}

bool GpuMonitor::EnumerateAdapters()
{
    snapshot_.gpus.clear();
    adapters_.clear();

    ComPtr<IDXGIFactory1> factory;

    HRESULT result = CreateDXGIFactory1(
        IID_PPV_ARGS(&factory)
    );

    if (FAILED(result))
        return false;

    for (UINT index = 0;; ++index)
    {
        ComPtr<IDXGIAdapter1> adapter;

        result = factory->EnumAdapters1(
            index,
            &adapter
        );

        if (result == DXGI_ERROR_NOT_FOUND)
            break;

        if (FAILED(result))
            return false;

        DXGI_ADAPTER_DESC1 description{};

        if (FAILED(adapter->GetDesc1(&description)))
            continue;

        if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        GpuAdapterInfo info{};

        info.index = static_cast<std::uint32_t>(
            snapshot_.gpus.size()
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

        info.isSoftware = false;

        // Guardamos el adaptador en el mismo orden que el snapshot.
        adapters_.push_back(adapter);

        snapshot_.gpus.push_back(std::move(info));
    }

    return true;
}

void GpuMonitor::UpdateStaticInformation()
{
    GpuDriverInfo::Update(snapshot_.gpus);

    GpuCapabilities::Update(snapshot_.gpus);

    GpuWddmInfo::Update(snapshot_.gpus);

    GpuPciInfo::Update(snapshot_.gpus);
}

bool GpuMonitor::Update()
{
    if (!initialized_)
        return false;

    bool success = true;

    // Actualizar memoria de cada adaptador.
    for (std::size_t i = 0; i < snapshot_.gpus.size(); ++i)
    {
        if (!GpuMemory::Update(
            adapters_[i],
            snapshot_.gpus[i]))
        {
            success = false;
        }
    }

    // Actualizar rendimiento mediante PDH. Se llama siempre para que
    // GpuPerformance pueda reenumerar contadores si no habia instancias
    // al arrancar; el propio Update marca performanceAvailable.
    performance_.Update(snapshot_.gpus);

    // Actualizar información que cambia con poca frecuencia.
    const auto now = std::chrono::steady_clock::now();

    if (now - lastStaticRefresh_ >= staticRefreshInterval_)
    {
        UpdateStaticInformation();
        lastStaticRefresh_ = now;
    }

    return success;
}

void GpuMonitor::Shutdown()
{
    performance_.Shutdown();

    adapters_.clear();
    snapshot_.gpus.clear();

    initialized_ = false;

    lastStaticRefresh_ = {};
}

bool GpuMonitor::IsInitialized() const
{
    return initialized_;
}

const GpuSnapshot& GpuMonitor::GetSnapshot() const
{
    return snapshot_;
}