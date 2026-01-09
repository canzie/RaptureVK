#ifndef RAPTURE__IMGUI_PANEL_STYLE_H
#define RAPTURE__IMGUI_PANEL_STYLE_H

#include "ImGuiThemeLoader.h"
#include "imguiPanels/IconsMaterialDesign.h"
#include <imgui.h>
#include <string>

namespace ColorPalette {

inline ColorPalette::Theme g_currentTheme;

// Color references
#define COLORS g_currentTheme.colors

inline ImVec4 &BACKGROUND_PRIMARY = COLORS.backgroundPrimary;
inline ImVec4 &BACKGROUND_SECONDARY = COLORS.backgroundSecondary;
inline ImVec4 &BACKGROUND_TERTIARY = COLORS.backgroundTertiary;
inline ImVec4 &BACKGROUND_HARD = COLORS.backgroundHard;
inline ImVec4 &BACKGROUND_SOFT = COLORS.backgroundSoft;
inline ImVec4 &BACKGROUND_PANEL = COLORS.bg1;

inline ImVec4 &BG0 = COLORS.bg0;
inline ImVec4 &BG1 = COLORS.bg1;
inline ImVec4 &BG2 = COLORS.bg2;
inline ImVec4 &BG3 = COLORS.bg3;
inline ImVec4 &BG4 = COLORS.bg4;

inline ImVec4 &FG1 = COLORS.fg1;
inline ImVec4 &FG3 = COLORS.fg3;
inline ImVec4 &FG4 = COLORS.fg4;

inline ImVec4 &TEXT_NORMAL = COLORS.textNormal;
inline ImVec4 &TEXT_MUTED = COLORS.textMuted;
inline ImVec4 &TEXT_FAINT = COLORS.textFaint;

inline ImVec4 &ACCENT_PRIMARY = COLORS.accentPrimary;
inline ImVec4 &ACCENT_HOVER = COLORS.accentHover;
inline ImVec4 &ACCENT_SECONDARY = COLORS.accentSecondary;
inline ImVec4 &ACCENT_TERTIARY = COLORS.accentTertiary;

inline ImVec4 &SUCCESS_COLOR = COLORS.successColor;
inline ImVec4 &WARNING_COLOR = COLORS.warningColor;
inline ImVec4 &ERROR_COLOR = COLORS.errorColor;
inline ImVec4 &INFO_COLOR = COLORS.infoColor;

inline ImVec4 &BORDER_COLOR = COLORS.borderColor;
inline ImVec4 &SEPARATOR_COLOR = COLORS.separatorColor;
inline ImVec4 &HIGHLIGHT_COLOR = COLORS.highlightColor;
inline ImVec4 &BUTTON_COLOR = COLORS.button;
inline ImVec4 &BUTTON_HOVER = COLORS.buttonHover;
inline ImVec4 &SELECTION_BG = COLORS.selectionBg;

#undef COLORS

inline void ApplyTheme(const ThemeColors &colors)
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *c = style.Colors;

    c[ImGuiCol_WindowBg] = colors.backgroundHard;
    c[ImGuiCol_ChildBg] = colors.backgroundSoft;
    c[ImGuiCol_PopupBg] = colors.bg1;
    c[ImGuiCol_Border] = colors.backgroundHard;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = colors.bg1;
    c[ImGuiCol_FrameBgHovered] = colors.bg2;
    c[ImGuiCol_FrameBgActive] = colors.bg3;

    c[ImGuiCol_Text] = colors.textNormal;
    c[ImGuiCol_TextDisabled] = colors.textFaint;
    c[ImGuiCol_TextSelectedBg] = colors.selectionBg;

    c[ImGuiCol_Header] = colors.bg2;
    c[ImGuiCol_HeaderHovered] = colors.bg3;
    c[ImGuiCol_HeaderActive] = colors.accentPrimary;

    c[ImGuiCol_Button] = colors.button;
    c[ImGuiCol_ButtonHovered] = colors.buttonHover;
    c[ImGuiCol_ButtonActive] = colors.accentPrimary;

    c[ImGuiCol_Tab] = colors.backgroundHard;
    c[ImGuiCol_TabHovered] = colors.bg1;
    c[ImGuiCol_TabActive] = colors.bg1;
    c[ImGuiCol_TabUnfocused] = colors.backgroundHard;
    c[ImGuiCol_TabUnfocusedActive] = colors.bg1;

    c[ImGuiCol_TitleBg] = colors.backgroundHard;
    c[ImGuiCol_TitleBgActive] = colors.backgroundHard;
    c[ImGuiCol_TitleBgCollapsed] = colors.backgroundHard;

    c[ImGuiCol_ScrollbarBg] = colors.backgroundSoft;
    c[ImGuiCol_ScrollbarGrab] = colors.bg3;
    c[ImGuiCol_ScrollbarGrabHovered] = colors.bg4;
    c[ImGuiCol_ScrollbarGrabActive] = colors.fg4;

    c[ImGuiCol_CheckMark] = colors.successColor;

    c[ImGuiCol_SliderGrab] = colors.fg3;
    c[ImGuiCol_SliderGrabActive] = colors.fg1;

    c[ImGuiCol_ResizeGrip] = ImVec4(colors.button.x, colors.button.y, colors.button.z, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(colors.accentPrimary.x, colors.accentPrimary.y, colors.accentPrimary.z, 0.5f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(colors.accentPrimary.x, colors.accentPrimary.y, colors.accentPrimary.z, 0.9f);

    c[ImGuiCol_Separator] = colors.separatorColor;
    c[ImGuiCol_SeparatorHovered] = colors.bg4;
    c[ImGuiCol_SeparatorActive] = colors.accentPrimary;

    c[ImGuiCol_PlotLines] = colors.accentSecondary;
    c[ImGuiCol_PlotLinesHovered] = colors.accentHover;
    c[ImGuiCol_PlotHistogram] = colors.accentTertiary;
    c[ImGuiCol_PlotHistogramHovered] = colors.highlightColor;

    c[ImGuiCol_TableHeaderBg] = colors.bg1;
    c[ImGuiCol_TableBorderStrong] = colors.bg3;
    c[ImGuiCol_TableBorderLight] = colors.bg2;
    c[ImGuiCol_TableRowBg] = colors.backgroundPrimary;
    c[ImGuiCol_TableRowBgAlt] = colors.backgroundSecondary;

    c[ImGuiCol_DockingPreview] = ImVec4(colors.accentPrimary.x, colors.accentPrimary.y, colors.accentPrimary.z, 0.7f);
    c[ImGuiCol_DockingEmptyBg] = colors.backgroundHard;

    c[ImGuiCol_MenuBarBg] = colors.bg1;

    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(10, 8);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 8.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 0.0f;
}

inline bool SetTheme(const std::string &themePath, bool forceLinear = true)
{
    g_currentTheme = LoadTheme(themePath, forceLinear);

    if (g_currentTheme.metadata.name.empty()) {
        return false;
    }

    ApplyTheme(g_currentTheme.colors);
    return true;
}

inline const std::string &GetThemeName()
{
    return g_currentTheme.metadata.name;
}

inline ImFont *g_regularFont = nullptr;
inline ImFont *g_boldFont = nullptr;
inline ImFont *g_lightFont = nullptr;
inline ImFont *g_italicFont = nullptr;

inline void InitializeFonts(const std::string &rootPath)
{
    ImGuiIO &io = ImGui::GetIO();

    std::string regularPath = rootPath + "/assets/fonts/IBMPlexMono-Regular.ttf";
    std::string boldPath = rootPath + "/assets/fonts/IBMPlexMono-Bold.ttf";
    std::string lightPath = rootPath + "/assets/fonts/IBMPlexMono-Light.ttf";
    std::string italicPath = rootPath + "/assets/fonts/IBMPlexMono-Italic.ttf";
    std::string iconPath = rootPath + "/assets/fonts/" FONT_ICON_FILE_NAME_MD;

    g_regularFont = io.Fonts->AddFontFromFileTTF(regularPath.c_str(), 16.0f);

    static const ImWchar iconRanges[] = {ICON_MIN_MD, ICON_MAX_16_MD, 0};
    ImFontConfig iconConfig;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    iconConfig.GlyphOffset.y = 3.0f;
    io.Fonts->AddFontFromFileTTF(iconPath.c_str(), 16.0f, &iconConfig, iconRanges);

    g_boldFont = io.Fonts->AddFontFromFileTTF(boldPath.c_str(), 16.0f);
    g_lightFont = io.Fonts->AddFontFromFileTTF(lightPath.c_str(), 16.0f);
    g_italicFont = io.Fonts->AddFontFromFileTTF(italicPath.c_str(), 16.0f);

    if (g_regularFont) {
        io.FontDefault = g_regularFont;
    }
}

inline ImFont *GetRegularFont()
{
    return g_regularFont;
}
inline ImFont *GetBoldFont()
{
    return g_boldFont;
}
inline ImFont *GetLightFont()
{
    return g_lightFont;
}
inline ImFont *GetItalicFont()
{
    return g_italicFont;
}

} // namespace ColorPalette

#endif
