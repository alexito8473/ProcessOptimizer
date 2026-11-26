#include "TrayApp.h"
#include "../Service/ServiceControl.h"
#include "../../resource.h"

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

        constexpr int PopupWidth = 300;
        constexpr int PopupHeight = 260;
        constexpr int PopupMargin = 12;
        constexpr int MetricTextTop = 8;
        constexpr int MetricRowStep = 46;
        constexpr int MetricTextHeight = 16;
        constexpr int SparklineTopOffset = 17;
        constexpr int SparklineHeight = 20;
        constexpr int SparklineWidth = 276;
        constexpr int ProcessLineTop = 144;
        constexpr int GpuLineTop = 160;
        constexpr int ServiceLineTop = 180;
        constexpr int ServiceLineStep = 16;

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

        std::wstring Utf8ToWide(const std::string& text)
        {
            if (text.empty())
                return L"-";

            int length = MultiByteToWideChar(
                CP_UTF8, 0, text.c_str(), -1, nullptr, 0);

            if (length <= 0)
                return L"-";

            std::wstring result(
                static_cast<std::size_t>(length), L'\0');

            MultiByteToWideChar(
                CP_UTF8, 0, text.c_str(), -1,
                result.data(), length);

            result.resize(static_cast<std::size_t>(length) - 1);
            return result;
        }

        const wchar_t* ModeName(
            Optimization::OptimizerMode mode)
        {
            switch (mode)
            {
            case Optimization::OptimizerMode::Off:
                return L"Off";
            case Optimization::OptimizerMode::Balanced:
                return L"Balanceado";
            default:
                return L"Desconocido";
            }
        }

        const IPC::GpuSeriesHistory* SelectedGpuSeries(
            const IPC::State& state)
        {
            const GpuAdapterInfo* top = nullptr;

            if (state.hasGpu)
            {
                for (const GpuAdapterInfo& gpu :
                     state.gpu.gpus)
                {
                    if (top == nullptr ||
                        gpu.usagePercent > top->usagePercent)
                    {
                        top = &gpu;
                    }
                }
            }

            if (top != nullptr)
            {
                const auto it = std::find_if(
                    state.overview.gpus.begin(),
                    state.overview.gpus.end(),
                    [top](const IPC::GpuSeriesHistory& series)
                    {
                        return series.luidLowPart ==
                                   top->luidLowPart &&
                               series.luidHighPart ==
                                   top->luidHighPart;
                    });

                if (it != state.overview.gpus.end())
                    return &(*it);
            }

            if (!state.overview.gpus.empty())
                return &state.overview.gpus.front();

            return nullptr;
        }

        template<std::size_t N>
        void BuildMetric(
            const IPC::FloatRing<N>& history,
            int sparklineTop,
            const wchar_t* label,
            double current,
            std::wstring& line,
            std::vector<POINT>& points)
        {
            const std::size_t capacity =
                history.values.size();
            const std::size_t count =
                (std::min)(history.count, capacity);

            std::vector<float> ordered;
            ordered.reserve(count);

            if (count != 0)
            {
                const std::size_t start =
                    (history.index + capacity - count) %
                    capacity;

                for (std::size_t i = 0; i < count; ++i)
                {
                    ordered.push_back(
                        history.values[(start + i) % capacity]);
                }
            }

            double minimum = 0.0;
            double median = 0.0;
            double maximum = 0.0;

            if (count != 0)
            {
                std::vector<float> sorted = ordered;
                std::sort(sorted.begin(), sorted.end());

                minimum = sorted.front();
                maximum = sorted.back();

                if (sorted.size() % 2 != 0)
                {
                    median = sorted[sorted.size() / 2];
                }
                else
                {
                    median =
                        (static_cast<double>(
                             sorted[sorted.size() / 2 - 1]) +
                         sorted[sorted.size() / 2]) /
                        2.0;
                }
            }

            wchar_t buffer[128]{};
            swprintf_s(
                buffer,
                L"%s %.1f%%  mín %.0f / med %.0f / máx %.0f",
                label, current, minimum, median, maximum);
            line = buffer;

            points.clear();

            if (count == 0)
                return;

            points.reserve(count);

            const double span = maximum - minimum;
            const int usableX = SparklineWidth - 1;
            const int usableY = SparklineHeight - 1;

            for (std::size_t i = 0; i < count; ++i)
            {
                double normalized =
                    span > 0.0001
                        ? (ordered[i] - minimum) / span
                        : 0.5;

                normalized =
                    (std::min)(1.0, (std::max)(0.0, normalized));

                const int x =
                    PopupMargin +
                    (count == 1
                         ? 0
                         : static_cast<int>(
                               i * usableX / (count - 1)));
                const int y =
                    sparklineTop + usableY -
                    static_cast<int>(normalized * usableY);

                points.push_back({ x, y });
            }
        }

        void DrawSparkline(
            HDC device,
            const std::vector<POINT>& points,
            COLORREF color)
        {
            if (points.empty())
                return;

            HPEN pen = CreatePen(PS_SOLID, 1, color);
            if (pen == nullptr)
                return;

            HPEN previous =
                reinterpret_cast<HPEN>(SelectObject(device, pen));

            if (points.size() == 1)
            {
                MoveToEx(
                    device, points[0].x, points[0].y, nullptr);
                LineTo(
                    device, points[0].x + 1, points[0].y);
            }
            else
            {
                Polyline(
                    device, points.data(),
                    static_cast<int>(points.size()));
            }

            SelectObject(device, previous);
            DeleteObject(pen);
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
            0, 0, PopupWidth, PopupHeight,
            nullptr, nullptr, instance_, this);

        icon_ = LoadIconW(
            instance_, MAKEINTRESOURCEW(IDI_APP));

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

            PopupView view;
            if (app != nullptr)
            {
                std::lock_guard<std::mutex> lock(
                    app->stateMutex_);
                view = app->popupView_;
            }

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
            HFONT previousFont = reinterpret_cast<HFONT>(
                SelectObject(device, font));

            SetBkMode(device, TRANSPARENT);
            SetTextColor(device, RGB(235, 238, 245));

            const int right = (std::max)(
                PopupMargin + 1,
                static_cast<int>(rect.right) - PopupMargin);

            auto drawLine =
                [device, right](const std::wstring& text, int top)
            {
                RECT lineRect{
                    PopupMargin, top, right,
                    top + MetricTextHeight };

                DrawTextW(
                    device, text.c_str(), -1, &lineRect,
                    DT_LEFT | DT_TOP | DT_SINGLELINE |
                        DT_END_ELLIPSIS | DT_NOPREFIX);
            };

            for (int i = 0; i < 3; ++i)
            {
                drawLine(
                    view.metricLines[i],
                    MetricTextTop + i * MetricRowStep);
            }

            DrawSparkline(
                device, view.cpuPoints,
                RGB(120, 200, 255));
            DrawSparkline(
                device, view.memoryPoints,
                RGB(255, 190, 110));
            DrawSparkline(
                device, view.gpuPoints,
                RGB(160, 255, 170));

            drawLine(view.processLine, ProcessLineTop);
            drawLine(view.gpuLine, GpuLineTop);

            for (int i = 0; i < 3; ++i)
            {
                drawLine(
                    view.serviceLines[i],
                    ServiceLineTop + i * ServiceLineStep);
            }

            SelectObject(device, previousFont);

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

                PopupView view;

                if (ok)
                {
                    const int sparklineTop =
                        MetricTextTop + SparklineTopOffset;

                    BuildMetric(
                        state_.cpuHistory, sparklineTop, L"CPU",
                        state_.hasCpu
                            ? state_.cpu.totalUsagePercent
                            : 0.0,
                        view.metricLines[0],
                        view.cpuPoints);

                    BuildMetric(
                        state_.memoryHistory,
                        sparklineTop + MetricRowStep, L"RAM",
                        state_.hasMemory
                            ? state_.memory.usagePercent
                            : 0.0,
                        view.metricLines[1],
                        view.memoryPoints);

                    const IPC::GpuSeriesHistory* gpuSeries =
                        SelectedGpuSeries(state_);
                    const IPC::OverviewRing emptyGpuHistory{};
                    const IPC::OverviewRing& gpuHistory =
                        gpuSeries != nullptr
                            ? gpuSeries->usage
                            : emptyGpuHistory;

                    BuildMetric(
                        gpuHistory,
                        sparklineTop + 2 * MetricRowStep,
                        L"GPU", MaxGpuUsage(state_),
                        view.metricLines[2],
                        view.gpuPoints);

                    view.processLine =
                        L"Proceso: " + TopProcess(state_);
                    view.gpuLine =
                        L"GPU: " +
                        (gpuSeries != nullptr
                             ? Utf8ToWide(gpuSeries->name)
                             : L"-");

                    view.serviceLines[0] =
                        L"Servicio: Conectado";

                    wchar_t buffer[128]{};

                    if (!state_.hasOptimizer)
                    {
                        view.serviceLines[1] =
                            L"Optimizador: sin datos";
                        view.serviceLines[2] =
                            L"Actualización: -";
                    }
                    else
                    {
                        swprintf_s(
                            buffer,
                            L"Optimizador: %s - %s",
                            state_.optimizer.enabled
                                ? L"Activado"
                                : L"Desactivado",
                            ModeName(state_.optimizer.mode));
                        view.serviceLines[1] = buffer;

                        const ULONGLONG lastUpdate =
                            state_.optimizer.lastUpdateMs;

                        if (lastUpdate == 0)
                        {
                            view.serviceLines[2] =
                                L"Actualización: -";
                        }
                        else
                        {
                            const ULONGLONG now =
                                GetTickCount64();
                            const ULONGLONG elapsed =
                                now >= lastUpdate
                                    ? now - lastUpdate
                                    : 0;

                            if (elapsed < 1000)
                            {
                                view.serviceLines[2] =
                                    L"Actualización: ahora";
                            }
                            else if (elapsed < 60000)
                            {
                                swprintf_s(
                                    buffer,
                                    L"Actualización: hace %llu s",
                                    static_cast<unsigned long long>(
                                        elapsed / 1000));
                                view.serviceLines[2] = buffer;
                            }
                            else
                            {
                                swprintf_s(
                                    buffer,
                                    L"Actualización: hace %llu min",
                                    static_cast<unsigned long long>(
                                        elapsed / 60000));
                                view.serviceLines[2] = buffer;
                            }
                        }
                    }
                }
                else
                {
                    view.metricLines[0] =
                        L"CPU  --  mín -- / med -- / máx --";
                    view.metricLines[1] =
                        L"RAM  --  mín -- / med -- / máx --";
                    view.metricLines[2] =
                        L"GPU  --  mín -- / med -- / máx --";
                    view.processLine = L"Proceso: -";
                    view.gpuLine = L"GPU: -";
                    view.serviceLines[0] =
                        L"Servicio: Detenido";
                    view.serviceLines[1] =
                        L"Optimizador: sin datos";
                    view.serviceLines[2] =
                        L"Actualización: -";
                }

                popupView_ = view;
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

        int width = PopupWidth;
        int height = PopupHeight;
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
