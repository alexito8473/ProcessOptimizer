#include "ServiceControl.h"
#include <aclapi.h>
#include <sddl.h>
#include <winsvc.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace Service
{
    namespace
    {
        std::atomic<DWORD> lastError{ 0 };

        void RememberError(DWORD error)
        {
            lastError.store(error, std::memory_order_release);
        }

        SC_HANDLE OpenManager(DWORD access)
        {
            return OpenSCManagerW(
                nullptr, nullptr, access);
        }

        SC_HANDLE OpenServiceHandle(DWORD access)
        {
            SC_HANDLE manager =
                OpenManager(SC_MANAGER_CONNECT);

            if (manager == nullptr)
                return nullptr;

            SC_HANDLE service = OpenServiceW(
                manager, ServiceName, access);

            CloseServiceHandle(manager);
            return service;
        }

        bool WaitForState(
            SC_HANDLE service,
            DWORD target,
            std::uint32_t timeoutMs)
        {
            using namespace std::chrono;

            const auto deadline =
                steady_clock::now() +
                milliseconds(timeoutMs);

            while (steady_clock::now() < deadline)
            {
                SERVICE_STATUS status{};
                if (!QueryServiceStatus(service, &status))
                    return false;

                if (status.dwCurrentState == target)
                    return true;

                std::this_thread::sleep_for(
                    milliseconds(100));
            }

            RememberError(ERROR_SERVICE_REQUEST_TIMEOUT);
            return false;
        }

        bool GrantUserControl(SC_HANDLE service)
        {
            DWORD needed = 0;
            QueryServiceObjectSecurity(
                service,
                DACL_SECURITY_INFORMATION,
                nullptr,
                0,
                &needed);

            if (needed == 0)
                return false;

            std::vector<std::uint8_t> buffer(needed);
            auto* descriptor =
                reinterpret_cast<PSECURITY_DESCRIPTOR>(
                    buffer.data());

            if (!QueryServiceObjectSecurity(
                    service,
                    DACL_SECURITY_INFORMATION,
                    descriptor,
                    needed,
                    &needed))
            {
                return false;
            }

            BOOL present = FALSE;
            BOOL defaulted = FALSE;
            PACL oldAcl = nullptr;

            if (!GetSecurityDescriptorDacl(
                    descriptor, &present, &oldAcl,
                    &defaulted))
            {
                return false;
            }

            constexpr DWORD controlRights =
                READ_CONTROL |
                SERVICE_QUERY_STATUS |
                SERVICE_INTERROGATE |
                SERVICE_START |
                SERVICE_STOP |
                SERVICE_USER_DEFINED_CONTROL;

            EXPLICIT_ACCESSW entries[5]{};
            PSID sids[5]{};

            const auto addWellKnown =
                [&](std::size_t index,
                    WELL_KNOWN_SID_TYPE type,
                    DWORD rights)
                {
                    DWORD size = SECURITY_MAX_SID_SIZE;
                    sids[index] = LocalAlloc(LPTR, size);

                    if (sids[index] == nullptr ||
                        !CreateWellKnownSid(
                            type, nullptr,
                            sids[index], &size))
                    {
                        return false;
                    }

                    EXPLICIT_ACCESSW& entry =
                        entries[index];

                    entry.grfAccessPermissions = rights;
                    entry.grfAccessMode = SET_ACCESS;
                    entry.grfInheritance = NO_INHERITANCE;
                    entry.Trustee.TrusteeForm =
                        TRUSTEE_IS_SID;
                    entry.Trustee.TrusteeType =
                        TRUSTEE_IS_GROUP;
                    entry.Trustee.ptstrName =
                        reinterpret_cast<LPWSTR>(
                            sids[index]);
                    return true;
                };

            bool sidsReady = true;

            // Cualquier usuario autenticado/interactivo debe poder
            // arrancar y parar el servicio sin elevar con UAC.
            sidsReady &= addWellKnown(
                0, WinAuthenticatedUserSid,
                controlRights);
            sidsReady &= addWellKnown(
                1, WinBuiltinUsersSid,
                controlRights);
            sidsReady &= addWellKnown(
                2, WinInteractiveSid,
                controlRights);

            sidsReady &= addWellKnown(
                3, WinBuiltinAdministratorsSid,
                SERVICE_ALL_ACCESS);
            sidsReady &= addWellKnown(
                4, WinLocalSystemSid,
                SERVICE_ALL_ACCESS);

            if (!sidsReady)
            {
                const DWORD error = GetLastError();
                for (PSID sid : sids)
                    if (sid != nullptr)
                        LocalFree(sid);
                RememberError(error);
                return false;
            }

            PACL newAcl = nullptr;
            const DWORD result = SetEntriesInAclW(
                5, entries,
                present ? oldAcl : nullptr, &newAcl);

            for (PSID sid : sids)
                if (sid != nullptr)
                    LocalFree(sid);

            if (result != ERROR_SUCCESS)
            {
                RememberError(result);
                return false;
            }

            if (!SetSecurityDescriptorDacl(
                    descriptor, TRUE, newAcl, FALSE))
            {
                const DWORD error = GetLastError();
                LocalFree(newAcl);
                RememberError(error);
                return false;
            }

            const bool ok = SetServiceObjectSecurity(
                service,
                DACL_SECURITY_INFORMATION,
                descriptor);

            if (!ok)
                RememberError(GetLastError());

            LocalFree(newAcl);
            return ok;
        }
    }

    bool IsInstalled()
    {
        SC_HANDLE service =
            OpenServiceHandle(SERVICE_QUERY_STATUS);

        if (service == nullptr)
            return false;

        CloseServiceHandle(service);
        return true;
    }

    std::uint32_t QueryStatus()
    {
        SC_HANDLE service =
            OpenServiceHandle(SERVICE_QUERY_STATUS);

        if (service == nullptr)
            return 0;

        SERVICE_STATUS status{};
        const bool ok =
            QueryServiceStatus(service, &status);

        CloseServiceHandle(service);
        return ok ? status.dwCurrentState : 0;
    }

    bool Start()
    {
        lastError.store(0, std::memory_order_release);

        SC_HANDLE service = OpenServiceHandle(
            SERVICE_START | SERVICE_QUERY_STATUS);

        if (service == nullptr)
        {
            RememberError(GetLastError());
            return false;
        }

        const bool requested =
            StartServiceW(service, 0, nullptr) != FALSE;
        const DWORD startError =
            requested ? 0 : GetLastError();

        const bool ok =
            requested ||
            startError == ERROR_SERVICE_ALREADY_RUNNING;

        if (ok)
        {
            if (!WaitForState(
                    service, SERVICE_RUNNING, 5000))
            {
                CloseServiceHandle(service);
                return false;
            }
        }
        else
        {
            RememberError(startError);
        }

        CloseServiceHandle(service);
        return ok;
    }

    bool Stop()
    {
        lastError.store(0, std::memory_order_release);

        SC_HANDLE service = OpenServiceHandle(
            SERVICE_STOP | SERVICE_QUERY_STATUS);

        if (service == nullptr)
        {
            RememberError(GetLastError());
            return false;
        }

        SERVICE_STATUS status{};
        const BOOL requested = ControlService(
            service, SERVICE_CONTROL_STOP, &status);
        const DWORD stopError =
            requested != FALSE ? 0 : GetLastError();

        const bool ok =
            requested != FALSE ||
            stopError == ERROR_SERVICE_NOT_ACTIVE;

        if (!ok)
            RememberError(stopError);
        else if (!WaitForState(
                     service, SERVICE_STOPPED, 5000))
        {
            CloseServiceHandle(service);
            return false;
        }

        CloseServiceHandle(service);
        return ok;
    }

    DWORD LastError()
    {
        return lastError.load(std::memory_order_acquire);
    }

    bool RepairPermissions()
    {
        lastError.store(0, std::memory_order_release);

        SC_HANDLE service = OpenServiceHandle(
            WRITE_DAC | READ_CONTROL);

        if (service == nullptr)
        {
            RememberError(GetLastError());
            return false;
        }

        const bool ok = GrantUserControl(service);

        if (!ok && lastError.load(
                       std::memory_order_acquire) == 0)
        {
            RememberError(GetLastError());
        }

        CloseServiceHandle(service);
        return ok;
    }

    bool Install(
        const std::wstring& binaryPath,
        bool startAfter)
    {
        lastError.store(0, std::memory_order_release);

        SC_HANDLE manager =
            OpenManager(SC_MANAGER_ALL_ACCESS);

        if (manager == nullptr)
        {
            RememberError(GetLastError());
            return false;
        }

        SC_HANDLE service = OpenServiceW(
            manager, ServiceName,
            SERVICE_ALL_ACCESS);

        if (service == nullptr)
        {
            service = CreateServiceW(
                manager,
                ServiceName,
                ServiceDisplayName,
                SERVICE_ALL_ACCESS,
                SERVICE_WIN32_OWN_PROCESS,
                SERVICE_AUTO_START,
                SERVICE_ERROR_NORMAL,
                binaryPath.c_str(),
                nullptr,
                nullptr,
                nullptr,
                nullptr,   // LocalSystem
                nullptr);  // sin contrasena
        }
        else
        {
            ChangeServiceConfigW(
                service,
                SERVICE_WIN32_OWN_PROCESS,
                SERVICE_AUTO_START,
                SERVICE_ERROR_NORMAL,
                binaryPath.c_str(),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr);
        }

        if (service == nullptr)
        {
            RememberError(GetLastError());
            CloseServiceHandle(manager);
            return false;
        }

        SERVICE_DESCRIPTIONW description{};
        description.lpDescription =
            const_cast<wchar_t*>(ServiceDescription);
        ChangeServiceConfig2W(
            service,
            SERVICE_CONFIG_DESCRIPTION,
            &description);

        // Usuarios interactivos pueden arrancar/parar el servicio
        // desde la UI sin pedir elevacion de UAC.
        if (!GrantUserControl(service))
        {
            wchar_t message[160]{};
            swprintf_s(
                message,
                L"[Service] No se pudo ajustar la DACL (error %lu).\n",
                LastError());
            OutputDebugStringW(message);
        }

        bool ok = true;
        if (startAfter)
        {
            if (StartServiceW(service, 0, nullptr) == FALSE &&
                GetLastError() !=
                    ERROR_SERVICE_ALREADY_RUNNING)
            {
                ok = false;
            }
            else
            {
                WaitForState(
                    service, SERVICE_RUNNING, 5000);
            }
        }

        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return ok;
    }

    bool Uninstall()
    {
        SC_HANDLE service =
            OpenServiceHandle(DELETE);

        if (service == nullptr)
            return false;

        SERVICE_STATUS status{};
        if (QueryServiceStatus(service, &status) &&
            status.dwCurrentState != SERVICE_STOPPED)
        {
            ControlService(
                service, SERVICE_CONTROL_STOP, &status);
            WaitForState(
                service, SERVICE_STOPPED, 5000);
        }

        const bool ok = DeleteService(service) != FALSE;
        CloseServiceHandle(service);
        return ok;
    }
}
