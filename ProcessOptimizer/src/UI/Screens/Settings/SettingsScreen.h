#pragma once

#include "../../Localization/Localization.h"
#include "../../Theme/Theme.h"

namespace UI::Screens::Settings
{
    class SettingsScreen
    {
    public:
        static void Render(
            UI::Theme::AppSettings& settings,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language);
    };
}
