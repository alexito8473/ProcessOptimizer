
#include "MemoryDashboard.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "../../Components/Charts/Chart.h"

namespace UI::Dashboard::Memory
{
    namespace
    {
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

        void FormatBytes(
            std::uint64_t bytes,
            char* buffer,
            std::size_t bufferSize)
        {
            constexpr double KB = 1024.0;
            constexpr double MB = KB * 1024.0;
            constexpr double GB = MB * 1024.0;
            constexpr double TB = GB * 1024.0;

            const double value = static_cast<double>(bytes);

            if (value >= TB)
                sprintf_s(buffer, bufferSize, "%.2f TB", value / TB);
            else if (value >= GB)
                sprintf_s(buffer, bufferSize, "%.2f GB", value / GB);
            else if (value >= MB)
                sprintf_s(buffer, bufferSize, "%.1f MB", value / MB);
            else if (value >= KB)
                sprintf_s(buffer, bufferSize, "%.1f KB", value / KB);
            else
                sprintf_s(buffer, bufferSize, "%llu B",
                    static_cast<unsigned long long>(bytes));
        }

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

        void RenderSummary(const MemorySnapshot& snapshot)
        {
            char totalText[64]{};
            char usedText[64]{};
            char speedText[64]{};
            char slotsText[64]{};

            FormatBytes(
                snapshot.totalPhysicalBytes,
                totalText,
                sizeof(totalText));

            FormatBytes(
                snapshot.usedPhysicalBytes,
                usedText,
                sizeof(usedText));

            // Utilizamos la mayor velocidad configurada disponible
            // entre los módulos detectados.
            std::uint32_t speed = 0;

            for (const auto& module : snapshot.modules)
            {
                const std::uint32_t moduleSpeed =
                    module.configuredSpeed > 0
                    ? module.configuredSpeed
                    : module.speed;

                if (moduleSpeed > speed)
                    speed = moduleSpeed;
            }

            if (speed > 0)
                sprintf_s(speedText, "%u MT/s", speed);
            else
                sprintf_s(speedText, "Unavailable");

            if (snapshot.totalSlots > 0)
            {
                sprintf_s(
                    slotsText,
                    "%u / %u",
                    snapshot.usedSlots,
                    snapshot.totalSlots);
            }
            else
            {
                sprintf_s(slotsText, "Unavailable");
            }

            const float usage = static_cast<float>(
                std::clamp(snapshot.usagePercent, 0.0, 100.0));

            if (ImGui::BeginTable(
                "MemorySummary",
                4,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();

                MetricCard(
                    "MemoryTotalCard",
                    "TOTAL MEMORY",
                    totalText,
                    "Detected physical memory");

                ImGui::TableNextColumn();

                MetricCard(
                    "MemoryUsedCard",
                    "MEMORY IN USE",
                    usedText,
                    "Currently used physical memory",
                    GetUsageColor(usage));

                ImGui::TableNextColumn();

                MetricCard(
                    "MemorySpeedCard",
                    "MEMORY SPEED",
                    speedText,
                    "Configured module speed");

                ImGui::TableNextColumn();

                MetricCard(
                    "MemorySlotsCard",
                    "MEMORY SLOTS",
                    slotsText,
                    "Used slots / total slots");

                ImGui::EndTable();
            }

            ImGui::Spacing();

            ImGui::TextDisabled("CURRENT MEMORY UTILIZATION");

            ImGui::SameLine();

            ImGui::TextColored(
                GetUsageColor(usage),
                "%.1f%%",
                usage);

            ImGui::ProgressBar(
                usage / 100.0f,
                ImVec2(-1.0f, 12.0f),
                "");

            ImGui::Spacing();
        }

        void RenderMemoryHistory(
            std::span<const float> history,
            std::size_t historyIndex,
            std::size_t historyCount)
        {
            SectionTitle(
                "MEMORY PERFORMANCE",
                "Real-time physical memory utilization");

            if (history.empty() || historyCount == 0)
            {
                ImGui::TextDisabled(
                    "Collecting memory history...");

                ImGui::Spacing();
                return;
            }

            std::size_t sampleCount = historyCount;

            if (sampleCount > history.size())
                sampleCount = history.size();

            static std::vector<float> orderedHistory;
            orderedHistory.resize(sampleCount);

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

            config.title = "MEMORY USAGE HISTORY";
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
                "RAM",
                std::span<const float>(orderedHistory)
            };

            UI::Components::Chart::Render(
                config,
                std::span<const UI::Components::ChartSeries>(&series,1), true);

            ImGui::Spacing();
        }

        void RenderMemoryInformation(
            const MemorySnapshot& snapshot)
        {
            SectionTitle(
                "MEMORY INFORMATION",
                "Operating system memory statistics");

            char committed[64]{};
            char commitLimit[64]{};
            char cached[64]{};
            char paged[64]{};
            char nonPaged[64]{};

            FormatBytes(
                snapshot.committedBytes,
                committed,
                sizeof(committed));

            FormatBytes(
                snapshot.commitLimitBytes,
                commitLimit,
                sizeof(commitLimit));

            FormatBytes(
                snapshot.cachedBytes,
                cached,
                sizeof(cached));

            FormatBytes(
                snapshot.pagedPoolBytes,
                paged,
                sizeof(paged));

            FormatBytes(
                snapshot.nonPagedPoolBytes,
                nonPaged,
                sizeof(nonPaged));

            if (ImGui::BeginTable(
                "MemoryInformation",
                2,
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("METRIC");
                ImGui::TableSetupColumn("VALUE");

                ImGui::TableHeadersRow();

                auto addRow = [](const char* label, const char* value)
                    {
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextDisabled("%s", label);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%s", value);
                    };

                addRow("Committed memory", committed);
                addRow("Commit limit", commitLimit);
                addRow("Cached memory", cached);
                addRow("Paged pool", paged);
                addRow("Non-paged pool", nonPaged);

                ImGui::EndTable();
            }

            ImGui::Spacing();
        }

        void RenderMemoryModules(const MemorySnapshot& snapshot)
        {
            SectionTitle(
                "MEMORY MODULES",
                "Physical memory slots");

            if (!snapshot.moduleInformationAvailable)
            {
                ImGui::TextDisabled(
                    "Memory module information is unavailable.");

                ImGui::Spacing();
                return;
            }

            if (snapshot.totalSlots == 0)
            {
                ImGui::TextDisabled(
                    "No memory slot information was reported by the system.");

                ImGui::Spacing();
                return;
            }

            const int columns = 4;

            if (ImGui::BeginTable(
                "MemorySlotGrid",
                columns,
                ImGuiTableFlags_SizingStretchSame))
            {
                std::size_t moduleIndex = 0;

                for (std::uint32_t slot = 0;
                    slot < snapshot.totalSlots;
                    ++slot)
                {
                    ImGui::TableNextColumn();

                    const MemoryModuleInfo* module = nullptr;

                    // Solo representamos como ocupados los módulos
                    // que WMI haya identificado expresamente.
                    while (moduleIndex < snapshot.modules.size())
                    {
                        if (snapshot.modules[moduleIndex].isPopulated)
                        {
                            module = &snapshot.modules[moduleIndex];
                            ++moduleIndex;
                            break;
                        }

                        ++moduleIndex;
                    }

                    const std::string cardId =
                        "MemorySlot_" + std::to_string(slot);

                    ImGui::BeginChild(
                        cardId.c_str(),
                        ImVec2(0.0f, 165.0f),
                        true);

                    if (module)
                    {
                        ImGui::TextColored(
                            AccentColor,
                            "SLOT %u",
                            slot + 1);

                        ImGui::SameLine();

                        ImGui::TextColored(
                            AccentColor,
                            "OCCUPIED");

                        ImGui::Spacing();

                        char capacity[64]{};

                        FormatBytes(
                            module->capacityBytes,
                            capacity,
                            sizeof(capacity));

                        ImGui::Text("%s", capacity);

                        ImGui::TextDisabled(
                            "%s",
                            module->deviceLocator.empty()
                            ? L"Location unavailable"
                            : module->deviceLocator.c_str());

                        ImGui::Separator();

                        ImGui::TextDisabled(
                            "%s",
                            module->manufacturer.empty()
                            ? L"Unknown manufacturer"
                            : module->manufacturer.c_str());

                        const std::uint32_t moduleSpeed =
                            module->configuredSpeed > 0
                            ? module->configuredSpeed
                            : module->speed;

                        if (moduleSpeed > 0)
                        {
                            ImGui::Text(
                                "%u MT/s",
                                moduleSpeed);
                        }
                        else
                        {
                            ImGui::TextDisabled(
                                "Speed unavailable");
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "SLOT %u",
                            slot + 1);

                        ImGui::Spacing();

                        ImGui::TextDisabled("EMPTY");

                        ImGui::TextDisabled(
                            "No installed module reported");
                    }

                    ImGui::EndChild();
                }

                ImGui::EndTable();
            }

            ImGui::TextDisabled(
                "Slot order is illustrative unless the firmware reports exact locations.");

            ImGui::Spacing();
        }
    }

    void MemoryDashboard::Render(
        const MemorySnapshot& snapshot,
        std::span<const float> memoryHistory,
        std::size_t historyIndex,
        std::size_t historyCount)
    {
        ImGui::TextColored(
            AccentColor,
            "MEMORY MONITOR");

        ImGui::SameLine();

        ImGui::TextDisabled(
            "| Real-time memory monitoring");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        RenderSummary(snapshot);

        RenderMemoryHistory(
            memoryHistory,
            historyIndex,
            historyCount);

        RenderMemoryInformation(snapshot);

        if (ImGui::CollapsingHeader(
            "MEMORY MODULES",
            ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginChild(
                "MemoryModulesPanel",
                ImVec2(0.0f, 500.0f),
                true);

            RenderMemoryModules(snapshot);

            ImGui::EndChild();
        }
    }
}
