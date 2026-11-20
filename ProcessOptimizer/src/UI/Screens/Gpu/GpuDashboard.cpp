
#include "GpuDashboard.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <cstdio>
#include "../../Components/Charts/Chart.h"

namespace UI::Dashboard::Gpu
{
    namespace
    {
        int selectedGpu = 0;

        std::string WideToUtf8(const std::wstring& value)
        {
            if (value.empty())
                return {};

            int size = WideCharToMultiByte(
                CP_UTF8, 0, value.c_str(), -1,
                nullptr, 0, nullptr, nullptr
            );

            if (size <= 1)
                return {};

            std::string result(static_cast<std::size_t>(size), '\0');

            WideCharToMultiByte(
                CP_UTF8, 0, value.c_str(), -1,
                result.data(), size, nullptr, nullptr
            );

            result.resize(static_cast<std::size_t>(size - 1));
            return result;
        }

        std::string FormatBytes(std::uint64_t bytes)
        {
            constexpr double GB = 1024.0 * 1024.0 * 1024.0;
            constexpr double MB = 1024.0 * 1024.0;

            char buffer[64]{};

            if (bytes >= static_cast<std::uint64_t>(GB))
            {
                std::snprintf(
                    buffer, sizeof(buffer), "%.2f GB",
                    static_cast<double>(bytes) / GB
                );
            }
            else
            {
                std::snprintf(
                    buffer, sizeof(buffer), "%.1f MB",
                    static_cast<double>(bytes) / MB
                );
            }

            return buffer;
        }

        const char* GetEngineName(GpuEngineType type)
        {
            switch (type)
            {
            case GpuEngineType::ThreeD:            return "3D";
            case GpuEngineType::Copy:              return "Copy";
            case GpuEngineType::VideoDecode:       return "Video Decode";
            case GpuEngineType::VideoEncode:       return "Video Encode";
            case GpuEngineType::VideoProcessing:   return "Video Processing";
            case GpuEngineType::Compute:           return "Compute";
            case GpuEngineType::Graphics:          return "Graphics";
            case GpuEngineType::Overlay:            return "Overlay";
            default:                               return "Unknown";
            }
        }

        template<std::size_t N>
        std::array<float, N> GetOrderedHistory(
            const std::array<float, N>& values,
            std::size_t index,
            std::size_t count)
        {
            std::array<float, N> ordered{};

            if (count > N)
                count = N;

            const std::size_t start = (index + N - count) % N;

            for (std::size_t i = 0; i < count; ++i)
                ordered[i] = values[(start + i) % N];

            return ordered;
        }

        void BeginCard(const char* id, float height = 0.0f)
        {
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(14.0f, 12.0f)
            );

            ImGui::BeginChild(
                id,
                ImVec2(0.0f, height),
                ImGuiChildFlags_Borders
            );
        }
        void EndCard()
        {
            ImGui::PopStyleVar();
            ImGui::EndChild();
        }

        void SectionTitle(const char* title, const char* subtitle = nullptr)
        {
            ImGui::TextColored(
                ImVec4(0.25f, 0.82f, 0.80f, 1.0f),
                "%s", title
            );

            if (subtitle)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", subtitle);
            }

            ImGui::Spacing();
        }

        void MetricCard(
            const char* title,
            const char* value,
            const char* description)
        {
            BeginCard(title, 140.0f);

            ImGui::TextDisabled("%s", title);
            ImGui::Spacing();

            ImGui::SetWindowFontScale(1.45f);
            ImGui::TextUnformatted(value);
            ImGui::SetWindowFontScale(1.0f);

            if (description)
                ImGui::TextDisabled("%s", description);

            EndCard();
        }

        void InfoRow(const char* label, const std::string& value)
        {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", label);

            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", value.c_str());
        }

        void RenderChart(
            const char* title,
            const std::array<float, GpuHistorySize>& values,
            std::size_t index,
            std::size_t count,
            const char* unit = "%",
            bool percentage = true)
        {
            count = std::min(count, GpuHistorySize);
            auto ordered = GetOrderedHistory(
                values, index, count
            );

            UI::Components::ChartConfig config;
            config.title = title;
            config.unit = unit;
            config.height = 205.0f;
            config.lockYAxis = percentage;
            config.yAxisLabel = "Usage (%)";
            if (percentage)
            {
                config.yMin = 0.0;
                config.yMax = 100.0;
            }
            config.lockXAxis = true;
            config.xMin = 0.0;
            config.xMax = static_cast<double>(
                std::max<std::size_t>(count - 1, 1)
                );
            config.showStatistics = true;
            config.showCurrentValue = true;
            config.showAverage = true;

            std::array<UI::Components::ChartSeries, 1> series =
            { {
                { "GPU", std::span<const float>(
                    ordered.data(), count) }
            } };

            UI::Components::Chart::Render(
                config,
                series,
                true
            );
        }

        void RenderUsageBar(const char* name, double usage)
        {
            float value = static_cast<float>(
                std::clamp(usage, 0.0, 100.0)
                );

            ImGui::TextUnformatted(name);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 55.0f);
            ImGui::Text("%.1f%%", value);

            ImGui::PushStyleColor(
                ImGuiCol_PlotHistogram,
                ImVec4(0.20f, 0.78f, 0.75f, 1.0f)
            );

            ImGui::ProgressBar(
                value / 100.0f,
                ImVec2(-1.0f, 9.0f),
                ""
            );

            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        void RenderOverview(const GpuAdapterInfo& gpu)
        {
            SectionTitle("GPU OVERVIEW", "Current adapter status");

            const std::string usage =
                std::to_string(static_cast<int>(gpu.usagePercent)) + "%";

            const std::string vram =
                FormatBytes(gpu.dedicatedVideoMemoryBytes);

            const std::string engines =
                std::to_string(gpu.engines.size());

            const std::string memoryUsed =
                FormatBytes(gpu.processDedicatedMemoryUsedBytes);

            if (ImGui::BeginTable(
                "GpuOverviewCards", 4,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();
                MetricCard("GPU USAGE", usage.c_str(), "Total engine utilization");

                ImGui::TableNextColumn();
                MetricCard("DEDICATED VRAM", vram.c_str(), "Physical capacity");

                ImGui::TableNextColumn();
                MetricCard("PROCESS VRAM", memoryUsed.c_str(), "ProcessOptimizer");

                ImGui::TableNextColumn();
                MetricCard("GPU ENGINES", engines.c_str(), "Detected engines");

                ImGui::EndTable();
            }

        }

        void RenderEngineDetails(const GpuAdapterInfo& gpu)
        {

            SectionTitle("GPU ENGINES", "Individual engine utilization");

            if (gpu.engines.empty())
            {
                ImGui::TextDisabled(
                    "No GPU engine counters are currently available."
                );
                return;
            }
        
            if (ImGui::BeginTable(
                "GpuEngineTable", 3,
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("ENGINE");
                ImGui::TableSetupColumn("INSTANCE");
                ImGui::TableSetupColumn("USAGE");
                ImGui::TableHeadersRow();

                for (const GpuEngineInfo& engine : gpu.engines)
                {
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(
                        GetEngineName(engine.type)
                    );

                    ImGui::TableNextColumn();
                    const std::string name = WideToUtf8(engine.name);
                    ImGui::TextWrapped("%s", name.c_str());

                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f%%", engine.usagePercent);
                }

                ImGui::EndTable();
            }
        }

        void RenderMemory(const GpuAdapterInfo& gpu)
        {
            SectionTitle("GPU MEMORY", "Dedicated and shared memory");

            if (ImGui::BeginTable(
                "GpuMemoryTable", 2,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();
                BeginCard("DedicatedMemoryCard", 125.0f);

                ImGui::TextDisabled("DEDICATED VIDEO MEMORY");
                ImGui::Spacing();

                const std::string total =
                    FormatBytes(gpu.dedicatedVideoMemoryBytes);

                ImGui::Text("Total: %s", total.c_str());

                const std::string used =
                    FormatBytes(gpu.processDedicatedMemoryUsedBytes);

                ImGui::Text("Process usage: %s", used.c_str());

                const std::string budget =
                    FormatBytes(gpu.processDedicatedMemoryBudgetBytes);

                ImGui::Text("Process budget: %s", budget.c_str());

                EndCard();

                ImGui::TableNextColumn();
                BeginCard("SharedMemoryCard", 125.0f);

                ImGui::TextDisabled("SHARED SYSTEM MEMORY");
                ImGui::Spacing();

                ImGui::Text(
                    "Physical capacity: %s",
                    FormatBytes(gpu.sharedSystemMemoryBytes).c_str()
                );

                ImGui::Text(
                    "Process usage: %s",
                    FormatBytes(gpu.processSharedMemoryUsedBytes).c_str()
                );

                ImGui::Text(
                    "Process budget: %s",
                    FormatBytes(gpu.processSharedMemoryBudgetBytes).c_str()
                );

                EndCard();

                ImGui::EndTable();
            }
        }

        void RenderHardware(const GpuAdapterInfo& gpu)
        {
            SectionTitle("HARDWARE INFORMATION", "Physical adapter and PCI");

            if (ImGui::BeginTable(
                "GpuHardwareTable", 2,
                ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("PROPERTY");
                ImGui::TableSetupColumn("VALUE");
                ImGui::TableHeadersRow();

                InfoRow("GPU name", WideToUtf8(gpu.name));
                InfoRow("Vendor ID", std::to_string(gpu.vendorId));
                InfoRow("Device ID", std::to_string(gpu.deviceId));
                InfoRow("Subsystem ID", std::to_string(gpu.subsystemId));
                InfoRow("Revision", std::to_string(gpu.revision));
                InfoRow("Dedicated memory", FormatBytes(gpu.dedicatedVideoMemoryBytes));
                InfoRow("Shared memory", FormatBytes(gpu.sharedSystemMemoryBytes));

                if (gpu.pciLocationAvailable)
                {
                    InfoRow("PCI bus", std::to_string(gpu.pciBus));
                    InfoRow("PCI device", std::to_string(gpu.pciDevice));
                    InfoRow("PCI function", std::to_string(gpu.pciFunction));
                }
                else
                {
                    InfoRow("PCI location", "Not available");
                }

                ImGui::EndTable();
            }
        }

        void RenderDriver(const GpuAdapterInfo& gpu)
        {
            SectionTitle("DRIVER & DIRECTX", "Graphics software information");

            if (ImGui::BeginTable(
                "GpuDriverTable", 2,
                ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("PROPERTY");
                ImGui::TableSetupColumn("VALUE");
                ImGui::TableHeadersRow();

                InfoRow(
                    "Driver provider",
                    gpu.driverInfoAvailable
                    ? WideToUtf8(gpu.driverProvider)
                    : "Not available"
                );

                InfoRow(
                    "Driver version",
                    WideToUtf8(gpu.driverVersion)
                );

                InfoRow(
                    "Driver date",
                    WideToUtf8(gpu.driverDate)
                );

                InfoRow(
                    "Device description",
                    WideToUtf8(gpu.driverDescription)
                );

                InfoRow(
                    "Device location",
                    WideToUtf8(gpu.deviceLocation)
                );

                InfoRow(
                    "Direct3D",
                    WideToUtf8(gpu.direct3DVersion)
                );

                InfoRow(
                    "Feature Level",
                    WideToUtf8(gpu.featureLevel)
                );

                InfoRow(
                    "WDDM",
                    gpu.wddmInfoAvailable
                    ? WideToUtf8(gpu.wddmVersion)
                    : "Not available"
                );

                InfoRow(
                    "Direct3D 11",
                    gpu.d3d11Supported ? "Supported" : "Not detected"
                );

                InfoRow(
                    "Direct3D 12",
                    gpu.d3d12Supported ? "Supported" : "Not detected"
                );

                InfoRow(
                    "DirectCompute",
                    gpu.directComputeSupported ? "Supported" : "Not detected"
                );

                ImGui::EndTable();
            }
        }
    }

    void GpuDashboard::Render(
        const GpuSnapshot& snapshot,
        const GpuDashboardHistory& history)
    {
        if (snapshot.gpus.empty())
        {
            ImGui::TextDisabled("No GPU detected.");
            return;
        }

        if (selectedGpu < 0 ||
            selectedGpu >= static_cast<int>(snapshot.gpus.size()))
        {
            selectedGpu = 0;
        }

        // ========================================================
        // GPU SELECTION
        // ========================================================


// ========================================================
// GPU SELECTION
// ========================================================

        if (snapshot.gpus.size() > 1)
        {
            ImGui::Spacing();

            ImGui::TextColored(
                ImVec4(0.30f, 0.85f, 0.72f, 1.0f),
                "SELECT GRAPHICS PROCESSOR"
            );

            ImGui::TextDisabled(
                "Choose the GPU you want to monitor"
            );

            ImGui::Spacing();

            const int gpuCount = static_cast<int>(snapshot.gpus.size());

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float buttonWidth =
                (availableWidth - spacing * (gpuCount - 1)) / gpuCount;

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
            ImGui::PushStyleVar(
                ImGuiStyleVar_FramePadding,
                ImVec2(12.0f, 12.0f)
            );

            for (int i = 0; i < gpuCount; ++i)
            {
                if (i > 0)
                    ImGui::SameLine();

                const bool selected = (selectedGpu == i);

                const ImVec4 normalColor =
                    ImVec4(0.10f, 0.13f, 0.17f, 1.0f);

                const ImVec4 hoveredColor =
                    ImVec4(0.13f, 0.21f, 0.23f, 1.0f);

                const ImVec4 activeColor =
                    ImVec4(0.10f, 0.35f, 0.31f, 1.0f);

                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    selected ? activeColor : normalColor
                );

                ImGui::PushStyleColor(
                    ImGuiCol_ButtonHovered,
                    hoveredColor
                );

                ImGui::PushStyleColor(
                    ImGuiCol_ButtonActive,
                    ImVec4(0.12f, 0.42f, 0.36f, 1.0f)
                );

                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    selected
                    ? ImVec4(0.40f, 1.0f, 0.83f, 1.0f)
                    : ImVec4(0.82f, 0.86f, 0.90f, 1.0f)
                );

                std::string label =
                    "GPU " + std::to_string(i + 1) + "\n" +
                    WideToUtf8(snapshot.gpus[i].name) +
                    "##GPU" + std::to_string(i);

                if (ImGui::Button(
                    label.c_str(),
                    ImVec2(buttonWidth, 58.0f)))
                {
                    selectedGpu = i;
                }

                ImGui::PopStyleColor(4);

                if (selected)
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();

                    const ImVec2 min = ImGui::GetItemRectMin();
                    const ImVec2 max = ImGui::GetItemRectMax();

                    drawList->AddRect(
                        min,
                        max,
                        IM_COL32(65, 220, 185, 255),
                        9.0f,
                        0,
                        2.0f
                    );
                }
            }

            ImGui::PopStyleVar(2);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }


        const GpuAdapterInfo& gpu =
            snapshot.gpus[selectedGpu];

        // ========================================================
        // FIND SELECTED GPU HISTORY
        // ========================================================

        const GpuAdapterHistory* gpuHistory = nullptr;

        for (const auto& adapterHistory : history.adapters)
        {
            if (adapterHistory.luidLowPart == gpu.luidLowPart &&
                adapterHistory.luidHighPart == gpu.luidHighPart)
            {
                gpuHistory = &adapterHistory;
                break;
            }
        }

        const float heightCard = 250.0f;

        ImGui::Spacing();

        // ========================================================
        // OVERVIEW
        // ========================================================

        RenderOverview(gpu);

        ImGui::Spacing();

        SectionTitle(
            "PERFORMANCE HISTORY",
            "GPU utilization"
        );

        if (!gpuHistory || gpuHistory->historyCount == 0)
        {
            ImGui::TextDisabled(
                "Waiting for GPU history..."
            );
        }
        else
        {
            // ====================================================
            // PERFORMANCE CHARTS
            // ====================================================

            if (ImGui::BeginTable(
                "GpuMainCharts",
                2,
                ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();

                BeginCard("GpuUsageChartCard", heightCard);
    
                RenderChart(
                    "GPU USAGE",
                    gpuHistory->usage,
                    gpuHistory->historyIndex,
                    gpuHistory->historyCount,
                    "%",
                    true
                );

                EndCard();

                ImGui::TableNextColumn();

                BeginCard("Gpu3DChartCard", heightCard);

                RenderChart(
                    "3D ENGINE",
                    gpuHistory->usage3D,
                    gpuHistory->historyIndex,
                    gpuHistory->historyCount,
                    "%",
                    true
                );

                EndCard();

                ImGui::TableNextColumn();

                BeginCard("GpuCopyChartCard", heightCard);

                RenderChart(
                    "COPY ENGINE",
                    gpuHistory->usageCopy,
                    gpuHistory->historyIndex,
                    gpuHistory->historyCount,
                    "%",
                    true
                );

                EndCard();

                ImGui::TableNextColumn();

                BeginCard("GpuComputeChartCard", heightCard);

                RenderChart(
                    "COMPUTE ENGINE",
                    gpuHistory->usageCompute,
                    gpuHistory->historyIndex,
                    gpuHistory->historyCount,
                    "%",
                    true
                );

                EndCard();

                ImGui::TableNextColumn();

                BeginCard("GpuDecodeChartCard", heightCard);

                RenderChart(
                    "VIDEO DECODE",
                    gpuHistory->usageVideoDecode,
                    gpuHistory->historyIndex,
                    gpuHistory->historyCount,
                    "%",
                    true
                );

                EndCard();

                ImGui::TableNextColumn();

                BeginCard("GpuEncodeChartCard", heightCard);

                RenderChart(
                    "VIDEO ENCODE",
                    gpuHistory->usageVideoEncode,
                    gpuHistory->historyIndex,
                    gpuHistory->historyCount,
                    "%",
                    true
                );

                EndCard();

                ImGui::TableNextColumn();

                BeginCard("GpuProcessingChartCard", heightCard);

                RenderChart(
                    "VIDEO PROCESSING",
                    gpuHistory->usageVideoProcessing,
                    gpuHistory->historyIndex,
                    gpuHistory->historyCount,
                    "%",
                    true
                );

                EndCard();

                ImGui::EndTable();
            }

            // ====================================================
            // MEMORY HISTORY
            // ====================================================

            ImGui::Spacing();

            if (ImGui::CollapsingHeader(
                "MEMORY HISTORY",
                ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginTable(
                    "GpuMemoryCharts",
                    2,
                    ImGuiTableFlags_SizingStretchSame))
                {
                    ImGui::TableNextColumn();

                    BeginCard("GpuDedicatedHistoryCard");

                    RenderChart(
                        "PROCESS DEDICATED MEMORY",
                        gpuHistory->dedicatedMemory,
                        gpuHistory->historyIndex,
                        gpuHistory->historyCount,
                        "MB",
                        true
                    );

                    EndCard();

                    ImGui::TableNextColumn();

                    BeginCard("GpuSharedHistoryCard");

                    RenderChart(
                        "PROCESS SHARED MEMORY",
                        gpuHistory->sharedMemory,
                        gpuHistory->historyIndex,
                        gpuHistory->historyCount,
                        "MB",
                        false
                    );

                    EndCard();

                    ImGui::EndTable();
                }
            }
        }

        // ========================================================
        // ENGINE DETAILS
        // ========================================================

        ImGui::Spacing();

        BeginCard("GpuEngineDetailsCard",300);
        RenderEngineDetails(gpu);
        EndCard();

        // ========================================================
        // MEMORY DETAILS
        // ========================================================

        ImGui::Spacing();

        BeginCard("GpuMemoryDetailsCard");
        RenderMemory(gpu);
        EndCard();

        // ========================================================
        // HARDWARE INFORMATION
        // ========================================================

        ImGui::Spacing();

        if (ImGui::CollapsingHeader(
            "HARDWARE INFORMATION",
            ImGuiTreeNodeFlags_DefaultOpen))
        {
            BeginCard("GpuHardwareCard", 500);
            RenderHardware(gpu);
            EndCard();
        }

        // ========================================================
        // DRIVER INFORMATION
        // ========================================================

        ImGui::Spacing();

        if (ImGui::CollapsingHeader(
            "DRIVER & DIRECTX",
            ImGuiTreeNodeFlags_DefaultOpen))
        {
            BeginCard("GpuDriverCard", 500);
            RenderDriver(gpu);
            EndCard();
        }
    }
}
