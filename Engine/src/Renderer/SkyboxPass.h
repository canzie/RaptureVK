#ifndef RAPTURE__SKYBOX_PASS_H
#define RAPTURE__SKYBOX_PASS_H

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/IndexBuffers/IndexBuffer.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/VertexBuffers/VertexBuffer.h"
#include "Pipelines/GraphicsPipeline.h"
#include "RenderTargets/SceneRenderTarget.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"
#include "WindowContext/Application.h"

#include <memory>
#include <vector>

namespace Rapture {

class SkyboxPass {
  public:
    SkyboxPass(std::shared_ptr<Texture> skyboxTexture, std::vector<std::shared_ptr<Texture>> depthTextures, VkFormat colorFormat);
    SkyboxPass(std::vector<std::shared_ptr<Texture>> depthTextures, VkFormat colorFormat);

    ~SkyboxPass();

    CommandBuffer *recordSecondary(SceneRenderTarget &renderTarget, uint32_t frameInFlightIndex,
                                   const SecondaryBufferInheritance &inheritance);

    void beginDynamicRendering(CommandBuffer *commandBuffer, SceneRenderTarget &renderTarget, uint32_t imageIndex,
                               uint32_t frameInFlightIndex);
    void endDynamicRendering(CommandBuffer *commandBuffer);

    void setSkyboxTexture(std::shared_ptr<Texture> skyboxTexture);

    bool hasActiveSkybox() const { return m_skyboxTexture != nullptr; }

  private:
    void createPipeline();
    void createSkyboxGeometry();
    void setupCommandResources();

    void setupDynamicRenderingMemoryBarriers(CommandBuffer *commandBuffer, VkImage targetImage, VkImage depthImage);

  private:
    VkDevice m_device;
    VmaAllocator m_vmaAllocator;

    std::weak_ptr<Shader> m_shader;
    std::shared_ptr<GraphicsPipeline> m_pipeline;

    std::shared_ptr<Texture> m_skyboxTexture;
    std::vector<std::shared_ptr<Texture>> m_depthTextures;
    std::shared_ptr<VertexBuffer> m_skyboxVertexBuffer;
    std::shared_ptr<IndexBuffer> m_skyboxIndexBuffer;

    float m_width;
    float m_height;
    VkFormat m_colorFormat;

    CommandPoolHash m_commandPoolHash = 0;
};

} // namespace Rapture

#endif // RAPTURE__SKYBOX_PASS_H
