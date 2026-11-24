#pragma once

#include <cstddef>

#include <imgui.h>

#include "../../Theme/Theme.h"

namespace UI::Widgets
{
    void Title(
        const char* text,
        const UI::Theme::UiPalette& palette);

    void Subtitle(
        const char* text,
        const UI::Theme::UiPalette& palette);

    void SectionTitle(
        const char* title,
        const char* subtitle,
        const UI::Theme::UiPalette& palette);

    void MutedText(
        const char* text,
        const UI::Theme::UiPalette& palette);

    void ValueText(
        const char* text,
        ImVec4 color,
        float scale = 1.0f);

    void BeginCard(
        const char* id,
        float height,
        const UI::Theme::UiPalette& palette);

    void EndCard();

    void StatCard(
        const char* id,
        const char* title,
        const char* value,
        const char* detail,
        ImVec4 color,
        const UI::Theme::UiPalette& palette);

    void Badge(
        const char* text,
        ImVec4 color,
        const UI::Theme::UiPalette& palette);

    void Divider(const UI::Theme::UiPalette& palette);

    bool PrimaryButton(
        const char* label,
        const UI::Theme::UiPalette& palette);

    bool SecondaryButton(
        const char* label,
        const UI::Theme::UiPalette& palette);

    bool GhostButton(
        const char* label,
        const UI::Theme::UiPalette& palette);

    bool ToggleButton(
        const char* label,
        bool active,
        const UI::Theme::UiPalette& palette);

    bool SegmentedControl(
        const char* id,
        const char* const* items,
        int count,
        int& selected,
        const UI::Theme::UiPalette& palette);

    void UsageBar(
        float percentage,
        ImVec4 color,
        const UI::Theme::UiPalette& palette);

    void ColorSwatch(
        const char* label,
        ImVec4 color,
        const UI::Theme::UiPalette& palette);
}
