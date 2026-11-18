
#include "Chart.h"

#include <imgui.h>
#include <implot.h>

namespace UI::Components
{
    void Chart::Render(
        const ChartConfig& config,
        std::span<const ChartSeries> series
    )
    {
        ImPlotFlags flags = ImPlotFlags_NoInputs |
            ImPlotFlags_NoFrame;
        ImPlot::PushStyleVar(
            ImPlotStyleVar_PlotPadding,
            ImVec2(2.0f, 2.0f)
        );
        if (ImPlot::BeginPlot(
            config.title.c_str(),
            ImVec2(-1.0f, config.height),
            flags
        ))
        {
            ImPlot::SetupAxes(
                nullptr,
                nullptr,
                ImPlotAxisFlags_NoTickLabels,
                ImPlotAxisFlags_NoTickLabels
            );

            if (config.lockXAxis)
            {
                ImPlot::SetupAxisLimits(
                    ImAxis_X1,
                    config.xMin,
                    config.xMax,
                    ImGuiCond_None
                );
            }

            if (config.lockYAxis)
            {
                ImPlot::SetupAxisLimits(
                    ImAxis_Y1,
                    config.yMin,
                    config.yMax,
                    ImGuiCond_None
                );
            }

            // Representar todas las series recibidas.
            for (const ChartSeries& item : series)
            {
                if (item.values.empty())
                    continue;

                ImPlot::PlotLine(
                    item.name.c_str(),
                    item.values.data(),
                    static_cast<int>(item.values.size())
                );
            }

            ImPlot::EndPlot();
        }
    }
}
