#pragma once

#include "imgui.h"
#include "imguiPanels/IconsMaterialDesign.h"
#include <cmath> // For std::pow
#include <string>
#include <unordered_map>

namespace ImGuiPanelStyle {

// --- Helper function for sRGB to Linear conversion ---
// This function converts a single color component from sRGB (0.0-1.0) to linear (0.0-1.0)
// This is the core of the solution to correct the "super bright" issue.
inline float srgb_to_linear_component(float c)
{
    if (c <= 0.04045f) {
        return c / 12.92f;
    } else {
        return std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
}

// This helper converts an ImVec4 (assuming RGB components are sRGB) to linear.
// Alpha is typically already linear and does not need conversion.
inline ImVec4 srgb_to_linear(const ImVec4 &srgb_color)
{
    return ImVec4(srgb_to_linear_component(srgb_color.x), srgb_to_linear_component(srgb_color.y),
                  srgb_to_linear_component(srgb_color.z),
                  srgb_color.w // Alpha usually remains unchanged (linear)
    );
}
// --- End Helper functions ---

// Font pointers
static ImFont *s_RegularFont = nullptr;
static ImFont *s_BoldFont = nullptr;
static ImFont *s_LightFont = nullptr;
static ImFont *s_ItalicFont = nullptr;

// Gruvbox Dark Color Palette (Original sRGB values - these will be converted below)
// Reference: https://github.com/morhetz/gruvbox

// Declare original sRGB values first. These are conceptually what you'd pick from a color picker.
// We'll then use these to define the LINEAR ImVec4 constants.
namespace OriginalGruvboxSRGB {
// Backgrounds
static const ImVec4 BG0_HARD_SRGB = ImVec4(0.114f, 0.125f, 0.129f, 1.00f); // #1d2021
static const ImVec4 BG0_SRGB = ImVec4(0.157f, 0.157f, 0.157f, 1.00f);      // #282828
static const ImVec4 BG0_SOFT_SRGB = ImVec4(0.196f, 0.188f, 0.184f, 1.00f); // #32302f
static const ImVec4 BG1_SRGB = ImVec4(0.235f, 0.220f, 0.212f, 1.00f);      // #3c3836
static const ImVec4 BG2_SRGB = ImVec4(0.314f, 0.286f, 0.271f, 1.00f);      // #504945
static const ImVec4 BG3_SRGB = ImVec4(0.400f, 0.361f, 0.329f, 1.00f);      // #665c54
static const ImVec4 BG4_SRGB = ImVec4(0.486f, 0.435f, 0.392f, 1.00f);      // #7c6f64

// Foregrounds
static const ImVec4 FG0_SRGB = ImVec4(0.984f, 0.945f, 0.843f, 1.00f); // #fbf1c7
static const ImVec4 FG1_SRGB = ImVec4(0.922f, 0.859f, 0.698f, 1.00f); // #ebdbb2 (Primary Text)
static const ImVec4 FG2_SRGB = ImVec4(0.835f, 0.769f, 0.631f, 1.00f); // #d5c4a1 (Secondary Text)
static const ImVec4 FG3_SRGB = ImVec4(0.741f, 0.682f, 0.576f, 1.00f); // #bdae93 (Muted Text)
static const ImVec4 FG4_SRGB = ImVec4(0.659f, 0.600f, 0.518f, 1.00f); // #a89984 (Faint/Disabled Text)

// Accent Colors (Normal)
static const ImVec4 RED_NORMAL_SRGB = ImVec4(0.800f, 0.141f, 0.114f, 1.00f);    // #cc241d
static const ImVec4 GREEN_NORMAL_SRGB = ImVec4(0.596f, 0.592f, 0.102f, 1.00f);  // #98971a
static const ImVec4 YELLOW_NORMAL_SRGB = ImVec4(0.843f, 0.600f, 0.129f, 1.00f); // #d79921
static const ImVec4 BLUE_NORMAL_SRGB = ImVec4(0.271f, 0.522f, 0.533f, 1.00f);   // #458588
static const ImVec4 PURPLE_NORMAL_SRGB = ImVec4(0.694f, 0.384f, 0.525f, 1.00f); // #b16286
static const ImVec4 AQUA_NORMAL_SRGB = ImVec4(0.408f, 0.616f, 0.416f, 1.00f);   // #689d6a
static const ImVec4 ORANGE_NORMAL_SRGB = ImVec4(0.839f, 0.365f, 0.055f, 1.00f); // #d65d0e

// Accent Colors (Bright)
static const ImVec4 RED_BRIGHT_SRGB = ImVec4(0.984f, 0.286f, 0.204f, 1.00f);    // #fb4934
static const ImVec4 GREEN_BRIGHT_SRGB = ImVec4(0.722f, 0.733f, 0.149f, 1.00f);  // #b8bb26
static const ImVec4 YELLOW_BRIGHT_SRGB = ImVec4(0.980f, 0.741f, 0.184f, 1.00f); // #fabd2f
static const ImVec4 BLUE_BRIGHT_SRGB = ImVec4(0.514f, 0.647f, 0.596f, 1.00f);   // #83a598
static const ImVec4 PURPLE_BRIGHT_SRGB = ImVec4(0.827f, 0.525f, 0.608f, 1.00f); // #d3869b
static const ImVec4 AQUA_BRIGHT_SRGB = ImVec4(0.557f, 0.753f, 0.486f, 1.00f);   // #8ec07c
static const ImVec4 ORANGE_BRIGHT_SRGB = ImVec4(0.996f, 0.502f, 0.098f, 1.00f); // #fe8019

// Grays
static const ImVec4 GRAY_SRGB = ImVec4(0.573f, 0.514f, 0.455f, 1.00f); // #928374
} // namespace OriginalGruvboxSRGB

// Now, define the public facing LINEAR ImVec4 constants by converting from the sRGB values
// Backgrounds
inline const ImVec4 GRUVBOX_BG0_HARD = srgb_to_linear(OriginalGruvboxSRGB::BG0_HARD_SRGB);
inline const ImVec4 GRUVBOX_BG0 = srgb_to_linear(OriginalGruvboxSRGB::BG0_SRGB);
inline const ImVec4 GRUVBOX_BG0_SOFT = srgb_to_linear(OriginalGruvboxSRGB::BG0_SOFT_SRGB);
inline const ImVec4 GRUVBOX_BG1 = srgb_to_linear(OriginalGruvboxSRGB::BG1_SRGB);
inline const ImVec4 GRUVBOX_BG2 = srgb_to_linear(OriginalGruvboxSRGB::BG2_SRGB);
inline const ImVec4 GRUVBOX_BG3 = srgb_to_linear(OriginalGruvboxSRGB::BG3_SRGB);
inline const ImVec4 GRUVBOX_BG4 = srgb_to_linear(OriginalGruvboxSRGB::BG4_SRGB);

// Foregrounds
inline const ImVec4 GRUVBOX_FG0 = srgb_to_linear(OriginalGruvboxSRGB::FG0_SRGB);
inline const ImVec4 GRUVBOX_FG1 = srgb_to_linear(OriginalGruvboxSRGB::FG1_SRGB);
inline const ImVec4 GRUVBOX_FG2 = srgb_to_linear(OriginalGruvboxSRGB::FG2_SRGB);
inline const ImVec4 GRUVBOX_FG3 = srgb_to_linear(OriginalGruvboxSRGB::FG3_SRGB);
inline const ImVec4 GRUVBOX_FG4 = srgb_to_linear(OriginalGruvboxSRGB::FG4_SRGB);

// Accent Colors (Normal)
inline const ImVec4 GRUVBOX_RED_NORMAL = srgb_to_linear(OriginalGruvboxSRGB::RED_NORMAL_SRGB);
inline const ImVec4 GRUVBOX_GREEN_NORMAL = srgb_to_linear(OriginalGruvboxSRGB::GREEN_NORMAL_SRGB);
inline const ImVec4 GRUVBOX_YELLOW_NORMAL = srgb_to_linear(OriginalGruvboxSRGB::YELLOW_NORMAL_SRGB);
inline const ImVec4 GRUVBOX_BLUE_NORMAL = srgb_to_linear(OriginalGruvboxSRGB::BLUE_NORMAL_SRGB);
inline const ImVec4 GRUVBOX_PURPLE_NORMAL = srgb_to_linear(OriginalGruvboxSRGB::PURPLE_NORMAL_SRGB);
inline const ImVec4 GRUVBOX_AQUA_NORMAL = srgb_to_linear(OriginalGruvboxSRGB::AQUA_NORMAL_SRGB);
inline const ImVec4 GRUVBOX_ORANGE_NORMAL = srgb_to_linear(OriginalGruvboxSRGB::ORANGE_NORMAL_SRGB);

// Accent Colors (Bright)
inline const ImVec4 GRUVBOX_RED_BRIGHT = srgb_to_linear(OriginalGruvboxSRGB::RED_BRIGHT_SRGB);
inline const ImVec4 GRUVBOX_GREEN_BRIGHT = srgb_to_linear(OriginalGruvboxSRGB::GREEN_BRIGHT_SRGB);
inline const ImVec4 GRUVBOX_YELLOW_BRIGHT = srgb_to_linear(OriginalGruvboxSRGB::YELLOW_BRIGHT_SRGB);
inline const ImVec4 GRUVBOX_BLUE_BRIGHT = srgb_to_linear(OriginalGruvboxSRGB::BLUE_BRIGHT_SRGB);
inline const ImVec4 GRUVBOX_PURPLE_BRIGHT = srgb_to_linear(OriginalGruvboxSRGB::PURPLE_BRIGHT_SRGB);
inline const ImVec4 GRUVBOX_AQUA_BRIGHT = srgb_to_linear(OriginalGruvboxSRGB::AQUA_BRIGHT_SRGB);
inline const ImVec4 GRUVBOX_ORANGE_BRIGHT = srgb_to_linear(OriginalGruvboxSRGB::ORANGE_BRIGHT_SRGB);

// Grays
inline const ImVec4 GRUVBOX_GRAY = srgb_to_linear(OriginalGruvboxSRGB::GRAY_SRGB);

// UI Specific mapping using Gruvbox names (these now refer to the LINEAR versions)
inline const ImVec4 BACKGROUND_PRIMARY = GRUVBOX_BG0;
inline const ImVec4 BACKGROUND_SECONDARY = GRUVBOX_BG1;     // For sidebars, popups
inline const ImVec4 BACKGROUND_TERTIARY = GRUVBOX_BG0_SOFT; // For input fields, panel backgrounds slightly different from main

inline const ImVec4 TEXT_NORMAL = GRUVBOX_FG1;
inline const ImVec4 TEXT_MUTED = GRUVBOX_FG3;
inline const ImVec4 TEXT_FAINT = GRUVBOX_FG4;

// Accent colors (choose primary, secondary, tertiary from Gruvbox palette)
inline const ImVec4 ACCENT_PRIMARY = GRUVBOX_BLUE_NORMAL;    // Main accent
inline const ImVec4 ACCENT_HOVER = GRUVBOX_BLUE_BRIGHT;      // Lighter version for hover
inline const ImVec4 ACCENT_SECONDARY = GRUVBOX_AQUA_NORMAL;  // Secondary accent
inline const ImVec4 ACCENT_TERTIARY = GRUVBOX_YELLOW_NORMAL; // Tertiary accent

// State colors
inline const ImVec4 SUCCESS_COLOR = GRUVBOX_GREEN_NORMAL;
inline const ImVec4 WARNING_COLOR = GRUVBOX_ORANGE_NORMAL;
inline const ImVec4 ERROR_COLOR = GRUVBOX_RED_NORMAL;
inline const ImVec4 INFO_COLOR = GRUVBOX_BLUE_NORMAL;

// UI element specific colors
inline const ImVec4 BORDER_COLOR = GRUVBOX_BG3;
inline const ImVec4 SEPARATOR_COLOR = GRUVBOX_BG2;
inline const ImVec4 HIGHLIGHT_COLOR = GRUVBOX_YELLOW_NORMAL; // For highlighted items
// For selection background, apply alpha directly to the linear BG4 color
inline const ImVec4 SELECTION_BG_COLOR = ImVec4(GRUVBOX_BG4.x, GRUVBOX_BG4.y, GRUVBOX_BG4.z, 0.5f);

// Named style mapping
inline const std::unordered_map<std::string, ImVec4> NamedColors = {
    {"background_primary", BACKGROUND_PRIMARY},
    {"background_secondary", BACKGROUND_SECONDARY},
    {"background_tertiary", BACKGROUND_TERTIARY},
    {"text_normal", TEXT_NORMAL},
    {"text_muted", TEXT_MUTED},
    {"text_faint", TEXT_FAINT},
    {"accent_primary", ACCENT_PRIMARY},
    {"accent_hover", ACCENT_HOVER},
    {"accent_secondary", ACCENT_SECONDARY},
    {"accent_tertiary", ACCENT_TERTIARY},
    {"success", SUCCESS_COLOR},
    {"warning", WARNING_COLOR},
    {"error", ERROR_COLOR},
    {"info", INFO_COLOR},
    {"border", BORDER_COLOR},
    {"separator", SEPARATOR_COLOR},
    {"highlight", HIGHLIGHT_COLOR},
    {"selection_bg", SELECTION_BG_COLOR},

    // Raw Gruvbox colors for more granular access if needed (these are now linear)
    {"gruvbox_bg0_hard", GRUVBOX_BG0_HARD},
    {"gruvbox_bg0", GRUVBOX_BG0},
    {"gruvbox_bg0_soft", GRUVBOX_BG0_SOFT},
    {"gruvbox_bg1", GRUVBOX_BG1},
    {"gruvbox_bg2", GRUVBOX_BG2},
    {"gruvbox_bg3", GRUVBOX_BG3},
    {"gruvbox_bg4", GRUVBOX_BG4},
    {"gruvbox_fg0", GRUVBOX_FG0},
    {"gruvbox_fg1", GRUVBOX_FG1},
    {"gruvbox_fg2", GRUVBOX_FG2},
    {"gruvbox_fg3", GRUVBOX_FG3},
    {"gruvbox_fg4", GRUVBOX_FG4},
    {"gruvbox_red_normal", GRUVBOX_RED_NORMAL},
    {"gruvbox_green_normal", GRUVBOX_GREEN_NORMAL},
    {"gruvbox_yellow_normal", GRUVBOX_YELLOW_NORMAL},
    {"gruvbox_blue_normal", GRUVBOX_BLUE_NORMAL},
    {"gruvbox_purple_normal", GRUVBOX_PURPLE_NORMAL},
    {"gruvbox_aqua_normal", GRUVBOX_AQUA_NORMAL},
    {"gruvbox_orange_normal", GRUVBOX_ORANGE_NORMAL},
    {"gruvbox_red_bright", GRUVBOX_RED_BRIGHT},
    {"gruvbox_green_bright", GRUVBOX_GREEN_BRIGHT},
    {"gruvbox_yellow_bright", GRUVBOX_YELLOW_BRIGHT},
    {"gruvbox_blue_bright", GRUVBOX_BLUE_BRIGHT},
    {"gruvbox_purple_bright", GRUVBOX_PURPLE_BRIGHT},
    {"gruvbox_aqua_bright", GRUVBOX_AQUA_BRIGHT},
    {"gruvbox_orange_bright", GRUVBOX_ORANGE_BRIGHT},
    {"gruvbox_gray", GRUVBOX_GRAY}};

static bool s_StyleInitialized = false;

inline void InitializeFonts(const std::string &rootPath)
{
    ImGuiIO &io = ImGui::GetIO();

    std::string regularFontPath = rootPath + "/assets/fonts/IBMPlexMono-Regular.ttf";
    std::string boldFontPath = rootPath + "/assets/fonts/IBMPlexMono-Bold.ttf";
    std::string lightFontPath = rootPath + "/assets/fonts/IBMPlexMono-Light.ttf";
    std::string italicFontPath = rootPath + "/assets/fonts/IBMPlexMono-Italic.ttf";
    std::string iconFontPath = rootPath + "/assets/fonts/" + FONT_ICON_FILE_NAME_MD;

    s_RegularFont = io.Fonts->AddFontFromFileTTF(regularFontPath.c_str(), 16.0f);

    // Add this line to merge icons into the default font
    // The static keyword is important here to ensure the config persists through the font building process
    static const ImWchar icons_ranges[] = {(ImWchar)ICON_MIN_MD, (ImWchar)ICON_MAX_MD, 0};
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphOffset.y = 3.0f; // Vertically align icons with text

    io.Fonts->AddFontFromFileTTF(iconFontPath.c_str(), 16.0f, &icons_config, icons_ranges);

    s_BoldFont = io.Fonts->AddFontFromFileTTF(boldFontPath.c_str(), 16.0f);
    s_LightFont = io.Fonts->AddFontFromFileTTF(lightFontPath.c_str(), 16.0f);
    s_ItalicFont = io.Fonts->AddFontFromFileTTF(italicFontPath.c_str(), 16.0f);

    if (s_RegularFont) {
        io.FontDefault = s_RegularFont;
    }
}

inline void ApplyStyle()
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    // All these colors are now defined as LINEAR values, so they will be
    // correctly gamma-encoded by the VK_FORMAT_B8G8R8A8_SRGB swapchain.

    // Main
    colors[ImGuiCol_WindowBg] = BACKGROUND_PRIMARY;                     // Typically GRUVBOX_BG0
    colors[ImGuiCol_ChildBg] = GRUVBOX_BG0_SOFT;                        // Can be BG0 or BG0_SOFT for slight differentiation
    colors[ImGuiCol_PopupBg] = GRUVBOX_BG1;                             // Popups, tooltips
    colors[ImGuiCol_Border] = BORDER_COLOR;                             // GRUVBOX_BG3 or GRUVBOX_BG1
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f); // Usually transparent
    colors[ImGuiCol_FrameBg] = GRUVBOX_BG1;                             // Background of checkboxes, radio buttons, input fields
    colors[ImGuiCol_FrameBgHovered] = GRUVBOX_BG2;
    colors[ImGuiCol_FrameBgActive] = GRUVBOX_BG3;

    // Text
    colors[ImGuiCol_Text] = TEXT_NORMAL;                  // GRUVBOX_FG1
    colors[ImGuiCol_TextDisabled] = TEXT_FAINT;           // GRUVBOX_FG4
    colors[ImGuiCol_TextSelectedBg] = SELECTION_BG_COLOR; // GRUVBOX_BG4 with alpha

    // Headers (CollapsingHeader, TreeNode, Selectable, etc.)
    colors[ImGuiCol_Header] = GRUVBOX_BG2; // Background of selectable items
    colors[ImGuiCol_HeaderHovered] = GRUVBOX_BG3;
    colors[ImGuiCol_HeaderActive] = ACCENT_PRIMARY; // Or GRUVBOX_BG4

    // Buttons
    colors[ImGuiCol_Button] = GRUVBOX_GRAY;         // A neutral button color
    colors[ImGuiCol_ButtonHovered] = GRUVBOX_FG4;   // Brighter gray or a subtle accent
    colors[ImGuiCol_ButtonActive] = ACCENT_PRIMARY; // Accent color when pressed

    // Tabs
    colors[ImGuiCol_Tab] = GRUVBOX_BG1; // Inactive tab background
    colors[ImGuiCol_TabHovered] = GRUVBOX_BG2;
    colors[ImGuiCol_TabActive] = ACCENT_PRIMARY; // Active tab background
    colors[ImGuiCol_TabUnfocused] = GRUVBOX_BG0_SOFT;
    colors[ImGuiCol_TabUnfocusedActive] = GRUVBOX_BG1; // Active tab in unfocused window

    // Title (Window title bar)
    colors[ImGuiCol_TitleBg] = GRUVBOX_BG0_HARD;          // Title bar of unfocused window
    colors[ImGuiCol_TitleBgActive] = GRUVBOX_BG0_HARD;    // Title bar of focused window (can be an accent too)
    colors[ImGuiCol_TitleBgCollapsed] = GRUVBOX_BG0_HARD; // Title bar of collapsed window

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = GRUVBOX_BG0_SOFT; // Background of the scrollbar slot
    colors[ImGuiCol_ScrollbarGrab] = GRUVBOX_BG3;
    colors[ImGuiCol_ScrollbarGrabHovered] = GRUVBOX_BG4;
    colors[ImGuiCol_ScrollbarGrabActive] = GRUVBOX_FG4; // Or a brighter gray

    // CheckMark, RadioButton
    colors[ImGuiCol_CheckMark] = GRUVBOX_GREEN_BRIGHT;

    // Slider
    colors[ImGuiCol_SliderGrab] = GRUVBOX_FG3;
    colors[ImGuiCol_SliderGrabActive] = GRUVBOX_FG1; // Brighter grab when active

    // Other interactive elements
    colors[ImGuiCol_ResizeGrip] = ImVec4(GRUVBOX_GRAY.x, GRUVBOX_GRAY.y, GRUVBOX_GRAY.z, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(GRUVBOX_GRAY.x, GRUVBOX_GRAY.y, GRUVBOX_GRAY.z, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(GRUVBOX_GRAY.x, GRUVBOX_GRAY.y, GRUVBOX_GRAY.z, 0.95f);

    // Separator
    colors[ImGuiCol_Separator] = SEPARATOR_COLOR; // GRUVBOX_BG2 or BG3
    colors[ImGuiCol_SeparatorHovered] = GRUVBOX_BG4;
    colors[ImGuiCol_SeparatorActive] = ACCENT_PRIMARY;

    // Plot lines, histograms
    colors[ImGuiCol_PlotLines] = GRUVBOX_AQUA_NORMAL;
    colors[ImGuiCol_PlotLinesHovered] = GRUVBOX_AQUA_BRIGHT;
    colors[ImGuiCol_PlotHistogram] = GRUVBOX_YELLOW_NORMAL;
    colors[ImGuiCol_PlotHistogramHovered] = GRUVBOX_YELLOW_BRIGHT;

    // Table
    colors[ImGuiCol_TableHeaderBg] = GRUVBOX_BG1;
    colors[ImGuiCol_TableBorderStrong] = GRUVBOX_BG3; // Outer border
    colors[ImGuiCol_TableBorderLight] = GRUVBOX_BG2;  // Row/column lines
    colors[ImGuiCol_TableRowBg] = BACKGROUND_PRIMARY;
    colors[ImGuiCol_TableRowBgAlt] = BACKGROUND_SECONDARY;

    // Docking
    colors[ImGuiCol_DockingPreview] = ImVec4(ACCENT_PRIMARY.x, ACCENT_PRIMARY.y, ACCENT_PRIMARY.z, 0.7f);
    colors[ImGuiCol_DockingEmptyBg] = GRUVBOX_BG0_HARD;

    // Menubar
    colors[ImGuiCol_MenuBarBg] = GRUVBOX_BG1;

    // Styles (these are your existing values, review if they fit Gruvbox's typically less rounded style)
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 8.0f; // Increased slightly

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f; // Gruvbox often has no frame borders, or very subtle ones
    style.TabBorderSize = 0.0f;

    style.WindowRounding = 4.0f; // Gruvbox tends to be sharper, less rounded
    style.ChildRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;

    // Anti-aliasing (enable for sharper text, important for pixel art like fonts sometimes used with Gruvbox)
    // style.AntiAliasedLines = true; // This is default true
    // style.AntiAliasedFill = true;  // This is default true
}

inline void InitializeStyle()
{
    if (!s_StyleInitialized) {
        // InitializeFonts(); // Call this from your main application setup, after ImGui::CreateContext() but before first
        // NewFrame()
        ApplyStyle();
        s_StyleInitialized = true;
    }
}

inline ImFont *GetRegularFont()
{
    return s_RegularFont;
}
inline ImFont *GetBoldFont()
{
    return s_BoldFont;
}
inline ImFont *GetLightFont()
{
    return s_LightFont;
}
inline ImFont *GetItalicFont()
{
    return s_ItalicFont;
}

inline ImVec4 GetColor(const std::string &colorName)
{
    auto it = NamedColors.find(colorName);
    if (it != NamedColors.end()) {
        return it->second;
    }
    // Return a default color (e.g., primary text) if not found
    return TEXT_NORMAL;
}
} // namespace ImGuiPanelStyle