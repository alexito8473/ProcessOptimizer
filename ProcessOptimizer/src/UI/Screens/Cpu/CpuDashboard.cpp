
#include "CpuDashboard.h"
#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "../../Components/Widgets/Widgets.h"

namespace UI::Dashboard::Cpu
{
    namespace
    {
        using Text = UI::Localization::Text;

        UI::Theme::UiPalette ActivePalette{};
        UI::Localization::Language ActiveLanguage =
            UI::Localization::Language::Spanish;

        const char* T(Text text)
        {
            return UI::Localization::Translate(
                ActiveLanguage, text);
        }

        const ImVec4& AccentColor()
        {
            return ActivePalette.Cpu;
        }

        const ImVec4& TextColor()
        {
            return ActivePalette.Text;
        }

        const ImVec4& SecondaryColor()
        {
            return ActivePalette.TextMuted;
        }

        const ImVec4& WarningColor()
        {
            return ActivePalette.Warning;
        }

        const ImVec4& DangerColor()
        {
            return ActivePalette.Danger;
        }

        ImVec4 GetUsageColor(float usage)
        {
            if (usage >= 85.0f)
                return DangerColor();

            if (usage >= 65.0f)
                return WarningColor();

            return AccentColor();
        }

        float ClampUsage(float value)
        {
            return std::clamp(value, 0.0f, 100.0f);
        }

        void SectionTitle(
            const char* title,
            const char* description = nullptr)
        {
            ImGui::TextColored(AccentColor(), "%s", title);

            if (description)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("| %s", description);
            }

            ImGui::Spacing();
        }

        void DrawUsageBar(float usage)
        {
            usage = ClampUsage(usage);

            ImGui::PushStyleColor(
                ImGuiCol_PlotHistogram,
                GetUsageColor(usage));

            ImGui::ProgressBar(
                usage / 100.0f,
                ImVec2(-1.0f, 10.0f),
                "");

            ImGui::PopStyleColor();
        }

        void MetricCard(
            const char* id,
            const char* title,
            const char* value,
            const char* description,
            ImVec4 valueColor = TextColor())
        {
            ImGui::BeginChild(
                id,
                ImVec2(0.0f, 110.0f),
                true);

            ImGui::TextDisabled("%s", title);
            ImGui::Spacing();

            ImGui::PushStyleColor(
                ImGuiCol_Text,
                valueColor);

            ImGui::SetWindowFontScale(1.4f);
            ImGui::TextUnformatted(value);
            ImGui::SetWindowFontScale(1.0f);

            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::TextDisabled("%s", description);

            ImGui::EndChild();
        }

        void RenderSummary(const CpuSnapshot& snapshot)
        {
            const float usage = ClampUsage(
                static_cast<float>(
                    snapshot.totalUsagePercent));

            char usageText[32]{};
            char frequencyText[32]{};
            char coresText[32]{};
            char logicalText[32]{};
            char socketsText[32]{};

            std::snprintf(
                usageText,
                sizeof(usageText),
                "%.1f%%",
                usage);

            if (snapshot.frequency.estimatedMHz > 0.0)
            {
                std::snprintf(
                    frequencyText,
                    sizeof(frequencyText),
                    "%.2f GHz",
                    snapshot.frequency.estimatedMHz / 1000.0);
            }
            else
            {
                std::snprintf(
                    frequencyText,
                    sizeof(frequencyText),
                    "--");
            }

            std::snprintf(
                coresText,
                sizeof(coresText),
                "%lu",
                static_cast<unsigned long>(
                    snapshot.processor.physicalCoreCount));

            std::snprintf(
                logicalText,
                sizeof(logicalText),
                "%lu",
                static_cast<unsigned long>(
                    snapshot.processor.logicalProcessorCount));

            std::snprintf(
                socketsText,
                sizeof(socketsText),
                "%lu",
                static_cast<unsigned long>(
                    snapshot.processor.socketCount));

            if (ImGui::BeginTable(
                "CpuSummary",
                5,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();

                MetricCard(
                    "CpuUsageCard",
                    T(Text::CpuUsage),
                    usageText,
                    T(Text::TotalUsage),
                    GetUsageColor(usage));

                ImGui::TableNextColumn();

                MetricCard(
                    "CpuFrequencyCard",
                    T(Text::EstimatedFrequencyLabel),
                    frequencyText,
                    T(Text::EstimatedFrequencyLabel));

                ImGui::TableNextColumn();

                MetricCard(
                    "CpuCoresCard",
                    T(Text::PhysicalCores),
                    coresText,
                    T(Text::DetectedPhysicalCores));

                ImGui::TableNextColumn();

                MetricCard(
                    "CpuLogicalCard",
                    T(Text::LogicalProcessorsCard),
                    logicalText,
                    T(Text::AvailableLogicalProcessors));

                ImGui::TableNextColumn();

                MetricCard(
                    "CpuSocketsCard",
                    T(Text::Sockets),
                    socketsText,
                    T(Text::DetectedPackages));

                ImGui::EndTable();
            }

            ImGui::Spacing();
        }

        void RenderProcessorIdentity(const CpuSnapshot& snapshot)
        {
            ImGui::BeginChild(
                "CpuIdentity",
                ImVec2(0.0f, 95.0f),
                true);

            ImGui::TextColored(
                AccentColor(),
                "PROCESSOR");

            ImGui::Spacing();

            ImGui::TextWrapped(
                "%s",
                snapshot.processor.brand.empty()
                ? "Processor model unavailable"
                : snapshot.processor.brand.c_str());

            ImGui::TextDisabled(
                "%s | %s",
                snapshot.processor.manufacturer.empty()
                ? "Unknown manufacturer"
                : snapshot.processor.manufacturer.c_str(),
                snapshot.processor.architecture.empty()
                ? "Unknown architecture"
                : snapshot.processor.architecture.c_str());

            ImGui::EndChild();
            ImGui::Spacing();
        }

        void RenderHistory(
            const char* label,
            std::span<const float> history,
            std::size_t historyIndex,
            std::size_t historyCount,
            float height)
        {
            if (history.empty() || historyCount == 0)
            {
                ImGui::TextDisabled("%s", T(Text::CollectingData));
                return;
            }

            const std::size_t count =
               (std::min)(historyCount, history.size());

            const std::size_t oldest =
                (historyIndex + history.size() - count)
                % history.size();

            ImPlot::SetNextAxesLimits(
                0.0,
                static_cast<double>(
                    std::max<std::size_t>(count - 1, 1)),
                0.0,
                100.0,
                ImPlotCond_Always);

            if (ImPlot::BeginPlot(
                label,
                ImVec2(-1.0f, height)))
            {
                ImPlot::SetupAxes(
                    "Samples",
                    "Usage (%)",
                    ImPlotAxisFlags_NoTickLabels,
                    ImPlotAxisFlags_RangeFit);

                ImPlot::SetupAxisLimits(
                    ImAxis_Y1,
                    0.0,
                    100.0,
                    ImPlotCond_Always);

                ImPlot::PlotLineG(
                    "CPU",
                    [](int index, void* data) -> ImPlotPoint
                    {
                        const auto* values =
                            static_cast<const std::span<const float>*>(data);

                        return ImPlotPoint(
                            index,
                            (*values)[static_cast<std::size_t>(index)]);
                    },
                    &history,
                    static_cast<int>(count));

                // La fuente de datos es circular; PlotLineG
                // recibe el índice cronológico mediante la función
                // de acceso siguiente.
                (void)oldest;

                ImPlot::EndPlot();
            }
        }

        struct CircularHistoryData
        {
            std::span<const float> values;
            std::size_t oldest = 0;
        };

        ImPlotPoint GetCircularHistoryPoint(
            int index,
            void* userData)
        {
            const auto* data =
                static_cast<const CircularHistoryData*>(userData);

            const std::size_t position =
                (data->oldest +
                    static_cast<std::size_t>(index))
                % data->values.size();

            return ImPlotPoint(
                index,
                data->values[position]);
        }

        void RenderCircularHistory(
            const char* label,
            std::span<const float> history,
            std::size_t historyIndex,
            std::size_t historyCount,
            float height)
        {
            if (history.empty() || historyCount == 0)
            {
                ImGui::TextDisabled("%s", T(Text::CollectingData));
                return;
            }

            const std::size_t count =
                (std::min)(historyCount, history.size());

            CircularHistoryData data{
                history,
                (historyIndex + history.size() - count)
                    % history.size()
            };

            if (ImPlot::BeginPlot(
                label,
                ImVec2(-1.0f, height)))
            {
                ImPlot::SetupAxes(
                    "Samples",
                    "Usage (%)",
                    ImPlotAxisFlags_NoTickLabels,
                    ImPlotAxisFlags_NoTickLabels);

                ImPlot::SetupAxisLimits(
                    ImAxis_X1,
                    0.0,
                    static_cast<double>(
                        std::max<std::size_t>(count - 1, 1)),
                    ImPlotCond_Always);

                ImPlot::SetupAxisLimits(
                    ImAxis_Y1,
                    0.0,
                    100.0,
                    ImPlotCond_Always);

                ImPlot::PlotLineG(
                    "CPU",
                    GetCircularHistoryPoint,
                    &data,
                    static_cast<int>(count));

                ImPlot::EndPlot();
            }
        }

        void RenderTopology(const CpuSnapshot& snapshot)
        {
            SectionTitle(
                T(Text::ProcessorTopology),
                T(Text::PhysicalLogicalConfig));

            if (ImGui::BeginTable(
                "CpuTopology",
                3,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", T(Text::Sockets));
                ImGui::Text(
                    "%lu",
                    static_cast<unsigned long>(
                        snapshot.processor.socketCount));

                ImGui::TableNextColumn();
                ImGui::TextDisabled(
                    "%s", T(Text::PhysicalCores));
                ImGui::Text(
                    "%lu",
                    static_cast<unsigned long>(
                        snapshot.processor.physicalCoreCount));

                ImGui::TableNextColumn();
                ImGui::TextDisabled(
                    "%s", T(Text::LogicalProcessorsCard));
                ImGui::Text(
                    "%lu",
                    static_cast<unsigned long>(
                        snapshot.processor.logicalProcessorCount));

                ImGui::EndTable();
            }
        }

        void RenderFrequency(const CpuSnapshot& snapshot)
        {
            SectionTitle(
                T(Text::FrequencyTitle),
                T(Text::FrequencyDesc));

            if (ImGui::BeginTable(
                "CpuFrequency",
                3,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("ESTIMATED");

                if (snapshot.frequency.estimatedMHz > 0.0)
                {
                    ImGui::TextColored(
                        AccentColor(),
                        "%.2f GHz",
                        snapshot.frequency.estimatedMHz / 1000.0);
                }
                else
                {
                    ImGui::TextDisabled("Unavailable");
                }

                ImGui::TableNextColumn();
                ImGui::TextDisabled("EFFECTIVE");

                if (snapshot.frequency.effectiveMHz > 0.0)
                {
                    ImGui::Text(
                        "%.2f GHz",
                        snapshot.frequency.effectiveMHz / 1000.0);
                }
                else
                {
                    ImGui::TextDisabled("Unavailable");
                }

                ImGui::TableNextColumn();
                ImGui::TextDisabled("PERFORMANCE");

                ImGui::Text(
                    "%.1f%%",
                    snapshot.frequency.performancePercent);

                ImGui::EndTable();
            }

            ImGui::TextDisabled(
                "Base frequency: %lu MHz",
                static_cast<unsigned long>(
                    snapshot.frequency.baseMHz));

            ImGui::TextDisabled(
                "Maximum frequency counter: %.1f%%",
                snapshot.frequency.maximumFrequencyPercent);
        }

        void RenderCache(const CpuSnapshot& snapshot)
        {
            SectionTitle(
                T(Text::CacheTitle),
                T(Text::CacheDesc));

            if (snapshot.caches.empty())
            {
                ImGui::TextDisabled(
                    "Cache information unavailable.");
                return;
            }

            if (ImGui::BeginTable(
                "CpuCache",
                4,
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("LEVEL");
                ImGui::TableSetupColumn("SIZE");
                ImGui::TableSetupColumn("LINE SIZE");
                ImGui::TableSetupColumn("ASSOCIATIVITY");

                ImGui::TableHeadersRow();

                for (const auto& cache : snapshot.caches)
                {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("L%u", cache.level);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text(
                        "%lu KB",
                        static_cast<unsigned long>(cache.sizeKB));

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text(
                        "%lu bytes",
                        static_cast<unsigned long>(cache.lineSize));

                    ImGui::TableSetColumnIndex(3);

                    if (cache.associativity == 0xFF)
                        ImGui::Text("Fully associative");
                    else if (cache.associativity == 0)
                        ImGui::Text("Unknown");
                    else
                        ImGui::Text("%u-way", cache.associativity);
                }

                ImGui::EndTable();
            }
        }

        void RenderVirtualization(const CpuSnapshot& snapshot)
        {
            SectionTitle(
                T(Text::VirtualizationTitle),
                T(Text::VirtualizationDesc));

            if (ImGui::BeginTable(
                "CpuVirtualization",
                3,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("HARDWARE SUPPORT");

                ImGui::TextColored(
                    snapshot.virtualization.supported
                    ? AccentColor()
                    : DangerColor(),
                    "%s",
                    snapshot.virtualization.supported
                    ? "Supported"
                    : "Not supported");

                ImGui::TableNextColumn();
                ImGui::TextDisabled("FIRMWARE");

                ImGui::Text(
                    "%s",
                    snapshot.virtualization.firmwareEnabled
                    ? "Enabled"
                    : "Disabled / Unknown");

                ImGui::TableNextColumn();
                ImGui::TextDisabled("HYPERVISOR");

                ImGui::Text(
                    "%s",
                    snapshot.virtualization.hypervisorPresent
                    ? "Detected"
                    : "Not detected");

                ImGui::EndTable();
            }
        }

        void RenderSystemActivity(const CpuSnapshot& snapshot)
        {
            SectionTitle(
                T(Text::SystemActivityTitle),
                T(Text::SystemActivityDesc));

            if (ImGui::BeginTable(
                "CpuSystemActivity",
                5,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("PROCESSES");
                ImGui::Text(
                    "%lu",
                    static_cast<unsigned long>(
                        snapshot.activity.processCount));

                ImGui::TableNextColumn();
                ImGui::TextDisabled("THREADS");
                ImGui::Text(
                    "%lu",
                    static_cast<unsigned long>(
                        snapshot.activity.threadCount));

                ImGui::TableNextColumn();
                ImGui::TextDisabled("HANDLES");
                ImGui::Text(
                    "%lu",
                    static_cast<unsigned long>(
                        snapshot.activity.handleCount));

                ImGui::TableNextColumn();
                ImGui::TextDisabled("CONTEXT SWITCHES");
                ImGui::Text(
                    "%lu",
                    static_cast<unsigned long>(
                        snapshot.activity.contextSwitches));

                ImGui::TableNextColumn();
                ImGui::TextDisabled("INTERRUPTS");
                ImGui::Text(
                    "%lu",
                    static_cast<unsigned long>(
                        snapshot.activity.interrupts));

                ImGui::EndTable();
            }
        }

        void RenderHardwareInformation(const CpuSnapshot& snapshot)
        {
            if (!ImGui::CollapsingHeader(
                    T(Text::HardwareInformation),
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }

            ImGui::BeginChild(
                "CpuHardwareInformation",
                ImVec2(0.0f, 500.0f),
                true);

            RenderTopology(snapshot);
            ImGui::Separator();

            RenderFrequency(snapshot);
            ImGui::Separator();

            RenderCache(snapshot);
            ImGui::Separator();

            RenderVirtualization(snapshot);
            ImGui::Separator();

            RenderSystemActivity(snapshot);

            ImGui::EndChild();
        }

        void RenderProcessorMiniChart(
            const ProcessorChartHistory& history)
        {
            const std::size_t count =
                (std::min)(history.historyCount, history.values.size());

            if (count == 0)
            {
                ImGui::TextDisabled("%s", T(Text::CollectingData));
                return;
            }

            CircularHistoryData data{
                std::span<const float>(history.values),
                (history.historyIndex + history.values.size() - count)
                    % history.values.size()
            };

            const std::string label =
                "##ProcessorChart_" +
                std::to_string(history.group) + "_" +
                std::to_string(history.logicalIndex);

            if (ImPlot::BeginPlot(
                label.c_str(),
                ImVec2(-1.0f, 75.0f),
                ImPlotFlags_NoTitle |
                ImPlotFlags_NoLegend |
                ImPlotFlags_NoMouseText))
            {
                ImPlot::SetupAxes(
                    nullptr,
                    nullptr,
                    ImPlotAxisFlags_NoDecorations,
                    ImPlotAxisFlags_NoDecorations);

                ImPlot::SetupAxisLimits(
                    ImAxis_X1,
                    0.0,
                    static_cast<double>(
                        std::max<std::size_t>(count - 1, 1)),
                    ImPlotCond_Always);

                ImPlot::SetupAxisLimits(
                    ImAxis_Y1,
                    0.0,
                    100.0,
                    ImPlotCond_Always);

                ImPlot::PlotLineG(
                    "Usage",
                    GetCircularHistoryPoint,
                    &data,
                    static_cast<int>(count));

                ImPlot::EndPlot();
            }
        }

        void RenderLogicalProcessors(
            const CpuSnapshot& snapshot,
            std::span<const ProcessorChartHistory> histories,
            bool& showCharts)
        {
            SectionTitle(
                T(Text::LogicalProcessors),
                T(Text::IndividualUtilization));

            if (ImGui::RadioButton(
                    T(Text::Bars), !showCharts))
            {
                showCharts = false;
            }

            ImGui::SameLine();

            if (ImGui::RadioButton(
                    T(Text::MiniCharts), showCharts))
            {
                showCharts = true;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (snapshot.logicalProcessors.empty())
            {
                ImGui::TextDisabled(
                    "%s", T(Text::NoLogicalProcessors));
                return;
            }

            const int columns = showCharts ? 2 : 1;

            if (!ImGui::BeginTable(
                "LogicalProcessorGrid",
                columns,
                ImGuiTableFlags_SizingStretchSame))
            {
                return;
            }

            for (const auto& processor : snapshot.logicalProcessors)
            {
                ImGui::TableNextColumn();

                const float usage = ClampUsage(
                    static_cast<float>(processor.usagePercent));

                const std::string cardId =
                    "Processor_" +
                    std::to_string(processor.processorGroup) + "_" +
                    std::to_string(processor.logicalProcessorIndex);

                ImGui::BeginChild(
                    cardId.c_str(),
                    ImVec2(0.0f, showCharts ? 150.0f : 65.0f),
                    true);

                ImGui::Text(
                    "Group %u | CPU %u",
                    processor.processorGroup,
                    processor.logicalProcessorIndex);

                ImGui::SameLine();

                ImGui::TextColored(
                    GetUsageColor(usage),
                    "%.1f%%",
                    usage);

                ImGui::SameLine();

                if (processor.estimatedFrequencyMHz > 0.0)
                {
                    ImGui::Text(
                        "%.2f GHz",
                        processor.estimatedFrequencyMHz / 1000.0);
                }
                else
                {
                    ImGui::TextDisabled("-- GHz");
                }

                DrawUsageBar(usage);

                if (showCharts)
                {
                    const auto it = std::find_if(
                        histories.begin(),
                        histories.end(),
                        [&processor](const ProcessorChartHistory& history)
                        {
                            return history.group ==
                                processor.processorGroup &&
                                history.logicalIndex ==
                                processor.logicalProcessorIndex;
                        });

                    if (it != histories.end())
                        RenderProcessorMiniChart(*it);
                    else
                        ImGui::TextDisabled("%s", T(Text::CollectingData));
                }

                ImGui::EndChild();
            }

            ImGui::EndTable();
        }
    }

    void CpuDashboard::Render(
        const CpuSnapshot& snapshot,
        std::span<const float> cpuHistory,
        std::size_t cpuHistoryIndex,
        std::size_t cpuHistoryCount,
        std::span<const ProcessorChartHistory> processorHistories,
        bool& showProcessorCharts,
        const UI::Theme::UiPalette& palette,
        UI::Localization::Language language)
    {
        ActivePalette = palette;
        ActiveLanguage = language;

        UI::Widgets::Title(
            T(Text::CpuMonitor), palette);

        ImGui::SameLine();
        UI::Widgets::Subtitle(
            T(Text::RealTimeCpu), palette);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        RenderProcessorIdentity(snapshot);

        RenderSummary(snapshot);

        SectionTitle(
            T(Text::PerformanceHistory),
            T(Text::CpuUtilization));

        RenderCircularHistory(
            "CpuPerformanceHistory",
            cpuHistory,
            cpuHistoryIndex,
            cpuHistoryCount,
            230.0f);

        ImGui::Spacing();

        RenderHardwareInformation(snapshot);

        if (ImGui::CollapsingHeader(
                T(Text::LogicalProcessors),
                ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginChild(
                "LogicalProcessorsPanel",
                ImVec2(0.0f, 500.0f),
                true);

            RenderLogicalProcessors(
                snapshot,
                processorHistories,
                showProcessorCharts);

            ImGui::EndChild();
        }
    }
}
