
#include "MemoryMonitor.h"
#include <Windows.h>
#include <Psapi.h>

#pragma comment(lib, "Psapi.lib")
#include <Wbemidl.h>
#include <comdef.h>
#include <cstdint>
#include <string>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
namespace
{
    template<typename T>
    void SafeRelease(T*& ptr)
    {
        if (ptr)
        {
            ptr->Release();
            ptr = nullptr;
        }
    }

    std::wstring GetWmiString(
        IWbemClassObject* object,
        const wchar_t* property
    )
    {
        VARIANT value;
        VariantInit(&value);

        std::wstring result;

        if (SUCCEEDED(object->Get(property, 0, &value, nullptr, nullptr)))
        {
            if (value.vt == VT_BSTR && value.bstrVal)
                result = value.bstrVal;
        }

        VariantClear(&value);
        return result;
    }

    std::uint64_t GetWmiUInt64(
        IWbemClassObject* object,
        const wchar_t* property
    )
    {
        VARIANT value;
        VARIANT converted;
        VariantInit(&value);
        VariantInit(&converted);

        std::uint64_t result = 0;

        if (SUCCEEDED(object->Get(property, 0, &value, nullptr, nullptr)))
        {
            if (SUCCEEDED(VariantChangeType(
                &converted, &value, 0, VT_UI8)))
            {
                result = converted.ullVal;
            }
        }

        VariantClear(&converted);
        VariantClear(&value);

        return result;
    }

    std::uint32_t GetWmiUInt32(
        IWbemClassObject* object,
        const wchar_t* property
    )
    {
        VARIANT value;
        VARIANT converted;
        VariantInit(&value);
        VariantInit(&converted);

        std::uint32_t result = 0;

        if (SUCCEEDED(object->Get(property, 0, &value, nullptr, nullptr)))
        {
            if (SUCCEEDED(VariantChangeType(
                &converted, &value, 0, VT_UI4)))
            {
                result = converted.ulVal;
            }
        }

        VariantClear(&converted);
        VariantClear(&value);

        return result;
    }
}
MemoryMonitor::~MemoryMonitor()
{
    Shutdown();
}

bool MemoryMonitor::Initialize()
{
    if (initialized_)
        return true;

    snapshot_ = MemorySnapshot{};

    // Primera recopilación de estadísticas.
    const bool physicalMemoryUpdated = UpdatePhysicalMemory();
    const bool performanceInfoUpdated = UpdatePerformanceInfo();

    if (!physicalMemoryUpdated || !performanceInfoUpdated)
    {
        Shutdown();
        return false;
    }
    UpdatePhysicalMemory();
    UpdatePerformanceInfo();

    DiscoverMemorySpeed();
    DiscoverMemoryModules();
    initialized_ = true;

    lastModuleRefresh_ = std::chrono::steady_clock::now();

    return true;
}
bool MemoryMonitor::DiscoverMemoryModules()
{
    snapshot_.modules.clear();
    snapshot_.totalSlots = 0;
    snapshot_.usedSlots = 0;
    snapshot_.moduleInformationAvailable = false;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    const bool mustUninitialize = SUCCEEDED(hr);

    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return false;

    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;
    IEnumWbemClassObject* enumerator = nullptr;

    bool success = false;

    do
    {
        hr = CoCreateInstance(
            CLSID_WbemLocator,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IWbemLocator,
            reinterpret_cast<void**>(&locator)
        );

        if (FAILED(hr))
            break;

        BSTR namespaceName = SysAllocString(L"ROOT\\CIMV2");

        if (!namespaceName)
            break;

        hr = locator->ConnectServer(
            namespaceName,
            nullptr,
            nullptr,
            nullptr,
            0,
            nullptr,
            nullptr,
            &services
        );

        SysFreeString(namespaceName);

        if (FAILED(hr))
            break;

        hr = CoSetProxyBlanket(
            services,
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE,
            nullptr,
            RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE
        );

        if (FAILED(hr))
            break;

        // Obtener módulos físicos instalados.
        BSTR queryLanguage = SysAllocString(L"WQL");
        BSTR query = SysAllocString(
            L"SELECT Capacity, Speed, ConfiguredClockSpeed, "
            L"SMBIOSMemoryType, DeviceLocator, BankLabel, "
            L"Manufacturer, PartNumber, SerialNumber "
            L"FROM Win32_PhysicalMemory"
        );

        if (!queryLanguage || !query)
        {
            if (queryLanguage) SysFreeString(queryLanguage);
            if (query) SysFreeString(query);
            break;
        }

        hr = services->ExecQuery(
            queryLanguage,
            query,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr,
            &enumerator
        );

        SysFreeString(queryLanguage);
        SysFreeString(query);

        if (FAILED(hr))
            break;

        IWbemClassObject* object = nullptr;
        ULONG returned = 0;

        while (enumerator->Next(
            WBEM_INFINITE,
            1,
            &object,
            &returned
        ) == S_OK)
        {
            MemoryModuleInfo module{};

            module.capacityBytes = GetWmiUInt64(
                object, L"Capacity");

            module.speed = GetWmiUInt32(
                object, L"Speed");

            module.configuredSpeed = GetWmiUInt32(
                object, L"ConfiguredClockSpeed");

            module.memoryType = static_cast<std::uint16_t>(
                GetWmiUInt32(object, L"SMBIOSMemoryType"));

            module.deviceLocator = GetWmiString(
                object, L"DeviceLocator");

            module.bankLabel = GetWmiString(
                object, L"BankLabel");

            module.manufacturer = GetWmiString(
                object, L"Manufacturer");

            module.partNumber = GetWmiString(
                object, L"PartNumber");

            module.serialNumber = GetWmiString(
                object, L"SerialNumber");

            module.isPopulated = module.capacityBytes > 0;

            if (module.isPopulated)
                ++snapshot_.usedSlots;

            snapshot_.modules.push_back(std::move(module));

            SafeRelease(object);
        }

        SafeRelease(object);
        SafeRelease(enumerator);

        // Obtener el número total de ranuras físicas.
        queryLanguage = SysAllocString(L"WQL");
        query = SysAllocString(
            L"SELECT MemoryDevices FROM Win32_PhysicalMemoryArray"
        );

        if (!queryLanguage || !query)
        {
            if (queryLanguage) SysFreeString(queryLanguage);
            if (query) SysFreeString(query);
            break;
        }

        hr = services->ExecQuery(
            queryLanguage,
            query,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr,
            &enumerator
        );

        SysFreeString(queryLanguage);
        SysFreeString(query);

        if (FAILED(hr))
            break;

        while (enumerator->Next(
            WBEM_INFINITE,
            1,
            &object,
            &returned
        ) == S_OK)
        {
            snapshot_.totalSlots += GetWmiUInt32(
                object, L"MemoryDevices");

            SafeRelease(object);
        }

        SafeRelease(object);
        SafeRelease(enumerator);

        // Si el firmware no informa de las ranuras,
        // utilizamos el número de módulos detectados.
        if (snapshot_.totalSlots == 0)
        {
            snapshot_.totalSlots =
                static_cast<std::uint32_t>(snapshot_.modules.size());
        }

        snapshot_.moduleInformationAvailable = true;
        success = true;

    } while (false);

    SafeRelease(enumerator);
    SafeRelease(services);
    SafeRelease(locator);

    if (mustUninitialize)
        CoUninitialize();

    return success;
}
bool MemoryMonitor::DiscoverMemorySpeed()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool mustUninitialize = SUCCEEDED(hr);

    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return false;

    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;
    IEnumWbemClassObject* enumerator = nullptr;

    bool success = false;

    do
    {
        hr = CoCreateInstance(
            CLSID_WbemLocator,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IWbemLocator,
            reinterpret_cast<void**>(&locator)
        );

        if (FAILED(hr))
            break;

        hr = locator->ConnectServer(
            _bstr_t(L"ROOT\\CIMV2"),
            nullptr, nullptr, nullptr,
            0, nullptr, nullptr,
            &services
        );

        if (FAILED(hr))
            break;

        hr = CoSetProxyBlanket(
            services,
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE,
            nullptr,
            RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE
        );

        if (FAILED(hr))
            break;

        hr = services->ExecQuery(
            _bstr_t(L"WQL"),
            _bstr_t(
                L"SELECT Speed, ConfiguredClockSpeed "
                L"FROM Win32_PhysicalMemory"
            ),
            WBEM_FLAG_FORWARD_ONLY |
            WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr,
            &enumerator
        );

        if (FAILED(hr))
            break;

        std::uint32_t fastestSpeed = 0;
        std::uint32_t configuredSpeed = 0;

        IWbemClassObject* object = nullptr;
        ULONG returned = 0;

        while (enumerator->Next(
            WBEM_INFINITE,
            1,
            &object,
            &returned
        ) == S_OK)
        {
            std::uint32_t speed =
                GetWmiUInt32(object, L"Speed");

            std::uint32_t configured =
                GetWmiUInt32(object, L"ConfiguredClockSpeed");

            if (speed > fastestSpeed)
                fastestSpeed = speed;

            if (configured > configuredSpeed)
                configuredSpeed = configured;

            SafeRelease(object);
        }

        SafeRelease(object);

        if (fastestSpeed > 0 || configuredSpeed > 0)
        {
            for (auto& module : snapshot_.modules)
            {
                if (module.speed == 0)
                    module.speed = fastestSpeed;

                if (module.configuredSpeed == 0)
                    module.configuredSpeed = configuredSpeed;
            }
        }

        success = true;

    } while (false);

    SafeRelease(enumerator);
    SafeRelease(services);
    SafeRelease(locator);

    if (mustUninitialize)
        CoUninitialize();

    return success;
}
bool MemoryMonitor::Update()
{
    if (!initialized_)
        return false;

    const bool physicalMemoryUpdated = UpdatePhysicalMemory();
    const bool performanceInfoUpdated = UpdatePerformanceInfo();

    if (!physicalMemoryUpdated || !performanceInfoUpdated)
        return false;

    // La información de los módulos se actualizará
    // por separado para no consultar WMI continuamente.
    const auto now = std::chrono::steady_clock::now();

    if (now - lastModuleRefresh_ >= moduleRefreshInterval_)
    {
        DiscoverMemoryModules();
        lastModuleRefresh_ = now;
    }

    return true;
}

void MemoryMonitor::Shutdown()
{
    initialized_ = false;

    snapshot_ = MemorySnapshot{};

    lastModuleRefresh_ = {};
}

bool MemoryMonitor::IsInitialized() const
{
    return initialized_;
}

const MemorySnapshot& MemoryMonitor::GetSnapshot() const
{
    return snapshot_;
}

bool MemoryMonitor::UpdatePhysicalMemory()
{
    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(MEMORYSTATUSEX);

    if (!GlobalMemoryStatusEx(&memoryStatus))
        return false;

    snapshot_.totalPhysicalBytes =
        memoryStatus.ullTotalPhys;

    snapshot_.availablePhysicalBytes =
        memoryStatus.ullAvailPhys;

    snapshot_.usedPhysicalBytes =
        snapshot_.totalPhysicalBytes -
        snapshot_.availablePhysicalBytes;

    if (snapshot_.totalPhysicalBytes > 0)
    {
        snapshot_.usagePercent =
            (static_cast<double>(snapshot_.usedPhysicalBytes) /
                static_cast<double>(snapshot_.totalPhysicalBytes)) * 100.0;
    }
    else
    {
        snapshot_.usagePercent = 0.0;
    }

    return true;
}

bool MemoryMonitor::UpdatePerformanceInfo()
{
    PERFORMANCE_INFORMATION performanceInfo{};
    performanceInfo.cb = sizeof(PERFORMANCE_INFORMATION);

    if (!GetPerformanceInfo(&performanceInfo, sizeof(PERFORMANCE_INFORMATION)))
        return false;

    const std::uint64_t pageSize =
        static_cast<std::uint64_t>(performanceInfo.PageSize);

    // Memoria confirmada.
    snapshot_.committedBytes =
        static_cast<std::uint64_t>(performanceInfo.CommitTotal) * pageSize;

    snapshot_.commitLimitBytes =
        static_cast<std::uint64_t>(performanceInfo.CommitLimit) * pageSize;

    // Memoria en caché.
    snapshot_.cachedBytes =
        static_cast<std::uint64_t>(performanceInfo.SystemCache) * pageSize;

    // Pools del sistema.
    snapshot_.pagedPoolBytes =
        static_cast<std::uint64_t>(performanceInfo.KernelPaged) * pageSize;

    snapshot_.nonPagedPoolBytes =
        static_cast<std::uint64_t>(performanceInfo.KernelNonpaged) * pageSize;

    return true;
}
