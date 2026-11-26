#pragma once

#include "../../Localization/Localization.h"
#include "../../Theme/Theme.h"

#include "../../../IPC/PipeTransport.h"
#include "../../../Optimization/OptimizationTypes.h"

namespace UI::Screens::Optimization
{
    struct OptimizationCommand
    {
        bool changed = false;
        bool startBenchmark = false;
        bool enabled = false;
        ::Optimization::OptimizerMode mode =
            ::Optimization::OptimizerMode::Off;

        bool startAb = false;
        bool stopAb = false;
        bool applyCandidate = true;
        bool setExperimental = false;
        bool experimentalEnabled = false;
        bool setEtw = false;
        bool etwEnabled = false;
        bool setContainment = false;
        bool containmentEnabled = false;
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
            bool serviceConnected,
            IPC::ConnectionIssue serviceIssue =
                IPC::ConnectionIssue::None);
    };
}
