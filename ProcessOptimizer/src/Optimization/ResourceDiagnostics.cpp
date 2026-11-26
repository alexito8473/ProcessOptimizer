#include "ResourceDiagnostics.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <vector>

namespace Optimization
{
    bool ResourceDiagnostics::Refresh(
        const TopologySnapshot& topology)
    {
        DiagnosticsSnapshot result{};
        result.available = true;

        ULONGLONG installedKb = 0;
        if (GetPhysicallyInstalledSystemMemory(&installedKb))
            result.physicalMemoryMb =
                static_cast<std::uint32_t>(installedKb / 1024ull);

        const SIZE_T largePageBytes = GetLargePageMinimum();
        result.largePageMinimumKb =
            static_cast<std::uint32_t>(largePageBytes / 1024ull);
        result.largePagesAvailable =
            largePageBytes != 0 && HasLargePagePrivilege();

        ULONG highestNode = 0;
        if (GetNumaHighestNodeNumber(&highestNode))
            result.numaNodes =
                static_cast<std::uint16_t>(highestNode + 1);

        result.memoryCompressionEnabled = MemoryCompressionEnabled();
        result.sysMainEnabled = SysMainEnabled();

        std::string note;
        result.bypassIoSupported = BypassIOLikely(note);

        std::set<std::uint16_t> l3Ids;
        std::uint32_t hotCacheKb = 0;
        for (const auto& cache : topology.caches)
        {
            if (cache.level != 3)
                continue;
            l3Ids.insert(cache.id);
            hotCacheKb = (std::max)(hotCacheKb, cache.sizeKB);
        }
        result.l3Domains = static_cast<std::uint32_t>(l3Ids.size());
        result.hotL3CacheKb = hotCacheKb;

        const std::uint32_t hags = HagsMode();
        char buffer[512]{};
        std::snprintf(
            buffer, sizeof(buffer),
            "HAGS=%u. Large pages y NUMA no se aplican al juego; "
            "compresion y SysMain son globales; BypassIO: %s.",
            hags, note.c_str());
        result.notes = buffer;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_ = result;
        }
        return true;
    }

    DiagnosticsSnapshot ResourceDiagnostics::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

    bool ResourceDiagnostics::HasLargePagePrivilege()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(
                GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;

        DWORD size = 0;
        GetTokenInformation(token, TokenPrivileges, nullptr, 0, &size);
        std::vector<std::uint8_t> buffer(size);
        bool enabled = false;
        if (GetTokenInformation(
                token, TokenPrivileges, buffer.data(), size, &size))
        {
            auto* privileges =
                reinterpret_cast<TOKEN_PRIVILEGES*>(buffer.data());
            for (DWORD i = 0; i < privileges->PrivilegeCount; ++i)
            {
                wchar_t name[64]{};
                DWORD nameSize = sizeof(name) / sizeof(name[0]);
                if (LookupPrivilegeNameW(
                        nullptr,
                        &privileges->Privileges[i].Luid,
                        name, &nameSize) &&
                    std::wcscmp(name, L"SeLockMemoryPrivilege") == 0)
                {
                    enabled =
                        (privileges->Privileges[i].Attributes &
                         SE_PRIVILEGE_ENABLED) != 0;
                    break;
                }
            }
        }

        CloseHandle(token);
        return enabled;
    }

    bool ResourceDiagnostics::MemoryCompressionEnabled()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(
                HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\"
                L"Memory Management",
                0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return true; // Valor por defecto en Windows 11.
        }

        DWORD disabled = 0;
        DWORD size = sizeof(disabled);
        const bool found =
            RegQueryValueExW(
                key, L"DisableMemoryCompression", nullptr, nullptr,
                reinterpret_cast<LPBYTE>(&disabled), &size) ==
            ERROR_SUCCESS;
        RegCloseKey(key);
        return !found || disabled == 0;
    }

    bool ResourceDiagnostics::SysMainEnabled()
    {
        SC_HANDLE manager = OpenSCManagerW(
            nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!manager)
            return false;

        SC_HANDLE service = OpenServiceW(
            manager, L"SysMain", SERVICE_QUERY_STATUS);
        if (!service)
        {
            CloseServiceHandle(manager);
            return false;
        }

        SERVICE_STATUS status{};
        const bool queried = QueryServiceStatus(service, &status);
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return queried && status.dwCurrentState == SERVICE_RUNNING;
    }

    bool ResourceDiagnostics::BypassIOLikely(std::string& note)
    {
        wchar_t fileSystem[80]{};
        DWORD flags = 0;
        const BOOL ok = GetVolumeInformationW(
            L"C:\\", nullptr, 0, nullptr, nullptr, &flags,
            fileSystem, sizeof(fileSystem) / sizeof(fileSystem[0]));
        if (!ok)
        {
            note = "no se pudo consultar el volumen";
            return false;
        }

        const bool ntfs = std::wcscmp(fileSystem, L"NTFS") == 0;
        note = ntfs
            ? "NTFS detectado; antivirus/filtros pueden denegarlo"
            : "requiere NVMe + NTFS; sistema detectado distinto";
        return ntfs;
    }

    std::uint32_t ResourceDiagnostics::HagsMode()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(
                HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
                0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return 0;
        }

        DWORD mode = 0;
        DWORD size = sizeof(mode);
        RegQueryValueExW(
            key, L"HwSchMode", nullptr, nullptr,
            reinterpret_cast<LPBYTE>(&mode), &size);
        RegCloseKey(key);
        return mode;
    }
}
