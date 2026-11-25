#include "OptimizationScreen.h"

#include <imgui.h>

#include "../../Components/Widgets/Widgets.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace UI::Screens::Optimization
{
    namespace
    {
        using Text = UI::Localization::Text;
        using ::Optimization::BenchmarkStatus;
        using ::Optimization::CoreClass;
        using ::Optimization::OptimizerMode;
        using ::Optimization::PlanStatus;
        using ::Optimization::TopologyKind;

        const char* T(
            UI::Localization::Language language,
            Text text)
        {
            return UI::Localization::Translate(language, text);
        }

        std::string ToUtf8(const std::wstring& value)
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

        std::string HexMask(std::uint64_t mask)
        {
            char buffer[32]{};
            std::snprintf(
                buffer, sizeof(buffer),
                "0x%llx",
                static_cast<unsigned long long>(mask));
            return buffer;
        }

        const char* ModeText(
            UI::Localization::Language language,
            OptimizerMode mode)
        {
            switch (mode)
            {
            case OptimizerMode::Gaming:
                return T(language, Text::OptimizerGaming);
            case OptimizerMode::Multitasking:
                return T(language, Text::OptimizerMultitasking);
            case OptimizerMode::Off:
            default:
                return T(language, Text::OptimizerOff);
            }
        }

        const char* TopologyText(
            UI::Localization::Language language,
            TopologyKind kind)
        {
            switch (kind)
            {
            case TopologyKind::Hybrid:
                return T(language, Text::TopologyHybrid);
            case TopologyKind::Uniform:
                return T(language, Text::TopologyUniform);
            case TopologyKind::Unknown:
            default:
                return T(language, Text::TopologyUnknown);
            }
        }

        const char* PlanStatusText(
            UI::Localization::Language language,
            PlanStatus status)
        {
            switch (status)
            {
            case PlanStatus::Applied:
                return T(language, Text::PlanApplied);
            case PlanStatus::Verified:
                return T(language, Text::PlanVerified);
            case PlanStatus::Planned:
                return T(language, Text::PlanPlanned);
            case PlanStatus::Skipped:
                return T(language, Text::PlanSkipped);
            case PlanStatus::Failed:
            default:
                return T(language, Text::PlanFailed);
            }
        }

        ImVec4 CoreColor(
            CoreClass coreClass,
            const UI::Theme::UiPalette& palette)
        {
            switch (coreClass)
            {
            case CoreClass::Performance:
                return palette.Warning;
            case CoreClass::Efficiency:
                return palette.Memory;
            case CoreClass::Uniform:
                return palette.Cpu;
            case CoreClass::Unknown:
            default:
                return palette.TextMuted;
            }
        }

        void RenderControls(
            UI::Theme::AppSettings& settings,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language,
            const ::Optimization::OptimizerState& optimizer,
            bool serviceConnected,
            OptimizationCommand& command)
        {
            UI::Widgets::SectionTitle(
                T(language, Text::Optimization),
                T(language, Text::OptimizationDescription),
                palette);

            UI::Widgets::BeginCard(
                "OptimizerControls", 125.0f, palette);

            const bool enabled = settings.optimizerEnabled;
            const bool profileReady =
                optimizer.benchmark.status ==
                    BenchmarkStatus::Completed &&
                optimizer.benchmark.profile.HasData();

            if (UI::Widgets::ToggleButton(
                    T(language,
                      enabled
                          ? Text::OptimizerEnabled
                          : Text::OptimizerDisabled),
                    enabled,
                    palette))
            {
                if (enabled)
                {
                    settings.optimizerEnabled = false;
                    command.changed = true;
                }
                else if (profileReady)
                {
                    settings.optimizerEnabled = true;
                    if (settings.optimizerMode == 0)
                        settings.optimizerMode = 1;
                    command.changed = true;
                }
            }

            ImGui::SameLine();

            const char* modes[]{
                T(language, Text::OptimizerOff),
                T(language, Text::OptimizerGaming),
                T(language, Text::OptimizerMultitasking)
            };

            int selected = settings.optimizerMode;
            if (UI::Widgets::SegmentedControl(
                    "OptimizerMode",
                    modes,
                    3,
                    selected,
                    palette))
            {
                settings.optimizerMode = selected;
                settings.optimizerEnabled =
                    selected != 0 && profileReady;
                command.changed = true;
            }

            if (!profileReady)
            {
                ImGui::Spacing();
                UI::Widgets::MutedText(
                    T(language, Text::BenchmarkRequired),
                    palette);
            }

            ImGui::Spacing();

            UI::Widgets::MutedText(
                T(language, Text::OptimizerStatus),
                palette);
            ImGui::SameLine();
            UI::Widgets::Badge(
                ModeText(language, optimizer.mode),
                optimizer.enabled
                    ? palette.Success
                    : palette.TextMuted,
                palette);

            if (!optimizer.status.empty())
            {
                ImGui::SameLine();
                UI::Widgets::Subtitle(
                    optimizer.status.c_str(), palette);
            }

            ImGui::Spacing();

            if (!serviceConnected)
            {
                UI::Widgets::Badge(
                    T(language,
                      Text::OptimizationServiceRequired),
                    palette.Warning,
                    palette);
            }
            else
            {
                UI::Widgets::MutedText(
                    T(language, Text::OptimizationSavedNote),
                    palette);
            }

            UI::Widgets::EndCard();
        }

        void RenderBenchmark(
            const ::Optimization::OptimizerState& optimizer,
            bool serviceConnected,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language,
            OptimizationCommand& command)
        {
            UI::Widgets::SectionTitle(
                T(language, Text::BenchmarkTitle),
                nullptr,
                palette);

            UI::Widgets::BeginCard(
                "OptimizerBenchmark", 0.0f, palette);

            const ::Optimization::BenchmarkState& benchmark =
                optimizer.benchmark;

            const bool running =
                benchmark.status == BenchmarkStatus::Running;

            if (running)
            {
                ImGui::ProgressBar(
                    static_cast<float>(
                        benchmark.progressPercent) / 100.0f,
                    ImVec2(-1.0f, 0.0f),
                    T(language, Text::BenchmarkRunning));
            }
            else if (!serviceConnected)
            {
                UI::Widgets::Badge(
                    T(language,
                      Text::OptimizationServiceRequired),
                    palette.Warning,
                    palette);
            }
            else if (UI::Widgets::SecondaryButton(
                         T(language, Text::BenchmarkButton),
                         palette))
            {
                command.startBenchmark = true;
            }

            if (!benchmark.statusText.empty())
            {
                ImGui::Spacing();
                UI::Widgets::MutedText(
                    benchmark.statusText.c_str(), palette);
            }

            if (!benchmark.profile.HasData())
            {
                UI::Widgets::MutedText(
                    T(language, Text::BenchmarkNoData),
                    palette);
                UI::Widgets::EndCard();
                return;
            }

            ImGui::Spacing();

            std::uint64_t ageSeconds = 0;
            if (benchmark.profile.createdMs != 0)
            {
                const std::uint64_t now = GetTickCount64();
                ageSeconds =
                    now >= benchmark.profile.createdMs
                        ? (now - benchmark.profile.createdMs) /
                              1000
                        : 0;
            }

            UI::Widgets::Badge(
                T(language, Text::BenchmarkProfile),
                palette.Success,
                palette);
            ImGui::SameLine();
            ImGui::Text(
                "%llu s",
                static_cast<unsigned long long>(ageSeconds));

            std::vector<
                const ::Optimization::ProcessorBenchmark*> ordered;
            ordered.reserve(
                benchmark.profile.processors.size());

            for (const ::Optimization::ProcessorBenchmark& entry :
                 benchmark.profile.processors)
            {
                ordered.push_back(&entry);
            }

            std::sort(
                ordered.begin(), ordered.end(),
                [](const auto* left, const auto* right)
                {
                    if (left->rank != right->rank)
                        return left->rank < right->rank;

                    if (left->group != right->group)
                        return left->group < right->group;

                    return left->logicalIndex <
                           right->logicalIndex;
                });

            constexpr ImGuiTableFlags flags =
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg;

            if (ImGui::BeginTable(
                    "OptimizerBenchmarkTable", 6, flags))
            {
                ImGui::TableSetupColumn(
                    T(language, Text::BenchmarkRank),
                    ImGuiTableColumnFlags_WidthFixed, 52.0f);
                ImGui::TableSetupColumn(
                    "CPU",
                    ImGuiTableColumnFlags_WidthFixed, 72.0f);
                ImGui::TableSetupColumn(
                    T(language, Text::PhysicalCores),
                    ImGuiTableColumnFlags_WidthFixed, 78.0f);
                ImGui::TableSetupColumn(
                    T(language, Text::BenchmarkPeak),
                    ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn(
                    T(language, Text::BenchmarkAverage),
                    ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn(
                    T(language, Text::BenchmarkScore),
                    ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto* entry : ordered)
                {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%u", entry->rank);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text(
                        "G%u:L%u",
                        entry->group, entry->logicalIndex);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text(
                        "%u", entry->physicalCoreIndex);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text(
                        "%.0f", entry->peakMHz);

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text(
                        "%.0f", entry->averageMHz);

                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text(
                        "%.1f", entry->score);
                }

                ImGui::EndTable();
            }

            UI::Widgets::EndCard();
        }

        void RenderTopology(
            const ::Optimization::TopologySnapshot& topology,
            bool hasTopology,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language)
        {
            UI::Widgets::SectionTitle(
                T(language, Text::TopologyDetected),
                nullptr,
                palette);

            UI::Widgets::BeginCard(
                "OptimizerTopology", 118.0f, palette);

            if (!hasTopology || !topology.available)
            {
                UI::Widgets::Badge(
                    T(language, Text::TopologyUnavailable),
                    palette.Warning,
                    palette);
                UI::Widgets::EndCard();
                return;
            }

            UI::Widgets::Badge(
                TopologyText(language, topology.kind),
                topology.kind == TopologyKind::Hybrid
                    ? palette.Warning
                    : palette.Accent,
                palette);

            ImGui::SameLine();
            ImGui::Text(
                "%zu %s | %zu %s | %u %s | %u %s",
                topology.cores.size(),
                T(language, Text::PhysicalCores),
                topology.processors.size(),
                T(language, Text::LogicalProcessorsCard),
                topology.groupCount,
                "groups",
                topology.numaNodeCount,
                "NUMA");

            ImGui::Spacing();

            std::size_t l2 = 0;
            std::size_t l3 = 0;
            for (const ::Optimization::TopologyCache& cache :
                 topology.caches)
            {
                if (cache.level == 2)
                    ++l2;
                else if (cache.level == 3)
                    ++l3;
            }

            ImGui::Text("L2: %zu  L3: %zu", l2, l3);

            UI::Widgets::EndCard();
        }

        void RenderCoreMap(
            const ::Optimization::TopologySnapshot& topology,
            const ::Optimization::OptimizerState& optimizer,
            bool hasTopology,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language)
        {
            UI::Widgets::SectionTitle(
                T(language, Text::CoreMap),
                nullptr,
                palette);

            UI::Widgets::BeginCard(
                "OptimizerCoreMap", 140.0f, palette);

            if (!hasTopology || topology.processors.empty())
            {
                UI::Widgets::MutedText(
                    T(language, Text::TopologyUnavailable),
                    palette);
                UI::Widgets::EndCard();
                return;
            }

            std::size_t column = 0;
            for (const ::Optimization::TopologyProcessor& processor :
                 topology.processors)
            {
                std::uint32_t assignedThreads = 0;
                bool bestFrequency = false;
                double maxMHz = processor.maxMHz;
                double currentMHz = processor.currentMHz;

                for (const ::Optimization::CoreAssignment& assignment :
                     optimizer.cores)
                {
                    if (assignment.group == processor.group &&
                        assignment.logicalIndex ==
                            processor.logicalIndex)
                    {
                        assignedThreads =
                            assignment.threadCount;
                        bestFrequency =
                            assignment.bestFrequency;
                        maxMHz = assignment.maxMHz;
                        currentMHz = assignment.currentMHz;
                        break;
                    }
                }

                const ImVec4 color =
                    CoreColor(processor.coreClass, palette);

                ImGui::BeginGroup();
                ImGui::Text(
                    "G%u L%u",
                    processor.group,
                    processor.logicalIndex);

                ImGui::PushStyleColor(
                    ImGuiCol_PlotHistogram, color);
                ImGui::ProgressBar(
                    (std::min)(
                        1.0f,
                        static_cast<float>(
                            (std::max)(
                                0.0,
                                processor.usagePercent)) /
                            100.0f),
                    ImVec2(62.0f, 0.0f),
                    "");
                ImGui::PopStyleColor();

                if (ImGui::IsItemHovered())
                {
                    char detail[256]{};
                    if (processor.frequencyAvailable)
                    {
                        std::snprintf(
                            detail,
                            sizeof(detail),
                            "L2 %u | L3 %u | NUMA %u | hilos %u\n"
                            "max %.0f MHz | actual %.0f MHz%s",
                            processor.l2CacheId,
                            processor.l3CacheId,
                            processor.numaNode,
                            assignedThreads,
                            maxMHz,
                            currentMHz,
                            bestFrequency
                                ? " | mejor frecuencia"
                                : "");
                    }
                    else
                    {
                        std::snprintf(
                            detail,
                            sizeof(detail),
                            "L2 %u | L3 %u | NUMA %u | hilos %u",
                            processor.l2CacheId,
                            processor.l3CacheId,
                            processor.numaNode,
                            assignedThreads);
                    }
                    ImGui::SetTooltip("%s", detail);
                }

                ImGui::EndGroup();

                ++column;
                if (column % 8 != 0)
                    ImGui::SameLine();
                else
                    column = 0;
            }

            UI::Widgets::EndCard();
        }

        void RenderSummary(
            const ::Optimization::OptimizerState& optimizer,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language)
        {
            UI::Widgets::BeginCard(
                "OptimizerSummary", 72.0f, palette);

            std::uint64_t ageSeconds = 0;
            if (optimizer.lastUpdateMs != 0)
            {
                const std::uint64_t now = GetTickCount64();
                ageSeconds = now >= optimizer.lastUpdateMs
                    ? (now - optimizer.lastUpdateMs) / 1000
                    : 0;
            }

            ImGui::Text(
                "%s: %u   %s: %u   %s: %u",
                T(language, Text::AppliedChanges),
                optimizer.appliedChanges,
                T(language, Text::RevertedChanges),
                optimizer.revertedChanges,
                T(language, Text::FailedChanges),
                optimizer.failedChanges);

            if (optimizer.lastUpdateMs != 0)
            {
                ImGui::Text(
                    "%s: %llu s",
                    T(language, Text::LastUpdate),
                    static_cast<unsigned long long>(
                        ageSeconds));
            }

            UI::Widgets::EndCard();
        }

        void RenderProcessPlan(
            const ::Optimization::OptimizerState& optimizer,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language)
        {
            UI::Widgets::SectionTitle(
                T(language, Text::ProcessPlan),
                nullptr,
                palette);

            UI::Widgets::BeginCard(
                "OptimizerProcessPlan", 0.0f, palette);

            if (optimizer.processes.empty())
            {
                UI::Widgets::MutedText(
                    T(language, Text::NoProcessPlan),
                    palette);
                UI::Widgets::EndCard();
                return;
            }

            static DWORD selectedPid = 0;

            bool selectedStillPresent = false;
            for (const ::Optimization::ProcessPlan& plan :
                 optimizer.processes)
            {
                if (plan.processId == selectedPid)
                {
                    selectedStillPresent = true;
                    break;
                }
            }
            if (!selectedStillPresent)
                selectedPid = 0;

            constexpr ImGuiTableFlags flags =
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable;

            if (ImGui::BeginTable(
                    "OptimizerPlanTable", 7, flags))
            {
                ImGui::TableSetupColumn(
                    T(language, Text::Name));
                ImGui::TableSetupColumn(
                    T(language, Text::Pid),
                    ImGuiTableColumnFlags_WidthFixed, 72.0f);
                ImGui::TableSetupColumn(
                    T(language, Text::CpuShort),
                    ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn(
                    T(language, Text::GpuShort),
                    ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn(
                    T(language, Text::PlanReason),
                    ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(
                    T(language, Text::Foreground),
                    ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn(
                    T(language, Text::ThreadCount),
                    ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableHeadersRow();

                for (const ::Optimization::ProcessPlan& plan :
                     optimizer.processes)
                {
                    ImGui::TableNextRow();

                    const std::string name = ToUtf8(plan.name);
                    const bool isSelected =
                        selectedPid == plan.processId;

                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable(
                            name.empty()
                                ? "(unknown)"
                                : name.c_str(),
                            isSelected))
                    {
                        selectedPid =
                            isSelected ? 0 : plan.processId;
                    }

                    if (plan.processId ==
                        optimizer.targetProcessId)
                    {
                        ImGui::SameLine();
                        UI::Widgets::Badge(
                            T(language, Text::TargetProcess),
                            palette.Warning,
                            palette);
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%lu",
                                static_cast<unsigned long>(
                                    plan.processId));

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.1f",
                                plan.cpuUsagePercent);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.1f",
                                plan.gpuUsagePercent);

                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(
                        plan.decision.c_str());

                    ImGui::TableSetColumnIndex(5);
                    if (plan.foreground)
                    {
                        UI::Widgets::Badge(
                            T(language, Text::Foreground),
                            palette.Accent,
                            palette);
                    }

                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%lu",
                                static_cast<unsigned long>(
                                    plan.threadCount));
                }

                ImGui::EndTable();
            }

            for (const ::Optimization::ProcessPlan& plan :
                 optimizer.processes)
            {
                if (plan.processId != selectedPid)
                    continue;

                ImGui::Spacing();
                UI::Widgets::Subtitle(
                    ToUtf8(plan.name).c_str(), palette);

                if (ImGui::BeginTable(
                        "OptimizerThreadTable", 9, flags))
                {
                    ImGui::TableSetupColumn(
                        T(language, Text::Thread),
                        ImGuiTableColumnFlags_WidthFixed, 86.0f);
                    ImGui::TableSetupColumn(
                        T(language, Text::CpuShort),
                        ImGuiTableColumnFlags_WidthFixed, 64.0f);
                    ImGui::TableSetupColumn(
                        T(language, Text::Current),
                        ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn(
                        T(language, Text::Target),
                        ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn(
                        T(language, Text::Ideal),
                        ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn(
                        T(language, Text::Priority),
                        ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn(
                        T(language, Text::L3),
                        ImGuiTableColumnFlags_WidthFixed, 56.0f);
                    ImGui::TableSetupColumn(
                        T(language, Text::PlanReason),
                        ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn(
                        T(language, Text::PlanStatusTitle),
                        ImGuiTableColumnFlags_WidthFixed, 90.0f);
                    ImGui::TableHeadersRow();

                    for (const ::Optimization::ThreadPlan& thread :
                         plan.threads)
                    {
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%lu",
                                    static_cast<unsigned long>(
                                        thread.threadId));

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.1f",
                                    thread.cpuUsagePercent);

                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(
                            HexMask(thread.currentAffinity).c_str());

                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(
                            HexMask(thread.targetAffinity).c_str());

                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text(
                            "G%u:L%u",
                            thread.targetGroup,
                            thread.targetIdealProcessor);

                        ImGui::TableSetColumnIndex(5);
                        ImGui::Text(
                            "%d -> %d",
                            thread.currentPriority,
                            thread.targetPriority);

                        ImGui::TableSetColumnIndex(6);
                        ImGui::Text("%u", thread.l3CacheId);

                        ImGui::TableSetColumnIndex(7);
                        ImGui::TextUnformatted(
                            thread.reason.c_str());

                        ImGui::TableSetColumnIndex(8);
                        const ImVec4 statusColor =
                            thread.status == PlanStatus::Verified
                                ? palette.Success
                                : (thread.status ==
                                           PlanStatus::Failed
                                       ? palette.Danger
                                       : palette.Warning);
                        UI::Widgets::Badge(
                            PlanStatusText(
                                language, thread.status),
                            statusColor,
                            palette);
                    }

                    ImGui::EndTable();
                }
            }

            UI::Widgets::EndCard();
        }
    }

    OptimizationCommand OptimizationScreen::Render(
        UI::Theme::AppSettings& settings,
        const UI::Theme::UiPalette& palette,
        UI::Localization::Language language,
        const ::Optimization::TopologySnapshot& topology,
        const ::Optimization::OptimizerState& optimizer,
        bool hasTopology,
        bool hasOptimizer,
        bool serviceConnected)
    {
        OptimizationCommand command;
        command.enabled = settings.optimizerEnabled;
        command.mode = static_cast<OptimizerMode>(
            (std::min)(2, (std::max)(0,
                settings.optimizerMode)));

        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding, 6.0f);

        RenderControls(
            settings,
            palette,
            language,
            optimizer,
            serviceConnected,
            command);

        RenderBenchmark(
            optimizer,
            serviceConnected,
            palette,
            language,
            command);

        RenderTopology(
            topology,
            hasTopology,
            palette,
            language);

        RenderCoreMap(
            topology,
            optimizer,
            hasTopology,
            palette,
            language);

        RenderSummary(
            optimizer,
            palette,
            language);

        RenderProcessPlan(
            hasOptimizer ? optimizer : ::Optimization::OptimizerState{},
            palette,
            language);

        ImGui::PopStyleVar();

        command.enabled =
            command.mode != OptimizerMode::Off &&
            settings.optimizerEnabled;

        return command;
    }
}
