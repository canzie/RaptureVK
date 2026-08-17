#ifndef RAPTURE__RENDER_PASS_CONTEXT_H
#define RAPTURE__RENDER_PASS_CONTEXT_H

#include "core/ecs/entity_accessor.h"

#include <cstdint>

namespace Rapture {

class Scene;
class SceneGeometryDraw;
class SceneRenderTarget;
class TerrainGenerator;
class Texture;
struct RenderSettings;

/**
 * @brief The textures passes hand to one another, owned by the renderer and refilled on resize
 *
 * A producing pass renders into these, a consuming pass reads them instead of holding a pointer to
 * the pass that produced them. Bindless indices come from Texture::getBindlessIndex.
 */
struct RenderPassTargets {
    Texture *gbufferNormalMotion = nullptr;
    Texture *gbufferBaseColor = nullptr;
    Texture *gbufferMaterial = nullptr;
    Texture *gbufferShadingModel = nullptr;
    Texture *depthStencil = nullptr;

    Texture *sceneColorHdr = nullptr;
    Texture *ambientOcclusion = nullptr;
};

/**
 * @brief Per-frame inputs every render pass reads from
 *
 * Filled once by the renderer at the top of a frame and passed to every pass, which takes only the
 * fields it needs.
 */
struct RenderPassContext {
    Scene *scene = nullptr;
    ecs::EntityAccessor camera;
    SceneRenderTarget *renderTarget = nullptr;
    const RenderPassTargets *targets = nullptr;
    const RenderSettings *settings = nullptr;
    TerrainGenerator *terrain = nullptr;
    SceneGeometryDraw *opaqueGeometry = nullptr; ///< this view's batched opaque geometry, populated for this frame

    uint32_t frameInFlight = 0; ///< indexes per-frame-in-flight resources
    uint32_t imageIndex = 0;    ///< image of the render target being written
};

} // namespace Rapture

#endif // RAPTURE__RENDER_PASS_CONTEXT_H
