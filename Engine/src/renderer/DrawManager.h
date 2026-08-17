#ifndef RAPTURE__DRAW_MANAGER_H
#define RAPTURE__DRAW_MANAGER_H

#include "core/ecs/entity_accessor.h"
#include "core/events/EventSignal.h"
#include "gpu/command_buffers/CommandPool.h"
#include "gpu/render_targets/SceneRenderTarget.h"
#include "gpu/swap_chains/SwapChain.h"
#include "gpu/vulkan_context/RenderContext.h"
#include "gpu/vulkan_context/VulkanQueue.h"
#include "renderer/SceneGeometryDraw.h"
#include "renderer/passes/CompositePass.h"
#include "renderer/passes/RenderPassContext.h"

#include <array>
#include <memory>
#include <vector>

namespace Rapture {

class Renderer;
class Scene;
class TerrainGenerator;
struct RenderSettings;

/**
 * @brief Where in the frame a renderer runs, relative to the composite
 */
enum DrawPhase {
    DRAW_PHASE_PRE_COMPOSITE,  ///< draws into the HDR scene colour, and is tonemapped
    DRAW_PHASE_POST_COMPOSITE, ///< draws into the output image, in the colours it wrote
    DRAW_PHASE_COUNT
};

/**
 * @brief Creation-time configuration for a draw manager and the target it draws into
 */
struct DrawManagerConfig {
    SceneRenderTarget::TargetType targetType;
    uint32_t width;
    uint32_t height;
    uint32_t framesInFlight;
    bool allowReadback = false;
};

/**
 * @brief Draws one view, running an ordered set of renderers into a single submission
 *
 * Owns what a view has one of no matter how many renderers draw into it: the target, the frame
 * index, the primary command buffer and the submission. A renderer records into that frame and
 * decides nothing about it.
 */
class DrawManager {
  public:
    DrawManager(RenderContext renderContext, const DrawManagerConfig &config);
    ~DrawManager();

    DrawManager(const DrawManager &) = delete;
    DrawManager &operator=(const DrawManager &) = delete;

    /**
     * @brief Put a renderer at an order within a phase, which has to be free
     *
     * Renderers in a phase run from lowest order to highest, so leaving gaps is what lets one be
     * slotted between two others later.
     *
     * @param renderer The renderer to take ownership of
     * @param phase The phase it runs in
     * @param order Its position within that phase
     */
    void addRenderer(std::unique_ptr<Renderer> renderer, DrawPhase phase, uint32_t order);

    /**
     * @brief Put a renderer at an order within a phase, replacing whatever holds it
     * @param renderer The renderer to take ownership of
     * @param phase The phase it runs in
     * @param order Its position within that phase
     */
    void setRenderer(std::unique_ptr<Renderer> renderer, DrawPhase phase, uint32_t order);

    /**
     * @brief Record and submit one frame of every renderer
     * @param scene Scene being drawn
     * @param camera Camera the frame is drawn from
     * @param settings Display overrides for this view
     */
    void drawFrame(Scene &scene, ecs::EntityAccessor camera, const RenderSettings &settings);

    /**
     * @brief Resize the target and everything the renderers size to it, at the start of the next frame
     * @param width New target width in pixels
     * @param height New target height in pixels
     */
    void resize(uint32_t width, uint32_t height);

    void onSwapChainRecreated();

    SceneRenderTarget &getSceneRenderTarget() { return *m_sceneRenderTarget; }
    const SceneRenderTarget &getSceneRenderTarget() const { return *m_sceneRenderTarget; }

    /**
     * @brief Format of the images drawn into, which a renderer's final pass has to match
     * @return The target's format
     */
    VkFormat getOutputFormat() const { return m_sceneRenderTarget->getFormat(); }

    /**
     * @brief Format of the depth buffer the renderers of this view share
     * @return The format, UNDEFINED before the depth buffer is built
     */
    VkFormat getDepthFormat() const;

    /**
     * @brief Format of the HDR scene colour the renderers of this view draw into
     * @return The format, UNDEFINED before the scene colour is built
     */
    VkFormat getSceneColorFormat() const;

    uint32_t getCurrentFrame() const { return m_currentFrame; }

    /**
     * @brief Index of the render target slot most recently fully drawn
     *
     * Only advances on an actual draw, so consumers that sample the output stay aligned even on
     * frames where drawFrame early-returns.
     *
     * @return The slot index of the most recently drawn frame
     */
    uint32_t getLastRenderedFrameIndex() const { return m_lastRenderedFrame; }

  private:
    void createRenderTarget();
    void setupCommandResources();
    void applyPendingResize();

    /**
     * @brief Build the depth buffer and HDR scene colour every renderer of this view shares
     */
    void createSharedTextures();

    /**
     * @brief Batch this view's opaque geometry for the frame
     *
     * A renderer drawing a different set, such as only the transparent materials, batches its own.
     *
     * @param scene Scene being drawn
     * @param camera Camera the frame is drawn from
     */
    void populateOpaqueGeometry(Scene &scene, ecs::EntityAccessor camera);

    /**
     * @brief Record every renderer of one phase, in order
     * @param phase The phase to run
     * @param context The frame being drawn
     * @param commandBuffer Primary buffer the frame is recorded into
     */
    void recordPhase(DrawPhase phase, const RenderPassContext &context, CommandBuffer *commandBuffer);

    /**
     * @brief Tonemap the HDR scene colour into the output image
     * @param context The frame being drawn
     * @param commandBuffer Primary buffer the frame is recorded into
     */
    void recordComposite(const RenderPassContext &context, CommandBuffer *commandBuffer);

    /**
     * @brief The terrain of a scene
     * @param scene Scene being drawn
     * @return The generator, or nullptr where the scene has none ready
     */
    static TerrainGenerator *findTerrain(Scene &scene);

    /**
     * @brief Whether any phase holds a renderer
     * @return True if there is anything to draw
     */
    bool hasRenderers() const;

    /**
     * @brief Take the image this frame draws into
     * @param imageIndex Receives the index of the image
     * @return True if an image was taken, false if the swapchain could not give one
     */
    bool acquireImage(uint32_t &imageIndex);

    void recordFrame(CommandBuffer *commandBuffer, Scene &scene, ecs::EntityAccessor camera, uint32_t imageIndex,
                     const RenderSettings &settings);

    /**
     * @brief Submit the frame, presenting it where the target is the swapchain
     * @param commandBuffer The recorded primary buffer
     * @param imageIndex Index of the image that was drawn into
     * @return True if the frame reached the queue, false if the swapchain has to be recreated first
     */
    bool submitFrame(CommandBuffer *commandBuffer, uint32_t imageIndex);

  private:
    RenderContext m_renderContext;
    DrawManagerConfig m_config;

    /**
     * @brief One renderer and where it sits in its phase
     */
    struct RendererSlot {
        uint32_t order = 0;
        std::unique_ptr<Renderer> renderer;
    };

    std::array<std::vector<RendererSlot>, DRAW_PHASE_COUNT> m_renderers; ///< each phase kept sorted by order

    std::shared_ptr<SwapChain> m_swapChain;
    std::unique_ptr<SceneRenderTarget> m_sceneRenderTarget;

    std::vector<std::unique_ptr<Texture>> m_depthTextures;
    std::vector<std::unique_ptr<Texture>> m_sceneColorHdrTextures;
    std::unique_ptr<CompositePass> m_compositePass;
    std::unique_ptr<SceneGeometryDraw> m_opaqueGeometry;
    RenderPassTargets m_sharedTargets;

    std::shared_ptr<VulkanQueue> m_graphicsQueue;
    std::shared_ptr<VulkanQueue> m_presentQueue;

    CommandPoolHash m_commandPoolHash = 0;
    uint32_t m_currentFrame = 0;
    uint32_t m_lastRenderedFrame = 0;

    uint32_t m_pendingWidth = 0;
    uint32_t m_pendingHeight = 0;
    bool m_resizePending = false;
    bool m_swapChainNeedsResize = false;

    EventConnection m_swapChainRecreatedConn;
};

} // namespace Rapture

#endif // RAPTURE__DRAW_MANAGER_H
