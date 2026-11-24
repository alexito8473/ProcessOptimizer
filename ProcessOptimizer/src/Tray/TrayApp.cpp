#include "TrayApp.h"
#include "../Service/ServiceControl.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <shellapi.h>

namespace Tray
{
    namespace
    {
        constexpr UINT TrayMessage = WM_APP + 1;
        constexpr UINT TrayIconId = 1;
        constexpr UINT PopupTimerId = 1;
        constexpr UINT MenuOpenUi = 1;
        constexpr UINT MenuStartService = 2;
        constexpr UINT MenuStopService = 3;
        constexpr UINT MenuExit = 4;

        const wchar_t* WindowClass =
            L"ProcessOptimizerTrayWindow";
        const wchar_t* PopupClass =
            L"ProcessOptimizerTrayPopup";

        TrayApp* FromWindow(HWND hwnd)
        {
            return reinterpret_cast<TrayApp*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        double MaxGpuUsage(const IPC::State& state)
        {
            double maximum = 0.0;

            for (const GpuAdapterInfo& gpu :
                 state.gpu.gpus)
            {
                maximum =
                    (std::max)(maximum, gpu.usagePercent);
            }

            return maximum;
        }

        std::wstring TopProcess(const IPC::State& state)
        {
            const ProcessInfo* top = nullptr;

            for (const ProcessInfo& process :
                 state.processes.processes)
            {
                if (top == nullptr ||
                    process.cpuUsagePercent >
                        top->cpuUsagePercent)
                {
                    top = &process;
                }
            }

            if (top == nullptr || top->name.empty())
                return L"-";

            wchar_t buffer[160]{};
            swprintf_s(
                buffer, L"%s (%.1f%%)",
                top->name.c_str(),
                top->cpuUsagePercent);
            return buffer;
        }
    }

    bool TrayApp::Run(HINSTANCE instance)
    {
        instance_ = instance;

        HANDLE mutex = CreateMutexW(
            nullptr, TRUE,
            L"Local\\ProcessOptimizer.Tray.SingleInstance");

        if (mutex == nullptr ||
            GetLastError() == ERROR_ALREADY_EXISTS)
        {
            if (mutex != nullptr)
                CloseHandle(mutex);
            return false;
        }

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(WNDCLASSEXW);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = WindowClass;

        RegisterClassExW(&windowClass);

        hwnd_ = CreateWindowExW(
            0, WindowClass, L"ProcessOptimizerTray",
            0, 0, 0, 0, 0,
            nullptr, nullptr, instance_, this);

        if (hwnd_ == nullptr)
        {
            CloseHandle(mutex);
            return false;
        }

        WNDCLASSEXW popupClass{};
        popupClass.cbSize = sizeof(WNDCLASSEXW);
        popupClass.lpfnWndProc = PopupProc;
        popupClass.hInstance = instance_;
        popupClass.lpszClassName = PopupClass;
        popupClass.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassExW(&popupClass);

        popup_ = CreateWindowExW(
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            PopupClass, L"", WS_POPUP | WS_BORDER,
            0, 0, 250, 150,
            nullptr, nullptr, instance_, this);

        icon_ = LoadIconW(nullptr, IDI_APPLICATION);

        notifyData_.cbSize = sizeof(NOTIFYICONDATAW);
        notifyData_.hWnd = hwnd_;
        notifyData_.uID = TrayIconId;
        notifyData_.uFlags =
            NIF_ICON | NIF_MESSAGE | NIF_TIP;
        notifyData_.uCallbackMessage = TrayMessage;
        notifyData_.hIcon = icon_;
        wcscpy_s(
            notifyData_.szTip,
            L"ProcessOptimizer - iniciando...");

        if (!Shell_NotifyIconW(NIM_ADD, &notifyData_))
        {
            DestroyWindow(hwnd_);
            CloseHandle(mutex);
            return false;
        }

        running_.store(true);
        pollThread_ = std::thread(&TrayApp::PollLoop, this);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        running_.store(false);
        if (pollThread_.joinable())
            pollThread_.join();

        Shell_NotifyIconW(NIM_DELETE, &notifyData_);
        DestroyWindow(popup_);
        DestroyWindow(hwnd_);
        CloseHandle(mutex);

        return true;
    }

    LRESULT CALLBACK TrayApp::WindowProc(
        HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            const auto* create =
                reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(
                hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(
                    create->lpCreateParams));
            return TRUE;
        }

        TrayApp* app = FromWindow(hwnd);

        switch (message)
        {
        case TrayMessage:
            if (wParam != TrayIconId)
                return 0;

            if (lParam == WM_MOUSEMOVE)
            {
                if (app != nullptr)
                    app->ShowPopup();
                SetTimer(hwnd, PopupTimerId, 400, nullptr);
            }
            else if (lParam == WM_RBUTTONUP)
            {
                POINT point{};
                GetCursorPos(&point);
                if (app != nullptr)
                    app->ShowMenu(point);
            }
            else if (lParam == WM_LBUTTONDBLCLK)
            {
                if (app != nullptr)
                    app->LaunchUi();
            }
            return 0;

        case WM_TIMER:
        {
            if (wParam != PopupTimerId)
                break;

            POINT point{};
            GetCursorPos(&point);

            bool overTray = false;
            NOTIFYICONIDENTIFIER identifier{};
            identifier.cbSize = sizeof(identifier);
            identifier.hWnd = hwnd;
            identifier.uID = TrayIconId;

            RECT trayRect{};
            if (Shell_NotifyIconGetRect(
                    &identifier, &trayRect) == S_OK)
            {
                overTray = PtInRect(&trayRect, point) != FALSE;
            }

            bool overPopup = false;
            if (app != nullptr && app->popup_ != nullptr)
            {
                RECT popupRect{};
                if (GetWindowRect(
                        app->popup_, &popupRect))
                {
                    overPopup =
                        PtInRect(&popupRect, point) != FALSE;
                }
            }

            if (!overTray && !overPopup)
            {
                KillTimer(hwnd, PopupTimerId);
                if (app != nullptr)
                    app->HidePopup();
            }
            return 0;
        }

        case WM_COMMAND:
            if (app != nullptr)
            {
                switch (LOWORD(wParam))
                {
                case MenuOpenUi:
                    app->LaunchUi();
                    break;
                case MenuStartService:
                    Service::Start();
                    break;
                case MenuStopService:
                    Service::Stop();
                    break;
                case MenuExit:
                    PostQuitMessage(0);
                    break;
                }
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK TrayApp::PopupProc(
        HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            const auto* create =
                reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(
                hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(
                    create->lpCreateParams));
            return TRUE;
        }

        if (message == WM_PAINT)
        {
            TrayApp* app = FromWindow(hwnd);

            PAINTSTRUCT paint{};
            HDC device = BeginPaint(hwnd, &paint);

            RECT rect{};
            GetClientRect(hwnd, &rect);

            HBRUSH background =
                CreateSolidBrush(RGB(24, 27, 34));
            FillRect(device, &rect, background);
            DeleteObject(background);

            HFONT font = reinterpret_cast<HFONT>(
                GetStockObject(DEFAULT_GUI_FONT));
            SelectObject(device, font);

            SetBkMode(device, TRANSPARENT);
            SetTextColor(device, RGB(235, 238, 245));

            RECT textRect = rect;
            textRect.left += 12;
            textRect.top += 10;
            textRect.right -= 12;

            std::wstring text;
            if (app != nullptr)
            {
                std::lock_guard<std::mutex> lock(
                    app->stateMutex_);
                text = app->popupText_;
            }

            DrawTextW(
                device, text.c_str(), -1, &textRect,
                DT_LEFT | DT_TOP);

            EndPaint(hwnd, &paint);
            return 0;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void TrayApp::PollLoop()
    {
        while (running_.load())
        {
            IPC::PipeClient client;
            IPC::State remote;

            const bool ok = client.GetState(remote);

            {
                std::lock_guard<std::mutex> lock(stateMutex_);

                if (ok)
                {
                    state_ = remote;
                    connected_ = true;
                }
                else
                {
                    connected_ = false;
                }

                popupText_.clear();

                if (ok)
                {
                    wchar_t buffer[512]{};

                    swprintf_s(
                        buffer,
                        L"CPU      %.1f%%\n"
                        L"RAM      %.1f%%\n"
                        L"GPU      %.1f%%\n"
                        L"Proceso  %s\n"
                        L"Modo     Servicio",
                        state_.hasCpu
                            ? state_.cpu.totalUsagePercent
                            : 0.0,
                        state_.hasMemory
                            ? state_.memory.usagePercent
                            : 0.0,
                        MaxGpuUsage(state_),
                        TopProcess(state_).c_str());

                    popupText_ = buffer;
                }
                else
                {
                    popupText_ =
                        L"Servicio detenido\n"
                        L"Modo local";
                }
            }

            UpdateTooltip();

            std::this_thread::sleep_for(
                std::chrono::milliseconds(1000));
        }
    }

    void TrayApp::UpdateTooltip()
    {
        std::wstring tooltip;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            if (connected_)
            {
                wchar_t buffer[128]{};
                swprintf_s(
                    buffer,
                    L"CPU %.0f%% | RAM %.0f%% | GPU %.0f%%",
                    state_.hasCpu
                        ? state_.cpu.totalUsagePercent
                        : 0.0,
                    state_.hasMemory
                        ? state_.memory.usagePercent
                        : 0.0,
                    MaxGpuUsage(state_));
                tooltip = buffer;
            }
            else
            {
                tooltip = L"ProcessOptimizer - modo local";
            }
        }

        NOTIFYICONDATAW data = notifyData_;
        data.uFlags = NIF_TIP;
        wcscpy_s(data.szTip, tooltip.c_str());
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void TrayApp::ShowPopup()
    {
        if (popup_ == nullptr)
            return;

        POINT point{};
        GetCursorPos(&point);

        int width = 250;
        int height = 150;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        int x = point.x + 12;
        int y = point.y + 12;

        if (x + width > screenWidth)
            x = point.x - width - 12;
        if (y + height > screenHeight)
            y = point.y - height - 12;

        SetWindowPos(
            popup_, HWND_TOPMOST, x, y, width, height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(popup_, nullptr, TRUE);
    }

    void TrayApp::HidePopup()
    {
        if (popup_ != nullptr)
            ShowWindow(popup_, SW_HIDE);
    }

    void TrayApp::ShowMenu(POINT point)
    {
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
            return;

        AppendMenuW(
            menu, MF_STRING, MenuOpenUi,
            L"Abrir ProcessOptimizer");

        const std::uint32_t status =
            Service::QueryStatus();

        AppendMenuW(
            menu,
            status == SERVICE_RUNNING
                ? MF_STRING | MF_DISABLED
                : MF_STRING,
            MenuStartService,
            L"Iniciar servicio");

        AppendMenuW(
            menu,
            status == SERVICE_RUNNING
                ? MF_STRING
                : MF_STRING | MF_DISABLED,
            MenuStopService,
            L"Detener servicio");

        AppendMenuW(
            menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, MenuExit, L"Salir");

        SetForegroundWindow(hwnd_);

        const UINT command = TrackPopupMenuEx(
            menu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON |
                TPM_NONOTIFY,
            point.x, point.y, hwnd_, nullptr);

        DestroyMenu(menu);

        if (command == 0)
            return;

        switch (command)
        {
        case MenuOpenUi:
            LaunchUi();
            break;
        case MenuStartService:
            Service::Start();
            break;
        case MenuStopService:
            Service::Stop();
            break;
        case MenuExit:
            PostQuitMessage(0);
            break;
        }
    }

    void TrayApp::LaunchUi() const
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);

        std::wstring executable(path);
        const std::size_t slash =
            executable.find_last_of(L"\\/");

        if (slash != std::wstring::npos)
            executable.resize(slash + 1);

        executable += L"ProcessOptimizer.UI.exe";

        ShellExecuteW(
            nullptr, L"open", executable.c_str(),
            nullptr, nullptr, SW_SHOWNORMAL);
    }
}
