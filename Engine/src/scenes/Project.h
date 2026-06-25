#pragma once

#include "scenes/Scene.h"
#include "scenes/SceneManager.h"
#include "scenes/World.h"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace Rapture {
struct ProjectConfig {
    std::string name;
    std::filesystem::path rootDirectory;
    std::filesystem::path shaderDirectory;

    std::string initialWorldName;
};

class Project {
  public:
    Project() : m_config{"New Project", std::filesystem::current_path(), std::filesystem::current_path(), "DefaultWorld"}
    {

        RP_CORE_INFO("Creating Project: {0}", m_config.name);

        // Create a default world with a default scene
        auto defaultWorld = std::make_shared<World>("DefaultWorld");
        auto defaultScene = m_sceneManager.createScene("DefaultScene");
        m_sceneManager.setActiveScene("DefaultScene");

        // Add scene to world and set as main
        defaultWorld->addScene("DefaultScene", defaultScene);
        defaultWorld->setMainScene("DefaultScene");

        // Register world with the scene manager
        m_sceneManager.registerWorld(defaultWorld);

        // Set as active
        m_sceneManager.setActiveWorld("DefaultWorld");
    }

    SceneManager &getSceneManager() { return m_sceneManager; }
    const SceneManager &getSceneManager() const { return m_sceneManager; }

    // Get the active scene from the scene manager
    Scene *getActiveScene() const { return m_sceneManager.getActiveScene(); }

    // Set the active scene via the scene manager
    void setActiveScene(Scene *scene) { m_sceneManager.setActiveScene(scene); }

    // World management
    std::shared_ptr<World> createWorld(const std::string &name)
    {
        auto world = std::make_shared<World>(name);
        m_worlds[name] = world;
        m_sceneManager.registerWorld(world);
        return world;
    }

    std::shared_ptr<World> getWorld(const std::string &name) const
    {
        auto it = m_worlds.find(name);
        if (it != m_worlds.end()) {
            return it->second;
        }
        return nullptr;
    }

    void setActiveWorld(const std::string &name) { m_sceneManager.setActiveWorld(name); }

    std::shared_ptr<World> getActiveWorld() const { return m_sceneManager.getActiveWorld(); }

    // Project file operations
    static void saveProject(std::filesystem::path path)
    {
        (void)path;
        // Implementation for serializing project
    }

    static std::shared_ptr<Project> loadProject(std::filesystem::path path)
    {
        (void)path;

        auto project = std::make_shared<Project>();

        // Implementation for deserializing project

        return project;
    }

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
    std::unordered_map<std::string, std::shared_ptr<World>> m_worlds;
};
} // namespace Rapture
