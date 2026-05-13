#ifndef RAPTURE__SKYBOX_PASS_H
#define RAPTURE__SKYBOX_PASS_H

#include "asset_manager/Asset.h"
#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/command_buffers/CommandPool.h"
#include "buffers/descriptors/DescriptorSet.h"
#include "buffers/IndexBuffer.h"
#include "buffers/UniformBuffer.h"
#include "buffers/VertexBuffer.h"
#include "pipelines/GraphicsPipeline.h"
#include "render_targets/SceneRenderTarget.h"
#include "shaders/Shader.h"
#include "textures/Texture.h"
#include "window_context/Application.h"

#include <memory>
#include <vector>

namespace Rapture {

class SkyboxPass {
  public:
    SkyboxPass(std::vector<std::shared_ptr<Texture>> depthTextures, VkFormat colorFormat);

    ~SkyboxPass();

    CommandBuffer *recordSecondary(SceneRenderTarget &renderTarget, uint32_t frameInFlightIndex,
                                   const SecondaryBufferInheritance &inheritance);

    void beginDynamicRendering(CommandBuffer *commandBuffer, SceneRenderTarget &renderTarget, uint32_t imageIndex,
                               uint32_t frameInFlightIndex);
    void endDynamicRendering(CommandBuffer *commandBuffer);

    void setSkyboxTexture(Texture *skyboxTexture);

    bool hasActiveSkybox() const { return m_skyboxTexture != nullptr; }

  private:
    void createPipeline();
    void createSkyboxGeometry();

    void setupDynamicRenderingMemoryBarriers(CommandBuffer *commandBuffer, VkImage targetImage, VkImage depthImage);

  private:
    const RenderContext *m_rc = nullptr;
    VkDevice m_device;
    VmaAllocator m_vmaAllocator;

    Shader *m_shader = nullptr;
    std::vector<AssetRef> m_shaderAssets;
    std::shared_ptr<GraphicsPipeline> m_pipeline;

    Texture *m_skyboxTexture;
    std::vector<std::shared_ptr<Texture>> m_depthTextures;
    std::shared_ptr<VertexBuffer> m_skyboxVertexBuffer;
    std::shared_ptr<IndexBuffer> m_skyboxIndexBuffer;

    float m_width;
    float m_height;
    VkFormat m_colorFormat;
};

} // namespace Rapture

#endif // RAPTURE__SKYBOX_PASS_H
