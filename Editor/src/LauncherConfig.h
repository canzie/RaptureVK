#ifndef RAPTURE__LAUNCHER_CONFIG_H
#define RAPTURE__LAUNCHER_CONFIG_H

#include <filesystem>
#include <span>
#include <vector>

/**
 * @brief The editor's own settings, which live beside the executable because they outlive any project.
 */
class LauncherConfig {
  public:
    /**
     * @brief Reads the settings file, returning defaults when it is missing or unreadable
     */
    static LauncherConfig load();

    /**
     * @brief Writes the settings file
     * @return True if the file now holds these settings
     */
    bool save() const;

    /**
     * @brief Projects the launcher offers, most recently opened first
     */
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
    std::vector<std::filesystem::path> m_recentProjects;
    std::filesystem::path m_autoLaunchProject;
};

#endif // RAPTURE__LAUNCHER_CONFIG_H
