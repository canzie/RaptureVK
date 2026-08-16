#ifndef RAPTURE__SCENE_GEOMETRY_DRAW_H
#define RAPTURE__SCENE_GEOMETRY_DRAW_H

#include "gpu/command_buffers/CommandBuffer.h"
#include "gpu/vulkan_context/RenderContext.h"
#include "renderer/MDIBatch.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Rapture {

class Frustum;
class Scene;

/**
 * @brief Gathers the scene's meshes into indirect draw batches for whoever wants to draw them
 *
 * Owns the batches for every frame in flight and the traversal that fills them, so a pass supplies
 * only a frustum and then issues its own draws. Which descriptor sets are bound, what the push
 * constants hold and how the batch is drawn stay with the pass, since those follow its shader.
 */
class SceneGeometryDraw {
  public:
    SceneGeometryDraw(RenderContext renderContext, uint32_t framesInFlight);

    SceneGeometryDraw(const SceneGeometryDraw &) = delete;
    SceneGeometryDraw &operator=(const SceneGeometryDraw &) = delete;

    /**
     * @brief Refill this frame's batches with the meshes a frustum keeps
     * @param scene Scene to traverse
     * @param frustum Frustum to cull against, or nullptr to keep every mesh
     * @param frameInFlight Frame whose batches are filled
     */
    void populate(Scene &scene, const Frustum *frustum, uint32_t frameInFlight);

    /**
     * @brief The batches filled for a frame, each holding at least one draw
     * @param frameInFlight Frame to read
     * @return The batches, in no particular order
     */
    std::span<MDIBatch *const> batches(uint32_t frameInFlight) const;

    /**
     * @brief Upload a batch and bind everything its draw needs except the pipeline and its constants
     * @param commandBuffer Buffer the draw is recorded into
     * @param batch Batch being drawn
     */
    void bindBatch(CommandBuffer *commandBuffer, MDIBatch *batch);

  private:
    RenderContext m_rc;

    std::vector<std::unique_ptr<MDIBatchMap>> m_batchMaps;
    std::vector<std::vector<MDIBatch *>> m_populatedBatches;
};

} // namespace Rapture

#endif // RAPTURE__SCENE_GEOMETRY_DRAW_H
