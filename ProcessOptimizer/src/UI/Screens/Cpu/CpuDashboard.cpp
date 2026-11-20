
#include "CpuDashboard.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <span>
#include <imgui.h>
#include <vector>
#include "../../Components/Charts/Chart.h"

namespace UI::Dashboard::Cpu
{
    namespace
    {
        // ========================================================
        // COLORS
        // ========================================================

        constexpr ImVec4 AccentColor =
            ImVec4(0.30f, 0.85f, 0.72f, 1.0f);

        constexpr ImVec4 TextColor =
            ImVec4(0.92f, 0.95f, 0.98f, 1.0f);

        constexpr ImVec4 SecondaryColor =
            ImVec4(0.55f, 0.62f, 0.70f, 1.0f);

        constexpr ImVec4 WarningColor =
            ImVec4(0.95f, 0.70f, 0.30f, 1.0f);

        constexpr ImVec4 DangerColor =
            ImVec4(0.95f, 0.35f, 0.35f, 1.0f);

        ImVec4 GetUsageColor(float usage)
        {
            if (usage >= 85.0f)
                return DangerColor;

            if (usage >= 65.0f)
                return WarningColor;

            return AccentColor;
        }

        // ========================================================
        // COMMON UI
        // ========================================================

        void SectionTitle(
            const char* title,
            const char* description = nullptr)
        {
            ImGui::TextColored(AccentColor, "%s", title);

            if (description)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("| %s", description);
            }

            ImGui::Spacing();
        }

        void DrawUsageBar(float usage)
        {
            usage = std::clamp(usage, 0.0f, 100.0f);

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
            ImVec4 valueColor = TextColor)
        {
            ImGui::BeginChild(
                id,
                ImVec2(0.0f, 118.0f),
                true);

            ImGui::TextDisabled("%s", title);
            ImGui::Spacing();

            ImGui::PushStyleColor(
                ImGuiCol_Text,
                valueColor);

            ImGui::SetWindowFontScale(1.45f);
            ImGui::TextUnformatted(value);
            ImGui::SetWindowFontScale(1.0f);

            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::TextDisabled("%s", description);

            ImGui::EndChild();
        }

        void InformationRow(
            const char* label,
            const char* value)
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", value);
        }

        // ========================================================
        // SUMMARY
        // ========================================================

        void RenderSummary(const CpuSnapshot& snapshot)
        {
            const float usage = std::clamp(
                static_cast<float>(snapshot.totalUsagePercent),
                0.0f,
                100.0f);

            char usageText[32]{};
            char frequencyText[32]{};
            char coresText[32]{};
            char logicalText[32]{};
            char socketsText[32]{};

            sprintf_s(usageText, "%.1f%%", usage);

            if (snapshot.frequency.estimatedMHz > 0.0)
            {
                sprintf_s(
                    frequencyText,
                    "%.2f GHz",
                    snapshot.frequency.estimatedMHz / 1000.0);
            }
            else
            {
                sprintf_s(frequencyText, "--");
            }

            sprintf_s(
                coresText,
                "%lu",
                snapshot.processor.physicalCoreCount);

            sprintf_s(
                logicalText,
                "%lu",
                snapshot.processor.logicalProcessorCount);

            sprintf_s(
                socketsText,
                "%lu",
                snapshot.processor.socketCount);

            if (ImGui::BeginTable(
                "CpuSummary",
                5,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();

                MetricCard(
                    "CpuUsageCard",
                    "CPU USAGE",
                    usageText,
                    "Total processor usage",
                    GetUsageColor(usage));

                ImGui::TableNextColumn();

                MetricCard(
                    "CpuFrequencyCard",
                    "ESTIMATED FREQUENCY",
                    frequencyText,
                    "Estimated, not hardware-measured",
                    AccentColor);

                ImGui::TableNextColumn();

                MetricCard(
                    "CpuCoresCard",
                    "PHYSICAL CORES",
                    coresText,
                    "Detected physical cores");

                ImGui::TableNextColumn();

                MetricCard(
                    "CpuLogicalCard",
                    "LOGICAL PROCESSORS",
                    logicalText,
                    "Available logical processors");

                ImGui::TableNextColumn();

                MetricCard(
                    "CpuSocketsCard",
                    "SOCKETS",
                    socketsText,
                    "Detected CPU packages");

                ImGui::EndTable();
            }

            ImGui::Spacing();
        }

        // ========================================================
        // PROCESSOR IDENTITY
        // ========================================================

        void RenderProcessorIdentity(const CpuSnapshot& snapshot)
        {
            ImGui::BeginChild(
                "CpuIdentity",
                ImVec2(0.0f, 105.0f),
                true);

            ImGui::TextColored(
                AccentColor,
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

        // ========================================================
        // GLOBAL CPU GRAPH
        // ========================================================

        void RenderCpuHistory(
            std::span<const float> history,
            std::size_t historyIndex,
            std::size_t historyCount,
            double estimatedGHz)
        {
            char title[128]{};

            if (estimatedGHz > 0.0)
            {
                sprintf_s(
                    title,
                    "CPU PERFORMANCE | %.2f GHz",
                    estimatedGHz);
            }
            else
            {
                sprintf_s(title, "CPU PERFORMANCE HISTORY");
            }

            SectionTitle(
                "PERFORMANCE HISTORY",
                "Real-time processor utilization");

            if (history.empty())
            {
                ImGui::TextDisabled("Collecting CPU history...");
                ImGui::Spacing();
                return;
            }

            std::size_t sampleCount = historyCount;

            if (sampleCount > history.size())
            {
                sampleCount = history.size();
            }

            if (sampleCount == 0)
            {
                ImGui::TextDisabled("Collecting CPU history...");
                ImGui::Spacing();
                return;
            }

            // Reorganizar las muestras en orden cronológico.
            std::vector<float> orderedHistory(sampleCount);

            const std::size_t oldestIndex =
                (historyIndex + history.size() - sampleCount)
                % history.size();

            for (std::size_t i = 0; i < sampleCount; ++i)
            {
                const std::size_t index =
                    (oldestIndex + i) % history.size();

                orderedHistory[i] = history[index];
            }

            UI::Components::ChartConfig config;

            config.title = title;
            config.xAxisLabel = "Samples";
            config.yAxisLabel = "Usage (%)";
            config.height = 230.0f;

            config.lockXAxis = true;
            config.xMin = 0.0;
            config.xMax = static_cast<double>(sampleCount - 1);

            config.lockYAxis = true;
            config.yMin = 0.0;
            config.yMax = 100.0;

            UI::Components::ChartSeries series
            {
                "CPU",
                std::span<const float>(orderedHistory)
            };

            UI::Components::Chart::Render(
                config,
                std::span<const UI::Components::ChartSeries>(&series,1), true);

            ImGui::Spacing();
        }

        // ========================================================
        // TOPOLOGY
        // ========================================================

        void RenderTopology(const CpuSnapshot& snapshot)
        {
            SectionTitle(
                "PROCESSOR TOPOLOGY",
                "Physical and logical configuration");

            if (ImGui::BeginTable(
                "CpuTopology",
                3,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("SOCKETS");
                ImGui::Text(
                    "%lu",
                    snapshot.processor.socketCount);

                ImGui::TableNextColumn();
                ImGui::TextDisabled("PHYSICAL CORES");
                ImGui::Text(
                    "%lu",
                    snapshot.processor.physicalCoreCount);

                ImGui::TableNextColumn();
                ImGui::TextDisabled("LOGICAL PROCESSORS");
                ImGui::Text(
                    "%lu",
                    snapshot.processor.logicalProcessorCount);

                ImGui::EndTable();
            }

            ImGui::Spacing();
        }

        // ========================================================
        // FREQUENCY
        // ========================================================

        void RenderFrequency(const CpuSnapshot& snapshot)
        {
            SectionTitle(
                "FREQUENCY",
                "Processor performance information");

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
                        AccentColor,
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
                "PDH reference: %lu MHz",
                snapshot.frequency.baseMHz);

            ImGui::TextDisabled(
                "Maximum frequency counter: %.1f%%",
                snapshot.frequency.maximumFrequencyPercent);

            ImGui::Spacing();
        }

        // ========================================================
        // CACHE
        // ========================================================

        void RenderCache(const CpuSnapshot& snapshot)
        {
            SectionTitle(
                "CACHE INFORMATION",
                "Processor cache hierarchy");

            if (snapshot.caches.empty())
            {
                ImGui::TextDisabled(
                    "Cache information unavailable.");

                ImGui::Spacing();
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
                    ImGui::Text("%lu KB", cache.sizeKB);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%lu bytes", cache.lineSize);

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

            ImGui::Spacing();
        }

        // ========================================================
        // VIRTUALIZATION
        // ========================================================

        void RenderVirtualization(const CpuSnapshot& snapshot)
        {
            SectionTitle(
                "VIRTUALIZATION",
                "Hardware and hypervisor status");

            if (ImGui::BeginTable(
                "CpuVirtualization",
                3,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();

                ImGui::TextDisabled("HARDWARE SUPPORT");
                ImGui::TextColored(
                    snapshot.virtualization.supported
                    ? AccentColor
                    : DangerColor,
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

            ImGui::Spacing();
        }

        // ========================================================
        // SYSTEM ACTIVITY
        // ========================================================

        void RenderSystemActivity(const CpuSnapshot& snapshot)
        {
            SectionTitle(
                "SYSTEM ACTIVITY",
                "Operating system activity");

            if (ImGui::BeginTable(
                "CpuSystemActivity",
                5,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("PROCESSES");
                ImGui::Text("%lu", snapshot.activity.processCount);

                ImGui::TableNextColumn();
                ImGui::TextDisabled("THREADS");
                ImGui::Text("%lu", snapshot.activity.threadCount);

                ImGui::TableNextColumn();
                ImGui::TextDisabled("HANDLES");
                ImGui::Text("%lu", snapshot.activity.handleCount);

                ImGui::TableNextColumn();
                ImGui::TextDisabled("CONTEXT SWITCHES");
                ImGui::Text("%lu", snapshot.activity.contextSwitches);

                ImGui::TableNextColumn();
                ImGui::TextDisabled("INTERRUPTS");
                ImGui::Text("%lu", snapshot.activity.interrupts);

                ImGui::EndTable();
            }

            ImGui::Spacing();
        }

        // ========================================================
        // CPU HARDWARE INFORMATION
        // ========================================================

        void RenderHardwareInformation(const CpuSnapshot& snapshot)
        {
            if (!ImGui::CollapsingHeader(
                "HARDWARE INFORMATION",
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
            ImGui::Spacing();
        }

        // ========================================================
        // PROCESSOR MINI CHART
        // ========================================================

        void RenderProcessorMiniChart(
            const ProcessorChartHistory& history)
        {
            std::array<float, HistorySize> orderedValues{};

            std::size_t sampleCount = history.historyCount;
            if (sampleCount > history.values.size())
                sampleCount = history.values.size();

            const std::size_t oldestIndex =
                (history.historyIndex +
                    history.values.size() -
                    sampleCount) %
                history.values.size();

            for (std::size_t i = 0;
                i < sampleCount;
                ++i)
            {
                const std::size_t index =
                    (oldestIndex + i) % history.values.size();

                orderedValues[i] = history.values[index];
            }

            const std::string label =
                "##ProcessorChart_" +
                std::to_string(history.group) + "_" +
                std::to_string(history.logicalIndex);

            UI::Components::ChartConfig config;

            config.title = label;
            config.xAxisLabel = "";
            config.yAxisLabel = "";
            config.height = 85.0f;

            config.lockXAxis = true;
            config.xMin = 0.0;
            config.xMax = static_cast<double>(HistorySize - 1);

            config.lockYAxis = true;
            config.yMin = 0.0;
            config.yMax = 100.0;

            UI::Components::ChartSeries series
            {
                "Usage",
                std::span<const float>(
                    orderedValues.data(),
                    sampleCount)
            };

            UI::Components::Chart::Render(
                config,
                std::span<const UI::Components::ChartSeries>(&series, 1), false);
        }

        // ========================================================
        // LOGICAL PROCESSORS
        // ========================================================

        void RenderLogicalProcessors(
            const CpuSnapshot& snapshot,
            std::span<const ProcessorChartHistory> histories,
            bool& showCharts)
        {
            SectionTitle(
                "LOGICAL PROCESSORS",
                "Individual processor utilization");

            if (ImGui::RadioButton("Bars", !showCharts))
                showCharts = false;

            ImGui::SameLine();

            if (ImGui::RadioButton("Mini charts", showCharts))
                showCharts = true;

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (snapshot.logicalProcessors.empty())
            {
                ImGui::TextDisabled(
                    "No logical processors detected.");
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

                const float usage = std::clamp(
                    static_cast<float>(processor.usagePercent),
                    0.0f,
                    100.0f);

                const std::string cardId =
                    "Processor_" +
                    std::to_string(processor.processorGroup) + "_" +
                    std::to_string(processor.logicalProcessorIndex);

                ImGui::BeginChild(
                    cardId.c_str(),
                    ImVec2(0.0f, showCharts ? 165.0f : 65.0f),
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
                            return history.group == processor.processorGroup &&
                                history.logicalIndex ==
                                processor.logicalProcessorIndex;
                        });

                    if (it != histories.end())
                        RenderProcessorMiniChart(*it);
                    else
                        ImGui::TextDisabled("Collecting data...");
                }

                ImGui::EndChild();
            }

            ImGui::EndTable();
        }

    } // namespace

    // ============================================================
    // PUBLIC SCREEN ENTRY POINT
    // ============================================================

    void CpuDashboard::Render(
        const CpuSnapshot& snapshot,
        std::span<const float> cpuHistory,
        std::size_t cpuHistoryIndex,
        std::size_t cpuHistoryCount,
        std::span<const ProcessorChartHistory> processorHistories,
        bool& showProcessorCharts)
    {
        // Header
        ImGui::TextColored(
            AccentColor,
            "CPU MONITOR");

        ImGui::SameLine();

        ImGui::TextDisabled(
            "| Real-time processor monitoring");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Processor identity
        RenderProcessorIdentity(snapshot);

        // Five summary cards
        RenderSummary(snapshot);

        // Global CPU performance graph
        RenderCpuHistory(
            cpuHistory,
            cpuHistoryIndex,
            cpuHistoryCount,
            snapshot.frequency.estimatedMHz / 1000.0);

        // Hardware details: topology, frequency, cache, virtualization
        // and system activity.
        RenderHardwareInformation(snapshot);

        // Per-logical-processor utilization and optional mini charts
        if (ImGui::CollapsingHeader(
            "LOGICAL PROCESSORS",
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
 