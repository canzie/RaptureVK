#ifndef RAPTURE__SCENE_QUERY_RENDERER_H
#define RAPTURE__SCENE_QUERY_RENDERER_H

#include "assets/asset_manager/AssetManager.h"
#include "core/ecs/entity_accessor.h"
#include "gpu/buffers/StorageBuffer.h"
#include "gpu/command_buffers/CommandPool.h"
#include "gpu/vulkan_context/RenderContext.h"
#include "renderer/SceneGeometryDraw.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Rapture {

class CommandBuffer;
class Frustum;
class GraphicsPipeline;
class GizmoDrawList;
class Scene;
class Shader;

/**
 * @brief The screen region a scene query covers
 *
 * A one pixel region is an exact point query; widening it makes the region itself the tolerance, at
 * a cost bounded by the region rather than by the viewport.
 */
struct SceneQuery {
    uint32_t x = 0; ///< region origin in viewport pixels
    uint32_t y = 0;
    uint32_t width = 1; ///< region size in viewport pixels
    uint32_t height = 1;
    uint32_t maxLayers = 4; ///< entries kept per pixel
};

/**
 * @brief One thing covering one pixel of a scene query
 *
 * The user data is whatever whoever drew the thing decided it should be, and this reports it back
 * without reading it. Geometry the renderer drew answers with its entity, which is the only
 * identity the renderer holds for it.
 */
struct SceneQueryHit {
    uint64_t userData = 0;
    float depth = 0.0f;
};

/**
 * @brief Everything covering every pixel of a scene query
 *
 * A pixel keeps up to maxLayers hits, nearest first, so cycling through what sits under the cursor
 * and outlining something behind another thing both read from the same result. A pixel's count is
 * the number of fragments that covered it, and so reports how many were dropped when it exceeds
 * maxLayers.
 */
struct SceneQueryResult {
  public:
    /**
     * @brief The entities covering one pixel of the region, nearest first
     * @param x Pixel x within the region
     * @param y Pixel y within the region
     * @return The pixel's hits, empty where nothing was drawn or the pixel lies outside the region
     */
    std::span<const SceneQueryHit> at(uint32_t x, uint32_t y) const;

    /**
     * @brief Whether a pixel was covered by more entities than were kept
     * @param x Pixel x within the region
     * @param y Pixel y within the region
     * @return True when hits were dropped at that pixel
     */
    bool overflowed(uint32_t x, uint32_t y) const;

  public:
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t maxLayers = 0;
    std::vector<uint32_t> counts;    ///< fragments that covered each pixel, may exceed maxLayers
    std::vector<SceneQueryHit> hits; ///< maxLayers per pixel, the first counts[pixel] of them valid
};

/**
 * @brief Renders the scene for a screen region and reports which entities cover each of its pixels
 *
 * Out of band rather than a pass in the frame: it renders when asked, into no attachment at all, and
 * blocks until the answer is back. Depth is written into the per pixel list instead of being tested,
 * so nothing an entity hides is lost.
 */
class SceneQueryRenderer {
  public:
    explicit SceneQueryRenderer(RenderContext renderContext);
    ~SceneQueryRenderer();

    SceneQueryRenderer(const SceneQueryRenderer &) = delete;
    SceneQueryRenderer &operator=(const SceneQueryRenderer &) = delete;

    /**
     * @brief Render a region and read back what covers it
     *
     * Blocks until the GPU is done, so it belongs on the thread driving the editor and never on a
     * job worker.
     *
     * @param scene Scene to render
     * @param camera Camera the region's pixels belong to
     * @param viewportWidth Width of the image the region's pixels are in
     * @param viewportHeight Height of the image the region's pixels are in
     * @param region The region to cover
     * @param drawList Shapes drawn over the scene, whose pick candidates answer alongside it, or nullptr for none
     * @return The region's hits, empty where the query could not be rendered
     */
    SceneQueryResult query(Scene &scene, ecs::EntityAccessor camera, uint32_t viewportWidth, uint32_t viewportHeight,
                           const SceneQuery &region, const GizmoDrawList *drawList = nullptr);

  private:
    void createPipeline();
    bool resizeBuffers(uint32_t pixelCount, uint32_t maxLayers);
    void recordQuery(CommandBuffer *commandBuffer, Scene &scene, const glm::mat4 &viewProj, uint32_t viewportWidth,
                     uint32_t viewportHeight, const SceneQuery &region);
    SceneQueryResult readBack(const SceneQuery &region);

    /**
     * @brief Where a region's pixel centre sends a ray into the world
     */
    struct PixelRay {
        glm::vec3 origin;
        glm::vec3 direction;
    };

    /**
     * @brief A ray per pixel of a region, in the order the region's pixels are stored
     * @param inverseViewProj Takes clip space back to the world
     * @param viewportWidth Width of the image the region's pixels are in
     * @param viewportHeight Height of the image the region's pixels are in
     * @param region The region the rays cover
     * @return The rays
     */
    static std::vector<PixelRay> buildRegionRays(const glm::mat4 &inverseViewProj, uint32_t viewportWidth,
                                                 uint32_t viewportHeight, const SceneQuery &region);

    /**
     * @brief Adds the hits a draw list's pick candidates cover a region's pixels with
     * @param[in,out] result The region's hits, which the candidates' are merged into by depth
     * @param drawList The shapes to test
     * @param regionFrustum Frustum covering the region, which a candidate outside cannot be hit through
     * @param viewProj Places a hit at the depth the scene's own hits are measured in
     * @param rays One ray per pixel of the region
     */
    static void addPickCandidateHits(SceneQueryResult &result, const GizmoDrawList &drawList,
                                     const Frustum &regionFrustum, const glm::mat4 &viewProj,
                                     const std::vector<PixelRay> &rays);

  private:
    RenderContext m_rc;

    Shader *m_shader = nullptr;
    std::vector<AssetRef> m_shaderAssets;
    std::shared_ptr<GraphicsPipeline> m_pipeline;

    std::unique_ptr<SceneGeometryDraw> m_geometry;

    std::unique_ptr<StorageBuffer> m_countBuffer;
    std::unique_ptr<StorageBuffer> m_entryBuffer;
    uint32_t m_pixelCapacity = 0;
    uint32_t m_entryCapacity = 0;

    CommandPoolHash m_commandPoolHash = 0;
    VkFence m_fence = VK_NULL_HANDLE;
};

} // namespace Rapture

#endif // RAPTURE__SCENE_QUERY_RENDERER_H
