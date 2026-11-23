
#include "Window.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <mutex>
#include <vector>
#include <chrono>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include "../Screens/Cpu/CpuDashboard.h"
#include "../Screens/Memory/MemoryDashboard.h"
#include "../Screens/Gpu/GpuDashboard.h"
#include "../Screens/Process/ProcessDashboard.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
);

// ============================================================
// CONSTRUCTOR
// ============================================================

Window::Window(HINSTANCE instance)
    : instance_(instance)
{
    sharedState_ = std::make_shared<SharedState>();
}

// ============================================================
// DESTRUCTOR
// ============================================================

Window::~Window()
{
    StopMonitoring();
}

// ============================================================
// CREATE WINDOW
// ============================================================

bool Window::Create(int showCommand)
{
    WNDCLASSEXW wc{};

    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = ClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0,
        ClassName,
        L"ProcessOptimizer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1450,
        900,
        nullptr,
        nullptr,
        instance_,
        this
    );

    if (!hwnd_)
        return false;

    sharedState_->hwnd = hwnd_;

    if (!renderer_.Initialize(hwnd_))
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    if (!direct2DRenderer_.Initialize(hwnd_))
    {
        MessageBoxW(
            hwnd_,
            L"No se pudo inicializar Direct2D.",
            L"ProcessOptimizer",
            MB_OK | MB_ICONERROR
        );

        renderer_.Shutdown();
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    ApplyTheme();

    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);

    StartMonitoring();

    return true;
}

// ============================================================
// MAIN LOOP
// ============================================================

int Window::Run()
{
    MSG message{};

    using Clock = std::chrono::steady_clock;

    constexpr auto frameDuration =
        std::chrono::microseconds(22222); // Máximo 45 FPS

    auto lastFrame = Clock::now();

    while (message.message != WM_QUIT)
    {
        // Procesar mensajes de Windows.
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
                break;

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (message.message == WM_QUIT)
            break;

        // Si no hay cambios, esperar sin renderizar.
        if (!needsRedraw_.load(std::memory_order_acquire))
        {
            MsgWaitForMultipleObjectsEx(
                0,
                nullptr,
                INFINITE,
                QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);

            continue;
        }

        const auto now = Clock::now();

        // Limitar a 45 FPS.
        if (now - lastFrame < frameDuration)
        {
            Sleep(1);
            continue;
        }

        // Renderizar solamente si hay una actualización pendiente.
        if (needsRedraw_.exchange(
            false, std::memory_order_acq_rel))
        {
            renderer_.BeginFrame();

            RenderDashboard();

            renderer_.EndFrame();

            lastFrame = Clock::now();
        }
    }

    StopMonitoring();

    return static_cast<int>(message.wParam);
}

// ============================================================
// CPU HISTORY
// ============================================================

void Window::AddCpuSample(
    SharedState& state,
    float value)
{
    state.cpuHistory[state.historyIndex] = value;

    state.historyIndex =
        (state.historyIndex + 1) % HistorySize;

    if (state.historyCount < HistorySize)
    {
        ++state.historyCount;
    }
}

// ============================================================
// MEMORY HISTORY
// ============================================================

void Window::AddMemorySample(float value)
{
    memoryHistory_[memoryHistoryIndex_] = value;

    memoryHistoryIndex_ =
        (memoryHistoryIndex_ + 1) % HistorySize;

    if (memoryHistoryCount_ < HistorySize)
        ++memoryHistoryCount_;
}


// ============================================================
// GPU HISTORY
// ============================================================

void Window::AddGpuSample(const GpuSnapshot& snapshot)
{
    constexpr double BytesToMB = 1024.0 * 1024.0;

    for (const auto& gpu : snapshot.gpus)
    {
        auto it = std::find_if(
            gpuHistory_.adapters.begin(),
            gpuHistory_.adapters.end(),
            [&gpu](const UI::Dashboard::Gpu::GpuAdapterHistory& history)
            {
                return history.luidLowPart == gpu.luidLowPart &&
                    history.luidHighPart == gpu.luidHighPart;
            }
        );

        if (it == gpuHistory_.adapters.end())
        {
            gpuHistory_.adapters.emplace_back();

            it = std::prev(gpuHistory_.adapters.end());

            it->luidLowPart = gpu.luidLowPart;
            it->luidHighPart = gpu.luidHighPart;
        }

        const std::size_t index = it->historyIndex;

        it->usage[index] = static_cast<float>(gpu.usagePercent);
        it->usage3D[index] = static_cast<float>(gpu.usage3D);
        it->usageCopy[index] = static_cast<float>(gpu.usageCopy);
        it->usageCompute[index] = static_cast<float>(gpu.usageCompute);
        it->usageVideoDecode[index] = static_cast<float>(gpu.usageVideoDecode);
        it->usageVideoEncode[index] = static_cast<float>(gpu.usageVideoEncode);
        it->usageVideoProcessing[index] = static_cast<float>(gpu.usageVideoProcessing);

        it->dedicatedMemory[index] = static_cast<float>(
            gpu.processDedicatedMemoryUsedBytes / BytesToMB
            );

        it->sharedMemory[index] = static_cast<float>(
            gpu.processSharedMemoryUsedBytes / BytesToMB
            );

        it->historyIndex = (it->historyIndex + 1) % UI::Dashboard::Gpu::GpuHistorySize;

        if (it->historyCount < UI::Dashboard::Gpu::GpuHistorySize)
            ++it->historyCount;

#ifdef _DEBUG
        std::ostringstream debug;

        debug << std::fixed << std::setprecision(2);

        debug << "[GPU HISTORY] "
            << "GPU: " << gpu.index
            << " | " << std::string(
                gpu.name.begin(), gpu.name.end())
            << " | Usage: " << gpu.usagePercent << "%"
            << " | 3D: " << gpu.usage3D << "%"
            << " | Copy: " << gpu.usageCopy << "%"
            << " | Compute: " << gpu.usageCompute << "%"
            << " | Index: " << it->historyIndex
            << " | Count: " << it->historyCount
            << "\n";

        OutputDebugStringA(debug.str().c_str());
#endif
    }
}
// ============================================================
// PROCESSOR HISTORIES
// ============================================================

void Window::AddProcessorSamples(
    SharedState& state,
    const CpuSnapshot& snapshot)
{
    for (const auto& processor : snapshot.logicalProcessors)
    {
        auto it = std::find_if(
            state.processorHistories.begin(),
            state.processorHistories.end(),
            [&processor](const auto& history)
            {
                return history.group == processor.processorGroup &&
                    history.logicalIndex == processor.logicalProcessorIndex;
            }
        );

        if (it == state.processorHistories.end())
        {
            state.processorHistories.emplace_back();

            it = std::prev(
                state.processorHistories.end()
            );

            it->group = processor.processorGroup;
            it->logicalIndex = processor.logicalProcessorIndex;
        }

        it->values[it->historyIndex] =
            static_cast<float>(processor.usagePercent);

        it->historyIndex =
            (it->historyIndex + 1) %
            it->values.size();

        if (it->historyCount < it->values.size())
        {
            ++it->historyCount;
        }
    }
}

// ============================================================
// START MONITORING
// ============================================================
void Window::StartMonitoring()
{
    if (monitor_)
        return;

    monitor_ = std::make_unique<SystemMonitor>(events_);

    cpuSubscriptionId_ = events_.Subscribe<CpuSnapshot>(
        "CPU",
        [this](const CpuSnapshot& snapshot)
        {
            {
                std::lock_guard<std::mutex> lock(
                    sharedState_->mutex
                );

                sharedState_->snapshot = snapshot;
                sharedState_->hasSnapshot = true;

                AddCpuSample(
                    *sharedState_,
                    static_cast<float>(snapshot.totalUsagePercent)
                );

                AddProcessorSamples(
                    *sharedState_,
                    snapshot
                );
            }

            if (hwnd_)
            {
                PostMessageW(
                    hwnd_,
                    WM_DATA_UPDATED,
                    0,
                    0
                );
            }
        }
    );
    memorySubscriptionId_ = events_.Subscribe<MemorySnapshot>(
        "MEMORY",
        [this](const MemorySnapshot& snapshot)
        {
            {
                std::lock_guard<std::mutex> lock(
                    sharedState_->mutex
                );

                memorySnapshot_ = snapshot;
                hasMemorySnapshot_ = true;

                AddMemorySample(
                    static_cast<float>(snapshot.usagePercent)
                );
            }

            if (hwnd_)
            {
                PostMessageW(
                    hwnd_,
                    WM_DATA_UPDATED,
                    0,
                    0
                );
            }
        }
    );
    gpuSubscriptionId_ = events_.Subscribe<GpuSnapshot>(
        "GPU",
        [this](const GpuSnapshot& snapshot)
        {
            {
                std::lock_guard<std::mutex> lock(
                    sharedState_->mutex
                );

                gpuSnapshot_ = snapshot;
                hasGpuSnapshot_ = true;

                AddGpuSample(snapshot);
            }

            if (hwnd_)
            {
                PostMessageW(
                    hwnd_,
                    WM_DATA_UPDATED,
                    0,
                    0
                );
            }
        }
    );
    processSubscriptionId_ = events_.Subscribe<ProcessSnapshot>(
        "PROCESS",
        [this](const ProcessSnapshot& snapshot)
        {
            {
                std::lock_guard<std::mutex> lock(
                    sharedState_->mutex
                );

                processSnapshot_ = snapshot;
                hasProcessSnapshot_ = true;
            }

            if (hwnd_)
            {
                PostMessageW(
                    hwnd_,
                    WM_DATA_UPDATED,
                    0,
                    0
                );
            }
        }
    );
    subscribed_ = true;

    monitor_->Start();
}

// ============================================================
// STOP MONITORING
// ============================================================

void Window::StopMonitoring()
{
    if (monitor_)
    {
        monitor_->Stop();
        monitor_.reset();
    }

    if (subscribed_)
    {
        events_.Unsubscribe<CpuSnapshot>(
            "CPU",
            cpuSubscriptionId_
        );

        events_.Unsubscribe<MemorySnapshot>(
            "MEMORY",
            memorySubscriptionId_
        );
        events_.Unsubscribe<GpuSnapshot>(
            "GPU",
            gpuSubscriptionId_
        );
        events_.Unsubscribe<ProcessSnapshot>(
            "PROCESS",
            processSubscriptionId_
        );
        subscribed_ = false;
    }
}

// ============================================================
// THEME
// ============================================================

void Window::ApplyTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 10);

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg] =
        ImVec4(0.055f, 0.065f, 0.085f, 1.0f);

    colors[ImGuiCol_ChildBg] =
        ImVec4(0.075f, 0.085f, 0.110f, 1.0f);

    colors[ImGuiCol_PopupBg] =
        ImVec4(0.080f, 0.090f, 0.115f, 1.0f);

    colors[ImGuiCol_Border] =
        ImVec4(0.15f, 0.17f, 0.21f, 1.0f);

    colors[ImGuiCol_FrameBg] =
        ImVec4(0.12f, 0.14f, 0.18f, 1.0f);

    colors[ImGuiCol_FrameBgHovered] =
        ImVec4(0.16f, 0.19f, 0.24f, 1.0f);

    colors[ImGuiCol_FrameBgActive] =
        ImVec4(0.19f, 0.23f, 0.29f, 1.0f);

    colors[ImGuiCol_Button] =
        ImVec4(0.12f, 0.15f, 0.19f, 1.0f);

    colors[ImGuiCol_ButtonHovered] =
        ImVec4(0.18f, 0.22f, 0.27f, 1.0f);

    colors[ImGuiCol_ButtonActive] =
        ImVec4(0.22f, 0.27f, 0.32f, 1.0f);

    colors[ImGuiCol_Header] =
        ImVec4(0.13f, 0.18f, 0.21f, 1.0f);

    colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.17f, 0.24f, 0.27f, 1.0f);

    colors[ImGuiCol_HeaderActive] =
        ImVec4(0.20f, 0.29f, 0.31f, 1.0f);
}

// ============================================================
// DASHBOARD
// ============================================================

void Window::RenderDashboard()
{
    CpuSnapshot snapshot{};
    MemorySnapshot memorySnapshot{};
    GpuSnapshot gpuSnapshot{};
    ProcessSnapshot processSnapshot{};
    bool hasProcessSnapshot = false;
    UI::Dashboard::Gpu::GpuDashboardHistory gpuHistory{};
    std::size_t cpuHistoryIndex = 0;
    std::size_t cpuHistoryCount = 0;

    std::array<float, HistorySize> cpuHistory{};

    std::vector<ProcessorChartHistory> processorHistories;

    // RAM
    std::array<float, HistorySize> memoryHistory{};
    std::size_t memoryHistoryIndex = 0;
    std::size_t memoryHistoryCount = 0;

    bool hasMemorySnapshot = false;
    bool hasGpuSnapshot = false;
    {
        std::lock_guard<std::mutex> lock(
            sharedState_->mutex
        );

        // CPU
        snapshot = sharedState_->snapshot;

        cpuHistory = sharedState_->cpuHistory;
        cpuHistoryIndex = sharedState_->historyIndex;
        cpuHistoryCount = sharedState_->historyCount;

        processorHistories = sharedState_->processorHistories;

        // RAM
        memorySnapshot = memorySnapshot_;
        memoryHistory = memoryHistory_;
        memoryHistoryIndex = memoryHistoryIndex_;
        memoryHistoryCount = memoryHistoryCount_;
        hasMemorySnapshot = hasMemorySnapshot_;

        gpuSnapshot = gpuSnapshot_;
        gpuHistory = gpuHistory_;
        hasGpuSnapshot = hasGpuSnapshot_;
        processSnapshot = processSnapshot_;
        hasProcessSnapshot = hasProcessSnapshot_;
    }

    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);

    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGui::Begin(
        "MainDashboard",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus
    );

    // ========================================================
    // SIDEBAR
    // ========================================================

    ImGui::BeginChild(
        "Sidebar",
        ImVec2(210, 0),
        true
    );

    ImGui::Spacing();

    ImGui::TextColored(
        ImVec4(0.30f, 0.85f, 0.72f, 1.0f),
        "PROCESS OPTIMIZER"
    );

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const char* pages[] =
    {
        "Dashboard",
        "CPU",
        "RAM",
        "GPU",
        "Processes",
        "Settings"
    };

    for (int i = 0; i < IM_ARRAYSIZE(pages); ++i)
    {
        if (ImGui::Selectable(
            pages[i],
            selectedPage_ == i,
            0,
            ImVec2(0, 38)))
        {
            selectedPage_ = i;
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // ========================================================
    // MAIN CONTENT
    // ========================================================

    ImGui::BeginChild(
        "MainContent",
        ImVec2(0, 0),
        false
    );
    switch (selectedPage_)
    {
    case 0:
        ImGui::Text("Dashboard");
        ImGui::TextDisabled(
            "System overview will be implemented here."
        );
        break;

    case 1:
        UI::Dashboard::Cpu::CpuDashboard::Render(
            snapshot,
            cpuHistory,
            cpuHistoryIndex,
            cpuHistoryCount,
            processorHistories,
            showProcessorCharts_);
        break;

    case 2:
        if (hasMemorySnapshot)
        {
            UI::Dashboard::Memory::MemoryDashboard::Render(
                memorySnapshot,
                memoryHistory,
                memoryHistoryIndex,
                memoryHistoryCount
            );
        }
        else
        {
            ImGui::TextColored(
                ImVec4(0.30f, 0.85f, 0.72f, 1.0f),
                "MEMORY MONITOR"
            );

            ImGui::TextDisabled(
                "Waiting for memory information..."
            );
        }
        break;

    case 3:
        if (hasGpuSnapshot)
        {
            UI::Dashboard::Gpu::GpuDashboard::Render(
                gpuSnapshot,
                gpuHistory
            );
        }
        else
        {
            ImGui::TextColored(
                ImVec4(0.30f, 0.85f, 0.72f, 1.0f),
                "GPU MONITOR"
            );

            ImGui::TextDisabled(
                "Waiting for GPU information..."
            );
        }
        break;

    case 4:
        if (hasProcessSnapshot)
        {
            processDashboard_.Render(processSnapshot);
        }
        else
        {
            ImGui::TextColored(
                ImVec4(0.30f, 0.85f, 0.72f, 1.0f),
                "PROCESS MONITOR"
            );

            ImGui::TextDisabled(
                "Waiting for process information..."
            );
        }
        break;

    case 5:
        ImGui::Text("Settings");
        ImGui::TextDisabled(
            "Application settings will be implemented here."
        );
        break;

    default:
        selectedPage_ = 0;
        break;
    }

    ImGui::EndChild();

    ImGui::End();
}

// ============================================================
// WINDOW PROCEDURE
// ============================================================

LRESULT CALLBACK Window::WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    Window* window = nullptr;

    if (message == WM_NCCREATE)
    {
        const auto* createStruct =
            reinterpret_cast<CREATESTRUCTW*>(lParam);

        window = static_cast<Window*>(createStruct->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(window));

        window->hwnd_ = hwnd;
    }
    else
    {
        window = reinterpret_cast<Window*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    const bool imguiHandled =
        ImGui_ImplWin32_WndProcHandler(
            hwnd, message, wParam, lParam) != 0;

    switch (message)
    {
    case WM_DATA_UPDATED:
    {
        if (window)
        {
            window->needsRedraw_.store(
                true, std::memory_order_release);
        }

        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEMOVE:
    case WM_MOUSEHWHEEL:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_CAPTURECHANGED:
    {
        if (window)
        {
            window->needsRedraw_.store(
                true, std::memory_order_release);
        }

        break;
    }

    case WM_SIZE:
    {
        if (window && wParam != SIZE_MINIMIZED)
        {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);

            window->renderer_.Resize(width, height);

            window->needsRedraw_.store(
                true, std::memory_order_release);
        }

        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }

    if (imguiHandled)
        return 1;

    return DefWindowProcW(hwnd, message, wParam, lParam);
}