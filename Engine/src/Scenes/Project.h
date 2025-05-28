#pragma once


#include "Scenes/Scene.h"
#include "Scenes/World.h"
#include "Scenes/SceneManager.h"
#include <memory>
#include <string>
#include <filesystem>
#include <unordered_map>

namespace Rapture
{
    struct ProjectConfig
    {
        std::string name;
        std::filesystem::path rootDirectory;
        std::filesystem::path shaderDirectory;

        std::string initialWorldName;
    };

    class Project
    {
    public:
        Project()
            : m_config{"New Project", std::filesystem::current_path(), std::filesystem::current_path(), "DefaultWorld"}
        {

            RP_CORE_INFO("Creating Project: {0}", m_config.name);

            // Create a default world with a default scene
            auto defaultWorld = std::make_shared<World>("DefaultWorld");
            auto defaultScene = std::make_shared<Scene>();

            
            // Register with SceneManager
            SceneManager::getInstance().createScene("DefaultScene");
            SceneManager::getInstance().setActiveScene("DefaultScene");
            
            // Add scene to world and set as main
            defaultWorld->addScene("DefaultScene", defaultScene);
            defaultWorld->setMainScene("DefaultScene");
            
            // Register world with SceneManager
            SceneManager::getInstance().registerWorld(defaultWorld);
            
            // Set as active
            SceneManager::getInstance().setActiveWorld("DefaultWorld");


        }

        ~Project()
        {
            // Clear all worlds and scenes
            m_worlds.clear();
            SceneManager::getInstance().reset();
        }

        // Get the active scene from the SceneManager
        std::shared_ptr<Scene> getActiveScene() const 
        { 
            return SceneManager::getInstance().getActiveScene(); 
        }
        
        // Set the active scene via SceneManager
        void setActiveScene(std::shared_ptr<Scene> scene) 
        { 
            SceneManager::getInstance().setActiveScene(scene); 
        }
        
        // World management
        std::shared_ptr<World> createWorld(const std::string& name)
        {
            auto world = std::make_shared<World>(name);
            m_worlds[name] = world;
            SceneManager::getInstance().registerWorld(world);
            return world;
        }
        
        std::shared_ptr<World> getWorld(const std::string& name) const
        {
            auto it = m_worlds.find(name);
            if (it != m_worlds.end()) {
                return it->second;
            }
            return nullptr;
        }
        
        void setActiveWorld(const std::string& name)
        {
            SceneManager::getInstance().setActiveWorld(name);
        }
        
        std::shared_ptr<World> getActiveWorld() const
        {
            return SceneManager::getInstance().getActiveWorld();
        }

        // Project file operations
        static void saveProject(std::filesystem::path path) 
        {
            // Implementation for serializing project
        }
        
        static std::shared_ptr<Project> loadProject(std::filesystem::path path)
        {
            auto project = std::make_shared<Project>();
            
            // Implementation for deserializing project
            
            return project;
        }

        // Project config access
        std::filesystem::path getProjectRootDirectory() const { return m_config.rootDirectory; }
        std::filesystem::path getProjectShaderDirectory() const { return m_config.shaderDirectory; }
        std::string getProjectName() const { return m_config.name; }
        std::string getInitialWorldName() const { return m_config.initialWorldName; }
        
        void setProjectRootDirectory(const std::filesystem::path& dir) { 
            m_config.rootDirectory = dir;
        }
        void setProjectShaderDirectory(const std::filesystem::path& dir) { 
            m_config.shaderDirectory = dir;
        }

        void setProjectName(const std::string& name) { m_config.name = name; }
        void setInitialWorldName(const std::string& name) { m_config.initialWorldName = name; }

        const ProjectConfig& getConfig() const { return m_config; }
        
    private:
        ProjectConfig m_config;
        std::unordered_map<std::string, std::shared_ptr<World>> m_worlds;
    };
}
