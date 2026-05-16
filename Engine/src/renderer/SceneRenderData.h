#ifndef RAPTURE__SCENERENDERDATA_H
#define RAPTURE__SCENERENDERDATA_H

#include "GPUDataStructs.h"
#include "RenderPartition.h"

#include "window_context/vulkan_context/RenderContext.h"

#include <memory>

namespace Rapture {
class Scene;

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

    GPUDataStore<MeshGPUData> &getMeshes() { return m_meshes; }
    GPUDataStore<LightGPUData> &getLights() { return m_lights; }
    GPUDataStore<CameraGPUData> &getCameras() { return m_cameras; }
    GPUDataStore<ShadowGPUData> &getShadows() { return m_shadows; }
    const GPUDataStore<MeshGPUData> &getMeshes() const { return m_meshes; }
    const GPUDataStore<LightGPUData> &getLights() const { return m_lights; }
    const GPUDataStore<CameraGPUData> &getCameras() const { return m_cameras; }
    const GPUDataStore<ShadowGPUData> &getShadows() const { return m_shadows; }

  private:
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

    void updateMeshes(uint32_t frameIndex);
    void updateLights(uint32_t frameIndex);
    void updateCameras(uint32_t frameIndex);
    void updateShadows(uint32_t frameIndex);

    GPUDataStore<MeshGPUData> m_meshes;
    GPUDataStore<LightGPUData> m_lights;
    GPUDataStore<CameraGPUData> m_cameras;
    GPUDataStore<ShadowGPUData> m_shadows;

    RenderContext m_renderContext;
    Scene *m_scene = nullptr;
    uint32_t m_frameCount = 0;

    struct SignalBridge;
    std::unique_ptr<SignalBridge> m_signalBridge;
};

} // namespace Rapture

#endif // RAPTURE__SCENERENDERDATA_H
