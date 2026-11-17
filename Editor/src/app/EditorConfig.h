#ifndef RAPTURE__EDITOR_CONFIG_H
#define RAPTURE__EDITOR_CONFIG_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

/**
 * @brief What a config entry holds
 */
enum ConfigValueType {
    CONFIG_VALUE_FLOAT,
    CONFIG_VALUE_INT,
    CONFIG_VALUE_BOOL,
    CONFIG_VALUE_COUNT
};

/**
 * @brief How a number changes when the UI scale changes
 */
enum ScalingRule {
    SCALING_NONE,  ///< used as authored, for ratios, counts and hairlines
    SCALING_ROUND, ///< scaled, then rounded to a whole pixel
    SCALING_SNAP,  ///< scaled, then snapped to the nearest step an SVG renders cleanly at
    SCALING_COUNT
};

/**
 * @brief The steps SCALING_SNAP lands on
 */
constexpr std::array<float, 7> SNAP_STEPS = {12.0f, 14.0f, 16.0f, 20.0f, 24.0f, 32.0f, 48.0f};

using ConfigValue = std::variant<double, int64_t, bool>;

enum EditorConfigKey {
    EDITOR_CONFIG_UI_SCALE,

    EDITOR_CONFIG_MENU_BAR_HEIGHT,
    EDITOR_CONFIG_WORKSPACE_TAB_HEIGHT,
    EDITOR_CONFIG_WORKSPACE_TAB_WIDTH,
    EDITOR_CONFIG_BOTTOM_BAR_HEIGHT,

    EDITOR_CONFIG_HOTBAR_HEIGHT,
    EDITOR_CONFIG_HOTBAR_PADDING,
    EDITOR_CONFIG_HOTBAR_SEPARATOR_WIDTH,

    EDITOR_CONFIG_DOCK_OUTER_SPACING,
    EDITOR_CONFIG_DOCK_INNER_SPACING,

    EDITOR_CONFIG_TAB_ICON_SIZE,
    EDITOR_CONFIG_TAB_LABEL_PADDING,
    EDITOR_CONFIG_TAB_ICON_GAP,

    EDITOR_CONFIG_COMMAND_SOCKET,

    EDITOR_CONFIG_COUNT
};

/**
 * @brief The declaration of one config entry: what it is called, what it holds, what it falls back
 * to and how it answers to the UI scale.
 */
struct EditorConfigEntry {
    std::string_view key;
    ConfigValueType type;
    ConfigValue fallback;
    ScalingRule scaling;
};

constexpr std::array<EditorConfigEntry, EDITOR_CONFIG_COUNT> EDITOR_CONFIG_TABLE = {{
    {"uiScale", CONFIG_VALUE_FLOAT, 1.0, SCALING_NONE},

    {"menuBarHeight", CONFIG_VALUE_FLOAT, 28.0, SCALING_ROUND},
    {"workspaceTabHeight", CONFIG_VALUE_FLOAT, 32.0, SCALING_ROUND},
    {"workspaceTabWidth", CONFIG_VALUE_FLOAT, 196.0, SCALING_ROUND},
    {"bottomBarHeight", CONFIG_VALUE_FLOAT, 28.0, SCALING_ROUND},

    {"hotbarHeight", CONFIG_VALUE_FLOAT, 40.0, SCALING_ROUND},
    {"hotbarPadding", CONFIG_VALUE_FLOAT, 8.0, SCALING_ROUND},
    {"hotbarSeparatorWidth", CONFIG_VALUE_FLOAT, 1.0, SCALING_NONE},

    {"dockOuterSpacing", CONFIG_VALUE_FLOAT, 4.0, SCALING_ROUND},
    {"dockInnerSpacing", CONFIG_VALUE_FLOAT, 3.0, SCALING_ROUND},

    {"tabIconSize", CONFIG_VALUE_FLOAT, 16.0, SCALING_SNAP},
    {"tabLabelPadding", CONFIG_VALUE_FLOAT, 8.0, SCALING_ROUND},
    {"tabIconGap", CONFIG_VALUE_FLOAT, 6.0, SCALING_ROUND},

    {"commandSocket", CONFIG_VALUE_BOOL, true, SCALING_NONE},
}};

/**
 * @brief The editor's own settings, which live beside the executable because they outlive any project.
 *
 * Readers ask for a key and get the value the current UI scale calls for, so no call site multiplies
 * anything or knows which rule its number answers to.
 */
class EditorConfig {
  public:
    EditorConfig();

    /**
     * @brief Reads the settings file, falling back to the table where it holds nothing
     * @return The stored settings
     */
    static EditorConfig load();

    /**
     * @brief Writes the settings file
     * @return True if the file now holds these settings
     */
    bool save() const;

    /**
     * @brief The value of a float entry, with its scaling rule applied
     * @param key The entry to read
     * @return The scaled value
     */
    float getFloat(EditorConfigKey key) const;

    /**
     * @brief The value of a float entry as it was authored, before any scaling
     * @param key The entry to read
     * @return The unscaled value
     */
    float getUnscaledFloat(EditorConfigKey key) const;

    /**
     * @brief A size the config does not hold, answered at the current UI scale
     * @param pixels The size as it was authored
     * @param rule How that size answers to the scale
     * @return The scaled size
     */
    float scale(float pixels, ScalingRule rule = SCALING_ROUND) const;

    /**
     * @brief The value of an int entry
     * @param key The entry to read
     * @return The value
     */
    int64_t getInt(EditorConfigKey key) const;

    /**
     * @brief The value of a bool entry
     * @param key The entry to read
     * @return The value
     */
    bool getBool(EditorConfigKey key) const;

    /**
     * @brief Replaces an entry's value
     * @param key The entry to write
     * @param value The value it takes, whose type has to be the one the entry declares
     */
    void set(EditorConfigKey key, ConfigValue value);

    float uiScale() const { return m_scale; }

    /**
     * @brief Puts the editor on a new UI scale
     * @param scale The scale every SCALING_ROUND and SCALING_SNAP entry is answered at
     */
    void setUiScale(float scale);

    std::span<const std::filesystem::path> recentProjects() const { return m_recentProjects; }

    /**
     * @brief The project opened without asking, empty when the launcher should be shown
     */
    const std::filesystem::path &autoLaunchProject() const { return m_autoLaunchProject; }

    void setAutoLaunchProject(const std::filesystem::path &projectPath) { m_autoLaunchProject = projectPath; }

    /**
     * @brief Moves a project to the front of the recent list, dropping the oldest past the limit
     * @param projectPath The project file that was just opened
     */
    void addRecentProject(const std::filesystem::path &projectPath);

  private:
    std::array<ConfigValue, EDITOR_CONFIG_COUNT> m_values;
    float m_scale = 1.0f;

    std::vector<std::filesystem::path> m_recentProjects;
    std::filesystem::path m_autoLaunchProject;
};

#endif // RAPTURE__EDITOR_CONFIG_H
