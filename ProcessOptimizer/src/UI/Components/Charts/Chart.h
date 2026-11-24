#pragma once

#include <string>
#include <span>

namespace UI::Components
{
    // Una serie de datos que se puede representar en la gráfica.
    struct ChartSeries
    {
        std::string name;
        std::span<const float> values;
    };

    // Configuración general de la gráfica.

    struct ChartConfig
    {
        std::string title = "Chart";
        std::string xAxisLabel = "Samples";
        std::string yAxisLabel = "Value";

        std::string unit = "%";

        float height = 260.0f;

        bool lockXAxis = false;
        double xMin = 0.0;
        double xMax = 119.0;

        bool lockYAxis = false;
        double yMin = 0.0;
        double yMax = 100.0;

        bool showStatistics = true;
        bool showLegend = false;
        bool showGrid = true;
        bool showCurrentValue = true;
        bool showAverage = true;
    };


    class Chart
    {
    public:
        static void Render(
            const ChartConfig& config,
            std::span<const ChartSeries> series,
            bool activeAxes
        );
    };
}
