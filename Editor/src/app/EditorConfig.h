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

using ConfigValue = std::variant<double, int64_t, bool>;

enum EditorConfigKey {
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

    EDITOR_CONFIG_UNDO_STEP_LIMIT,
    EDITOR_CONFIG_UNDO_MEMORY_LIMIT_MB,

    EDITOR_CONFIG_COUNT
};

/**
 * @brief The declaration of one config entry: what it is called, what it holds, and what it falls
 * back to.
 */
struct EditorConfigEntry {
    std::string_view key;
    ConfigValueType type;
    ConfigValue fallback;
};

constexpr std::array<EditorConfigEntry, EDITOR_CONFIG_COUNT> EDITOR_CONFIG_TABLE = {{
    {"menuBarHeight", CONFIG_VALUE_FLOAT, 28.0},
    {"workspaceTabHeight", CONFIG_VALUE_FLOAT, 36.0},
    {"workspaceTabWidth", CONFIG_VALUE_FLOAT, 196.0},
    {"bottomBarHeight", CONFIG_VALUE_FLOAT, 28.0},

    {"hotbarHeight", CONFIG_VALUE_FLOAT, 40.0},
    {"hotbarPadding", CONFIG_VALUE_FLOAT, 12.0},
    {"hotbarSeparatorWidth", CONFIG_VALUE_FLOAT, 2.0},

    {"dockOuterSpacing", CONFIG_VALUE_FLOAT, 4.0},
    {"dockInnerSpacing", CONFIG_VALUE_FLOAT, 3.0},

    {"tabIconSize", CONFIG_VALUE_FLOAT, 16.0},
    {"tabLabelPadding", CONFIG_VALUE_FLOAT, 8.0},
    {"tabIconGap", CONFIG_VALUE_FLOAT, 6.0},

    {"commandSocket", CONFIG_VALUE_BOOL, true},

    {"undoStepLimit", CONFIG_VALUE_INT, static_cast<int64_t>(200)},
    {"undoMemoryLimitMb", CONFIG_VALUE_INT, static_cast<int64_t>(256)},
}};

/**
 * @brief The editor's own settings, which live beside the executable because they outlive any project.
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
     * @brief The value of a float entry
     * @param key The entry to read
     * @return The value
     */
    float getFloat(EditorConfigKey key) const;

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

    std::vector<std::filesystem::path> m_recentProjects;
    std::filesystem::path m_autoLaunchProject;
};

#endif // RAPTURE__EDITOR_CONFIG_H
