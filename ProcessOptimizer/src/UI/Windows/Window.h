
#pragma once

#include <Windows.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>
#include <array>

#include "../Rendering/ImGuiRenderer.h"
#include "../Screens/Cpu/CpuDashboard.h"
#include "../../Core/Events/EventSystem.h"
#include "../../Monitoring/SystemMonitor/SystemMonitor.h"
#include "../../Monitoring/MemoryMonitor/MemoryInfo.h"
#include "../Screens/Gpu/GpuDashboard.h"
#include "../Screens/Gpu/GpuDashboard.h"
class Window
{
public:
    explicit Window(HINSTANCE instance);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Create(int showCommand);
    int Run();

private:
    // Window
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;

    static constexpr wchar_t ClassName[] =
        L"ProcessOptimizerMainWindow";

    static constexpr UINT WM_DATA_UPDATED =
        WM_APP + 1;

    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    // Core
    EventSystem events_;

    std::shared_ptr<SystemMonitor> monitor_;

    std::shared_ptr<SharedState> sharedState_;

    ImGuiRenderer renderer_;

    // Navigation
    int selectedPage_ = 0;

    // CPU Dashboard
    bool showProcessorCharts_ = false;

    // Events
    std::size_t cpuSubscriptionId_ = 0;
    std::size_t memorySubscriptionId_ = 0;
    std::size_t gpuSubscriptionId_ = 0;
    bool subscribed_ = false;

    // Monitoring
    void StartMonitoring();
    void StopMonitoring();

    // Rendering
    void RenderDashboard();
    void ApplyTheme();

    // CPU history
    void AddCpuSample(
        SharedState& state,
        float value
    );

    void AddProcessorSamples(
        SharedState& state,
        const CpuSnapshot& snapshot
    );

    MemorySnapshot memorySnapshot_{};

    std::array<float, HistorySize> memoryHistory_{};
    std::size_t memoryHistoryIndex_ = 0;
    std::size_t memoryHistoryCount_ = 0;
    bool hasMemorySnapshot_ = false;

    // Memory history
    void AddMemorySample(
        float value
    );
    // GPU Dashboard
    GpuSnapshot gpuSnapshot_{};
    UI::Dashboard::Gpu::GpuDashboardHistory gpuHistory_{};
    bool hasGpuSnapshot_ = false;

    void AddGpuSample(const GpuSnapshot& snapshot);
};
