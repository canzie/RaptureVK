#ifndef RAPTURE__DDGI_PROBE_DEBUG_PASS_H
#define RAPTURE__DDGI_PROBE_DEBUG_PASS_H

#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/assets/shaders/AShader.h>
#include <rapture/gpu/command_buffers/CommandBuffer.h>
#include <rapture/gpu/pipelines/GraphicsPipeline.h>
#include <rapture/gpu/shaders/Shader.h>
#include <rapture/gpu/vulkan_context/RenderContext.h>
#include <rapture/renderer/passes/RenderPass.h>

#include <cstdint>
#include <memory>

namespace Rapture {


/**
 * @brief Creation-time configuration for a probe debug pass
 */
struct DDGIProbeDebugPassConfig {
    uint32_t width;
    uint32_t height;
    VkFormat colorFormat;
    VkFormat depthFormat;
};

/**
 * @brief Draws one sphere per probe of a volume over the rendered image
 */
class DDGIProbeDebugPass : public RenderPass {
  public:
    DDGIProbeDebugPass(const DDGIProbeDebugPassConfig &config);
    ~DDGIProbeDebugPass();

    CommandBufferLease record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance) override;
    void onResize(uint32_t width, uint32_t height) override;

    SecondaryBufferInheritance getInheritance(const RenderPassContext &context) override;

  protected:
    void updateAttachments(const RenderPassContext &context) override;

  private:
    void createPipeline();

  private:
    DDGIProbeDebugPassConfig m_config;

    const RenderContext *m_rc = nullptr;

    Ref<AShader> m_shader;
    std::shared_ptr<GraphicsPipeline> m_pipeline;

    float m_width;
    float m_height;
};

} // namespace Rapture

#endif // RAPTURE__DDGI_PROBE_DEBUG_PASS_H
