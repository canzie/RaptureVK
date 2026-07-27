#ifndef RAPTURE__LIGHTING_PASS_H
#define RAPTURE__LIGHTING_PASS_H

#include "pipelines/GraphicsPipeline.h"
#include "shaders/Shader.h"

#include "buffers/UniformBuffer.h"
#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/command_buffers/CommandPool.h"
#include "buffers/descriptors/DescriptorSet.h"
#include "scenes/Scene.h"
#include "scenes/entities/Entity.h"

#include "renderer/passes/RenderPass.h"
#include "textures/Texture.h"
#include <memory>

#include "renderer/gi/ddgi/DynamicDiffuseGI.h"

namespace Rapture {

class LightingPass : public RenderPass {
  public:
    LightingPass(float width, float height, DynamicDiffuseGI *ddgi, VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB);
    ~LightingPass();

    FramebufferSpecification getFramebufferSpecification();

    CommandBuffer *record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance) override;
    void onResize(uint32_t width, uint32_t height) override;

  protected:
    void updateAttachments(const RenderPassContext &context) override;

  private:
    void createPipeline();

  private:
    const RenderContext *m_rc = nullptr;
    Shader *m_shader = nullptr;
    std::vector<AssetRef> m_shaderAssets;

    VkFormat m_colorFormat;
    VmaAllocator m_vmaAllocator;
    VkDevice m_device;

    std::shared_ptr<GraphicsPipeline> m_pipeline;

    std::vector<std::shared_ptr<UniformBuffer>> m_lightUBOs;
    std::vector<std::shared_ptr<UniformBuffer>> m_shadowDataUBOs;
    std::vector<std::shared_ptr<DescriptorSet>> m_descriptorSets; // all sets are in set 0

    DynamicDiffuseGI *m_ddgi = nullptr;

    float m_width;
    float m_height;

    bool m_lightsChanged = true;
};

} // namespace Rapture

#endif // RAPTURE__LIGHTING_PASS_H
