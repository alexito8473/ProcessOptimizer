#pragma once

#include "../../Localization/Localization.h"
#include "../../Theme/Theme.h"

#include "../../../Optimization/OptimizationTypes.h"

namespace UI::Screens::Optimization
{
    struct OptimizationCommand
    {
        bool changed = false;
        bool enabled = false;
        ::Optimization::OptimizerMode mode =
            ::Optimization::OptimizerMode::Off;
    };

    class OptimizationScreen
    {
    public:
        static OptimizationCommand Render(
            UI::Theme::AppSettings& settings,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language,
            const ::Optimization::TopologySnapshot& topology,
            const ::Optimization::OptimizerState& optimizer,
            bool hasTopology,
            bool hasOptimizer,
            bool serviceConnected);
    };
}
