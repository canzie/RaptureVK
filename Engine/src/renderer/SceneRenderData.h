#ifndef RAPTURE__SCENERENDERDATA_H
#define RAPTURE__SCENERENDERDATA_H

#include "components/ChangeChannels.h"
#include "GPUDataStructs.h"
#include "RenderPartition.h"

#include "ecs/registry.h"
#include "window_context/vulkan_context/RenderContext.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace Rapture {
class Scene;
class ShadowMap;
class CascadedShadowMap;

/**
 * @brief GPU-side mirror of a scene's ECS data
 *
 * Manages GPUDataStores for meshes, lights, and cameras. Hooks into
 * the scene's registry via signals for slot lifecycle, and packs
 * component data into SSBOs each frame.
 */
class SceneRenderData {
  public:
    /**
     * @brief Construct and connect to a scene's registry
     * @param renderContext Vulkan context for buffer allocation
     * @param scene Scene whose registry to mirror
     * @param frameCount Number of frames in flight
     */
    SceneRenderData(const RenderContext &renderContext, Scene &scene, uint32_t frameCount);
    ~SceneRenderData();

    SceneRenderData(const SceneRenderData &) = delete;
    SceneRenderData &operator=(const SceneRenderData &) = delete;

    /**
     * @brief Pack component data and upload all SSBOs
     * @param frameIndex Current frame in flight index
     */
    void onUpdate(uint32_t frameIndex);

    /**
     * @brief Moves a mesh's slot into the partition its new mobility belongs to
     * @param entityId The entity whose mesh is changing mobility
     * @param mobility The mobility to move to
     */
    void setMeshMobility(EntityID entityId, Mobility mobility);

    /**
     * @brief Moves a light's slot into the partition its new mobility belongs to
     * @param entityId The entity whose light is changing mobility
     * @param mobility The mobility to move to
     */
    void setLightMobility(EntityID entityId, Mobility mobility);

    /**
     * @brief Moves a shadow's slot into the partition its new mobility belongs to
     * @param entityId The entity whose shadow is changing mobility
     * @param mobility The mobility to move to
     */
    void setShadowMobility(EntityID entityId, Mobility mobility);

    /**
     * @brief Moves a cascaded shadow's slot into the partition its new mobility belongs to
     * @param entityId The entity whose cascaded shadow is changing mobility
     * @param mobility The mobility to move to
     */
    void setCascadedShadowMobility(EntityID entityId, Mobility mobility);

    /**
     * @brief Get the shadow map owned on behalf of an entity
     * @param entityId Entity holding the ShadowComponent
     * @return The shadow map, or nullptr if the entity has none
     */
    ShadowMap *getShadowMap(EntityID entityId) const;

    /**
     * @brief Get the cascaded shadow map owned on behalf of an entity
     * @param entityId Entity holding the CascadedShadowComponent
     * @return The cascaded shadow map, or nullptr if the entity has none
     */
    CascadedShadowMap *getCascadedShadowMap(EntityID entityId) const;

    GPUDataStore<MeshGPUData> &getMeshes() { return m_meshes; }
    GPUDataStore<LightGPUData> &getLights() { return m_lights; }
    GPUDataStore<CameraGPUData> &getCameras() { return m_cameras; }
    GPUDataStore<ShadowGPUData> &getShadows() { return m_shadows; }
    const GPUDataStore<MeshGPUData> &getMeshes() const { return m_meshes; }
    const GPUDataStore<LightGPUData> &getLights() const { return m_lights; }
    const GPUDataStore<CameraGPUData> &getCameras() const { return m_cameras; }
    const GPUDataStore<ShadowGPUData> &getShadows() const { return m_shadows; }

  private:
    /**
     * @brief Subscribes to one concrete light type appearing and leaving
     * @param registry The registry holding the lights
     */
    template <typename T>
    void connectLightSignals(ecs::Registry &registry);

    void onMeshAdded(EntityID entityId);
    void onMeshRemoved(EntityID entityId);
    void onLightAdded(EntityID entityId);
    void onLightRemoved(EntityID entityId);
    void onCameraAdded(EntityID entityId);
    void onCameraRemoved(EntityID entityId);
    void onShadowAdded(EntityID entityId);
    void onShadowRemoved(EntityID entityId);
    void onCascadedShadowAdded(EntityID entityId);
    void onCascadedShadowRemoved(EntityID entityId);

    void createShadowMap(EntityID entityId);
    void destroyShadowMap(EntityID entityId);
    void createCascadedShadowMap(EntityID entityId);
    void destroyCascadedShadowMap(EntityID entityId);

    void updateMeshes(uint32_t frameIndex);
    void updateLights(uint32_t frameIndex);
    void updateCameras(uint32_t frameIndex);
    void updateShadows(uint32_t frameIndex);

    GPUDataStore<MeshGPUData> m_meshes;
    GPUDataStore<LightGPUData> m_lights;
    GPUDataStore<CameraGPUData> m_cameras;
    GPUDataStore<ShadowGPUData> m_shadows;

    std::unordered_map<EntityID, std::unique_ptr<ShadowMap>> m_shadowMaps;
    std::unordered_map<EntityID, std::unique_ptr<CascadedShadowMap>> m_cascadedShadowMaps;

    RenderContext m_renderContext;
    Scene *m_scene = nullptr;
    uint32_t m_frameCount = 0;

    /**
     * @brief One store's position in the channels it mirrors, for one frame's buffer
     */
    struct StoreBookmarks {
        ecs::Bookmark transform;
        ecs::Bookmark params;
    };

    /**
     * @brief Pulls both channels a store mirrors and repacks whatever they name
     * @param bookmarks This frame's position in both channels
     * @param paramsChannel The channel carrying this store's own parameters
     * @param frameIndex Frame whose buffer is being brought up to date
     * @param repackEntity Repacks one entity's slot, called only for entities that own one
     * @param repackAll Repacks every slot, used when the reader fell too far behind
     */
    void consumeChanges(StoreBookmarks &bookmarks, SceneChannel paramsChannel,
                        const std::function<void(EntityID)> &repackEntity, const std::function<void()> &repackAll);

    std::vector<ecs::SignalConnection> m_connections;

    std::vector<StoreBookmarks> m_meshBookmarks;
    std::vector<StoreBookmarks> m_lightBookmarks;
    std::vector<StoreBookmarks> m_cameraBookmarks;
    std::vector<StoreBookmarks> m_shadowBookmarks;
};

} // namespace Rapture

#endif // RAPTURE__SCENERENDERDATA_H
