#ifndef RAPTURE__DEFERRED_RENDERER_H
#define RAPTURE__DEFERRED_RENDERER_H

#include "renderer/Renderer.h"

#include "core/jobs/Counter.h"
#include "renderer/RtInstanceData.h"
#include "renderer/deferred/GBufferPass.h"
#include "renderer/deferred/LightingPass.h"
#include "renderer/gi/ddgi/DynamicDiffuseGI.h"
#include "renderer/passes/GroundTruthAmbientOcclusionPass.h"
#include "renderer/passes/SkyboxPass.h"

#include <memory>

namespace Rapture {

struct StaticMeshComponent;
struct TransformComponent;
struct LightComponent;

class DeferredRenderer : public Renderer {

  public:
    DeferredRenderer(RenderContext renderContext, const RendererConfig &config);
    ~DeferredRenderer() override;

    const char *name() const override { return "Deferred"; }

    void recordSecondaries(const RenderPassContext &context, JobContext &jobContext) override;
    void replay(const RenderPassContext &context, CommandBuffer *primaryCb) override;
    void onResize(uint32_t width, uint32_t height) override;

    GBufferPass *getGBufferPass() { return m_gbufferPass.get(); }

  private:
    void recreateRenderPasses();

    /**
     * @brief Add this renderer's own textures to the context handed to every render pass
     * @param context The frame being drawn
     * @return The context this renderer's passes read
     */
    RenderPassContext buildPassContext(const RenderPassContext &context);

  private:
    std::unique_ptr<GBufferPass> m_gbufferPass;
    std::unique_ptr<GroundTruthAmbientOcclusionPass> m_ambientOcclusionPass;
    std::unique_ptr<LightingPass> m_lightingPass;
    std::unique_ptr<SkyboxPass> m_skyboxPass;

    RenderPassTargets m_passTargets;

    Counter m_cmdCounter{};

    CommandBuffer *m_gbufferCmdBuffer = nullptr;
    CommandBuffer *m_lightingCmdBuffer = nullptr;
    CommandBuffer *m_skyboxCmdBuffer = nullptr;

    std::unique_ptr<DynamicDiffuseGI> m_dynamicDiffuseGI;
    std::unique_ptr<RtInstanceData> m_rtInstanceData;

    bool m_giActive = true;
    uint32_t m_lightingFlags = RENDER_ALL;
};

} // namespace Rapture

#endif // RAPTURE__DEFERRED_RENDERER_H
