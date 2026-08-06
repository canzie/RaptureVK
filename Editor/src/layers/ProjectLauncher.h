#ifndef RAPTURE__PROJECT_LAUNCHER_H
#define RAPTURE__PROJECT_LAUNCHER_H

#include "LauncherConfig.h"

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include <filesystem>
#include <functional>
#include <string_view>

/**
 * @brief The project picker, listing the recently opened projects and naming a new one.
 */
class ProjectLauncher {
  public:
    explicit ProjectLauncher(Amethyst::Instance &parent);
    ~ProjectLauncher();
    ProjectLauncher(const ProjectLauncher &) = delete;
    ProjectLauncher &operator=(const ProjectLauncher &) = delete;
    ProjectLauncher(ProjectLauncher &&) = delete;
    ProjectLauncher &operator=(ProjectLauncher &&) = delete;

    /**
     * @brief Invoked with the project file the user picked from the recent list.
     */
    std::function<void(const std::filesystem::path &)> onOpenProject;

    /**
     * @brief Invoked with the name the user typed for a new project.
     */
    std::function<void(std::string_view)> onCreateProject;

    /**
     * @brief Invoked when the user wants to pick a project file from disk.
     */
    std::function<void(void)> onBrowseForProject;

  private:
    void buildContent(void);
    void setupHeader(Amethyst::UIScope &root);
    void setupRecentList(Amethyst::UIScope &root);
    void setupFooter(Amethyst::UIScope &root);

    /**
     * @brief Adds one clickable entry to the recent list
     * @param list The scope of the list the row is added to
     * @param projectPath The project file the row opens
     * @param order The row's place in the list layout
     */
    void addRecentRow(Amethyst::UIScope &list, const std::filesystem::path &projectPath, int32_t order);

    void createProject(void);

  private:
    LauncherConfig m_config;
    Amethyst::Frame *m_root = nullptr;
    Amethyst::TextInput *m_nameField = nullptr;
    Amethyst::EventConnection m_rootDestroyConn;
};

#endif // RAPTURE__PROJECT_LAUNCHER_H
