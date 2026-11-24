#include "Widgets.h"

#include <algorithm>
#include <string>

namespace UI::Widgets
{
    namespace
    {
        ImVec4 WithAlpha(ImVec4 color, float alpha)
        {
            color.w = alpha;
            return color;
        }

        float ClampPercentage(float value)
        {
            return (std::max)(0.0f, (std::min)(value, 100.0f));
        }

        ImVec4 ContrastText(ImVec4 color)
        {
            const float luminance =
                0.2126f * color.x +
                0.7152f * color.y +
                0.0722f * color.z;

            return luminance > 0.55f
                ? ImVec4(0.05f, 0.05f, 0.05f, 1.0f)
                : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    void Title(
        const char* text,
        const UI::Theme::UiPalette& palette)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, palette.Text);
        ImGui::SetWindowFontScale(1.30f);
        ImGui::TextUnformatted(text);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
    }

    void Subtitle(
        const char* text,
        const UI::Theme::UiPalette& palette)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, palette.TextMuted);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    }

    void SectionTitle(
        const char* title,
        const char* subtitle,
        const UI::Theme::UiPalette& palette)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, palette.Text);
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();

        if (subtitle)
        {
            ImGui::SameLine();
            Subtitle(subtitle, palette);
        }

        ImGui::Spacing();
    }

    void MutedText(
        const char* text,
        const UI::Theme::UiPalette& palette)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, palette.TextMuted);
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    void ValueText(
        const char* text,
        ImVec4 color,
        float scale)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, color);

        if (scale != 1.0f)
            ImGui::SetWindowFontScale(scale);

        ImGui::TextUnformatted(text);

        if (scale != 1.0f)
            ImGui::SetWindowFontScale(1.0f);

        ImGui::PopStyleColor();
    }

    void BeginCard(
        const char* id,
        float height,
        const UI::Theme::UiPalette& palette)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Border, palette.Border);

        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildBorderSize, 1.0f);

        ImGui::BeginChild(
            id,
            ImVec2(0.0f, height),
            ImGuiChildFlags_Borders);

        ImGui::PopStyleVar();
    }

    void EndCard()
    {
        ImGui::PopStyleColor();
        ImGui::EndChild();
    }

    void StatCard(
        const char* id,
        const char* title,
        const char* value,
        const char* detail,
        ImVec4 color,
        const UI::Theme::UiPalette& palette)
    {
        BeginCard(id, 116.0f, palette);

        Subtitle(title, palette);
        ImGui::Spacing();
        ValueText(value, color, 1.45f);
        ImGui::Spacing();
        MutedText(detail, palette);

        EndCard();
    }

    void Badge(
        const char* text,
        ImVec4 color,
        const UI::Theme::UiPalette& palette)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button, WithAlpha(color, 0.20f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered, WithAlpha(color, 0.32f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive, WithAlpha(color, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Text, color);

        ImGui::SmallButton(text);

        ImGui::PopStyleColor(4);
    }

    void Divider(const UI::Theme::UiPalette& palette)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Separator, palette.Border);
        ImGui::Separator();
        ImGui::PopStyleColor();
    }

    bool PrimaryButton(
        const char* label,
        const UI::Theme::UiPalette& palette)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, palette.Accent);
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered, palette.AccentHover);
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive, palette.Accent);
        ImGui::PushStyleColor(
            ImGuiCol_Text, ContrastText(palette.Accent));

        const bool clicked = ImGui::Button(
            label, ImVec2(140.0f, 0.0f));

        ImGui::PopStyleColor(4);

        return clicked;
    }

    bool SecondaryButton(
        const char* label,
        const UI::Theme::UiPalette& palette)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button, WithAlpha(palette.Accent, 0.28f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            WithAlpha(palette.Accent, 0.48f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive, palette.Accent);
        ImGui::PushStyleColor(ImGuiCol_Text, palette.Text);

        const bool clicked = ImGui::Button(
            label, ImVec2(140.0f, 0.0f));

        ImGui::PopStyleColor(4);

        return clicked;
    }

    bool GhostButton(
        const char* label,
        const UI::Theme::UiPalette& palette)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            WithAlpha(palette.Accent, 0.30f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            WithAlpha(palette.Accent, 0.48f));
        ImGui::PushStyleColor(
            ImGuiCol_Text, palette.TextMuted);

        const bool clicked = ImGui::Button(label);

        ImGui::PopStyleColor(4);

        return clicked;
    }

    bool ToggleButton(
        const char* label,
        bool active,
        const UI::Theme::UiPalette& palette)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            active ? palette.Accent
                   : WithAlpha(palette.SurfaceAlt, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            active ? palette.AccentHover
                   : WithAlpha(palette.Accent, 0.35f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive, palette.Accent);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            active ? ContrastText(palette.Accent)
                   : palette.TextMuted);

        const bool clicked = ImGui::Button(label);

        ImGui::PopStyleColor(4);

        return clicked;
    }

    bool SegmentedControl(
        const char* id,
        const char* const* items,
        int count,
        int& selected,
        const UI::Theme::UiPalette& palette)
    {
        if (!items || count <= 0)
            return false;

        bool changed = false;

        ImGui::PushID(id);
        ImGui::BeginGroup();

        for (int i = 0; i < count; ++i)
        {
            if (i > 0)
                ImGui::SameLine();

            const bool active = i == selected;

            ImGui::PushStyleColor(
                ImGuiCol_Button,
                active ? palette.Accent
                       : WithAlpha(palette.SurfaceAlt, 1.0f));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                active ? palette.AccentHover
                       : WithAlpha(palette.Accent, 0.35f));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive, palette.Accent);
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                active ? ContrastText(palette.Accent)
                       : palette.TextMuted);

            const std::string label =
                std::string(items[i]) + "##" +
                std::to_string(i);

            if (ImGui::Button(label.c_str()))
            {
                selected = i;
                changed = true;
            }

            ImGui::PopStyleColor(4);
        }

        ImGui::EndGroup();
        ImGui::PopID();

        return changed;
    }

    void UsageBar(
        float percentage,
        ImVec4 color,
        const UI::Theme::UiPalette& palette)
    {
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg, palette.SurfaceAlt);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);

        ImGui::ProgressBar(
            ClampPercentage(percentage) / 100.0f,
            ImVec2(-1.0f, 0.0f),
            "");

        ImGui::PopStyleColor(2);
    }

    void ColorSwatch(
        const char* label,
        ImVec4 color,
        const UI::Theme::UiPalette& palette)
    {
        ImGui::BeginGroup();

        ImGui::PushStyleColor(ImGuiCol_Button, color);
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered, color);
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive, color);

        const std::string buttonId =
            std::string("##swatch_") + label;

        ImGui::Button(
            buttonId.c_str(), ImVec2(48.0f, 22.0f));

        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        Subtitle(label, palette);

        ImGui::EndGroup();
    }
}
