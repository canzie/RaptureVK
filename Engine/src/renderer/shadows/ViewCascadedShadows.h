#ifndef RAPTURE__VIEW_CASCADED_SHADOWS_H
#define RAPTURE__VIEW_CASCADED_SHADOWS_H

#include <rapture/core/ecs/entity_accessor.h>
#include <rapture/gpu/buffers/StorageBuffer.h>
#include <rapture/gpu/vulkan_context/RenderContext.h>
#include <rapture/renderer/GPUDataStructs.h>

#include <glm/glm.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

namespace Rapture {

class CascadedShadowMap;
class Scene;

/**
 * @brief The cascaded shadow maps of one view, one per directional light that casts them, fitted to that view's camera
 */
class ViewCascadedShadows {
  public:
    /**
     * @brief Creates the per frame buffers the cascades are handed to the lighting through
     * @param renderContext The device the maps and buffers live on
     * @param framesInFlight How many frames can be in flight at once
     */
    ViewCascadedShadows(const RenderContext &renderContext, uint32_t framesInFlight);
    ~ViewCascadedShadows();

    ViewCascadedShadows(const ViewCascadedShadows &) = delete;
    ViewCascadedShadows &operator=(const ViewCascadedShadows &) = delete;

    /**
     * @brief Matches the maps to the scene's cascaded lights, fits each to a camera and uploads them for a frame
     * @param scene The scene being drawn
     * @param camera The camera the view is drawn from
     * @param frameIndex The frame in flight being drawn
     */
    void update(Scene &scene, ecs::EntityAccessor camera, uint32_t frameIndex);

    /**
     * @brief The maps fitted by the last update, in no particular order
     * @return One map per cascaded light
     */
    const std::vector<CascadedShadowMap *> &getMaps() const { return m_fittedMaps; }

    /**
     * @brief Where the camera sat when the cascades were last fitted
     * @return The world space position
     */
    const glm::vec3 &getCameraPosition() const { return m_cameraPosition; }

    /**
     * @brief The shadow data buffer the lighting reads this view's cascades from
     * @param frameIndex The frame in flight
     * @return Its index in the shadow data binding
     */
    uint32_t getDescriptorIndex(uint32_t frameIndex) const { return m_descriptorIndices[frameIndex]; }

    /**
     * @brief How many entries the last update wrote into the shadow data buffer
     * @return The entry count
     */
    uint32_t getShadowCount() const { return m_shadowCount; }

  private:
    /**
     * @brief Drops every map, waiting for the GPU first since submitted frames may still sample them
     */
    void releaseMaps();

  private:
    RenderContext m_renderContext;

    Scene *m_scene = nullptr;
    std::unordered_map<ecs::Entity, std::unique_ptr<CascadedShadowMap>> m_maps;
    std::vector<CascadedShadowMap *> m_fittedMaps;
    std::vector<ecs::Entity> m_unseenLights;

    std::vector<std::unique_ptr<StorageBuffer>> m_buffers;
    std::vector<uint32_t> m_descriptorIndices;
    std::vector<ShadowGPUData> m_shadowData;
    uint32_t m_shadowCount = 0;

    glm::vec3 m_cameraPosition{0.0f};
};

} // namespace Rapture

#endif // RAPTURE__VIEW_CASCADED_SHADOWS_H
