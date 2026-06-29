#ifndef RAPTURE__RENDERER_H
#define RAPTURE__RENDERER_H

#include "buffers/command_buffers/CommandPool.h"
#include "renderer/RenderSettings.h"
#include "render_targets/SceneRenderTarget.h"
#include "render_targets/swap_chains/SwapChain.h"
#include "scenes/entities/Entity.h"
#include "window_context/vulkan_context/RenderContext.h"
#include "window_context/vulkan_context/VulkanQueue.h"

#include <memory>

namespace Rapture {

class Scene;

class Renderer {
  public:
    Renderer(RenderContext renderContext, SceneRenderTarget::TargetType targetType);
    virtual ~Renderer() = default;

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    virtual void drawFrame(Scene &activeScene, Entity camera, const RenderSettings &settings) = 0;
    virtual void onSwapChainRecreated() = 0;

    /**
     * @brief Resize this renderer's render target to match its viewport.
     * @param width New target width in pixels.
     * @param height New target height in pixels.
     */
    virtual void resizeRenderTarget(uint32_t width, uint32_t height) = 0;

    SceneRenderTarget &getSceneRenderTarget() { return *m_sceneRenderTarget; }
    const SceneRenderTarget &getSceneRenderTarget() const { return *m_sceneRenderTarget; }
    uint32_t getCurrentFrame() const { return m_currentFrame; }

    /**
     * @brief Index of the render target slot most recently fully rendered.
     *
     * Only advances on an actual render, so consumers that sample the output
     * stay aligned even on frames where drawFrame early-returns.
     *
     * @return The slot index of the most recently rendered frame.
     */
    uint32_t getLastRenderedFrameIndex() const { return m_lastRenderedFrame; }
    SceneRenderTarget::TargetType getTargetType() const { return m_targetType; }

  protected:
    RenderContext m_renderContext;
    SceneRenderTarget::TargetType m_targetType;

    std::shared_ptr<SwapChain> m_swapChain;
    std::unique_ptr<SceneRenderTarget> m_sceneRenderTarget;

    std::shared_ptr<VulkanQueue> m_graphicsQueue;
    std::shared_ptr<VulkanQueue> m_presentQueue;

    CommandPoolHash m_commandPoolHash = 0;
    uint32_t m_currentFrame = 0;
    uint32_t m_lastRenderedFrame = 0;

    float m_width = 0.0f;
    float m_height = 0.0f;
    bool m_framebufferNeedsResize = false;
};

} // namespace Rapture

#endif // RAPTURE__RENDERER_H
