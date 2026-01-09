/**
 * @file BetterPrimitives.h
 * @brief Improved ImGui primitives with better styling defaults
 */

#ifndef RAPTURE__BETTER_PRIMITIVES_H
#define RAPTURE__BETTER_PRIMITIVES_H

#include "imguiPanels/themes/imguiPanelStyle.h"
#include <imgui.h>
#include <string>

namespace BetterUi {

/**
 * @brief Begin a panel with proper background styling
 * @param name Window title
 * @param p_open Optional close button
 * @param flags Additional window flags
 * @return true if panel is open and visible
 */
inline bool BeginPanel(const char *name, bool *p_open = nullptr, ImGuiWindowFlags flags = 0)
{
    constexpr float borderPadding = 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(borderPadding, borderPadding));
    bool isOpen = ImGui::Begin(name, p_open, flags);
    ImGui::PopStyleVar();

    ImVec2 availSize = ImGui::GetContentRegionAvail();
    availSize.y -= 8.0f;

    bool isHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    float borderSize = isHovered ? 2.0f : 0.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ColorPalette::BACKGROUND_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, ColorPalette::BG3);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, borderSize);
    ImGui::BeginChild("##PanelContent", availSize, ImGuiChildFlags_Borders, 0);
    ImGui::PopStyleVar(2);

    return isOpen;
}

/**
 * @brief End a panel
 */
inline void EndPanel()
{
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::End();
}

/**
 * @brief Begin a content area with padding
 * @param paddingX Horizontal padding in pixels
 * @param paddingY Vertical padding in pixels
 */
inline void BeginContent(float paddingX = 10.0f, float paddingY = 10.0f)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(paddingX, paddingY));
    ImGui::BeginChild("##Content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    ImGui::PopStyleVar();
}

/**
 * @brief End a content area
 */
inline void EndContent()
{
    ImGui::EndChild();
}

/**
 * @brief Begin a collapsing header with proper background
 * @param name Header title
 * @param flags Tree node flags
 * @return true if header is expanded
 */
inline bool BeginCollapsingHeader(const char *name, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen)
{
    bool isOpen = ImGui::CollapsingHeader(name, flags);

    if (isOpen) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ColorPalette::BG1);
        ImGui::BeginChild((std::string(name) + "##CollapsingHeader").c_str(), ImVec2(0, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    }

    return isOpen;
}

/**
 * @brief End a collapsing header
 */
inline void EndCollapsingHeader()
{
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace BetterUi

#endif // RAPTURE__BETTER_PRIMITIVES_H
