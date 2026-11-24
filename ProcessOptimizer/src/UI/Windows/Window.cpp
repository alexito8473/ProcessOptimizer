
#include "Window.h"
#include "../../Service/MonitorState.h"
#include "../../../resource.h"
#include <imgui.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <span>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);

namespace
{
    const char* Tr(
        UI::Localization::Language language,
        UI::Localization::Text text)
    {
        return UI::Localization::Translate(language, text);
    }

    void AppendGpuHistorySample(
        UI::Dashboard::Gpu::GpuDashboardHistory& history,
        const GpuSnapshot& snapshot)
    {
        constexpr std::size_t capacity =
            UI::Dashboard::Gpu::GpuHistorySize;

        for (const GpuAdapterInfo& gpu : snapshot.gpus)
        {
            auto it = std::find_if(
                history.adapters.begin(),
                history.adapters.end(),
                [&gpu](const auto& entry)
                {
                    return entry.luidLowPart == gpu.luidLowPart &&
                           entry.luidHighPart == gpu.luidHighPart;
                });

            if (it == history.adapters.end())
            {
                history.adapters.emplace_back();
                it = std::prev(history.adapters.end());

                it->luidLowPart = gpu.luidLowPart;
                it->luidHighPart = gpu.luidHighPart;
            }

            auto& entry = *it;
            const std::size_t index = entry.historyIndex;

            entry.usage[index] =
                static_cast<float>(gpu.usagePercent);
            entry.usage3D[index] =
                static_cast<float>(gpu.usage3D);
            entry.usageCopy[index] =
                static_cast<float>(gpu.usageCopy);
            entry.usageCompute[index] =
                static_cast<float>(gpu.usageCompute);
            entry.usageVideoDecode[index] =
                static_cast<float>(gpu.usageVideoDecode);
            entry.usageVideoEncode[index] =
                static_cast<float>(gpu.usageVideoEncode);
            entry.usageVideoProcessing[index] =
                static_cast<float>(gpu.usageVideoProcessing);
            entry.dedicatedMemory[index] = static_cast<float>(
                gpu.processDedicatedMemoryUsedBytes /
                (1024.0 * 1024.0));
            entry.sharedMemory[index] = static_cast<float>(
                gpu.processSharedMemoryUsedBytes /
                (1024.0 * 1024.0));

            entry.historyIndex = (index + 1) % capacity;

            if (entry.historyCount < capacity)
                ++entry.historyCount;
        }
    }

    void AppendOverviewSample(SharedState& state)
    {
        constexpr std::size_t capacity =
            UI::Dashboard::Overview::HistorySize;

        double cpuUsage = 0.0;
        double memoryUsage = 0.0;

        if (state.hasCpuSnapshot)
            cpuUsage = state.cpuSnapshot.totalUsagePercent;

        if (state.hasMemorySnapshot)
            memoryUsage = state.memorySnapshot.usagePercent;

        const auto clampUsage =
            [](double value)
            {
                return static_cast<float>(
                    (std::max)(0.0, (std::min)(value, 100.0)));
            };

        const auto adapterNameToUtf8 =
            [](const std::wstring& value)
            {
                if (value.empty())
                    return std::string();

                const int size = WideCharToMultiByte(
                    CP_UTF8, 0, value.c_str(), -1,
                    nullptr, 0, nullptr, nullptr);

                if (size <= 1)
                    return std::string();

                std::string result(
                    static_cast<std::size_t>(size), '\0');

                WideCharToMultiByte(
                    CP_UTF8, 0, value.c_str(), -1,
                    result.data(), size, nullptr, nullptr);

                result.pop_back();
                return result;
            };

        auto& history = state.overviewHistory;
        const std::size_t index = history.index;

        // Registrar todos los adaptadores presentes. El LUID evita mezclar
        // muestras aunque Windows cambie el orden de la lista.
        if (state.hasGpuSnapshot)
        {
            for (const GpuAdapterInfo& gpu :
                 state.gpuSnapshot.gpus)
            {
                auto it = std::find_if(
                    history.gpus.begin(),
                    history.gpus.end(),
                    [&gpu](
                        const UI::Dashboard::Overview::GpuSeries& series)
                    {
                        return series.luidLowPart ==
                                   gpu.luidLowPart &&
                               series.luidHighPart ==
                                   gpu.luidHighPart;
                    });

                if (it == history.gpus.end())
                {
                    UI::Dashboard::Overview::GpuSeries series{};

                    series.luidLowPart = gpu.luidLowPart;
                    series.luidHighPart = gpu.luidHighPart;
                    series.name = adapterNameToUtf8(gpu.name);

                    history.gpus.push_back(series);
                }
                else if (it->name.empty())
                {
                    it->name = adapterNameToUtf8(gpu.name);
                }
            }
        }

        history.cpu[index] = clampUsage(cpuUsage);
        history.memory[index] = clampUsage(memoryUsage);

        for (UI::Dashboard::Overview::GpuSeries& series :
             history.gpus)
        {
            double gpuUsage = 0.0;

            if (state.hasGpuSnapshot)
            {
                auto it = std::find_if(
                    state.gpuSnapshot.gpus.begin(),
                    state.gpuSnapshot.gpus.end(),
                    [&series](const GpuAdapterInfo& gpu)
                    {
                        return gpu.luidLowPart ==
                                   series.luidLowPart &&
                               gpu.luidHighPart ==
                                   series.luidHighPart;
                    });

                if (it != state.gpuSnapshot.gpus.end())
                    gpuUsage = it->usagePercent;
            }

            series.usage[index] = clampUsage(gpuUsage);
        }

        history.index = (index + 1) % capacity;

        if (history.count < capacity)
            ++history.count;
    }

    void AppendProcessorHistorySamples(SharedState& state)
    {
        if (!state.hasCpuSnapshot)
            return;

        auto& histories = state.processorHistories;

        for (const LogicalProcessorInfo& processor :
             state.cpuSnapshot.logicalProcessors)
        {
            auto it = std::find_if(
                histories.begin(),
                histories.end(),
                [&processor](
                    const UI::Dashboard::Cpu::ProcessorChartHistory& h)
                {
                    return h.group == processor.processorGroup &&
                           h.logicalIndex ==
                               processor.logicalProcessorIndex;
                });

            if (it == histories.end())
            {
                UI::Dashboard::Cpu::ProcessorChartHistory history{};

                history.group = processor.processorGroup;
                history.logicalIndex =
                    processor.logicalProcessorIndex;

                histories.push_back(history);
                it = std::prev(histories.end());
            }

            auto& history = *it;
            const std::size_t index = history.historyIndex;

            history.values[index] = static_cast<float>(
                (std::max)(0.0, (std::min)(
                    processor.usagePercent, 100.0)));

            history.historyIndex =
                (index + 1) % history.values.size();

            if (history.historyCount < history.values.size())
                ++history.historyCount;
        }
    }

    template<std::size_t N>
    void CopyRing(
        const IPC::FloatRing<N>& source,
        std::array<float, N>& destination,
        std::size_t& index,
        std::size_t& count)
    {
        destination = source.values;
        index = source.index % N;
        count = (std::min)(source.count, N);
    }
}

std::uint32_t Window::PageDirtyMask(int page)
{
    using UI::Core::UiComponent;

    switch (page)
    {
    case 0:
        return UI::Core::Mask(UiComponent::OverviewPage);
    case 1:
        return UI::Core::Mask(UiComponent::CpuPage);
    case 2:
        return UI::Core::Mask(UiComponent::MemorySummary) |
               UI::Core::Mask(UiComponent::MemoryChart) |
               UI::Core::Mask(UiComponent::MemoryModules);
    case 3:
        return UI::Core::Mask(UiComponent::GpuPage);
    case 4:
        return UI::Core::Mask(UiComponent::ProcessPage);
    default:
        // Settings es estático: no tiene datos que invalidar.
        return 0;
    }
}

Window::Window()
{
    sharedState_ = std::make_shared<SharedState>();
    themeSettings_ = UI::Theme::LoadSettings();

    // Crear el archivo con los valores por defecto si no existe, para que
    // se pueda editar directamente antes de abrir la aplicacion.
    UI::Theme::SaveSettings(themeSettings_);
}

Window::~Window()
{
    StopServiceClient();
    StopMonitoring();
    ShutdownRegionRenderer();
}

bool Window::Create(HINSTANCE instance)
{
    instance_ = instance;

    HICON appIcon = LoadIconW(
        instance_, MAKEINTRESOURCEW(IDI_APP));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = ClassName;
    wc.hIcon = appIcon;
    wc.hIconSm = appIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    if (!RegisterClassExW(&wc))
    {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }

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
        this);

    if (!hwnd_)
        return false;

    sharedState_->hwnd = hwnd_;

    SendMessageW(
        hwnd_, WM_SETICON, ICON_BIG,
        reinterpret_cast<LPARAM>(appIcon));
    SendMessageW(
        hwnd_, WM_SETICON, ICON_SMALL,
        reinterpret_cast<LPARAM>(appIcon));

    if (!renderer_.Initialize(hwnd_))
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }

    // La inicialización del renderer de regiones se conecta
    // cuando se verifiquen los getters reales de ImGuiRenderer.
    regionRendererInitialized_ = false;

    ApplyTheme();

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    // Si el servicio ya esta disponible no arrancamos el monitor local:
    // el historico lo mantiene el servicio y aqui solo se consume.
    IPC::PipeClient probe;
    if (!probe.Ping())
        StartMonitoring();

    StartServiceClient();

    return true;
}



void Window::Run()
{
    MSG message{};

    using Clock = std::chrono::steady_clock;

    constexpr auto frameDuration =
        std::chrono::microseconds(16667);

    auto lastFrame = Clock::now();

    bool continuousRendering = false;

#ifdef _DEBUG
    std::uint64_t framesRendered = 0;
    std::uint64_t lastPresentCount =
        renderer_.GetPresentCount();
    auto lastStats = Clock::now();
#endif

    while (true)
    {
        while (PeekMessageW(
            &message,
            nullptr,
            0,
            0,
            PM_REMOVE))
        {
            if (message.message == WM_QUIT)
                return;

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        const bool interacting = continuousRendering;

        const bool inputPending =
            uiInputPending_.exchange(
                false, std::memory_order_acq_rel);

        if (!needsRedraw_.load(std::memory_order_acquire) &&
            !interacting &&
            !inputPending)
        {
            MsgWaitForMultipleObjectsEx(
                0,
                nullptr,
                INFINITE,
                QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);

            continue;
        }

        const std::uint32_t pageMask =
            PageDirtyMask(selectedPage_);

        if (!interacting && !inputPending)
        {
            if ((pageMask != 0 &&
                 !scheduler_.HasDirty(pageMask)) ||
                pageMask == 0)
            {
                // Dato de otra página, o dato llegado mientras se muestra
                // una página estática: conservamos los bits y no presentamos.
                needsRedraw_.store(
                    false, std::memory_order_release);

                continue;
            }
        }

        const auto now = Clock::now();
        const auto elapsed = now - lastFrame;

        // La UI se mantiene a 60 FPS. El ahorro de GPU no se hace bajando
        // la tasa de entrada, sino evitando Present cuando el frame dibujado
        // es idéntico (ver ImGuiRenderer::EndFrame).
        if (elapsed < frameDuration)
        {
            const auto remaining = frameDuration - elapsed;

            const DWORD waitMilliseconds =
                static_cast<DWORD>(
                    std::chrono::duration_cast<
                    std::chrono::milliseconds>(
                        remaining +
                        std::chrono::microseconds(999)).count());

            MsgWaitForMultipleObjectsEx(
                0,
                nullptr,
                waitMilliseconds,
                QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);

            continue;
        }

        lastFrame = Clock::now();

        needsRedraw_.store(
            false, std::memory_order_release);

        if (pageMask != 0)
            scheduler_.ConsumePage(pageMask);

        renderer_.BeginFrame();

        RenderDashboard();

        renderer_.EndFrame();

#ifdef _DEBUG
        ++framesRendered;

        const auto statsNow = Clock::now();

        if (statsNow - lastStats >= std::chrono::seconds(2))
        {
            const std::uint64_t presentCount =
                renderer_.GetPresentCount();

            const std::uint64_t presents =
                presentCount - lastPresentCount;

            char text[160]{};

            std::snprintf(
                text,
                sizeof(text),
                "[ProcessOptimizer] frames/2s=%llu "
                "presents/2s=%llu\n",
                static_cast<unsigned long long>(framesRendered),
                static_cast<unsigned long long>(presents));

            OutputDebugStringA(text);

            framesRendered = 0;
            lastPresentCount = presentCount;
            lastStats = statsNow;
        }
#endif

        continuousRendering = IsInteractingWithUI();

        if (continuousRendering)
        {
            needsRedraw_.store(
                true, std::memory_order_release);
        }
    }
}


bool Window::IsInteractingWithUI() const
{
    if (!ImGui::GetCurrentContext())
        return false;

    const ImGuiIO& io = ImGui::GetIO();

    // El hover NO entra aquí: se actualiza con los mensajes de entrada
    // (WM_MOUSEMOVE invalida la página activa). Meterlo aquí realimentaba
    // needsRedraw_ y mantenía 60 FPS con datos estáticos.
    return
        ImGui::IsAnyItemActive() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
        io.MouseDown[0] ||
        io.MouseDown[1] ||
        io.MouseDown[2] ||
        io.MouseWheel != 0.0f ||
        io.MouseWheelH != 0.0f;
}

void Window::RenderDashboard()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(
        ImGui::GetIO().DisplaySize);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse;

    ImGui::Begin(
        "MainWindow",
        nullptr,
        flags);

    // Sidebar.
    ImGui::BeginChild(
        "Sidebar",
        ImVec2(220, 0),
        true);

    const UI::Localization::Language language =
        themeSettings_.language;

    UI::Widgets::Title(
        Tr(language, UI::Localization::Text::AppTitle),
        palette_);

    ImGui::Separator();

    struct PageEntry
    {
        int index;
        UI::Localization::Text label;
    };

    constexpr PageEntry pages[]{
        {0, UI::Localization::Text::Dashboard},
        {1, UI::Localization::Text::Cpu},
        {2, UI::Localization::Text::Ram},
        {3, UI::Localization::Text::Gpu},
        {4, UI::Localization::Text::Processes},
        {5, UI::Localization::Text::Settings}
    };

    for (const PageEntry& page : pages)
    {
        if (UI::Widgets::ToggleButton(
                Tr(language, page.label),
                selectedPage_ == page.index,
                palette_))
        {
            selectedPage_ = page.index;
            InvalidateCurrentPage();
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // Main content.
    ImGui::BeginChild(
        "MainContent",
        ImVec2(0, 0),
        false);

    RenderRegionContent();

    ImGui::EndChild();

    ImGui::End();
}

void Window::RenderRegionContent()
{
    switch (selectedPage_)
    {
    case 0:
    {
        CpuSnapshot cpu{};
        MemorySnapshot memory{};
        GpuSnapshot gpu{};
        ProcessSnapshot processes{};
        UI::Dashboard::Overview::CombinedHistory history{};

        bool hasCpu = false;
        bool hasMemory = false;
        bool hasGpu = false;
        bool hasProcesses = false;

        {
            std::lock_guard<std::mutex> lock(
                sharedState_->mutex);

            hasCpu = sharedState_->hasCpuSnapshot;
            hasMemory = sharedState_->hasMemorySnapshot;
            hasGpu = sharedState_->hasGpuSnapshot;
            hasProcesses = sharedState_->hasProcessSnapshot;

            if (hasCpu)
                cpu = sharedState_->cpuSnapshot;

            if (hasMemory)
                memory = sharedState_->memorySnapshot;

            if (hasGpu)
                gpu = sharedState_->gpuSnapshot;

            if (hasProcesses)
                processes = sharedState_->processSnapshot;

            history = sharedState_->overviewHistory;
        }

        UI::Dashboard::Overview::OverviewDashboard::Render(
            hasCpu ? &cpu : nullptr,
            hasMemory ? &memory : nullptr,
            hasGpu ? &gpu : nullptr,
            hasProcesses ? &processes : nullptr,
            history,
            palette_,
            themeSettings_.language);

        break;
    }

    case 1:
    {
        CpuSnapshot snapshot{};

        std::array<
            float,
            UI::Dashboard::Cpu::HistorySize
        > history{};

        std::vector<
            UI::Dashboard::Cpu::ProcessorChartHistory
        > processorHistories;

        std::size_t historyIndex = 0;
        std::size_t historyCount = 0;

        bool available = false;

        {
            std::lock_guard<std::mutex> lock(
                sharedState_->mutex);

            available = sharedState_->hasCpuSnapshot;

            if (available)
            {
                snapshot = sharedState_->cpuSnapshot;
                history = sharedState_->cpuHistory;

                historyIndex = sharedState_->cpuHistoryIndex;
                historyCount = sharedState_->cpuHistoryCount;

                processorHistories =
                    sharedState_->processorHistories;
            }
        }

        if (available)
        {
            UI::Dashboard::Cpu::CpuDashboard::Render(
                snapshot,
                history,
                historyIndex,
                historyCount,
                processorHistories,
                showProcessorCharts_,
                palette_,
                themeSettings_.language);
        }
        else
        {
            ImGui::TextDisabled(
                "%s",
                Tr(themeSettings_.language,
                   UI::Localization::Text::WaitingCpu));
        }

        break;
    }

    case 2:
    {
        MemorySnapshot snapshot{};

        std::array<
            float,
            SharedState::MemoryHistorySize
        > history{};

        std::size_t historyIndex = 0;
        std::size_t historyCount = 0;

        bool available = false;

        {
            std::lock_guard<std::mutex> lock(
                sharedState_->mutex);

            available = sharedState_->hasMemorySnapshot;

            if (available)
            {
                snapshot = sharedState_->memorySnapshot;
                history = sharedState_->memoryHistory;
                historyIndex =
                    sharedState_->memoryHistoryIndex;
                historyCount =
                    sharedState_->memoryHistoryCount;
            }
        }

        if (available)
        {
            UI::Dashboard::Memory::MemoryDashboard::Render(
                snapshot,
                std::span<const float>(
                    history.data(), history.size()),
                historyIndex,
                historyCount);
        }
        else
        {
            ImGui::TextDisabled(
                "%s",
                Tr(themeSettings_.language,
                   UI::Localization::Text::WaitingMemory));
        }

        break;
    }

    case 3:
    {
        GpuSnapshot snapshot{};
        UI::Dashboard::Gpu::GpuDashboardHistory history{};

        bool available = false;

        {
            std::lock_guard<std::mutex> lock(
                sharedState_->mutex);

            available = sharedState_->hasGpuSnapshot;

            if (available)
            {
                snapshot = sharedState_->gpuSnapshot;
                history = sharedState_->gpuHistory;
            }
        }

        if (available)
        {
            UI::Dashboard::Gpu::GpuDashboard::Render(
                snapshot, history);
        }
        else
        {
            ImGui::TextDisabled(
                "%s",
                Tr(themeSettings_.language,
                   UI::Localization::Text::WaitingGpu));
        }

        break;
    }

    case 4:
    {
        ProcessSnapshot snapshot{};

        bool available = false;

        {
            std::lock_guard<std::mutex> lock(
                sharedState_->mutex);

            available = sharedState_->hasProcessSnapshot;

            if (available)
                snapshot = sharedState_->processSnapshot;
        }

        if (available)
        {
            processDashboard_.Render(snapshot);
        }
        else
        {
            ImGui::TextDisabled(
                "%s",
                Tr(themeSettings_.language,
                   UI::Localization::Text::WaitingProcesses));
        }

        break;
    }

    case 5:
    {
        UI::Theme::AppSettings settings =
            themeSettings_;

        const bool serviceChanged =
            UI::Screens::Settings::SettingsScreen::Render(
                settings,
                palette_,
                themeSettings_.language);

        SetAppSettings(settings);

        if (serviceChanged)
        {
            // Settings no tiene bits de datos propios: el render es bajo
            // demanda, asi que una accion Start/Stop necesita pedir un
            // frame nuevo para que el badge cambie en cuanto el SCM
            // confirme el estado.
            scheduler_.Invalidate(
                UI::Core::UiComponent::All);
            RequestFrame();
        }
        break;
    }

    default:
        ImGui::Text("Unknown page");
        break;
    }
}


bool Window::InitializeRegionRenderer()
{
    OutputDebugStringA("\n========== REGION RENDERER INIT ==========\n");

    if (regionRendererInitialized_)
    {
        OutputDebugStringA("[OK] RegionRenderer ya estaba inicializado.\n");
        return true;
    }

    // 1. Comprobar renderer principal.
    if (!renderer_.IsInitialized())
    {
        OutputDebugStringA("[ERROR] ImGuiRenderer no esta inicializado.\n");
        return false;
    }

    OutputDebugStringA("[OK] ImGuiRenderer inicializado.\n");

    // 2. Comprobar dispositivo DirectX.
    ID3D11Device* device = renderer_.GetDevice();

    if (!device)
    {
        OutputDebugStringA("[ERROR] GetDevice() devuelve nullptr.\n");
        return false;
    }

    OutputDebugStringA("[OK] Dispositivo D3D11 disponible.\n");

    // 3. Comprobar contexto DirectX.
    ID3D11DeviceContext* context = renderer_.GetDeviceContext();

    if (!context)
    {
        OutputDebugStringA("[ERROR] GetDeviceContext() devuelve nullptr.\n");
        return false;
    }

    OutputDebugStringA("[OK] Contexto D3D11 disponible.\n");

    // 4. Comprobar contexto ImGui.
    ImGuiContext* imguiContext = renderer_.GetImGuiContext();

    if (!imguiContext)
    {
        OutputDebugStringA("[ERROR] GetImGuiContext() devuelve nullptr.\n");
        return false;
    }

    OutputDebugStringA("[OK] Contexto ImGui disponible.\n");

    // 5. Inicializar RegionRenderer.
    OutputDebugStringA("[INFO] Inicializando RegionRenderer...\n");

    if (!regionRenderer_.Initialize(device, context, imguiContext))
    {
        OutputDebugStringA("[ERROR] RegionRenderer::Initialize fallo.\n");
        return false;
    }

    OutputDebugStringA("[OK] RegionRenderer inicializado.\n");

    // 6. Crear region CPU.
    OutputDebugStringA("[INFO] Creando region CPU...\n");

    if (!regionRenderer_.CreateRegion("CPU"))
    {
        OutputDebugStringA("[ERROR] No se pudo crear region CPU.\n");
        regionRenderer_.Shutdown();
        return false;
    }

    OutputDebugStringA("[OK] Region CPU creada.\n");

    // 7. Crear region Memory.
    OutputDebugStringA("[INFO] Creando region Memory...\n");

    if (!regionRenderer_.CreateRegion("Memory"))
    {
        OutputDebugStringA("[ERROR] No se pudo crear region Memory.\n");
        regionRenderer_.Shutdown();
        return false;
    }

    OutputDebugStringA("[OK] Region Memory creada.\n");

    // 8. Crear region GPU.
    OutputDebugStringA("[INFO] Creando region GPU...\n");

    if (!regionRenderer_.CreateRegion("GPU"))
    {
        OutputDebugStringA("[ERROR] No se pudo crear region GPU.\n");
        regionRenderer_.Shutdown();
        return false;
    }

    OutputDebugStringA("[OK] Region GPU creada.\n");

    // 9. Crear region Processes.
    OutputDebugStringA("[INFO] Creando region Processes...\n");

    if (!regionRenderer_.CreateRegion("Processes"))
    {
        OutputDebugStringA("[ERROR] No se pudo crear region Processes.\n");
        regionRenderer_.Shutdown();
        return false;
    }

    OutputDebugStringA("[OK] Region Processes creada.\n");

    regionRendererInitialized_ = true;

    OutputDebugStringA("[SUCCESS] RegionRenderer completamente inicializado.\n");
    OutputDebugStringA("==========================================\n");

    return true;
}

void Window::ShutdownRegionRenderer()
{
    if (!regionRendererInitialized_)
        return;

    regionRenderer_.DestroyRegion("CPU");
    regionRenderer_.DestroyRegion("Memory");
    regionRenderer_.DestroyRegion("GPU");
    regionRenderer_.DestroyRegion("Processes");

    regionRenderer_.Shutdown();

    regionRendererInitialized_ = false;
}

void Window::InvalidateCurrentPage()
{
    scheduler_.Invalidate(PageDirtyMask(selectedPage_));

    uiInputPending_.store(true, std::memory_order_release);
    needsRedraw_.store(true, std::memory_order_release);
}

void Window::MarkRegionDirty()
{
    // Solo para cambios globales (resize, tema, fuentes). Los datos de
    // monitorización invalidan componentes concretos.
    regionDirty_ = true;
    scheduler_.InvalidateAll();

    uiInputPending_.store(true, std::memory_order_release);
    needsRedraw_.store(true, std::memory_order_release);
}

void Window::RequestFrame()
{
    needsRedraw_.store(true, std::memory_order_release);

    const HWND hwnd = sharedState_->hwnd;

    if (hwnd)
    {
        PostMessageW(
            hwnd,
            WM_DATA_UPDATED,
            0,
            0);
    }
}

void Window::ApplyTheme()
{
    palette_ = UI::Theme::ResolvePalette(themeSettings_);
    UI::Theme::Apply(palette_);

    scheduler_.Invalidate(UI::Core::UiComponent::All);
    RequestFrame();
}

void Window::OnSystemThemeChanged()
{
    if (themeSettings_.mode != UI::Theme::ThemeMode::System)
        return;

    ApplyTheme();
}

void Window::SetAppSettings(
    const UI::Theme::AppSettings& settings)
{
    if (settings.mode == themeSettings_.mode &&
        settings.design == themeSettings_.design &&
        settings.language == themeSettings_.language)
    {
        return;
    }

    const bool appearanceChanged =
        settings.mode != themeSettings_.mode ||
        settings.design != themeSettings_.design;

    themeSettings_ = settings;
    UI::Theme::SaveSettings(themeSettings_);

    if (appearanceChanged)
        ApplyTheme();
    else
    {
        scheduler_.Invalidate(UI::Core::UiComponent::All);
        RequestFrame();
    }
}

const UI::Theme::AppSettings& Window::AppSettingsRef() const
{
    return themeSettings_;
}

const UI::Theme::UiPalette& Window::CurrentPalette() const
{
    return palette_;
}

void Window::AddCpuSample(float value)
{
    cpuHistory_[cpuHistoryIndex_] = value;
    cpuHistoryIndex_ =
        (cpuHistoryIndex_ + 1) % HistorySize;

    cpuHistoryCount_ =
        (std::min)(cpuHistoryCount_ + 1, HistorySize);

    MarkRegionDirty();
}

void Window::AddMemorySample(float value)
{
    memoryHistory_[memoryHistoryIndex_] = value;
    memoryHistoryIndex_ =
        (memoryHistoryIndex_ + 1) % HistorySize;

    memoryHistoryCount_ =
        (std::min)(memoryHistoryCount_ + 1, HistorySize);

    MarkRegionDirty();
}

void Window::AddGpuSample(float value)
{
    gpuHistory_[gpuHistoryIndex_] = value;
    gpuHistoryIndex_ =
        (gpuHistoryIndex_ + 1) % HistorySize;

    gpuHistoryCount_ =
        (std::min)(gpuHistoryCount_ + 1, HistorySize);

    MarkRegionDirty();
}

void Window::StartMonitoring()
{
    if (subscribed_)
        return;

    monitor_ = std::make_shared<SystemMonitor>(events_);

    cpuSubscription_ =
        events_.Subscribe<CpuSnapshot>(
            "CPU",
            [this](const CpuSnapshot& snapshot)
            {
                bool changed = false;

                {
                    std::lock_guard<std::mutex> lock(
                        sharedState_->mutex);

                    changed =
                        !sharedState_->hasCpuSnapshot ||
                        std::abs(
                            sharedState_->cpuSnapshot
                                .totalUsagePercent -
                            snapshot.totalUsagePercent) > 0.001;

                    // El uso total puede repetirse mientras cambian algunos nucleos.
                    if (!changed)
                    {
                        const auto& previous =
                            sharedState_->cpuSnapshot
                                .logicalProcessors;

                        if (previous.size() !=
                            snapshot.logicalProcessors.size())
                        {
                            changed = true;
                        }
                        else
                        {
                            for (std::size_t i = 0;
                                 i < previous.size();
                                 ++i)
                            {
                                if (std::abs(
                                        previous[i].usagePercent -
                                        snapshot.logicalProcessors[i]
                                            .usagePercent) > 0.001)
                                {
                                    changed = true;
                                    break;
                                }
                            }
                        }
                    }

                    sharedState_->cpuSnapshot = snapshot;
                    sharedState_->hasCpuSnapshot = true;

                    // El historial de cada procesador logico se alimenta en
                    // cada snapshot, aunque el uso total no haya cambiado.
                    AppendProcessorHistorySamples(*sharedState_);

                    if (changed)
                    {
                        const float usage =
                            static_cast<float>(
                                snapshot.totalUsagePercent);

                        sharedState_->cpuHistory[
                            sharedState_->cpuHistoryIndex] = usage;

                        sharedState_->cpuHistoryIndex =
                            (sharedState_->cpuHistoryIndex + 1) %
                            sharedState_->cpuHistory.size();

                        if (sharedState_->cpuHistoryCount <
                            sharedState_->cpuHistory.size())
                        {
                            ++sharedState_->cpuHistoryCount;
                        }

                        AppendOverviewSample(*sharedState_);
                    }
                }

                if (changed)
                {
                    using UI::Core::UiComponent;
                    scheduler_.Invalidate(
                        UI::Core::Mask(UiComponent::CpuPage) |
                        UI::Core::Mask(UiComponent::OverviewPage));
                    RequestFrame();
                }
            });

    memorySubscription_ =
        events_.Subscribe<MemorySnapshot>(
            "MEMORY",
            [this](const MemorySnapshot& snapshot)
            {
                bool changed = false;

                {
                    std::lock_guard<std::mutex> lock(
                        sharedState_->mutex);

                    changed =
                        !sharedState_->hasMemorySnapshot ||
                        std::abs(
                            sharedState_->memorySnapshot
                                .usagePercent -
                            snapshot.usagePercent) > 0.001;

                    sharedState_->memorySnapshot = snapshot;
                    sharedState_->hasMemorySnapshot = true;

                    if (changed)
                    {
                        const float usage =
                            static_cast<float>(
                                snapshot.usagePercent);

                        sharedState_->memoryHistory[
                            sharedState_->memoryHistoryIndex] = usage;

                        sharedState_->memoryHistoryIndex =
                            (sharedState_->memoryHistoryIndex + 1) %
                            sharedState_->memoryHistory.size();

                        if (sharedState_->memoryHistoryCount <
                            sharedState_->memoryHistory.size())
                        {
                            ++sharedState_->memoryHistoryCount;
                        }

                        AppendOverviewSample(*sharedState_);
                    }
                }

                if (changed)
                {
                    using UI::Core::UiComponent;
                    scheduler_.Invalidate(
                        UI::Core::Mask(UiComponent::MemorySummary) |
                        UI::Core::Mask(UiComponent::MemoryChart) |
                        UI::Core::Mask(UiComponent::MemoryModules) |
                        UI::Core::Mask(UiComponent::OverviewPage));
                    RequestFrame();
                }
            });

    gpuSubscription_ =
        events_.Subscribe<GpuSnapshot>(
            "GPU",
            [this](const GpuSnapshot& snapshot)
            {
                double currentMax = -1.0;

                for (const GpuAdapterInfo& gpu : snapshot.gpus)
                {
                    currentMax = (std::max)(
                        currentMax, gpu.usagePercent);
                }

                bool changed = false;

                {
                    std::lock_guard<std::mutex> lock(
                        sharedState_->mutex);

                    if (!sharedState_->hasGpuSnapshot)
                    {
                        changed = true;
                    }
                    else
                    {
                        double previousMax = -1.0;

                        for (const GpuAdapterInfo& gpu :
                             sharedState_->gpuSnapshot.gpus)
                        {
                            previousMax = (std::max)(
                                previousMax, gpu.usagePercent);
                        }

                        changed =
                            previousMax < 0.0 ||
                            currentMax < 0.0 ||
                            std::abs(previousMax - currentMax) >
                                0.01;
                    }

                    sharedState_->gpuSnapshot = snapshot;
                    sharedState_->hasGpuSnapshot = true;

                    if (changed)
                    {
                        AppendGpuHistorySample(
                            sharedState_->gpuHistory,
                            snapshot);

                        AppendOverviewSample(*sharedState_);
                    }
                }

                if (changed)
                {
                    using UI::Core::UiComponent;
                    scheduler_.Invalidate(
                        UI::Core::Mask(UiComponent::GpuPage) |
                        UI::Core::Mask(UiComponent::OverviewPage));
                    RequestFrame();
                }
            });

    processSubscription_ =
        events_.Subscribe<ProcessSnapshot>(
            "PROCESS",
            [this](const ProcessSnapshot& snapshot)
            {
                {
                    std::lock_guard<std::mutex> lock(
                        sharedState_->mutex);

                    sharedState_->processSnapshot = snapshot;
                    sharedState_->hasProcessSnapshot = true;
                }

                using UI::Core::UiComponent;
                scheduler_.Invalidate(
                    UI::Core::Mask(UiComponent::ProcessPage) |
                    UI::Core::Mask(UiComponent::OverviewPage));
                RequestFrame();
            });

    subscribed_ = true;

    monitor_->Start();
}


void Window::StopMonitoring()
{
    if (monitor_)
    {
        monitor_->Stop();
    }

    if (subscribed_)
    {
        events_.Unsubscribe<CpuSnapshot>(
            "CPU",
            cpuSubscription_);

        events_.Unsubscribe<MemorySnapshot>(
            "MEMORY",
            memorySubscription_);

        events_.Unsubscribe<GpuSnapshot>(
            "GPU",
            gpuSubscription_);

        events_.Unsubscribe<ProcessSnapshot>(
            "PROCESS",
            processSubscription_);
    }

    subscribed_ = false;

    monitor_.reset();
}

void Window::StartServiceClient()
{
    clientThreadRunning_.store(true);
    clientThread_ =
        std::thread(&Window::ServiceClientLoop, this);
}

void Window::StopServiceClient()
{
    clientThreadRunning_.store(false);

    if (clientThread_.joinable())
        clientThread_.join();
}

void Window::ServiceClientLoop()
{
    IPC::PipeClient client;

    while (clientThreadRunning_.load())
    {
        IPC::State remote;

        if (client.GetState(remote))
        {
            const bool wasServiceMode =
                serviceMode_.exchange(true);

            if (!wasServiceMode || subscribed_)
                StopMonitoring();

            if (remote.sequence != lastRemoteSequence_)
            {
                ApplyRemoteState(remote);
                lastRemoteSequence_ = remote.sequence;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(200));
            continue;
        }

        if (serviceMode_.exchange(false))
            StartMonitoring();

        std::this_thread::sleep_for(
            std::chrono::milliseconds(1500));
    }
}

void Window::ApplyRemoteState(const IPC::State& state)
{
    {
        std::lock_guard<std::mutex> lock(
            sharedState_->mutex);

        SharedState& shared = *sharedState_;

        if (state.hasCpu)
        {
            shared.cpuSnapshot = state.cpu;
            shared.hasCpuSnapshot = true;

            CopyRing(
                state.cpuHistory,
                shared.cpuHistory,
                shared.cpuHistoryIndex,
                shared.cpuHistoryCount);
        }

        shared.processorHistories.clear();
        for (const IPC::ProcessorHistory& remote :
             state.processorHistories)
        {
            UI::Dashboard::Cpu::ProcessorChartHistory
                history{};

            history.group = remote.group;
            history.logicalIndex = remote.logicalIndex;

            CopyRing(
                remote.values, history.values,
                history.historyIndex,
                history.historyCount);

            shared.processorHistories.push_back(history);
        }

        if (state.hasMemory)
        {
            shared.memorySnapshot = state.memory;
            shared.hasMemorySnapshot = true;

            CopyRing(
                state.memoryHistory,
                shared.memoryHistory,
                shared.memoryHistoryIndex,
                shared.memoryHistoryCount);
        }

        if (state.hasGpu)
        {
            shared.gpuSnapshot = state.gpu;
            shared.hasGpuSnapshot = true;

            shared.gpuHistory.adapters.clear();

            for (const IPC::GpuAdapterHistory& remote :
                 state.gpuHistories)
            {
                UI::Dashboard::Gpu::GpuAdapterHistory
                    history{};

                history.luidLowPart = remote.luidLowPart;
                history.luidHighPart = remote.luidHighPart;

                CopyRing(remote.usage, history.usage,
                         history.historyIndex,
                         history.historyCount);
                history.usage3D = remote.usage3D.values;
                history.usageCopy =
                    remote.usageCopy.values;
                history.usageCompute =
                    remote.usageCompute.values;
                history.usageVideoDecode =
                    remote.usageVideoDecode.values;
                history.usageVideoEncode =
                    remote.usageVideoEncode.values;
                history.usageVideoProcessing =
                    remote.usageVideoProcessing.values;
                history.dedicatedMemory =
                    remote.dedicatedMemory.values;
                history.sharedMemory =
                    remote.sharedMemory.values;

                shared.gpuHistory.adapters.push_back(
                    history);
            }
        }

        UI::Dashboard::Overview::CombinedHistory&
            overview = shared.overviewHistory;

        CopyRing(state.overview.cpu, overview.cpu,
                 overview.index, overview.count);
        overview.memory = state.overview.memory.values;

        overview.gpus.clear();
        for (const IPC::GpuSeriesHistory& remote :
             state.overview.gpus)
        {
            UI::Dashboard::Overview::GpuSeries series{};

            series.luidLowPart = remote.luidLowPart;
            series.luidHighPart = remote.luidHighPart;
            series.name = remote.name;
            series.usage = remote.usage.values;

            overview.gpus.push_back(series);
        }

        if (state.hasProcesses)
        {
            shared.processSnapshot = state.processes;
            shared.hasProcessSnapshot = true;
        }
    }

    std::uint32_t invalidated = 0;

    if (state.changedMask & Service::ChangeMask::Cpu)
        invalidated |= PageDirtyMask(0) | PageDirtyMask(1);
    if (state.changedMask & Service::ChangeMask::Memory)
        invalidated |= PageDirtyMask(0) | PageDirtyMask(2);
    if (state.changedMask & Service::ChangeMask::Gpu)
        invalidated |= PageDirtyMask(0) | PageDirtyMask(3);
    if (state.changedMask & Service::ChangeMask::Processes)
        invalidated |= PageDirtyMask(0) | PageDirtyMask(4);

    if (invalidated == 0)
        invalidated = PageDirtyMask(0);

    scheduler_.Invalidate(invalidated);
    RequestFrame();
}



LRESULT CALLBACK Window::WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    Window* window = reinterpret_cast<Window*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        auto* createStruct =
            reinterpret_cast<CREATESTRUCTW*>(lParam);

        window = static_cast<Window*>(
            createStruct->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(window));
    }

    if (!window)
    {
        return DefWindowProcW(
            hwnd, message, wParam, lParam);
    }

    // Entrada gestionada por Dear ImGui.
    if (ImGui::GetCurrentContext())
    {
        if (ImGui_ImplWin32_WndProcHandler(
            hwnd, message, wParam, lParam))
        {
            return TRUE;
        }
    }

    switch (message)
    {
    case WM_DATA_UPDATED:
    {
        // El callback ya invalidó los componentes concretos. Aquí solo
        // despertamos el bucle; no marcamos toda la UI como sucia.
        window->needsRedraw_.store(
            true, std::memory_order_release);
        return 0;
    }

    case WM_SIZE:
    {
        if (wParam != SIZE_MINIMIZED)
        {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);

            if (width > 0 && height > 0)
            {
                window->renderer_.Resize(width, height);
                window->MarkRegionDirty();
            }
        }

        return 0;
    }

    // Entrada del usuario: solicitar redibujado a 60 FPS.
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    {
        window->InvalidateCurrentPage();
        break;
    }

    case WM_SETTINGCHANGE:
    {
        // Windows avisa cuando cambia el tema del sistema (AppsUseLightTheme).
        window->OnSystemThemeChanged();
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY:
    {
        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            0);

        return DefWindowProcW(
            hwnd, message, wParam, lParam);
    }
    }

    return DefWindowProcW(
        hwnd, message, wParam, lParam);
}
