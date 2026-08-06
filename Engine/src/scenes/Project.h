#ifndef RAPTURE__PROJECT_H
#define RAPTURE__PROJECT_H

#include "scenes/Scene.h"
#include "scenes/SceneManager.h"
#include "scenes/World.h"
#include "serialization/SerialDocument.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Rapture {

static constexpr uint32_t PROJECT_FORMAT_VERSION = 1;

struct ProjectConfig {
    std::string name;
    std::filesystem::path projectDirectory;

    std::string initialWorldName;
    AssetHandle startupScene = INVALID_ASSET_HANDLE;
};

class Project {
  public:
    /**
     * @brief The empty project the editor runs on before one is opened, which touches no disk
     * @return An empty project, whose scene list every caller can iterate without a null check
     */
    static std::unique_ptr<Project> empty();

    /**
     * @brief Opens a project rooted at a directory, creating the directory tree if it is missing
     * @param projectDirectory The directory holding the project file, its content and its cache
     * @param name The project's name
     */
    Project(const std::filesystem::path &projectDirectory, std::string_view name);

    /**
     * @brief Whether this is a real project rather than the empty stand in
     */
    bool isValid() const { return !m_config.projectDirectory.empty(); }

    /**
     * @brief Creates the initial world holding one empty scene, and activates both
     */
    void createDefaultWorld();

    SceneManager &getSceneManager() { return m_sceneManager; }
    const SceneManager &getSceneManager() const { return m_sceneManager; }

    const std::vector<Scene *> &getActiveScenes() const { return m_sceneManager.getActiveScenes(); }

    void activateScene(Scene *scene) { m_sceneManager.activateScene(scene); }

    void deactivateScene(Scene *scene) { m_sceneManager.deactivateScene(scene); }

    World *createWorld(const std::string &name) { return m_sceneManager.createWorld(name); }

    World *getWorld(const std::string &name) { return m_sceneManager.getWorld(name); }

    void setActiveWorld(const std::string &name) { m_sceneManager.setActiveWorld(name); }

    World *getActiveWorld() const { return m_sceneManager.getActiveWorld(); }

    /**
     * @brief Writes the project file, replacing it atomically
     * @param path Destination .rapt file
     * @return True if the file now holds this project
     */
    bool saveProject(const std::filesystem::path &path);

    /**
     * @brief Reads a project file into this project and opens its startup scene
     * @param path Source .rapt file
     * @return True if the project now holds the file's contents
     */
    bool loadProject(const std::filesystem::path &path);

    AssetHandle getStartupScene() const { return m_config.startupScene; }
    void setStartupScene(AssetHandle startupScene) { m_config.startupScene = startupScene; }

    // Project config access
    std::filesystem::path getProjectDirectory() const { return m_config.projectDirectory; }

    /**
     * @brief The project file this project reads from and writes to
     */
    std::filesystem::path getProjectFilePath() const { return m_config.projectDirectory / (m_config.name + ".rapt"); }
    std::filesystem::path getCacheDirectory() const { return m_config.projectDirectory / ".cache"; }
    std::filesystem::path getBlobDirectory() const { return m_config.projectDirectory / "blobs"; }
    std::filesystem::path getContentDirectory() const { return m_config.projectDirectory / "content"; }
    std::filesystem::path getThumbnailDirectory() const { return getCacheDirectory() / "thumbnails"; }
    std::string getProjectName() const { return m_config.name; }
    std::string getInitialWorldName() const { return m_config.initialWorldName; }

    void setProjectName(const std::string &name) { m_config.name = name; }
    void setInitialWorldName(const std::string &name) { m_config.initialWorldName = name; }

    const ProjectConfig &getConfig() const { return m_config; }

  private:
    Project() = default;

    /**
     * @brief Creates the project directory and its cache subfolders on disk if missing
     */
    void createProjectDirectories();

    /**
     * @brief Opens the configured startup scene and makes it the active world's main scene
     */
    void openStartupScene();

  private:
    ProjectConfig m_config;
    SceneManager m_sceneManager;
    SerialDocument m_saveFile;
};
} // namespace Rapture

#endif // RAPTURE__PROJECT_H
