#ifndef RAPTURE__SCENE_H
#define RAPTURE__SCENE_H

#include "acceleration_structures/TLAS.h"
#include "asset_manager/AssetCommon.h"
#include "components/ChangeChannels.h"
#include "ecs/entity_accessor.h"
#include "events/EventSignal.h"
#include "scenes/entities/EntityCommon.h"
#include "serialization/SerialDocument.h"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Rapture {

class Controller;
class Environment;
class Instance;
class SceneRenderData;
class PhysicsSystem;
struct RenderContext;

static constexpr uint32_t SCENE_FORMAT_VERSION = 1;

struct SceneSettings {
    std::string sceneName;
    bool frustumCullingEnabled = true;
};

class Scene {
  public:
    Scene(const std::string &sceneName = "Untitled Scene");
    ~Scene();

    ecs::EntityAccessor createEntity(const std::string &name = "Untitled Entity");
    ecs::EntityAccessor createCube(const std::string &name = "Untitled Entity", Mobility mobility = MOBILITY_STATIC);
    ecs::EntityAccessor createSphere(const std::string &name = "Untitled Entity", Mobility mobility = MOBILITY_STATIC);

    /**
     * @brief Fills a newly created scene with a sun, a sky and a floor
     */
    void addDefaultContent();

    void destroyEntity(ecs::Entity entity);

    void onUpdate(float dt);

    /**
     * @brief Advances the rigid body simulation and writes the result back onto the transforms
     * @param dt Seconds to advance by
     */
    void stepPhysics(float dt);

    ecs::Registry &getRegistry() { return m_registry; }
    const ecs::Registry &getRegistry() const { return m_registry; }

    /**
     * @brief Binds an entity of this scene to the registry that resolves it
     * @param entity The entity to wrap
     * @return An accessor, invalid if the entity is not alive in this scene
     */
    ecs::EntityAccessor accessor(ecs::Entity entity) { return ecs::EntityAccessor(entity, &m_registry); }

    SceneSettings &getSettings();
    const SceneSettings &getSettings() const;

    std::string getSceneName() const;

    /**
     * @brief The controller currently driving this scene, whose camera scene wide work is done from
     * @return The controller, or nullptr if nothing is driving the scene
     */
    Controller *activeController() const { return m_activeController; }
    void setActiveController(Controller *controller) { m_activeController = controller; }

    /**
     * @brief The scene's environment, owner of skybox generation and image-based lighting
     * @return The environment, or nullptr if the scene has none
     */
    Environment *environment() const { return m_environment; }

    /**
     * @brief The scene's rigid body physics simulation.
     */
    PhysicsSystem *physics() const { return m_physics.get(); }

    /**
     * @brief The hidden root every authored instance lives under
     * @return The root, which is never shown, named or written to a scene file
     */
    Instance *root() const { return m_root.get(); }

    /**
     * @brief Finds the instance that owns an entity
     * @param entity The entity to look up
     * @return The instance, or nullptr if the entity is not authored
     */
    Instance *instanceFor(ecs::Entity entity) const;

    /**
     * @brief Destroys an instance along with its subtree
     * @param instance The instance to destroy, ignored if it is the root
     */
    void destroyInstance(Instance *instance);

    /**
     * @brief Writes the scene's settings and its whole instance tree
     * @param node Cursor to write the scene's object into
     */
    void serialize(WriteNode node) const;

    /**
     * @brief Builds a scene from a scene document
     * @param node Cursor to the scene's object
     * @return The new scene, or nullptr if the document could not be read
     */
    static std::unique_ptr<Scene> deserialize(ReadNode node);

    /**
     * @brief Captures this scene's current contents into a document it can be restored from
     * @return The snapshot, which has to outlive the restoreFrom call that reads it
     */
    SerialDocument snapshot() const;

    /**
     * @brief Reverts this scene to a snapshot, keeping every instance the snapshot and the scene share
     * @param node Cursor to the scene's object, which has to outlive the call
     * @return True if the scene now matches the snapshot
     */
    bool restoreFrom(ReadNode node);

    void registerBLAS(ecs::Entity entity);

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
    bool active = false;

    /**
     * @brief Fires when instances are added to or removed from this scene, for views mirroring the tree
     */
    EventSignal<void()> onHierarchyChanged;

  private:
    void onRigidBodyConstructed(ecs::Entity entity);
    void registerRigidBodies();
    void syncRigidBodyTransforms();

    /**
     * @brief Destroys every instance under the root, leaving the root itself
     */
    void clearInstances();

  private:
    ecs::Registry m_registry{CHANNEL_COUNT};
    Environment *m_environment = nullptr;
    std::unique_ptr<SceneRenderData> m_renderData;
    std::unique_ptr<PhysicsSystem> m_physics;
    Controller *m_activeController = nullptr;
    std::vector<ecs::Entity> m_pendingRigidBodies;
    SceneSettings m_config;

    std::shared_ptr<TLAS> m_tlas;
    bool m_tlasDirty = false;

    // declared last so its subtree tears down before anything destroyEntity touches
    std::unique_ptr<Instance> m_root;

    friend class Environment;
};
} // namespace Rapture

#endif // RAPTURE__SCENE_H
