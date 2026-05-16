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

    void drawFrame(std::shared_ptr<Scene> activeScene, Entity camera) override;
    void onSwapChainRecreated() override;

    std::shared_ptr<GBufferPass> getGBufferPass() { return m_gbufferPass; }

  private:
    void setupCommandResources();
    void createRenderTarget();
    void recreateRenderPasses();
    void processPendingViewportResize();

    void recordCommandBuffer(CommandBuffer *commandBuffer, std::shared_ptr<Scene> activeScene, Entity camera, uint32_t imageIndex);

  private:
    std::shared_ptr<GBufferPass> m_gbufferPass;
    std::shared_ptr<LightingPass> m_lightingPass;
    std::shared_ptr<StencilBorderPass> m_stencilBorderPass;
    std::shared_ptr<SkyboxPass> m_skyboxPass;
    std::shared_ptr<InstancedShapesPass> m_instancedShapesPass;

    // Pending viewport resize (deferred to start of next frame)
    uint32_t m_pendingViewportWidth = 0;
    uint32_t m_pendingViewportHeight = 0;
    bool m_viewportResizePending = false;

    std::shared_ptr<DynamicDiffuseGI> m_dynamicDiffuseGI;
    std::shared_ptr<RtInstanceData> m_rtInstanceData;
};

} // namespace Rapture

#endif // RAPTURE__DEFERRED_RENDERER_H
