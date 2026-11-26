#include "Theme.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <implot.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace UI::Theme
{
    namespace
    {
        constexpr wchar_t PersonalizeKey[] =
            L"Software\\Microsoft\\Windows\\CurrentVersion\\"
            L"Themes\\Personalize";

        ImVec4 WithAlpha(ImVec4 color, float alpha)
        {
            color.w = alpha;
            return color;
        }

        UiPalette DarkBase()
        {
            UiPalette p{};

            p.Background = ImVec4(0.055f, 0.065f, 0.085f, 1.0f);
            p.Surface = ImVec4(0.075f, 0.085f, 0.110f, 1.0f);
            p.SurfaceAlt = ImVec4(0.120f, 0.140f, 0.180f, 1.0f);
            p.Text = ImVec4(0.920f, 0.950f, 0.980f, 1.0f);
            p.TextMuted = ImVec4(0.560f, 0.610f, 0.680f, 1.0f);
            p.Border = ImVec4(0.200f, 0.240f, 0.310f, 1.0f);

            p.Accent = ImVec4(0.200f, 0.450f, 0.750f, 1.0f);
            p.AccentHover = ImVec4(0.280f, 0.580f, 0.920f, 1.0f);
            p.Success = ImVec4(0.350f, 0.780f, 0.450f, 1.0f);
            p.Warning = ImVec4(0.950f, 0.700f, 0.250f, 1.0f);
            p.Danger = ImVec4(0.900f, 0.350f, 0.400f, 1.0f);

            p.Cpu = ImVec4(0.300f, 0.750f, 1.000f, 1.0f);
            p.Memory = ImVec4(0.400f, 0.850f, 0.500f, 1.0f);
            p.Gpu = ImVec4(1.000f, 0.650f, 0.250f, 1.0f);
            p.Process = ImVec4(0.750f, 0.600f, 1.000f, 1.0f);

            return p;
        }

        UiPalette LightBase()
        {
            UiPalette p{};

            p.Background = ImVec4(0.940f, 0.950f, 0.970f, 1.0f);
            p.Surface = ImVec4(0.980f, 0.985f, 0.995f, 1.0f);
            p.SurfaceAlt = ImVec4(0.900f, 0.920f, 0.950f, 1.0f);
            p.Text = ImVec4(0.080f, 0.100f, 0.140f, 1.0f);
            p.TextMuted = ImVec4(0.380f, 0.420f, 0.480f, 1.0f);
            p.Border = ImVec4(0.780f, 0.820f, 0.870f, 1.0f);

            p.Accent = ImVec4(0.100f, 0.380f, 0.680f, 1.0f);
            p.AccentHover = ImVec4(0.160f, 0.500f, 0.850f, 1.0f);
            p.Success = ImVec4(0.120f, 0.550f, 0.250f, 1.0f);
            p.Warning = ImVec4(0.750f, 0.450f, 0.050f, 1.0f);
            p.Danger = ImVec4(0.720f, 0.180f, 0.220f, 1.0f);

            p.Cpu = ImVec4(0.050f, 0.450f, 0.850f, 1.0f);
            p.Memory = ImVec4(0.100f, 0.550f, 0.250f, 1.0f);
            p.Gpu = ImVec4(0.850f, 0.380f, 0.050f, 1.0f);
            p.Process = ImVec4(0.480f, 0.250f, 0.750f, 1.0f);

            return p;
        }

        void ApplyDesign(
            UiPalette& palette,
            UiDesign design,
            bool light)
        {
            switch (design)
            {
            case UiDesign::Ocean:
                palette.Accent = light
                    ? ImVec4(0.04f, 0.42f, 0.52f, 1.0f)
                    : ImVec4(0.10f, 0.55f, 0.65f, 1.0f);
                palette.AccentHover = light
                    ? ImVec4(0.08f, 0.58f, 0.70f, 1.0f)
                    : ImVec4(0.15f, 0.72f, 0.82f, 1.0f);
                palette.Cpu = ImVec4(0.10f, 0.65f, 0.85f, 1.0f);
                palette.Memory = ImVec4(0.20f, 0.80f, 0.70f, 1.0f);
                palette.Gpu = ImVec4(0.35f, 0.55f, 0.95f, 1.0f);
                palette.Process = ImVec4(0.55f, 0.45f, 0.90f, 1.0f);
                break;

            case UiDesign::Forest:
                palette.Accent = light
                    ? ImVec4(0.12f, 0.45f, 0.24f, 1.0f)
                    : ImVec4(0.22f, 0.60f, 0.32f, 1.0f);
                palette.AccentHover = light
                    ? ImVec4(0.18f, 0.60f, 0.32f, 1.0f)
                    : ImVec4(0.30f, 0.76f, 0.40f, 1.0f);
                palette.Cpu = ImVec4(0.35f, 0.70f, 0.95f, 1.0f);
                palette.Memory = ImVec4(0.30f, 0.80f, 0.35f, 1.0f);
                palette.Gpu = ImVec4(0.85f, 0.70f, 0.25f, 1.0f);
                palette.Process = ImVec4(0.60f, 0.55f, 0.85f, 1.0f);
                break;

            case UiDesign::Sunset:
                palette.Accent = light
                    ? ImVec4(0.72f, 0.30f, 0.12f, 1.0f)
                    : ImVec4(0.90f, 0.42f, 0.20f, 1.0f);
                palette.AccentHover = light
                    ? ImVec4(0.85f, 0.40f, 0.18f, 1.0f)
                    : ImVec4(1.00f, 0.55f, 0.28f, 1.0f);
                palette.Cpu = ImVec4(1.00f, 0.55f, 0.30f, 1.0f);
                palette.Memory = ImVec4(0.95f, 0.75f, 0.25f, 1.0f);
                palette.Gpu = ImVec4(0.90f, 0.30f, 0.50f, 1.0f);
                palette.Process = ImVec4(0.65f, 0.40f, 0.85f, 1.0f);
                break;

            case UiDesign::Graphite:
                palette.Accent = light
                    ? ImVec4(0.28f, 0.32f, 0.38f, 1.0f)
                    : ImVec4(0.48f, 0.52f, 0.58f, 1.0f);
                palette.AccentHover = light
                    ? ImVec4(0.40f, 0.45f, 0.52f, 1.0f)
                    : ImVec4(0.62f, 0.67f, 0.74f, 1.0f);
                palette.Cpu = ImVec4(0.65f, 0.72f, 0.80f, 1.0f);
                palette.Memory = ImVec4(0.55f, 0.78f, 0.62f, 1.0f);
                palette.Gpu = ImVec4(0.85f, 0.72f, 0.52f, 1.0f);
                palette.Process = ImVec4(0.72f, 0.65f, 0.82f, 1.0f);
                break;

            case UiDesign::HighContrast:
                palette.Text = light
                    ? ImVec4(0.02f, 0.02f, 0.02f, 1.0f)
                    : ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
                palette.TextMuted = light
                    ? ImVec4(0.18f, 0.18f, 0.18f, 1.0f)
                    : ImVec4(0.82f, 0.82f, 0.82f, 1.0f);
                palette.Border = light
                    ? ImVec4(0.25f, 0.25f, 0.25f, 1.0f)
                    : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
                palette.Accent = light
                    ? ImVec4(0.00f, 0.20f, 0.80f, 1.0f)
                    : ImVec4(1.00f, 0.90f, 0.20f, 1.0f);
                palette.AccentHover = light
                    ? ImVec4(0.00f, 0.32f, 1.00f, 1.0f)
                    : ImVec4(1.00f, 1.00f, 0.40f, 1.0f);
                palette.Cpu = ImVec4(0.00f, 0.60f, 1.00f, 1.0f);
                palette.Memory = ImVec4(0.00f, 0.90f, 0.20f, 1.0f);
                palette.Gpu = ImVec4(1.00f, 0.50f, 0.00f, 1.0f);
                palette.Process = ImVec4(0.90f, 0.20f, 0.90f, 1.0f);
                break;

            case UiDesign::Default:
            default:
                break;
            }
        }
    }

    std::filesystem::path SettingsFilePath()
    {
        wchar_t* appData = nullptr;
        std::size_t length = 0;

        if (_wdupenv_s(&appData, &length, L"APPDATA") != 0 ||
            !appData)
        {
            return std::filesystem::path(
                L"settings.json");
        }

        const std::filesystem::path path =
            std::filesystem::path(appData) /
            L"ProcessOptimizer" /
            L"settings.json";

        free(appData);
        return path;
    }

    std::string ReadJsonString(
        const std::string& json,
        const char* key)
    {
        const std::string needle =
            std::string("\"") + key + "\"";

        const std::size_t keyPos = json.find(needle);

        if (keyPos == std::string::npos)
            return {};

        const std::size_t colon =
            json.find(':', keyPos + needle.size());

        if (colon == std::string::npos)
            return {};

        const std::size_t begin =
            json.find('"', colon + 1);

        if (begin == std::string::npos)
            return {};

        const std::size_t end =
            json.find('"', begin + 1);

        if (end == std::string::npos)
            return {};

        return json.substr(begin + 1, end - begin - 1);
    }

    ThemeMode ParseMode(const std::string& value)
    {
        if (value == "light")
            return ThemeMode::Light;
        if (value == "dark")
            return ThemeMode::Dark;
        return ThemeMode::System;
    }

    UiDesign ParseDesign(const std::string& value)
    {
        if (value == "ocean")
            return UiDesign::Ocean;
        if (value == "forest")
            return UiDesign::Forest;
        if (value == "sunset")
            return UiDesign::Sunset;
        if (value == "graphite")
            return UiDesign::Graphite;
        if (value == "highcontrast")
            return UiDesign::HighContrast;
        return UiDesign::Default;
    }

    Localization::Language ParseLanguage(
        const std::string& value)
    {
        if (value == "en")
            return Localization::Language::English;
        return Localization::Language::Spanish;
    }

    const char* ModeToString(ThemeMode mode)
    {
        switch (mode)
        {
        case ThemeMode::Light:
            return "light";
        case ThemeMode::Dark:
            return "dark";
        case ThemeMode::System:
        default:
            return "system";
        }
    }

    const char* DesignToString(UiDesign design)
    {
        switch (design)
        {
        case UiDesign::Ocean:
            return "ocean";
        case UiDesign::Forest:
            return "forest";
        case UiDesign::Sunset:
            return "sunset";
        case UiDesign::Graphite:
            return "graphite";
        case UiDesign::HighContrast:
            return "highcontrast";
        case UiDesign::Default:
        default:
            return "default";
        }
    }

    const char* LanguageToString(
        Localization::Language language)
    {
        return language == Localization::Language::English
            ? "en"
            : "es";
    }

    bool ReadJsonBool(
        const std::string& json,
        const char* key,
        bool fallback)
    {
        const std::string needle =
            std::string("\"") + key + "\"";

        const std::size_t keyPos = json.find(needle);
        if (keyPos == std::string::npos)
            return fallback;

        const std::size_t colon =
            json.find(':', keyPos + needle.size());
        if (colon == std::string::npos)
            return fallback;

        const std::size_t valuePos = json.find_first_not_of(
            " \t\r\n", colon + 1);
        if (valuePos == std::string::npos)
            return fallback;

        if (json.compare(valuePos, 4, "true") == 0)
            return true;
        if (json.compare(valuePos, 5, "false") == 0)
            return false;
        return fallback;
    }

    int ParseOptimizerMode(const std::string& value)
    {
        if (value == "balanced" ||
            value == "gaming" ||
            value == "multitasking")
            return 1;
        return 0;
    }

    const char* OptimizerModeToString(int mode)
    {
        if (mode == 1)
            return "balanced";
        return "off";
    }

    ThemeSettings LoadSettings()
    {
        ThemeSettings settings{};

        std::ifstream file(SettingsFilePath());

        if (!file.is_open())
            return settings;

        const std::string json(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        const std::string mode = ReadJsonString(json, "mode");
        const std::string design =
            ReadJsonString(json, "design");
        const std::string language =
            ReadJsonString(json, "language");
        const std::string optimizer =
            ReadJsonString(json, "optimizerMode");

        if (!mode.empty())
            settings.mode = ParseMode(mode);

        if (!design.empty())
            settings.design = ParseDesign(design);

        if (!language.empty())
            settings.language = ParseLanguage(language);

        settings.optimizerEnabled = ReadJsonBool(
            json, "optimizerEnabled", false);
        settings.optimizerMode =
            ParseOptimizerMode(optimizer);

        return settings;
    }

    void SaveSettings(const ThemeSettings& settings)
    {
        const std::filesystem::path path =
            SettingsFilePath();

        std::error_code error;
        std::filesystem::create_directories(
            path.parent_path(), error);

        std::ofstream file(path);

        if (!file.is_open())
            return;

        file << "{\n"
             << "  \"mode\": \""
             << ModeToString(settings.mode) << "\",\n"
             << "  \"design\": \""
             << DesignToString(settings.design) << "\",\n"
             << "  \"language\": \""
             << LanguageToString(settings.language)
             << "\",\n"
             << "  \"optimizerEnabled\": "
             << (settings.optimizerEnabled ? "true" : "false")
             << ",\n"
             << "  \"optimizerMode\": \""
             << OptimizerModeToString(settings.optimizerMode)
             << "\"\n"
             << "}\n";
    }

    bool SystemPrefersLight()
    {
        DWORD value = 0;
        DWORD size = sizeof(value);

        if (RegGetValueW(
                HKEY_CURRENT_USER,
                PersonalizeKey,
                L"AppsUseLightTheme",
                RRF_RT_DWORD,
                nullptr,
                &value,
                &size) != ERROR_SUCCESS)
        {
            return false;
        }

        return value != 0;
    }

    UiPalette ResolvePalette(const ThemeSettings& settings)
    {
        bool light = false;

        switch (settings.mode)
        {
        case ThemeMode::Light:
            light = true;
            break;
        case ThemeMode::Dark:
            light = false;
            break;
        case ThemeMode::System:
        default:
            light = SystemPrefersLight();
            break;
        }

        UiPalette palette =
            light ? LightBase() : DarkBase();

        ApplyDesign(palette, settings.design, light);

        return palette;
    }

    void Apply(const UiPalette& palette)
    {
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding = 8.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 5.0f;
        style.PopupRounding = 5.0f;
        style.ScrollbarRounding = 5.0f;
        style.GrabRounding = 5.0f;

        style.WindowPadding = ImVec2(12.0f, 12.0f);
        style.FramePadding = ImVec2(8.0f, 5.0f);
        style.ItemSpacing = ImVec2(8.0f, 8.0f);

        style.Colors[ImGuiCol_Text] = palette.Text;
        style.Colors[ImGuiCol_TextDisabled] = palette.TextMuted;
        style.Colors[ImGuiCol_WindowBg] = palette.Background;
        style.Colors[ImGuiCol_ChildBg] = palette.Surface;
        style.Colors[ImGuiCol_PopupBg] = palette.Surface;
        style.Colors[ImGuiCol_Border] = palette.Border;
        style.Colors[ImGuiCol_BorderShadow] =
            ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        style.Colors[ImGuiCol_FrameBg] = palette.SurfaceAlt;
        style.Colors[ImGuiCol_FrameBgHovered] =
            WithAlpha(palette.Accent, 0.30f);
        style.Colors[ImGuiCol_FrameBgActive] =
            WithAlpha(palette.Accent, 0.45f);
        style.Colors[ImGuiCol_Button] =
            WithAlpha(palette.Accent, 0.55f);
        style.Colors[ImGuiCol_ButtonHovered] = palette.AccentHover;
        style.Colors[ImGuiCol_ButtonActive] = palette.Accent;
        style.Colors[ImGuiCol_Header] =
            WithAlpha(palette.Accent, 0.35f);
        style.Colors[ImGuiCol_HeaderHovered] =
            WithAlpha(palette.AccentHover, 0.55f);
        style.Colors[ImGuiCol_HeaderActive] = palette.Accent;
        style.Colors[ImGuiCol_Separator] = palette.Border;
        style.Colors[ImGuiCol_ScrollbarBg] = palette.Surface;
        style.Colors[ImGuiCol_ScrollbarGrab] = palette.SurfaceAlt;
        style.Colors[ImGuiCol_ScrollbarGrabHovered] =
            WithAlpha(palette.Accent, 0.45f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = palette.Accent;
        style.Colors[ImGuiCol_CheckMark] = palette.AccentHover;
        style.Colors[ImGuiCol_SliderGrab] = palette.Accent;
        style.Colors[ImGuiCol_SliderGrabActive] =
            palette.AccentHover;
        style.Colors[ImGuiCol_Tab] =
            WithAlpha(palette.Accent, 0.25f);
        style.Colors[ImGuiCol_TabHovered] = palette.AccentHover;
        style.Colors[ImGuiCol_TabActive] = palette.Accent;

        ImPlotStyle& plot = ImPlot::GetStyle();

        plot.Colors[ImPlotCol_FrameBg] = palette.SurfaceAlt;
        plot.Colors[ImPlotCol_PlotBg] = palette.Surface;
        plot.Colors[ImPlotCol_PlotBorder] = palette.Border;
        plot.Colors[ImPlotCol_LegendBg] =
            WithAlpha(palette.Surface, 0.92f);
        plot.Colors[ImPlotCol_LegendBorder] = palette.Border;
        plot.Colors[ImPlotCol_LegendText] = palette.Text;
        plot.Colors[ImPlotCol_TitleText] = palette.Text;
        plot.Colors[ImPlotCol_InlayText] = palette.Text;
        plot.Colors[ImPlotCol_AxisText] = palette.TextMuted;
        plot.Colors[ImPlotCol_AxisGrid] =
            WithAlpha(palette.Border, 0.55f);
        plot.Colors[ImPlotCol_AxisTick] = palette.Border;
        plot.Colors[ImPlotCol_AxisBgHovered] =
            WithAlpha(palette.Accent, 0.35f);
        plot.Colors[ImPlotCol_AxisBgActive] = palette.Accent;
        plot.Colors[ImPlotCol_Selection] =
            WithAlpha(palette.Accent, 0.35f);
        plot.Colors[ImPlotCol_Crosshairs] = palette.Border;
    }

    const char* ModeName(ThemeMode mode)
    {
        switch (mode)
        {
        case ThemeMode::Light:
            return "Light";
        case ThemeMode::Dark:
            return "Dark";
        case ThemeMode::System:
        default:
            return "System";
        }
    }

    const char* DesignName(UiDesign design)
    {
        switch (design)
        {
        case UiDesign::Ocean:
            return "Ocean";
        case UiDesign::Forest:
            return "Forest";
        case UiDesign::Sunset:
            return "Sunset";
        case UiDesign::Graphite:
            return "Graphite";
        case UiDesign::HighContrast:
            return "High contrast";
        case UiDesign::Default:
        default:
            return "Default";
        }
    }
}
