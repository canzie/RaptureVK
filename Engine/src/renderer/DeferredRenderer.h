#ifndef RAPTURE__DEFERRED_RENDERER_H
#define RAPTURE__DEFERRED_RENDERER_H

#include "renderer/Renderer.h"

#include "renderer/RtInstanceData.h"
#include "renderer/gi/ddgi/DynamicDiffuseGI.h"
#include "renderer/passes/GBufferPass.h"
#include "renderer/passes/InstancedShapesPass.h"
#include "renderer/passes/LightingPass.h"
#include "renderer/passes/SkyboxPass.h"
#include "renderer/passes/StencilBorderPass.h"

#include <memory>

namespace Rapture {

struct MeshComponent;
struct TransformComponent;
struct LightComponent;

class DeferredRenderer : public Renderer {

  public:
    DeferredRenderer(RenderContext renderContext, SceneRenderTarget::TargetType targetType);
    ~DeferredRenderer() override;

    void drawFrame(Scene &activeScene, Entity camera) override;
    void onSwapChainRecreated() override;

    GBufferPass *getGBufferPass() { return m_gbufferPass.get(); }

  private:
    void setupCommandResources();
    void createRenderTarget();
    void recreateRenderPasses();
    void processPendingViewportResize();

    void recordCommandBuffer(CommandBuffer *commandBuffer, Scene &activeScene, Entity camera, uint32_t imageIndex);

  private:
    std::unique_ptr<GBufferPass> m_gbufferPass;
    std::unique_ptr<LightingPass> m_lightingPass;
    std::unique_ptr<StencilBorderPass> m_stencilBorderPass;
    std::unique_ptr<SkyboxPass> m_skyboxPass;
    std::unique_ptr<InstancedShapesPass> m_instancedShapesPass;

    // Pending viewport resize (deferred to start of next frame)
    uint32_t m_pendingViewportWidth = 0;
    uint32_t m_pendingViewportHeight = 0;
    bool m_viewportResizePending = false;

    std::unique_ptr<DynamicDiffuseGI> m_dynamicDiffuseGI;
    std::unique_ptr<RtInstanceData> m_rtInstanceData;
};

} // namespace Rapture

#endif // RAPTURE__DEFERRED_RENDERER_H
