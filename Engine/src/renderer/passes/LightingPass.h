#ifndef RAPTURE__LIGHTING_PASS_H
#define RAPTURE__LIGHTING_PASS_H

#include "pipelines/GraphicsPipeline.h"
#include "shaders/Shader.h"

#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/command_buffers/CommandPool.h"
#include "buffers/descriptors/DescriptorSet.h"
#include "buffers/UniformBuffer.h"
#include "scenes/Scene.h"
#include "scenes/entities/Entity.h"

#include "render_targets/SceneRenderTarget.h"
#include "renderer/passes/GBufferPass.h"
#include <memory>

#include "renderer/gi/ddgi/DynamicDiffuseGI.h"

namespace Rapture {

class LightingPass {
  public:
    LightingPass(float width, float height, std::shared_ptr<GBufferPass> gBufferPass, std::shared_ptr<DynamicDiffuseGI> ddgi,
                 VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB);
    ~LightingPass();

    void beginDynamicRendering(CommandBuffer *commandBuffer, SceneRenderTarget &renderTarget, uint32_t imageIndex);
    void endDynamicRendering(CommandBuffer *commandBuffer);

    FramebufferSpecification getFramebufferSpecification();

    CommandBuffer *recordSecondary(std::shared_ptr<Scene> activeScene, Entity camera, SceneRenderTarget &renderTarget,
                                   const SecondaryBufferInheritance &inheritance);

  private:
    void createPipeline();

    void setupDynamicRenderingMemoryBarriers(CommandBuffer *commandBuffer, VkImage targetImage);

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

    std::shared_ptr<GBufferPass> m_gBufferPass;

    std::shared_ptr<DynamicDiffuseGI> m_ddgi;

    float m_width;
    float m_height;

    bool m_lightsChanged = true;
};

} // namespace Rapture

#endif // RAPTURE__LIGHTING_PASS_H
