#ifndef RAPTURE__PROJECT_H
#define RAPTURE__PROJECT_H

#include "scenes/Scene.h"
#include "scenes/SceneManager.h"
#include "scenes/World.h"
#include "serialization/SerialDocument.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Rapture {

struct ProjectConfig {
    std::string name;
    std::filesystem::path rootDirectory;
    std::filesystem::path shaderDirectory;

    std::string initialWorldName;
};

class Project {
  public:
    Project();

    SceneManager &getSceneManager() { return m_sceneManager; }
    const SceneManager &getSceneManager() const { return m_sceneManager; }

    Scene *getActiveScene() const { return m_sceneManager.getActiveScene(); }

    void setActiveScene(Scene *scene) { m_sceneManager.setActiveScene(scene); }

    World *createWorld(const std::string &name) { return m_sceneManager.createWorld(name); }

    World *getWorld(const std::string &name) { return m_sceneManager.getWorld(name); }

    void setActiveWorld(const std::string &name) { m_sceneManager.setActiveWorld(name); }

    World *getActiveWorld() const { return m_sceneManager.getActiveWorld(); }

    static void saveProject(std::filesystem::path path);

    static std::unique_ptr<Project> loadProject(std::filesystem::path path);

    // Project config access
    std::filesystem::path getProjectRootDirectory() const { return m_config.rootDirectory; }
    std::filesystem::path getProjectShaderDirectory() const { return m_config.shaderDirectory; }
    std::string getProjectName() const { return m_config.name; }
    std::string getInitialWorldName() const { return m_config.initialWorldName; }

    void setProjectRootDirectory(const std::filesystem::path &dir) { m_config.rootDirectory = dir; }
    void setProjectShaderDirectory(const std::filesystem::path &dir) { m_config.shaderDirectory = dir; }

    void setProjectName(const std::string &name) { m_config.name = name; }
    void setInitialWorldName(const std::string &name) { m_config.initialWorldName = name; }

    const ProjectConfig &getConfig() const { return m_config; }

  private:
    ProjectConfig m_config;
    SceneManager m_sceneManager;
    SerialDocument m_saveFile;
};
} // namespace Rapture

#endif // RAPTURE__PROJECT_H
