#include "SettingsScreen.h"

#include <imgui.h>

#include <array>

#include "../../Components/Widgets/Widgets.h"
#include "../../../IPC/PipeTransport.h"
#include "../../../Service/ServiceControl.h"

namespace UI::Screens::Settings
{
    namespace
    {
        using Text = UI::Localization::Text;

        const char* T(
            UI::Localization::Language language,
            Text text)
        {
            return UI::Localization::Translate(language, text);
        }

        void RenderAppearance(
            UI::Theme::AppSettings& settings,
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language)
        {
            UI::Widgets::SectionTitle(
                T(language, Text::Appearance),
                nullptr,
                palette);

            const char* modes[]{
                T(language, Text::System),
                T(language, Text::Light),
                T(language, Text::Dark)
            };

            int mode = static_cast<int>(settings.mode);

            if (UI::Widgets::SegmentedControl(
                    "ThemeMode",
                    modes,
                    3,
                    mode,
                    palette))
            {
                settings.mode =
                    static_cast<UI::Theme::ThemeMode>(mode);
            }

            if (settings.mode == UI::Theme::ThemeMode::System)
            {
                ImGui::SameLine();
                UI::Widgets::Badge(
                    T(language, Text::FollowingSystem),
                    palette.Success,
                    palette);
            }

            ImGui::Spacing();

            const char* designs[]{
                T(language, Text::DesignDefault),
                T(language, Text::DesignOcean),
                T(language, Text::DesignForest),
                T(language, Text::DesignSunset),
                T(language, Text::DesignGraphite),
                T(language, Text::DesignHighContrast)
            };

            int design = static_cast<int>(settings.design);

            if (UI::Widgets::SegmentedControl(
                    "UiDesign",
                    designs,
                    6,
                    design,
                    palette))
            {
                settings.design =
                    static_cast<UI::Theme::UiDesign>(design);
            }

            ImGui::Spacing();

            const char* languages[]{
                T(language, Text::English),
                T(language, Text::Spanish)
            };

            int currentLanguage =
                static_cast<int>(settings.language);

            if (UI::Widgets::SegmentedControl(
                    "Language",
                    languages,
                    2,
                    currentLanguage,
                    palette))
            {
                settings.language =
                    static_cast<UI::Localization::Language>(
                        currentLanguage);
            }

            ImGui::Spacing();

            UI::Widgets::Subtitle(
                T(language, Text::ColorPalette),
                palette);

            UI::Widgets::ColorSwatch(
                T(language, Text::CpuShort),
                palette.Cpu,
                palette);
            ImGui::SameLine();
            UI::Widgets::ColorSwatch(
                T(language, Text::Ram),
                palette.Memory,
                palette);
            ImGui::SameLine();
            UI::Widgets::ColorSwatch(
                T(language, Text::GpuShort),
                palette.Gpu,
                palette);
            ImGui::SameLine();
            UI::Widgets::ColorSwatch(
                T(language, Text::Processes),
                palette.Process,
                palette);
        }

        void RenderPreview(
            const UI::Theme::UiPalette& palette,
            UI::Localization::Language language)
        {
            ImGui::Spacing();
            ImGui::Spacing();

            UI::Widgets::SectionTitle(
                T(language, Text::Preview),
                T(language, Text::PreviewDescription),
                palette);

            UI::Widgets::BeginCard(
                "SettingsPreview",
                0.0f,
                palette);

            UI::Widgets::Title(
                T(language, Text::AppTitle),
                palette);
            UI::Widgets::MutedText(
                T(language, Text::LiveSnapshot),
                palette);

            ImGui::Spacing();

            if (UI::Widgets::PrimaryButton(
                    T(language, Text::Primary), palette))
            {
            }

            ImGui::SameLine();

            if (UI::Widgets::SecondaryButton(
                    T(language, Text::Secondary), palette))
            {
            }

            ImGui::SameLine();

            if (UI::Widgets::GhostButton(
                    T(language, Text::Ghost), palette))
            {
            }

            ImGui::Spacing();

            UI::Widgets::Subtitle(
                T(language, Text::UsageBar),
                palette);

            UI::Widgets::UsageBar(
                68.0f, palette.Accent, palette);

            ImGui::Spacing();
            UI::Widgets::Divider(palette);
            ImGui::Spacing();

            UI::Widgets::StatCard(
                "SettingsPreviewCard",
                T(language, Text::CpuUsage),
                "68.0%",
                T(language, Text::RealTimeCpu),
                palette.Cpu,
                palette);

            UI::Widgets::EndCard();
        }

        void RenderService(
            UI::Localization::Language language,
            const UI::Theme::UiPalette& palette)
        {
            UI::Widgets::SectionTitle(
                T(language, Text::ServiceSection),
                nullptr,
                palette);

            UI::Widgets::BeginCard(
                "ServiceStatusCard", 116.0f, palette);

            const bool installed = Service::IsInstalled();
            const std::uint32_t state =
                Service::QueryStatus();

            IPC::PipeClient client;
            const bool connected = client.Ping();

            ImGui::TextUnformatted(
                T(language, Text::ServiceStatus));
            ImGui::SameLine();

            if (!installed)
            {
                UI::Widgets::Badge(
                    T(language, Text::ServiceNotInstalled),
                    palette.TextMuted,
                    palette);
            }
            else if (state == SERVICE_RUNNING)
            {
                UI::Widgets::Badge(
                    T(language, Text::ServiceRunning),
                    palette.Success,
                    palette);
            }
            else
            {
                UI::Widgets::Badge(
                    T(language, Text::ServiceStopped),
                    palette.Warning,
                    palette);
            }

            ImGui::Spacing();

            UI::Widgets::Badge(
                connected
                    ? T(language, Text::ServiceModeLabelRunning)
                    : T(language, Text::LocalMode),
                connected ? palette.Accent : palette.TextMuted,
                palette);

            ImGui::Spacing();

            if (installed)
            {
                if (state == SERVICE_RUNNING)
                {
                    if (UI::Widgets::SecondaryButton(
                            T(language, Text::StopServiceButton),
                            palette))
                    {
                        Service::Stop();
                    }
                }
                else if (UI::Widgets::PrimaryButton(
                             T(language, Text::StartServiceButton),
                             palette))
                {
                    Service::Start();
                }
            }

            UI::Widgets::EndCard();
        }
    }

    void SettingsScreen::Render(
        UI::Theme::AppSettings& settings,
        const UI::Theme::UiPalette& palette,
        UI::Localization::Language language)
    {
        UI::Widgets::Title(
            T(language, Text::Settings),
            palette);

        ImGui::SameLine();
        UI::Widgets::Subtitle(
            T(language, Text::Appearance),
            palette);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        RenderAppearance(settings, palette, language);
        RenderService(language, palette);
        RenderPreview(palette, language);

        ImGui::Spacing();

        if (UI::Widgets::SecondaryButton(
                T(language, Text::ResetDefaults),
                palette))
        {
            settings = UI::Theme::AppSettings{};
        }

        ImGui::SameLine();
        UI::Widgets::MutedText(
            T(language, Text::SavedNote),
            palette);
    }
}
