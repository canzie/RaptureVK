#pragma once

#include <string>
#include <memory>
#include <entt/entt.hpp>
#include "AccelerationStructures/TLAS.h"

namespace Rapture {

    // Forward declaration
    class Entity;

    struct SkyboxComponent;
    
    struct SceneSettings {
        std::string sceneName;
        bool frustumCullingEnabled = true;
        std::shared_ptr<Entity> mainCamera;
        std::shared_ptr<Entity> skybox;
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

        SceneSettings& getSettings();
        const SceneSettings& getSettings() const;

        std::string getSceneName() const;

        void setMainCamera(Entity camera);
        void setMainCamera(std::shared_ptr<Entity> camera);

        std::weak_ptr<Entity> getMainCamera();

        void setSkybox(Entity entity);
        std::weak_ptr<Entity> getSkybox();
        SkyboxComponent* getSkyboxComponent();

        void registerBLAS(Entity& entity);
        void registerBLAS(std::shared_ptr<Entity> entity);

        void buildTLAS();
        const TLAS& getTLAS() {return *m_tlas;}

    private:
        entt::registry m_Registry;
        SceneSettings m_config;

        std::unique_ptr<TLAS> m_tlas;
    
        friend class Entity;
    };
}

