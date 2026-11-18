
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
    constexpr auto frameDuration = std::chrono::milliseconds(16);

    auto lastFrame = Clock::now();

    while (message.message != WM_QUIT)
    {
        if (PeekMessageW(
            &message,
            nullptr,
            0,
            0,
            PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            continue;
        }

        auto now = Clock::now();

        if (now - lastFrame < frameDuration)
        {
            Sleep(1);
            continue;
        }

        lastFrame = now;

        renderer_.BeginFrame();

        RenderDashboard();

        renderer_.EndFrame();
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

    std::size_t cpuHistoryIndex;
    std::array<float, HistorySize> cpuHistory{};

    std::size_t cpuHistoryCount = 0;

    std::vector<ProcessorChartHistory>
        processorHistories;

    {
        std::lock_guard<std::mutex> lock(
            sharedState_->mutex
        );

        snapshot = sharedState_->snapshot;

        cpuHistory = sharedState_->cpuHistory;

        cpuHistoryCount = sharedState_->historyCount;

        processorHistories =   sharedState_->processorHistories;
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
    cpuHistoryIndex = sharedState_.get()->historyIndex;
    cpuHistoryCount = sharedState_.get()->historyCount;
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
        ImGui::Text("RAM");
        ImGui::TextDisabled(
            "Memory dashboard will be implemented here."
        );
        break;

    case 3:
        ImGui::Text("GPU");
        ImGui::TextDisabled(
            "GPU dashboard will be implemented here."
        );
        break;

    case 4:
        ImGui::Text("Processes");
        ImGui::TextDisabled(
            "Process manager will be implemented here."
        );
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
    if (ImGui_ImplWin32_WndProcHandler(
        hwnd,
        message,
        wParam,
        lParam))
    {
        return 1;
    }

    Window* window = nullptr;

    if (message == WM_NCCREATE)
    {
        const auto* createStruct =
            reinterpret_cast<CREATESTRUCTW*>(lParam);

        window = static_cast<Window*>(
            createStruct->lpCreateParams
            );

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(window)
        );

        window->hwnd_ = hwnd;
    }
    else
    {
        window = reinterpret_cast<Window*>(
            GetWindowLongPtrW(
                hwnd,
                GWLP_USERDATA
            )
            );
    }

    switch (message)
    {
    case WM_DATA_UPDATED:
        return 0;

    case WM_SIZE:
        if (window && wParam != SIZE_MINIMIZED)
        {
            window->renderer_.Resize(
                LOWORD(lParam),
                HIWORD(lParam)
            );
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            0
        );
        break;
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam
    );
}
