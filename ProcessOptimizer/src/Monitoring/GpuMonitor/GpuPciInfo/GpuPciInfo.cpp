
#include "GpuPciInfo.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <SetupAPI.h>
#include <devguid.h>

#include <vector>
#include <cwchar>
#include <cstdint>

#pragma comment(lib, "Setupapi.lib")

namespace
{
    bool GetDwordProperty(
        HDEVINFO deviceInfoSet,
        SP_DEVINFO_DATA& deviceInfo,
        DWORD property,
        DWORD& value)
    {
        DWORD propertyType = 0;
        DWORD requiredSize = 0;

        if (!SetupDiGetDeviceRegistryPropertyW(
            deviceInfoSet,
            &deviceInfo,
            property,
            &propertyType,
            reinterpret_cast<PBYTE>(&value),
            sizeof(value),
            &requiredSize))
        {
            return false;
        }

        return propertyType == REG_DWORD &&
            requiredSize == sizeof(DWORD);
    }

    bool MatchesAdapter(
        HDEVINFO deviceInfoSet,
        SP_DEVINFO_DATA& deviceInfo,
        const GpuAdapterInfo& adapter)
    {
        DWORD requiredSize = 0;
        DWORD propertyType = 0;

        SetupDiGetDeviceRegistryPropertyW(
            deviceInfoSet,
            &deviceInfo,
            SPDRP_HARDWAREID,
            &propertyType,
            nullptr,
            0,
            &requiredSize
        );

        if (requiredSize == 0)
            return false;

        std::vector<wchar_t> hardwareIds(
            requiredSize / sizeof(wchar_t) + 1,
            L'\0'
        );

        if (!SetupDiGetDeviceRegistryPropertyW(
            deviceInfoSet,
            &deviceInfo,
            SPDRP_HARDWAREID,
            &propertyType,
            reinterpret_cast<PBYTE>(hardwareIds.data()),
            requiredSize,
            nullptr))
        {
            return false;
        }

        wchar_t expectedId[64]{};

        swprintf_s(
            expectedId,
            L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X",
            adapter.vendorId,
            adapter.deviceId,
            adapter.subsystemId
        );

        const std::size_t expectedLength = wcslen(expectedId);

        for (const wchar_t* id = hardwareIds.data();
            *id != L'\0';
            id += wcslen(id) + 1)
        {
            if (_wcsnicmp(
                id,
                expectedId,
                expectedLength) == 0)
            {
                return true;
            }
        }

        return false;
    }

    bool ReadPciLocation(
        HDEVINFO deviceInfoSet,
        SP_DEVINFO_DATA& deviceInfo,
        GpuAdapterInfo& adapter)
    {
        DWORD busNumber = 0;
        DWORD address = 0;

        if (!GetDwordProperty(
            deviceInfoSet,
            deviceInfo,
            SPDRP_BUSNUMBER,
            busNumber))
        {
            return false;
        }

        if (!GetDwordProperty(
            deviceInfoSet,
            deviceInfo,
            SPDRP_ADDRESS,
            address))
        {
            return false;
        }

        // Dirección PCI: dispositivo en los 16 bits superiores
        // y función en los 16 bits inferiores.
        adapter.pciBus = busNumber;
        adapter.pciDevice = (address >> 16) & 0xFFFF;
        adapter.pciFunction = address & 0xFFFF;

        adapter.pciLocationAvailable = true;
        return true;
    }
}

void GpuPciInfo::Update(
    std::vector<GpuAdapterInfo>& adapters)
{
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(
        &GUID_DEVCLASS_DISPLAY,
        nullptr,
        nullptr,
        DIGCF_PRESENT
    );

    if (deviceInfoSet == INVALID_HANDLE_VALUE)
        return;

    for (GpuAdapterInfo& adapter : adapters)
    {
        adapter.pciBus = 0;
        adapter.pciDevice = 0;
        adapter.pciFunction = 0;
        adapter.pciLocationAvailable = false;

        for (DWORD index = 0;; ++index)
        {
            SP_DEVINFO_DATA deviceInfo{};
            deviceInfo.cbSize = sizeof(SP_DEVINFO_DATA);

            if (!SetupDiEnumDeviceInfo(
                deviceInfoSet,
                index,
                &deviceInfo))
            {
                break;
            }

            if (MatchesAdapter(
                deviceInfoSet,
                deviceInfo,
                adapter))
            {
                ReadPciLocation(
                    deviceInfoSet,
                    deviceInfo,
                    adapter
                );

                break;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
}
