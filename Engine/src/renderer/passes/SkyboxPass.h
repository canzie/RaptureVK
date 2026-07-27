#ifndef RAPTURE__SKYBOX_PASS_H
#define RAPTURE__SKYBOX_PASS_H

#include "asset_manager/Asset.h"
#include "buffers/IndexBuffer.h"
#include "buffers/UniformBuffer.h"
#include "buffers/VertexBuffer.h"
#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/command_buffers/CommandPool.h"
#include "buffers/descriptors/DescriptorSet.h"
#include "pipelines/GraphicsPipeline.h"
#include "renderer/passes/RenderPass.h"
#include "shaders/Shader.h"
#include "textures/Texture.h"
#include "window_context/Application.h"

#include "scenes/entities/Entity.h"

#include <memory>
#include <vector>

namespace Rapture {

class Scene;

class SkyboxPass : public RenderPass {
  public:
    SkyboxPass(std::vector<Texture *> depthTextures, VkFormat colorFormat);

    ~SkyboxPass();

    CommandBuffer *record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance) override;
    void onResize(uint32_t width, uint32_t height) override;

    void beginRendering(const RenderPassContext &context, CommandBuffer *primaryCb) override;

  protected:
    void updateAttachments(const RenderPassContext &context) override;

  public:
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
    std::vector<Texture *> m_depthTextures;
    std::shared_ptr<VertexBuffer> m_skyboxVertexBuffer;
    std::shared_ptr<IndexBuffer> m_skyboxIndexBuffer;

    float m_width;
    float m_height;
    VkFormat m_colorFormat;
};

} // namespace Rapture

#endif // RAPTURE__SKYBOX_PASS_H
