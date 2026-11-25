#pragma once

#include <imgui.h>

#include "../Localization/Localization.h"

namespace UI::Theme
{
    enum class ThemeMode : int
    {
        System = 0,
        Light = 1,
        Dark = 2
    };

    enum class UiDesign : int
    {
        Default = 0,
        Ocean = 1,
        Forest = 2,
        Sunset = 3,
        Graphite = 4,
        HighContrast = 5
    };

    struct UiPalette
    {
        ImVec4 Background{};
        ImVec4 Surface{};
        ImVec4 SurfaceAlt{};
        ImVec4 Text{};
        ImVec4 TextMuted{};
        ImVec4 Border{};
        ImVec4 Accent{};
        ImVec4 AccentHover{};
        ImVec4 Success{};
        ImVec4 Warning{};
        ImVec4 Danger{};
        ImVec4 Cpu{};
        ImVec4 Memory{};
        ImVec4 Gpu{};
        ImVec4 Process{};
    };

    struct ThemeSettings
    {
        ThemeMode mode = ThemeMode::System;
        UiDesign design = UiDesign::Default;
        Localization::Language language =
            Localization::Language::Spanish;

        // Preferencia del orquestador. 0 = apagado, 1 = gaming,
        // 2 = multitarea; se guarda con el resto de ajustes.
        bool optimizerEnabled = false;
        int optimizerMode = 0;
    };

    using AppSettings = ThemeSettings;

    ThemeSettings LoadSettings();
    void SaveSettings(const ThemeSettings& settings);

    bool SystemPrefersLight();

    UiPalette ResolvePalette(const ThemeSettings& settings);

    void Apply(const UiPalette& palette);

    const char* ModeName(ThemeMode mode);
    const char* DesignName(UiDesign design);
}
