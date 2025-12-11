#pragma once

#include "AccelerationStructures/TLAS.h"
#include <entt/entt.hpp>
#include <memory>
#include <string>

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
    Scene(const std::string &sceneName = "Untitled Scene");
    ~Scene();

    Entity createEntity(const std::string &name = "Untitled Entity");
    Entity createCube(const std::string &name = "Untitled Entity");
    Entity createSphere(const std::string &name = "Untitled Entity");

    void destroyEntity(Entity entity);

    void onUpdate(float dt);

    entt::registry &getRegistry() { return m_Registry; }
    const entt::registry &getRegistry() const { return m_Registry; }

    SceneSettings &getSettings();
    const SceneSettings &getSettings() const;

    std::string getSceneName() const;

    void setMainCamera(Entity camera);
    void setMainCamera(std::shared_ptr<Entity> camera);

    std::weak_ptr<Entity> getMainCamera();

    void setSkybox(Entity entity);
    std::weak_ptr<Entity> getSkybox();
    SkyboxComponent *getSkyboxComponent();

    void registerBLAS(Entity &entity);
    void registerBLAS(std::shared_ptr<Entity> entity);

    void buildTLAS();
    std::shared_ptr<TLAS> getTLAS()
    {
        if (!m_tlas) {
            // RP_CORE_ERROR("Scene::getTLAS - TLAS is not built");
            return nullptr;
        }
        return m_tlas;
    }

    void updateTLAS();

  private:
    entt::registry m_Registry;
    SceneSettings m_config;

    std::shared_ptr<TLAS> m_tlas;

    friend class Entity;
};
} // namespace Rapture
