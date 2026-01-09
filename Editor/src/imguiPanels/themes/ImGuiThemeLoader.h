#ifndef RAPTURE__IMGUI_THEME_LOADER_H
#define RAPTURE__IMGUI_THEME_LOADER_H

#include <cmath>
#include <imgui.h>
#include <string>
#include <toml++/toml.h>

namespace ColorPalette {

struct ThemeMetadata {
    std::string name;
    std::string colorSpace;
};

struct ThemeColors {
    ImVec4 backgroundPrimary;
    ImVec4 backgroundSecondary;
    ImVec4 backgroundTertiary;
    ImVec4 backgroundHard;
    ImVec4 backgroundSoft;
    ImVec4 backgroundPanel;

    ImVec4 bg0, bg1, bg2, bg3, bg4;
    ImVec4 fg1, fg3, fg4;

    ImVec4 textNormal;
    ImVec4 textMuted;
    ImVec4 textFaint;

    ImVec4 accentPrimary;
    ImVec4 accentHover;
    ImVec4 accentSecondary;
    ImVec4 accentTertiary;

    ImVec4 successColor;
    ImVec4 warningColor;
    ImVec4 errorColor;
    ImVec4 infoColor;

    ImVec4 borderColor;
    ImVec4 separatorColor;
    ImVec4 highlightColor;
    ImVec4 button;
    ImVec4 buttonHover;

    ImVec4 selectionBg;
};

struct Theme {
    ThemeMetadata metadata;
    ThemeColors colors;
};

inline ImVec4 srgbToLinear(float r, float g, float b, float a = 1.0f)
{
    auto convert = [](float c) { return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    return {convert(r), convert(g), convert(b), a};
}

inline ImVec4 parseColor(const toml::array &arr, bool convertToLinear)
{
    if (arr.size() >= 3) {
        float r = arr[0].value_or(0.0);
        float g = arr[1].value_or(0.0);
        float b = arr[2].value_or(0.0);

        if (convertToLinear) {
            return srgbToLinear(r, g, b);
        }
        return {r, g, b, 1.0f};
    }
    return {0.0f, 0.0f, 0.0f, 1.0f};
}

inline Theme LoadTheme(const std::string &themePath, bool forceLinear = true)
{
    Theme theme;
    ThemeColors &colors = theme.colors;

    try {
        toml::table config = toml::parse_file(themePath);

        auto metadata = config["metadata"];
        theme.metadata.name = metadata["name"].value_or("Unknown");
        theme.metadata.colorSpace = metadata["color_space"].value_or("srgb");

        bool needsConversion = (theme.metadata.colorSpace == "srgb" && forceLinear);

        auto bg = config["colors"]["background"];
        colors.backgroundPrimary = parseColor(*bg["primary"].as_array(), needsConversion);
        colors.backgroundSecondary = parseColor(*bg["secondary"].as_array(), needsConversion);
        colors.backgroundTertiary = parseColor(*bg["tertiary"].as_array(), needsConversion);
        colors.backgroundHard = parseColor(*bg["hard"].as_array(), needsConversion);
        colors.backgroundSoft = parseColor(*bg["soft"].as_array(), needsConversion);
        colors.backgroundPanel = parseColor(*bg["panel"].as_array(), needsConversion);

        auto bgLevels = config["colors"]["background"]["levels"];
        colors.bg0 = parseColor(*bgLevels["bg0"].as_array(), needsConversion);
        colors.bg1 = parseColor(*bgLevels["bg1"].as_array(), needsConversion);
        colors.bg2 = parseColor(*bgLevels["bg2"].as_array(), needsConversion);
        colors.bg3 = parseColor(*bgLevels["bg3"].as_array(), needsConversion);
        colors.bg4 = parseColor(*bgLevels["bg4"].as_array(), needsConversion);

        auto fg = config["colors"]["foreground"];
        colors.textNormal = parseColor(*fg["normal"].as_array(), needsConversion);
        colors.textMuted = parseColor(*fg["muted"].as_array(), needsConversion);
        colors.textFaint = parseColor(*fg["faint"].as_array(), needsConversion);

        auto fgLevels = config["colors"]["foreground"]["levels"];
        colors.fg1 = parseColor(*fgLevels["fg1"].as_array(), needsConversion);
        colors.fg3 = parseColor(*fgLevels["fg3"].as_array(), needsConversion);
        colors.fg4 = parseColor(*fgLevels["fg4"].as_array(), needsConversion);

        auto accent = config["colors"]["accent"];
        colors.accentPrimary = parseColor(*accent["primary"].as_array(), needsConversion);
        colors.accentHover = parseColor(*accent["hover"].as_array(), needsConversion);
        colors.accentSecondary = parseColor(*accent["secondary"].as_array(), needsConversion);
        colors.accentTertiary = parseColor(*accent["tertiary"].as_array(), needsConversion);

        auto state = config["colors"]["state"];
        colors.successColor = parseColor(*state["success"].as_array(), needsConversion);
        colors.warningColor = parseColor(*state["warning"].as_array(), needsConversion);
        colors.errorColor = parseColor(*state["error"].as_array(), needsConversion);
        colors.infoColor = parseColor(*state["info"].as_array(), needsConversion);

        auto ui = config["colors"]["ui"];
        colors.borderColor = parseColor(*ui["border"].as_array(), needsConversion);
        colors.separatorColor = parseColor(*ui["separator"].as_array(), needsConversion);
        colors.highlightColor = parseColor(*ui["highlight"].as_array(), needsConversion);
        colors.button = parseColor(*ui["button"].as_array(), needsConversion);
        colors.buttonHover = parseColor(*ui["button_hover"].as_array(), needsConversion);

        auto bg4 = colors.bg4;
        colors.selectionBg = {bg4.x, bg4.y, bg4.z, 0.5f};

    } catch (const toml::parse_error &err) {
        theme.metadata = {};
        colors = {};
    }

    return theme;
}

} // namespace ColorPalette

#endif
