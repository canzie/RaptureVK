#ifndef RAPTURE__SHADOW_RENDERER_H
#define RAPTURE__SHADOW_RENDERER_H

#include "core/ecs/journal.h"
#include "renderer/Renderer.h"

#include <vector>

namespace Rapture {

class CascadedShadowMap;
class ShadowMap;

/**
 * @brief Renders the shadow map of every light that casts one
 *
 * Each map opens its own dynamic rendering scope against its own attachment, so this is a sequence
 * of recordings rather than one pass.
 */
class ShadowRenderer : public Renderer {
  public:
    ShadowRenderer(RenderContext renderContext, const RendererConfig &config);

    const char *name() const override { return "Shadow Maps"; }

    void recordSecondaries(const RenderPassContext &context, JobContext &jobContext) override;
    void replay(const RenderPassContext &context, CommandBuffer *primaryCb) override;
    void onResize(uint32_t width, uint32_t height) override;

  private:
    /**
     * @brief One shadow map and the buffer holding its drawing
     */
    template <typename T> struct RecordedShadow {
        T *map = nullptr;
        CommandBuffer *secondary = nullptr;
    };

  private:
    // this renderer's own position, so another view consuming the same change cannot hide it
    ecs::Bookmark m_transformBookmark;
    ecs::Bookmark m_lightBookmark;
    ecs::Bookmark m_settingsBookmark;

    std::vector<RecordedShadow<ShadowMap>> m_recordedMaps;
    std::vector<RecordedShadow<CascadedShadowMap>> m_recordedCascades;
};

} // namespace Rapture

#endif // RAPTURE__SHADOW_RENDERER_H
