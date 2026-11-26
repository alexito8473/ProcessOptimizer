#include "ServiceApp.h"
#include "ServiceControl.h"

#include <cstdio>
#include <string>

namespace Service
{
    namespace
    {
        SERVICE_STATUS_HANDLE statusHandle = nullptr;
        HANDLE stopEvent = nullptr;
        SERVICE_STATUS serviceStatus{};

        void Log(const wchar_t* message)
        {
            OutputDebugStringW(message);
            wprintf(L"%s\n", message);
        }

        void SetState(DWORD state, DWORD exitCode = 0)
        {
            if (statusHandle == nullptr)
                return;

            serviceStatus.dwCurrentState = state;
            serviceStatus.dwWin32ExitCode = exitCode;

            if (state == SERVICE_RUNNING ||
                state == SERVICE_STOP_PENDING)
            {
                serviceStatus.dwControlsAccepted =
                    SERVICE_ACCEPT_STOP |
                    SERVICE_ACCEPT_SHUTDOWN;
            }
            else
            {
                serviceStatus.dwControlsAccepted = 0;
            }

            serviceStatus.dwCheckPoint =
                state == SERVICE_STOP_PENDING ? 1 : 0;
            serviceStatus.dwWaitHint =
                state == SERVICE_STOP_PENDING ? 5000 : 0;

            SetServiceStatus(statusHandle, &serviceStatus);
        }

        void WINAPI ServiceControlHandler(DWORD control)
        {
            if (control == SERVICE_CONTROL_STOP ||
                control == SERVICE_CONTROL_SHUTDOWN)
            {
                SetState(SERVICE_STOP_PENDING);
                SetEvent(stopEvent);
            }
        }

        void WINAPI ServiceMain(DWORD, LPWSTR*)
        {
            statusHandle = RegisterServiceCtrlHandlerW(
                ServiceName, ServiceControlHandler);

            if (statusHandle == nullptr)
                return;

            // Reparacion automatica de la DACL: al correr como LocalSystem,
            // el servicio puede conceder START/STOP a los usuarios
            // interactivos incluso si fue instalado con una version
            // anterior que no ajustaba los permisos.
            RepairPermissions();

            serviceStatus.dwServiceType =
                SERVICE_WIN32_OWN_PROCESS;
            serviceStatus.dwCurrentState =
                SERVICE_START_PENDING;
            serviceStatus.dwControlsAccepted = 0;
            serviceStatus.dwWin32ExitCode = 0;
            serviceStatus.dwServiceSpecificExitCode = 0;
            serviceStatus.dwCheckPoint = 1;
            serviceStatus.dwWaitHint = 5000;

            SetServiceStatus(statusHandle, &serviceStatus);

            ServiceApp app;
            if (!app.Start())
            {
                SetState(SERVICE_STOPPED, 1);
                return;
            }

            SetState(SERVICE_RUNNING);

            WaitForSingleObject(stopEvent, INFINITE);

            app.Stop();
            SetState(SERVICE_STOPPED);
        }

        BOOL WINAPI ConsoleCtrlHandler(DWORD control)
        {
            if (control == CTRL_C_EVENT ||
                control == CTRL_BREAK_EVENT ||
                control == CTRL_CLOSE_EVENT)
            {
                SetEvent(stopEvent);
                return TRUE;
            }

            return FALSE;
        }

        std::wstring CurrentExecutablePath()
        {
            wchar_t path[MAX_PATH]{};
            const DWORD size = GetModuleFileNameW(
                nullptr, path, MAX_PATH);
            std::wstring result(path, size);

            // CreateServiceW necesita la ruta entre comillas si el
            // directorio tiene espacios (Program Files).
            if (result.find(L' ') != std::wstring::npos &&
                (result.empty() || result.front() != L'"'))
            {
                result = L"\"" + result + L"\"";
            }
            return result;
        }

        bool HasArgument(int argc, wchar_t** argv,
                         const wchar_t* needle)
        {
            for (int i = 1; i < argc; ++i)
            {
                if (_wcsicmp(argv[i], needle) == 0)
                    return true;
            }

            return false;
        }
    }

    int RunHost(int argc, wchar_t** argv)
    {
        if (HasArgument(argc, argv, L"--install"))
        {
            const bool start =
                HasArgument(argc, argv, L"--start");

            const bool ok = Install(
                CurrentExecutablePath(), start);

            Log(ok ? L"[Service] Servicio instalado."
                   : L"[Service] No se pudo instalar el servicio.");
            return ok ? 0 : 1;
        }

        if (HasArgument(argc, argv, L"--uninstall"))
        {
            Stop();
            const bool ok = Uninstall();
            Log(ok ? L"[Service] Servicio desinstalado."
                   : L"[Service] No se pudo desinstalar.");
            return ok ? 0 : 1;
        }

        if (HasArgument(argc, argv, L"--stop"))
        {
            const bool ok = Stop();
            Log(ok ? L"[Service] Servicio detenido."
                   : L"[Service] No se pudo detener.");
            return ok ? 0 : 1;
        }

        if (HasArgument(argc, argv, L"--start"))
        {
            const bool ok = Start();
            Log(ok ? L"[Service] Servicio iniciado."
                   : L"[Service] No se pudo iniciar.");
            return ok ? 0 : 1;
        }

        stopEvent = CreateEventW(
            nullptr, TRUE, FALSE, nullptr);
        if (stopEvent == nullptr)
            return 1;

        if (HasArgument(argc, argv, L"--console"))
        {
            SetConsoleCtrlHandler(
                ConsoleCtrlHandler, TRUE);

            ServiceApp app;
            app.Start();
            Log(L"[Service] Modo consola. Pulsa Ctrl+C para salir.");

            WaitForSingleObject(stopEvent, INFINITE);
            app.Stop();
        }
        else
        {
            SERVICE_TABLE_ENTRYW table[] =
            {
                { const_cast<wchar_t*>(ServiceName),
                  ServiceMain },
                { nullptr, nullptr }
            };

            if (!StartServiceCtrlDispatcherW(table))
                return 1;
        }

        CloseHandle(stopEvent);
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    return Service::RunHost(argc, argv);
}
