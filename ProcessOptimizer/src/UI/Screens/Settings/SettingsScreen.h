#pragma once

#include "../../Localization/Localization.h"
#include "../../Theme/Theme.h"

namespace UI::Screens::Settings
{
    class SettingsScreen
    {
    public:
        // Devuelve true si el usuario ha iniciado/detenido el servicio
        // en este frame, para que Window fuerce un redibujado.
        static bool Render(
            UI::Theme::AppSettings& settings,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language);
    };
}
