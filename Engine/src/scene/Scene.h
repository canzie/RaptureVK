#ifndef RAPTURE__SCENE_H
#define RAPTURE__SCENE_H

#include "assets/asset_manager/AssetCommon.h"
#include "core/ecs/entity_accessor.h"
#include "core/events/EventSignal.h"
#include "core/serialization/SerialDocument.h"
#include "core/utils/FreeList.h"
#include "gpu/acceleration_structures/TLAS.h"
#include "physics/Common.h"
#include "scene/EntityCommon.h"
#include "scene/TickPhase.h"
#include "scene/components/ChangeChannels.h"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Rapture {

class Camera3D;
class Controller;
class Environment;
class Instance;
class SceneComponent;
class SceneObject;
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
    PhysicsSystem *physicsSystem() const { return m_physics.get(); }

    /**
     * @brief The hidden root every authored instance lives under
     * @return The root, which is never shown, named or written to a scene file
     */
    SceneObject *root() const { return m_root.get(); }

    /**
     * @brief Finds the instance that owns an entity
     * @param entity The entity to look up
     * @return The instance, or nullptr if the entity is not authored
     */
    SceneObject *instanceFor(ecs::Entity entity) const;

    /**
     * @brief Destroys an instance along with its subtree
     * @param instance The instance to destroy, ignored if it is the root
     */
    void destroyInstance(SceneObject *instance);

    /**
     * @brief A slot in none of the tick lists
     */
    static constexpr uint32_t INVALID_TICK_SLOT = UINT32_MAX;

    /**
     * @brief Takes a slot in the list walked during one phase of each update
     * @param instance The instance to start ticking
     * @param phase The phase to tick it in
     * @return The slot the instance was put in
     */
    uint32_t registerTick(Instance *instance, TickPhase phase);

    /**
     * @brief Releases a slot in a phase's tick list
     * @param slot The slot to free
     * @param phase The phase the slot was taken in
     */
    void unregisterTick(uint32_t slot, TickPhase phase);

    /**
     * @brief Updates every instance registered in one phase
     * @param phase The phase to run
     * @param dt Seconds since the last update
     */
    void runTickPhase(TickPhase phase, float dt);

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

    /**
     * @brief Puts an entity's mesh into the TLAS, replacing the instance it already had
     * @param entity The entity to trace against
     */
    void registerBLAS(ecs::Entity entity);

    /**
     * @brief Takes an entity's mesh out of the TLAS
     * @param entity The entity to stop tracing against
     */
    void unregisterBLAS(ecs::Entity entity);

    /**
     * @brief The scene's top level acceleration structure
     * @return The structure, or nullptr if the scene has none
     */
    TLAS *getTLAS() { return m_tlas.get(); }

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
    /**
     * @brief Re-aims the shadow maps whose light or transform changed since this scene last looked
     * @param cameraPosition Where the directional shadow volume is centred
     * @param activeCamera The camera the cascade splits are built from, may be nullptr
     */
    void updateShadowViews(const glm::vec3 &cameraPosition, Camera3D *activeCamera);

    /**
     * @brief Brings the acceleration structure up to date with the scene
     */
    void updateTLAS();

    /**
     * @brief Hands every body the simulation moved this step back to the node it drives
     */
    void syncSimulatedTransforms();

    /**
     * @brief Destroys every instance under the root, leaving the root itself
     */
    void clearInstances();

  private:
    ecs::Registry m_registry{CHANNEL_COUNT};
    Environment *m_environment = nullptr;
    std::unique_ptr<SceneRenderData> m_renderData;
    std::unique_ptr<PhysicsSystem> m_physics;
    std::vector<physics::BodyState> m_simulatedStates;
    Controller *m_activeController = nullptr;
    SceneSettings m_config;

    // this scene's own position in the channels that stale a shadow map's view matrix
    ecs::Bookmark m_shadowTransformBookmark;
    ecs::Bookmark m_shadowLightBookmark;

    std::unique_ptr<TLAS> m_tlas;
    ecs::Bookmark m_tlasTransformBookmark;

    std::array<FreeList<Instance *>, TICK_COUNT> m_ticking;

    // declared last so its subtree tears down before anything destroyEntity touches
    std::unique_ptr<SceneObject> m_root;

    friend class Environment;
};
} // namespace Rapture

#endif // RAPTURE__SCENE_H
