
#pragma once

#include <Windows.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "../../Core/Events/EventSystem.h"
#include "../../IPC/PipeTransport.h"
#include "../../Monitoring/SystemMonitor/SystemMonitor.h"
#include "../../Monitoring/CpuMonitor/CpuInfo.h"
#include "../../Monitoring/MemoryMonitor/MemoryInfo.h"
#include "../../Monitoring/GpuMonitor/GpuInfo.h"
#include "../../Monitoring/ProcessMonitor/ProcessInfo.h"
#include "../Rendering/ImGuiRenderer.h"
#include "../Rendering/Core/UiScheduler/UiScheduler.h"
#include "../Rendering/ImGui/ImGuiRegionRenderer.h"
#include "../Components/Widgets/Widgets.h"
#include "../Screens/Cpu/CpuDashboard.h"
#include "../Screens/Dashboard/OverviewDashboard.h"
#include "../Screens/Memory/MemoryDashboard.h"
#include "../Screens/Gpu/GpuDashboard.h"
#include "../Screens/Process/ProcessDashboard.h"
#include "../Screens/Optimization/OptimizationScreen.h"
#include "../Screens/Settings/SettingsScreen.h"
#include "../Theme/Theme.h"

struct SharedState
{
    std::mutex mutex;

    HWND hwnd = nullptr;

    // CPU
    CpuSnapshot cpuSnapshot{};
    bool hasCpuSnapshot = false;

    std::array<
        float,
        UI::Dashboard::Cpu::HistorySize
    > cpuHistory{};

    std::size_t cpuHistoryIndex = 0;
    std::size_t cpuHistoryCount = 0;

    // Historial individual de procesadores lógicos
    std::vector<
        UI::Dashboard::Cpu::ProcessorChartHistory
    > processorHistories;

    // MEMORY
    MemorySnapshot memorySnapshot{};
    bool hasMemorySnapshot = false;

    static constexpr std::size_t MemoryHistorySize = 300;

    std::array<float, MemoryHistorySize> memoryHistory{};

    std::size_t memoryHistoryIndex = 0;
    std::size_t memoryHistoryCount = 0;

    // GPU
    GpuSnapshot gpuSnapshot{};
    bool hasGpuSnapshot = false;
    UI::Dashboard::Gpu::GpuDashboardHistory gpuHistory{};

    // OVERVIEW: tres series alineadas para el grafico combinado.
    UI::Dashboard::Overview::CombinedHistory overviewHistory{};

    // PROCESSES
    ProcessSnapshot processSnapshot{};
    bool hasProcessSnapshot = false;

    // OPTIMIZATION
    Optimization::TopologySnapshot topology{};
    Optimization::OptimizerState optimizer{};
    bool hasTopology = false;
    bool hasOptimizer = false;
};
class Window
{
public:
    Window();
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Create(HINSTANCE instance);
    void Run();

    void StartMonitoring();
    void StopMonitoring();

    bool IsServiceMode() const
    {
        return serviceMode_.load();
    }

private:
    void StartServiceClient();
    void StopServiceClient();
    void ServiceClientLoop();
    void ApplyRemoteState(const IPC::State& state);
    void ApplySavedOptimizerConfig(IPC::PipeClient& client);
    void SendForegroundProcess(IPC::PipeClient& client);
    bool IsInteractingWithUI() const;
    static constexpr wchar_t ClassName[] =
        L"ProcessOptimizerMainWindow";

    static constexpr UINT WM_DATA_UPDATED =
        WM_APP + 1;

    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    void RenderDashboard();
    void RenderRegionContent();

    void ApplyTheme();
    void OnSystemThemeChanged();
    void SetAppSettings(const UI::Theme::AppSettings& settings);
    const UI::Theme::AppSettings& AppSettingsRef() const;
    const UI::Theme::UiPalette& CurrentPalette() const;

    void AddCpuSample(float value);
    void AddMemorySample(float value);
    void AddGpuSample(float value);

    // Invalida solo los componentes de la página activa. Las páginas estáticas
    // no tienen componentes: con solicitar un frame basta.
    static std::uint32_t PageDirtyMask(int page);

    void InvalidateCurrentPage();
    void MarkRegionDirty();
    void RequestFrame();

    bool InitializeRegionRenderer();
    void ShutdownRegionRenderer();

private:
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;

    std::atomic<bool> needsRedraw_{ true };
    std::atomic<bool> uiInputPending_{ true };
    UI::Core::UiScheduler scheduler_;

    float currentFPS_ = 45.0f;

    EventSystem events_;

    std::shared_ptr<SystemMonitor> monitor_;
    std::shared_ptr<SharedState> sharedState_;

    ImGuiRenderer renderer_;
    ImGuiRegionRenderer regionRenderer_;

    bool regionRendererInitialized_ = false;
    bool regionDirty_ = true;

    int selectedPage_ = 0;

    UI::Theme::ThemeSettings themeSettings_{};
    UI::Theme::UiPalette palette_{};

    bool showProcessorCharts_ = true;
    bool subscribed_ = false;

    // Suscripciones a eventos.
    std::size_t cpuSubscription_ = 0;
    std::size_t memorySubscription_ = 0;
    std::size_t gpuSubscription_ = 0;
    std::size_t processSubscription_ = 0;

    // Snapshots.
    CpuSnapshot cpuSnapshot_{};
    MemorySnapshot memorySnapshot_{};
    GpuSnapshot gpuSnapshot_{};
    ProcessSnapshot processSnapshot_{};

    // Historiales.
    static constexpr std::size_t HistorySize = 120;

    std::array<float, HistorySize> cpuHistory_{};
    std::array<float, HistorySize> memoryHistory_{};
    std::array<float, HistorySize> gpuHistory_{};

    std::size_t cpuHistoryIndex_ = 0;
    std::size_t memoryHistoryIndex_ = 0;
    std::size_t gpuHistoryIndex_ = 0;

    std::size_t cpuHistoryCount_ = 0;
    std::size_t memoryHistoryCount_ = 0;
    std::size_t gpuHistoryCount_ = 0;

    ProcessDashboard processDashboard_;

    // Cliente del servicio de monitorizacion. Si el pipe no responde,
    // StartMonitoring mantiene el monitor local para que la UI siga
    // funcionando por si sola.
    std::atomic<bool> serviceMode_{ false };
    std::atomic<bool> clientThreadRunning_{ false };
    std::thread clientThread_;
    std::uint64_t lastRemoteSequence_ = 0;

    // Preferencia publicada por la pagina de optimizacion al hilo cliente,
    // para leerla sin acceder a themeSettings_ desde otro hilo.
    std::atomic<bool> optimizerEnabledPref_{ false };
    std::atomic<int> optimizerModePref_{ 0 };
    std::atomic<bool> optimizerConfigSent_{ false };
    DWORD lastForegroundPid_ = 0;
};
