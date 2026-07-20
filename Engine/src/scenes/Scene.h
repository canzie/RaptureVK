#pragma once

#include "acceleration_structures/TLAS.h"
#include "scenes/entities/EntityCommon.h"
#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Rapture {

class Entity;
class Environment;
class SceneRenderData;
class PhysicsSystem;
struct RenderContext;

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
    Entity createSphere(const std::string &name = "Untitled Entity", Mobility mobility = MOBILITY_STATIC);

    void destroyEntity(Entity entity);

    void onUpdate(float dt);

    entt::registry &getRegistry() { return m_registry; }
    const entt::registry &getRegistry() const { return m_registry; }

    SceneSettings &getSettings();
    const SceneSettings &getSettings() const;

    std::string getSceneName() const;

    void setMainCamera(Entity camera);
    Entity getMainCamera() const;

    /**
     * @brief The scene's single environment entity, always present and not destroyable.
     * @return The environment entity.
     */
    Entity environmentEntity() const;

    /**
     * @brief The scene's environment, owner of skybox generation and image-based lighting
     */
    Environment *environment() const { return m_environment.get(); }

    /**
     * @brief The scene's rigid body physics simulation.
     */
    PhysicsSystem *physics() const { return m_physics.get(); }

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

    /**
     * @brief Get the GPU data mirror
     */
    SceneRenderData *getRenderData() { return m_renderData.get(); }
    const SceneRenderData *getRenderData() const { return m_renderData.get(); }

  public:
    bool locked = false;

  private:
    void ensureBLASFreeBuckets();
    void onRigidBodyConstructed(entt::registry &registry, entt::entity entity);
    void registerRigidBodies();
    void syncRigidBodyTransforms();

  private:
    entt::registry m_registry;
    std::unique_ptr<Environment> m_environment;
    std::unique_ptr<SceneRenderData> m_renderData;
    std::unique_ptr<PhysicsSystem> m_physics;
    std::vector<entt::entity> m_pendingRigidBodies;
    SceneSettings m_config;

    std::shared_ptr<TLAS> m_tlas;
    bool m_tlasDirty = false;
    std::vector<std::vector<std::unique_ptr<BLAS>>> m_blasFreeBuckets;
    size_t m_blasFreeBucket = 0;

    friend class Entity;
};
} // namespace Rapture
