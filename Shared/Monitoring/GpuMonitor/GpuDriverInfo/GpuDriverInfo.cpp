
#include "GpuDriverInfo.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <SetupAPI.h>
#include <devguid.h>

#include <vector>
#include <string>
#include <cwchar>
#include <cstdint>

#pragma comment(lib, "Setupapi.lib")

namespace
{
    // Lee una propiedad de tipo cadena del registro del dispositivo.
    bool GetRegistryString(
        HKEY key,
        const wchar_t* valueName,
        std::wstring& value)
    {
        DWORD type = 0;
        DWORD size = 0;

        LONG result = RegQueryValueExW(
            key,
            valueName,
            nullptr,
            &type,
            nullptr,
            &size
        );

        if (result != ERROR_SUCCESS ||
            (type != REG_SZ && type != REG_EXPAND_SZ) ||
            size < sizeof(wchar_t))
        {
            return false;
        }

        std::vector<wchar_t> buffer(
            size / sizeof(wchar_t) + 1,
            L'\0'
        );

        result = RegQueryValueExW(
            key,
            valueName,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(buffer.data()),
            &size
        );

        if (result != ERROR_SUCCESS)
            return false;

        value = buffer.data();
        return !value.empty();
    }

    // Lee la fecha del controlador desde el registro.
    bool GetDriverDate(
        HKEY key,
        std::wstring& date)
    {
        FILETIME fileTime{};
        DWORD type = 0;
        DWORD size = sizeof(fileTime);

        LONG result = RegQueryValueExW(
            key,
            L"DriverDateData",
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(&fileTime),
            &size
        );

        // Algunos controladores almacenan la fecha en DriverDate.
        if (result != ERROR_SUCCESS ||
            type != REG_BINARY ||
            size != sizeof(FILETIME))
        {
            SYSTEMTIME systemTime{};

            size = sizeof(systemTime);
            type = 0;

            result = RegQueryValueExW(
                key,
                L"DriverDate",
                nullptr,
                &type,
                reinterpret_cast<LPBYTE>(&systemTime),
                &size
            );

            if (result == ERROR_SUCCESS &&
                type == REG_BINARY &&
                size == sizeof(SYSTEMTIME))
            {
                wchar_t buffer[16]{};

                swprintf_s(
                    buffer,
                    L"%04u-%02u-%02u",
                    systemTime.wYear,
                    systemTime.wMonth,
                    systemTime.wDay
                );

                date = buffer;
                return true;
            }

            return false;
        }

        SYSTEMTIME systemTime{};

        if (!FileTimeToSystemTime(&fileTime, &systemTime))
            return false;

        wchar_t buffer[16]{};

        swprintf_s(
            buffer,
            L"%04u-%02u-%02u",
            systemTime.wYear,
            systemTime.wMonth,
            systemTime.wDay
        );

        date = buffer;
        return true;
    }

    // Comprueba si el dispositivo PCI corresponde al adaptador DXGI.
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

    // Obtiene los datos del controlador instalado.
    bool ReadAdapterDriver(
        HDEVINFO deviceInfoSet,
        SP_DEVINFO_DATA& deviceInfo,
        GpuAdapterInfo& adapter)
    {
        HKEY driverKey = SetupDiOpenDevRegKey(
            deviceInfoSet,
            &deviceInfo,
            DICS_FLAG_GLOBAL,
            0,
            DIREG_DRV,
            KEY_QUERY_VALUE
        );

        if (driverKey == INVALID_HANDLE_VALUE)
            return false;

        bool found = false;

        found |= GetRegistryString(
            driverKey,
            L"ProviderName",
            adapter.driverProvider
        );

        found |= GetRegistryString(
            driverKey,
            L"DriverVersion",
            adapter.driverVersion
        );

        found |= GetDriverDate(
            driverKey,
            adapter.driverDate
        );

        RegCloseKey(driverKey);

        // Descripción y ubicación del dispositivo.
        wchar_t description[512]{};

        if (SetupDiGetDeviceRegistryPropertyW(
            deviceInfoSet,
            &deviceInfo,
            SPDRP_DEVICEDESC,
            nullptr,
            reinterpret_cast<PBYTE>(description),
            sizeof(description),
            nullptr))
        {
            adapter.driverDescription = description;
            found = true;
        }

        wchar_t location[512]{};

        if (SetupDiGetDeviceRegistryPropertyW(
            deviceInfoSet,
            &deviceInfo,
            SPDRP_LOCATION_INFORMATION,
            nullptr,
            reinterpret_cast<PBYTE>(location),
            sizeof(location),
            nullptr))
        {
            adapter.deviceLocation = location;
        }

        adapter.driverInfoAvailable = found;
        return found;
    }
}

void GpuDriverInfo::Update(
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
        adapter.driverProvider.clear();
        adapter.driverVersion.clear();
        adapter.driverDate.clear();
        adapter.driverDescription.clear();
        adapter.deviceLocation.clear();
        adapter.driverInfoAvailable = false;

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
                ReadAdapterDriver(
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