#include "OverviewDashboard.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "../../Components/Widgets/Widgets.h"

namespace UI::Dashboard::Overview
{
    namespace
    {
        using Text = UI::Localization::Text;

        const char* T(
            UI::Localization::Language language,
            Text text)
        {
            return UI::Localization::Translate(language, text);
        }

        std::string WideToUtf8(const std::wstring& value)
        {
            if (value.empty())
                return {};

            const int size = WideCharToMultiByte(
                CP_UTF8, 0, value.c_str(), -1,
                nullptr, 0, nullptr, nullptr);

            if (size <= 1)
                return {};

            std::string result(
                static_cast<std::size_t>(size), '\0');

            WideCharToMultiByte(
                CP_UTF8, 0, value.c_str(), -1,
                result.data(), size, nullptr, nullptr);

            result.pop_back();
            return result;
        }

        void FormatBytes(
            std::uint64_t bytes,
            char* buffer,
            std::size_t bufferSize)
        {
            constexpr double KB = 1024.0;
            constexpr double MB = KB * 1024.0;
            constexpr double GB = MB * 1024.0;
            constexpr double TB = GB * 1024.0;

            const double value =
                static_cast<double>(bytes);

            if (value >= TB)
                std::snprintf(
                    buffer, bufferSize, "%.2f TB", value / TB);
            else if (value >= GB)
                std::snprintf(
                    buffer, bufferSize, "%.2f GB", value / GB);
            else if (value >= MB)
                std::snprintf(
                    buffer, bufferSize, "%.1f MB", value / MB);
            else if (value >= KB)
                std::snprintf(
                    buffer, bufferSize, "%.1f KB", value / KB);
            else
                std::snprintf(
                    buffer, bufferSize, "%llu B",
                    static_cast<unsigned long long>(bytes));
        }

        float ClampUsage(double value)
        {
            return static_cast<float>(
                (std::max)(0.0, (std::min)(value, 100.0)));
        }

        const GpuAdapterInfo* FindBusiestGpu(
            const GpuSnapshot* gpu)
        {
            if (!gpu || gpu->gpus.empty())
                return nullptr;

            const GpuAdapterInfo* best = nullptr;

            for (const GpuAdapterInfo& adapter : gpu->gpus)
            {
                if (!best ||
                    adapter.usagePercent > best->usagePercent)
                {
                    best = &adapter;
                }
            }

            return best;
        }

        const ProcessInfo* FindTopProcess(
            const ProcessSnapshot* processes)
        {
            if (!processes || processes->processes.empty())
                return nullptr;

            const ProcessInfo* best = nullptr;
            double bestScore = -1.0;

            for (const ProcessInfo& process :
                 processes->processes)
            {
                const double score =
                    process.cpuUsagePercent +
                    process.gpuUsagePercent * 0.5;

                if (score > bestScore)
                {
                    bestScore = score;
                    best = &process;
                }
            }

            return best;
        }

        std::uint32_t GetMemorySpeed(
            const MemorySnapshot* memory)
        {
            if (!memory)
                return 0;

            std::uint32_t speed = 0;

            for (const MemoryModuleInfo& module :
                 memory->modules)
            {
                const std::uint32_t moduleSpeed =
                    module.configuredSpeed > 0
                    ? module.configuredSpeed
                    : module.speed;

                speed = (std::max)(speed, moduleSpeed);
            }

            return speed;
        }

        template<std::size_t N>
        void GetOrderedHistory(
            const std::array<float, N>& values,
            std::size_t index,
            std::size_t count,
            std::array<float, N>& ordered)
        {
            count = (std::min)(count, N);

            const std::size_t start =
                (index + N - count) % N;

            for (std::size_t i = 0; i < count; ++i)
                ordered[i] = values[(start + i) % N];
        }

        std::array<ImVec4, 6> GpuLineColors(
            const UI::Theme::UiPalette& palette)
        {
            return std::array<ImVec4, 6>{{
                palette.Gpu,
                palette.Process,
                palette.Warning,
                palette.Danger,
                palette.Accent,
                palette.Success
            }};
        }

        void RenderTopCards(
            const CpuSnapshot* cpu,
            const MemorySnapshot* memory,
            const GpuSnapshot* gpu,
            const ProcessSnapshot* processes,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language)
        {
            char cpuValue[32] = "--";
            char cpuDetail[192] = "Waiting for CPU data";
            char memoryValue[32] = "--";
            char memoryDetail[192] = "Waiting for memory data";
            char gpuValue[32] = "--";
            char gpuDetail[224] = "Waiting for GPU data";
            char processValue[32] = "--";
            char processDetail[192] = "Waiting for process data";

            if (cpu)
            {
                std::snprintf(
                    cpuValue, sizeof(cpuValue), "%.1f%%",
                    cpu->totalUsagePercent);

                const double frequency =
                    cpu->frequency.effectiveMHz > 0.0
                    ? cpu->frequency.effectiveMHz
                    : cpu->frequency.estimatedMHz;

                std::snprintf(
                    cpuDetail,
                    sizeof(cpuDetail),
                    "%lu %s / %lu %s | %.2f GHz",
                    static_cast<unsigned long>(
                        cpu->processor.physicalCoreCount),
                    T(language, Text::PhysicalCores),
                    static_cast<unsigned long>(
                        cpu->processor.logicalProcessorCount),
                    T(language, Text::LogicalProcessorsCard),
                    frequency / 1000.0);
            }
            else
            {
                std::snprintf(
                    cpuDetail, sizeof(cpuDetail), "%s",
                    T(language, Text::WaitingCpu));
            }

            if (memory)
            {
                char used[48]{};
                char total[48]{};

                FormatBytes(
                    memory->usedPhysicalBytes,
                    used, sizeof(used));
                FormatBytes(
                    memory->totalPhysicalBytes,
                    total, sizeof(total));

                std::snprintf(
                    memoryValue, sizeof(memoryValue), "%.1f%%",
                    memory->usagePercent);

                std::snprintf(
                    memoryDetail,
                    sizeof(memoryDetail),
                    "%s %s %s | %u %s",
                    used,
                    T(language, Text::UsedOf),
                    total,
                    GetMemorySpeed(memory),
                    T(language, Text::Speed));
            }
            else
            {
                std::snprintf(
                    memoryDetail, sizeof(memoryDetail), "%s",
                    T(language, Text::WaitingMemory));
            }

            const GpuAdapterInfo* adapter =
                FindBusiestGpu(gpu);

            if (adapter)
            {
                char vram[48]{};
                const std::string name =
                    WideToUtf8(adapter->name);

                FormatBytes(
                    adapter->processDedicatedMemoryUsedBytes,
                    vram, sizeof(vram));

                std::snprintf(
                    gpuValue, sizeof(gpuValue), "%.1f%%",
                    adapter->usagePercent);

                std::snprintf(
                    gpuDetail,
                    sizeof(gpuDetail),
                    "%.100s | %s %s",
                    name.empty()
                        ? T(language, Text::GpuShort)
                        : name.c_str(),
                    vram,
                    T(language, Text::Dedicated));
            }
            else
            {
                std::snprintf(
                    gpuDetail, sizeof(gpuDetail), "%s",
                    T(language, Text::WaitingGpu));
            }

            const ProcessInfo* top =
                FindTopProcess(processes);

            if (processes)
            {
                std::snprintf(
                    processValue,
                    sizeof(processValue),
                    "%zu",
                    processes->processes.size());

                if (top)
                {
                    const std::string name =
                        WideToUtf8(top->name);

                    std::snprintf(
                        processDetail,
                        sizeof(processDetail),
                        "%s: %.40s | %.1f%% %s",
                        T(language, Text::Top),
                        name.empty()
                            ? "unknown"
                            : name.c_str(),
                        top->cpuUsagePercent,
                        T(language, Text::CpuShort));
                }
                else
                {
                    std::snprintf(
                        processDetail,
                        sizeof(processDetail),
                        "%s",
                        T(language, Text::NoProcesses));
                }
            }
            else
            {
                std::snprintf(
                    processDetail, sizeof(processDetail),
                    "%s",
                    T(language, Text::WaitingProcesses));
            }

            if (!ImGui::BeginTable(
                    "OverviewCards",
                    4,
                    ImGuiTableFlags_SizingStretchSame))
            {
                return;
            }

            ImGui::TableNextColumn();
            UI::Widgets::StatCard(
                "OverviewCpuCard",
                T(language, Text::CpuUsage),
                cpuValue,
                cpuDetail,
                palette.Cpu,
                palette);

            ImGui::TableNextColumn();
            UI::Widgets::StatCard(
                "OverviewMemoryCard",
                T(language, Text::RamUsage),
                memoryValue,
                memoryDetail,
                palette.Memory,
                palette);

            ImGui::TableNextColumn();
            UI::Widgets::StatCard(
                "OverviewGpuCard",
                T(language, Text::GpuUsage),
                gpuValue,
                gpuDetail,
                palette.Gpu,
                palette);

            ImGui::TableNextColumn();
            UI::Widgets::StatCard(
                "OverviewProcessCard",
                T(language, Text::ProcessCount),
                processValue,
                processDetail,
                palette.Process,
                palette);

            ImGui::EndTable();
        }

        void RenderCombinedChart(
            const CombinedHistory& history,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language)
        {
            UI::Widgets::BeginCard(
                "OverviewChartCard", 320.0f, palette);

            UI::Widgets::SectionTitle(
                T(language, Text::CombinedUsage),
                T(language, Text::AllGpus),
                palette);

            if (history.count == 0)
            {
                UI::Widgets::MutedText(
                    T(language, Text::WaitingSamples),
                    palette);
                UI::Widgets::EndCard();
                return;
            }

            std::array<float, HistorySize> orderedCpu{};
            std::array<float, HistorySize> orderedMemory{};

            GetOrderedHistory(
                history.cpu, history.index, history.count,
                orderedCpu);
            GetOrderedHistory(
                history.memory, history.index, history.count,
                orderedMemory);

            std::vector<std::array<float, HistorySize>>
                orderedGpus;

            orderedGpus.resize(history.gpus.size());

            for (std::size_t i = 0;
                 i < history.gpus.size();
                 ++i)
            {
                GetOrderedHistory(
                    history.gpus[i].usage,
                    history.index,
                    history.count,
                    orderedGpus[i]);
            }

            const int count = static_cast<int>(
                (std::min)(history.count, HistorySize));

            ImPlot::PushStyleVar(
                ImPlotStyleVar_PlotPadding,
                ImVec2(10.0f, 10.0f));

            if (ImPlot::BeginPlot(
                    "##CombinedUsage",
                    ImVec2(-1.0f, 250.0f),
                    ImPlotFlags_NoInputs |
                    ImPlotFlags_NoFrame))
            {
                ImPlot::SetupAxes(
                    nullptr,
                    T(language, Text::UsagePercent),
                    ImPlotAxisFlags_NoTickLabels,
                    ImPlotAxisFlags_None);
                ImPlot::SetupLegend(
                    ImPlotLocation_North,
                    ImPlotLegendFlags_Horizontal);
                ImPlot::SetupAxisLimits(
                    ImAxis_X1,
                    0.0,
                    count > 1
                        ? static_cast<double>(count - 1)
                        : 1.0,
                    ImGuiCond_Always);
                ImPlot::SetupAxisLimits(
                    ImAxis_Y1,
                    0.0,
                    100.0,
                    ImGuiCond_Always);

                const ImPlotSpec cpuSpec(
                    ImPlotProp_LineColor, palette.Cpu,
                    ImPlotProp_LineWeight, 2.0f);
                const ImPlotSpec memorySpec(
                    ImPlotProp_LineColor, palette.Memory,
                    ImPlotProp_LineWeight, 2.0f);

                ImPlot::PlotLine(
                    T(language, Text::CpuShort),
                    orderedCpu.data(),
                    count,
                    1.0,
                    0.0,
                    cpuSpec);
                ImPlot::PlotLine(
                    T(language, Text::Ram),
                    orderedMemory.data(),
                    count,
                    1.0,
                    0.0,
                    memorySpec);

                const std::array<ImVec4, 6> colors =
                    GpuLineColors(palette);

                for (std::size_t i = 0;
                     i < history.gpus.size();
                     ++i)
                {
                    char label[192]{};

                    std::snprintf(
                        label,
                        sizeof(label),
                        "%s %zu%s%s",
                        T(language, Text::GpuShort),
                        i + 1,
                        history.gpus[i].name.empty()
                            ? ""
                            : " - ",
                        history.gpus[i].name.c_str());

                    const ImPlotSpec spec(
                        ImPlotProp_LineColor,
                        colors[i % colors.size()],
                        ImPlotProp_LineWeight, 2.0f);

                    ImPlot::PlotLine(
                        label,
                        orderedGpus[i].data(),
                        count,
                        1.0,
                        0.0,
                        spec);
                }

                ImPlot::EndPlot();
            }

            ImPlot::PopStyleVar();
            UI::Widgets::EndCard();
        }

        void RenderDetails(
            const CpuSnapshot* cpu,
            const MemorySnapshot* memory,
            const GpuSnapshot* gpu,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language)
        {
            UI::Widgets::BeginCard(
                "OverviewDetailsCard", 0.0f, palette);

            UI::Widgets::SectionTitle(
                T(language, Text::KeyDetails),
                nullptr,
                palette);

            UI::Widgets::BeginCard(
                "OverviewCpuDetail", 110.0f, palette);

            UI::Widgets::ValueText(
                T(language, Text::CpuShort),
                palette.Cpu);

            if (cpu)
            {
                const double frequency =
                    cpu->frequency.effectiveMHz > 0.0
                    ? cpu->frequency.effectiveMHz
                    : cpu->frequency.estimatedMHz;

                ImGui::Text(
                    "%s %.1f%% | %.2f GHz | %zu %s",
                    T(language, Text::UsagePercent),
                    cpu->totalUsagePercent,
                    frequency / 1000.0,
                    cpu->processes.size(),
                    T(language, Text::MonitoredProcesses));

                ImGui::TextDisabled(
                    "%lu %s | %lu %s | %lu %s",
                    static_cast<unsigned long>(
                        cpu->processor.physicalCoreCount),
                    T(language, Text::PhysicalCores),
                    static_cast<unsigned long>(
                        cpu->processor.logicalProcessorCount),
                    T(language, Text::LogicalProcessorsCard),
                    static_cast<unsigned long>(
                        cpu->processor.socketCount),
                    T(language, Text::Sockets));

                UI::Widgets::UsageBar(
                    static_cast<float>(
                        cpu->totalUsagePercent),
                    palette.Cpu,
                    palette);
            }
            else
            {
                UI::Widgets::MutedText(
                    T(language, Text::WaitingCpu),
                    palette);
            }

            UI::Widgets::EndCard();

            ImGui::Spacing();

            UI::Widgets::BeginCard(
                "OverviewMemoryDetail", 110.0f, palette);

            UI::Widgets::ValueText(
                T(language, Text::Ram),
                palette.Memory);

            if (memory)
            {
                char used[48]{};
                char total[48]{};
                char available[48]{};

                FormatBytes(
                    memory->usedPhysicalBytes,
                    used, sizeof(used));
                FormatBytes(
                    memory->totalPhysicalBytes,
                    total, sizeof(total));
                FormatBytes(
                    memory->availablePhysicalBytes,
                    available, sizeof(available));

                ImGui::Text(
                    "%s %s %s | %.1f%%",
                    used,
                    T(language, Text::UsedOf),
                    total,
                    memory->usagePercent);

                ImGui::TextDisabled(
                    "%s %s | %u/%u %s | %u %s",
                    T(language, Text::Available),
                    available,
                    memory->usedSlots,
                    memory->totalSlots,
                    T(language, Text::Slots),
                    GetMemorySpeed(memory),
                    T(language, Text::Speed));

                UI::Widgets::UsageBar(
                    static_cast<float>(
                        memory->usagePercent),
                    palette.Memory,
                    palette);
            }
            else
            {
                UI::Widgets::MutedText(
                    T(language, Text::WaitingMemory),
                    palette);
            }

            UI::Widgets::EndCard();

            ImGui::Spacing();

            const std::size_t gpuCount =
                gpu ? gpu->gpus.size() : 0;

            UI::Widgets::BeginCard(
                "OverviewGpuDetail",
                110.0f + 48.0f * gpuCount,
                palette);

            UI::Widgets::ValueText(
                T(language, Text::GpuShort),
                palette.Gpu);

            if (gpuCount == 0)
            {
                UI::Widgets::MutedText(
                    T(language, Text::WaitingGpu),
                    palette);
            }
            else
            {
                const std::array<ImVec4, 6> colors =
                    GpuLineColors(palette);

                for (std::size_t i = 0;
                     i < gpuCount;
                     ++i)
                {
                    const GpuAdapterInfo& adapter =
                        gpu->gpus[i];

                    char vram[48]{};
                    const std::string name =
                        WideToUtf8(adapter.name);

                    FormatBytes(
                        adapter.processDedicatedMemoryUsedBytes,
                        vram, sizeof(vram));

                    UI::Widgets::ValueText(
                        T(language, Text::GpuShort),
                        colors[i % colors.size()]);

                    ImGui::SameLine();

                    ImGui::Text(
                        "%zu: %.80s | %.1f%% | %s %s",
                        i + 1,
                        name.empty()
                            ? T(language, Text::GpuShort)
                            : name.c_str(),
                        adapter.usagePercent,
                        vram,
                        T(language, Text::Dedicated));

                    if (adapter.temperatureAvailable ||
                        adapter.powerUsageAvailable)
                    {
                        char temperature[32] = "--";
                        char power[32] = "--";

                        if (adapter.temperatureAvailable)
                        {
                            std::snprintf(
                                temperature,
                                sizeof(temperature),
                                "%.0f C",
                                adapter.temperatureCelsius);
                        }

                        if (adapter.powerUsageAvailable)
                        {
                            std::snprintf(
                                power,
                                sizeof(power),
                                "%.1f W",
                                adapter.powerUsageWatts);
                        }

                        ImGui::TextDisabled(
                            "%s %s | %s %s",
                            T(language, Text::Temperature),
                            temperature,
                            T(language, Text::Power),
                            power);
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "3D %.1f%% | Copy %.1f%% | "
                            "Compute %.1f%%",
                            adapter.usage3D,
                            adapter.usageCopy,
                            adapter.usageCompute);
                    }

                    UI::Widgets::UsageBar(
                        static_cast<float>(
                            adapter.usagePercent),
                        colors[i % colors.size()],
                        palette);
                }
            }

            UI::Widgets::EndCard();
            UI::Widgets::EndCard();
        }

        void RenderTopProcesses(
            const ProcessSnapshot* processes,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language)
        {
            UI::Widgets::BeginCard(
                "OverviewProcessesCard", 0.0f, palette);

            UI::Widgets::SectionTitle(
                T(language, Text::TopProcesses),
                T(language, Text::ByLoad),
                palette);

            std::vector<const ProcessInfo*> top;

            if (processes)
            {
                top.reserve((std::min)(
                    processes->processes.size(),
                    std::size_t(6)));

                for (const ProcessInfo& process :
                     processes->processes)
                {
                    top.push_back(&process);
                }

                std::sort(
                    top.begin(),
                    top.end(),
                    [](const ProcessInfo* a,
                       const ProcessInfo* b)
                    {
                        const double scoreA =
                            a->cpuUsagePercent +
                            a->gpuUsagePercent * 0.5;
                        const double scoreB =
                            b->cpuUsagePercent +
                            b->gpuUsagePercent * 0.5;

                        return scoreA > scoreB;
                    });

                if (top.size() > 6)
                    top.resize(6);
            }

            if (top.empty())
            {
                UI::Widgets::MutedText(
                    T(language, Text::WaitingProcesses),
                    palette);
                UI::Widgets::EndCard();
                return;
            }

            if (ImGui::BeginTable(
                    "OverviewProcessTable",
                    5,
                    ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn(
                    T(language, Text::Name));
                ImGui::TableSetupColumn(
                    T(language, Text::Pid));
                ImGui::TableSetupColumn(
                    T(language, Text::CpuShort));
                ImGui::TableSetupColumn(
                    T(language, Text::Memory));
                ImGui::TableSetupColumn(
                    T(language, Text::GpuShort));
                ImGui::TableHeadersRow();

                for (const ProcessInfo* process : top)
                {
                    char memory[48]{};

                    FormatBytes(
                        process->workingSetBytes,
                        memory, sizeof(memory));

                    const std::string name =
                        WideToUtf8(process->name);

                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(
                        name.empty()
                            ? "unknown"
                            : name.c_str());

                    ImGui::TableNextColumn();
                    ImGui::Text(
                        "%lu",
                        static_cast<unsigned long>(
                            process->processId));

                    ImGui::TableNextColumn();
                    ImGui::TextColored(
                        palette.Cpu, "%.1f%%",
                        process->cpuUsagePercent);

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(memory);

                    ImGui::TableNextColumn();
                    ImGui::TextColored(
                        palette.Gpu, "%.1f%%",
                        process->gpuUsagePercent);
                }

                ImGui::EndTable();
            }

            UI::Widgets::EndCard();
        }
    }

    void OverviewDashboard::Render(
        const CpuSnapshot* cpu,
        const MemorySnapshot* memory,
        const GpuSnapshot* gpu,
        const ProcessSnapshot* processes,
        const CombinedHistory& history,
        const UI::Theme::UiPalette& palette,
        UI::Localization::Language language)
    {
        UI::Widgets::Title(
            T(language, Text::Overview),
            palette);

        ImGui::SameLine();
        UI::Widgets::Subtitle(
            T(language, Text::LiveSnapshot),
            palette);

        ImGui::Spacing();

        RenderTopCards(
            cpu, memory, gpu, processes,
            palette, language);

        ImGui::Spacing();

        RenderCombinedChart(
            history, palette, language);

        ImGui::Spacing();

        if (!ImGui::BeginTable(
                "OverviewBottom",
                2,
                ImGuiTableFlags_SizingStretchProp))
        {
            return;
        }

        ImGui::TableNextColumn();
        RenderDetails(
            cpu, memory, gpu, palette, language);

        ImGui::TableNextColumn();
        RenderTopProcesses(
            processes, palette, language);

        ImGui::EndTable();
    }
}
