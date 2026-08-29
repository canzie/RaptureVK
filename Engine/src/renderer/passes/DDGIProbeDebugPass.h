#ifndef RAPTURE__DDGI_PROBE_DEBUG_PASS_H
#define RAPTURE__DDGI_PROBE_DEBUG_PASS_H

#include "assets/asset_manager/Asset.h"
#include "assets/shaders/AShader.h"
#include "gpu/command_buffers/CommandBuffer.h"
#include "gpu/pipelines/GraphicsPipeline.h"
#include "gpu/shaders/Shader.h"
#include "gpu/vulkan_context/RenderContext.h"
#include "renderer/passes/RenderPass.h"

#include <cstdint>
#include <memory>

namespace Rapture {

class DynamicDiffuseGI;

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
    DDGIProbeDebugPass(const DDGIProbeDebugPassConfig &config, const DynamicDiffuseGI *gi);
    ~DDGIProbeDebugPass();

    CommandBuffer *record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance) override;
    void onResize(uint32_t width, uint32_t height) override;

    SecondaryBufferInheritance getInheritance(const RenderPassContext &context) override;

  protected:
    void updateAttachments(const RenderPassContext &context) override;

  private:
    void createPipeline();

  private:
    const DynamicDiffuseGI *m_gi;
    DDGIProbeDebugPassConfig m_config;

    const RenderContext *m_rc = nullptr;

    Ref<AShader> m_shader;
    std::shared_ptr<GraphicsPipeline> m_pipeline;

    float m_width;
    float m_height;
};

} // namespace Rapture

#endif // RAPTURE__DDGI_PROBE_DEBUG_PASS_H
