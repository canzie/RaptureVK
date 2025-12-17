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

    entt::registry &getRegistry() { return m_registry; }
    const entt::registry &getRegistry() const { return m_registry; }

    SceneSettings &getSettings();
    const SceneSettings &getSettings() const;

    std::string getSceneName() const;

    void setMainCamera(Entity camera);
    Entity getMainCamera() const;

    Entity createEnvironmentEntity();
    Entity getEnvironmentEntity() const;

    void registerBLAS(Entity &entity);

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
    entt::registry m_registry;
    SceneSettings m_config;

    std::shared_ptr<TLAS> m_tlas;

    friend class Entity;
};
} // namespace Rapture
