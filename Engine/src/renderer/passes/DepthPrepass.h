#ifndef RAPTURE__DEPTH_PREPASS_H
#define RAPTURE__DEPTH_PREPASS_H

#include "assets/asset_manager/AssetManager.h"
#include "assets/shaders/AShader.h"
#include "gpu/command_buffers/CommandPool.h"
#include "gpu/pipelines/GraphicsPipeline.h"
#include "gpu/shaders/Shader.h"
#include "gpu/vulkan_context/RenderContext.h"
#include "renderer/passes/RenderPass.h"

#include <memory>

namespace Rapture {

/**
 * @brief Fills the shared depth buffer before anything shades against it
 */
class DepthPrepass : public RenderPass {
  public:
    DepthPrepass(float width, float height, VkFormat depthFormat);
    ~DepthPrepass();

    CommandBuffer *record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance) override;
    void onResize(uint32_t width, uint32_t height) override;

    FramebufferSpecification getFramebufferSpecification() const;

  protected:
    void updateAttachments(const RenderPassContext &context) override;

  private:
    /**
     * @brief Builds one of the two pipelines
     * @param skinned Whether the pipeline deforms its vertices by a skeleton pose
     */
    void createPipeline(bool skinned);

    void setupCommandResources();

  private:
    const RenderContext *m_rc = nullptr;

    Ref<AShader> m_shader;
    Ref<AShader> m_skinnedShader;

    // shared because DescriptorManager::bindSet takes ownership by shared_ptr
    std::shared_ptr<GraphicsPipeline> m_pipeline;
    std::shared_ptr<GraphicsPipeline> m_skinnedPipeline;

    float m_width;
    float m_height;
    VkFormat m_depthFormat;

    CommandPoolHash m_commandPoolHash = 0;
};

} // namespace Rapture

#endif // RAPTURE__DEPTH_PREPASS_H
