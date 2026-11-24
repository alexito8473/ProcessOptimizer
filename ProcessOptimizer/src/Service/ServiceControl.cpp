#include "ServiceControl.h"

#include <aclapi.h>
#include <sddl.h>
#include <winsvc.h>

#include <chrono>
#include <thread>
#include <vector>

namespace Service
{
    namespace
    {
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
                SERVICE_QUERY_STATUS |
                SERVICE_INTERROGATE |
                SERVICE_START |
                SERVICE_STOP |
                SERVICE_USER_DEFINED_CONTROL;

            EXPLICIT_ACCESSW entries[2]{};

            entries[0].grfAccessPermissions = controlRights;
            entries[0].grfAccessMode = SET_ACCESS;
            entries[0].grfInheritance = NO_INHERITANCE;
            entries[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
            entries[0].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
            ConvertStringSidToSidW(
                L"AU",
                reinterpret_cast<PSID*>(
                    &entries[0].Trustee.ptstrName));

            entries[1].grfAccessPermissions =
                SERVICE_ALL_ACCESS;
            entries[1].grfAccessMode = SET_ACCESS;
            entries[1].grfInheritance = NO_INHERITANCE;
            entries[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
            entries[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
            ConvertStringSidToSidW(
                L"BA",
                reinterpret_cast<PSID*>(
                    &entries[1].Trustee.ptstrName));

            PACL newAcl = nullptr;
            const DWORD result = SetEntriesInAclW(
                2, entries,
                present ? oldAcl : nullptr, &newAcl);

            if (entries[0].Trustee.ptstrName != nullptr)
                LocalFree(entries[0].Trustee.ptstrName);
            if (entries[1].Trustee.ptstrName != nullptr)
                LocalFree(entries[1].Trustee.ptstrName);

            if (result != ERROR_SUCCESS)
                return false;

            if (!SetSecurityDescriptorDacl(
                    descriptor, TRUE, newAcl, FALSE))
            {
                LocalFree(newAcl);
                return false;
            }

            const bool ok = SetServiceObjectSecurity(
                service,
                DACL_SECURITY_INFORMATION,
                descriptor);

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
        SC_HANDLE service = OpenServiceHandle(
            SERVICE_START | SERVICE_QUERY_STATUS);

        if (service == nullptr)
            return false;

        const bool ok = StartServiceW(
                            service, 0, nullptr) != FALSE ||
                        GetLastError() ==
                            ERROR_SERVICE_ALREADY_RUNNING;

        if (ok)
            WaitForState(
                service, SERVICE_RUNNING, 5000);

        CloseServiceHandle(service);
        return ok;
    }

    bool Stop()
    {
        SC_HANDLE service = OpenServiceHandle(
            SERVICE_STOP | SERVICE_QUERY_STATUS);

        if (service == nullptr)
            return false;

        SERVICE_STATUS status{};
        BOOL requested = ControlService(
            service, SERVICE_CONTROL_STOP, &status);

        const bool ok =
            requested != FALSE ||
            GetLastError() == ERROR_SERVICE_NOT_ACTIVE;

        if (ok)
            WaitForState(
                service, SERVICE_STOPPED, 5000);

        CloseServiceHandle(service);
        return ok;
    }

    bool Install(
        const std::wstring& binaryPath,
        bool startAfter)
    {
        SC_HANDLE manager =
            OpenManager(SC_MANAGER_ALL_ACCESS);

        if (manager == nullptr)
            return false;

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
        GrantUserControl(service);

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
