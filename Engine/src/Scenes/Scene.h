#pragma once

#include <string>
#include <entt/entt.hpp>

namespace Rapture {

    // Forward declaration
    class Entity;

    struct SceneSettings {
        std::string sceneName;
        bool frustumCullingEnabled = true;
        std::shared_ptr<Entity> mainCamera;
    };
    
    class Scene {
    public:
        Scene(const std::string& sceneName = "Untitled Scene");
        ~Scene();

        Entity createEntity(const std::string& name = "Untitled Entity");
        void destroyEntity(Entity entity);

        void onUpdate();

        entt::registry& getRegistry() { return m_Registry; }
        const entt::registry& getRegistry() const { return m_Registry; }

        SceneSettings& getSettings() { return m_config; }
        const SceneSettings& getSettings() const { return m_config; }

        std::string getSceneName() const { return m_config.sceneName; }

        void setMainCamera(std::shared_ptr<Entity> camera) { m_config.mainCamera = camera; }

    private:
        entt::registry m_Registry;
        SceneSettings m_config;
    
        friend class Entity;
    };
}

