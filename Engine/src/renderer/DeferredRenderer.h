#ifndef RAPTURE__DEFERRED_RENDERER_H
#define RAPTURE__DEFERRED_RENDERER_H

#include "renderer/Renderer.h"

#include "events/EventSignal.h"
#include "renderer/RtInstanceData.h"
#include "renderer/gi/ddgi/DynamicDiffuseGI.h"
#include "renderer/passes/CompositePass.h"
#include "renderer/passes/GBufferPass.h"
#include "renderer/passes/GroundTruthAmbientOcclusionPass.h"
#include "renderer/passes/InstancedShapesPass.h"
#include "renderer/passes/LightingPass.h"
#include "renderer/passes/SkyboxPass.h"
#include "renderer/passes/SelectionOutlinePass.h"

#include <memory>

namespace Rapture {

struct MeshComponent;
struct TransformComponent;
struct LightComponent;

class DeferredRenderer : public Renderer {

  public:
    DeferredRenderer(RenderContext renderContext, const RendererConfig &config);
    ~DeferredRenderer() override;

    void drawFrame(Scene &activeScene, Entity camera, const RenderSettings &settings) override;
    void onSwapChainRecreated() override;
    void resizeRenderTarget(uint32_t width, uint32_t height) override;
    EntityID pickEntity(uint32_t x, uint32_t y) override;

    GBufferPass *getGBufferPass() { return m_gbufferPass.get(); }

  private:
    void setupCommandResources();
    void createRenderTarget();
    void createSceneColorTextures();
    void recreateRenderPasses();
    void processPendingViewportResize();

    /**
     * @brief Build the per-frame context handed to every render pass
     * @param activeScene Scene being rendered
     * @param camera Camera the frame is rendered from
     * @param imageIndex Index of the render target image being written
     * @param settings Display overrides for this view
     * @return The context for this frame
     */
    RenderPassContext buildPassContext(Scene &activeScene, Entity camera, uint32_t imageIndex, const RenderSettings &settings);

    void recordCommandBuffer(CommandBuffer *commandBuffer, Scene &activeScene, Entity camera, uint32_t imageIndex,
                             const RenderSettings &settings);

  private:
    std::unique_ptr<GBufferPass> m_gbufferPass;
    std::unique_ptr<GroundTruthAmbientOcclusionPass> m_ambientOcclusionPass;
    std::unique_ptr<LightingPass> m_lightingPass;
    std::unique_ptr<SelectionOutlinePass> m_selectionOutlinePass;
    std::unique_ptr<SkyboxPass> m_skyboxPass;
    std::unique_ptr<InstancedShapesPass> m_instancedShapesPass;
    std::unique_ptr<CompositePass> m_compositePass;

    std::vector<std::unique_ptr<Texture>> m_sceneColorHdrTextures;
    RenderPassTargets m_passTargets;

    // Pending viewport resize (deferred to start of next frame)
    uint32_t m_pendingViewportWidth = 0;
    uint32_t m_pendingViewportHeight = 0;
    bool m_viewportResizePending = false;

    std::unique_ptr<DynamicDiffuseGI> m_dynamicDiffuseGI;
    std::unique_ptr<RtInstanceData> m_rtInstanceData;

    EventConnection m_swapchainRecreatedConn;

    bool m_giActive = true;
    uint32_t m_lightingFlags = RENDER_ALL;
};

} // namespace Rapture

#endif // RAPTURE__DEFERRED_RENDERER_H
