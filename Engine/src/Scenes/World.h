#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Scene.h"

namespace Rapture {

class World {
public:
    World(const std::string& name) 
        : m_name(name), m_isActive(false) {}
    
    ~World() {
        // Clear all scenes to ensure proper cleanup
        m_scenes.clear();
    }
    
    // World operations
    void initialize() {
        // Initialize world resources
    }
    
    void shutdown() {
        // Shutdown world resources
        m_scenes.clear();
        m_isActive = false;
    }
    
    void update(float deltaTime) {
        // Update all active scenes
        for (auto& [name, scene] : m_scenes) {
            scene->onUpdate();
        }
    }
    
    // Scene management within a world
    std::shared_ptr<Scene> createScene(const std::string& sceneName) {
        auto scene = std::make_shared<Scene>();
        m_scenes[sceneName] = scene;
        return scene;
    }
    
    void addScene(const std::string& sceneName, std::shared_ptr<Scene> scene) {
        m_scenes[sceneName] = scene;
    }
    
    void removeScene(const std::string& sceneName) {
        m_scenes.erase(sceneName);
    }
    
    std::shared_ptr<Scene> getScene(const std::string& sceneName) {
        auto it = m_scenes.find(sceneName);
        if (it != m_scenes.end()) {
            return it->second;
        }
        return nullptr;
    }
    
    std::vector<std::string> getSceneNames() const {
        std::vector<std::string> names;
        for (auto& [name, _] : m_scenes) {
            names.push_back(name);
        }
        return names;
    }
    
    // Set the main scene of this world
    void setMainScene(const std::string& sceneName) {
        auto it = m_scenes.find(sceneName);
        if (it != m_scenes.end()) {
            m_mainScene = it->second;
            m_mainSceneName = sceneName;
        }
    }
    
    std::shared_ptr<Scene> getMainScene() const {
        return m_mainScene;
    }
    
    // World state
    bool isActive() const { return m_isActive; }
    void setActive(bool active) { m_isActive = active; }
    const std::string& getName() const { return m_name; }
    
private:
    std::string m_name;
    bool m_isActive = false;
    std::unordered_map<std::string, std::shared_ptr<Scene>> m_scenes;
    std::shared_ptr<Scene> m_mainScene;
    std::string m_mainSceneName;
};

} // namespace Rapture 