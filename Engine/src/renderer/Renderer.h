#ifndef RAPTURE__RENDERER_H
#define RAPTURE__RENDERER_H

#include "gpu/command_buffers/CommandBuffer.h"
#include "gpu/vulkan_context/RenderContext.h"
#include "renderer/passes/RenderPassContext.h"

#include <cstdint>

namespace Rapture {

struct JobContext;

/**
 * @brief Creation-time configuration for a renderer
 */
struct RendererConfig {
    uint32_t width;
    uint32_t height;
    uint32_t framesInFlight;
    VkFormat outputFormat;
    VkFormat sceneColorFormat;
    bool enableAccelerationStructures = true;
};

/**
 * @brief An ordered set of passes drawing into a frame a DrawManager owns
 */
class Renderer {
  public:
    Renderer(RenderContext renderContext, const RendererConfig &config);
    virtual ~Renderer() = default;

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    /**
     * @brief What this renderer is called in profiles and job listings
     * @return The name, a literal that outlives the renderer
     */
    virtual const char *name() const = 0;

    /**
     * @brief Record this renderer's secondary buffers
     *
     * Runs as a job, and is called for every renderer of a phase before any of them replays.
     *
     * @param context The frame being drawn
     * @param jobContext The job this runs on, which any nested wait has to yield through
     */
    virtual void recordSecondaries(const RenderPassContext &context, JobContext &jobContext) = 0;

    /**
     * @brief Replay what recordSecondaries produced into the frame
     *
     * Runs in phase order on the thread driving the frame, since the layout transitions it emits
     * are tracked as one linear sequence.
     *
     * @param context The frame being drawn
     * @param primaryCb Primary buffer the frame is recorded into
     */
    virtual void replay(const RenderPassContext &context, CommandBuffer *primaryCb) = 0;

    /**
     * @brief Rebuild what this renderer sizes to the target
     * @param width New target width in pixels
     * @param height New target height in pixels
     */
    virtual void onResize(uint32_t width, uint32_t height) = 0;

  protected:
    RenderContext m_renderContext;
    RendererConfig m_config;

    float m_width = 0.0f;
    float m_height = 0.0f;
};

} // namespace Rapture

#endif // RAPTURE__RENDERER_H
