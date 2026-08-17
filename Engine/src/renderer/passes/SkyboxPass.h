#ifndef RAPTURE__SKYBOX_PASS_H
#define RAPTURE__SKYBOX_PASS_H

#include "assets/asset_manager/Asset.h"
#include "gpu/buffers/IndexBuffer.h"
#include "gpu/buffers/UniformBuffer.h"
#include "gpu/buffers/VertexBuffer.h"
#include "gpu/command_buffers/CommandBuffer.h"
#include "gpu/command_buffers/CommandPool.h"
#include "gpu/descriptors/DescriptorSet.h"
#include "gpu/pipelines/GraphicsPipeline.h"
#include "renderer/passes/RenderPass.h"
#include "gpu/shaders/Shader.h"
#include "gpu/textures/Texture.h"
#include "app/Application.h"

#include "core/ecs/entity_accessor.h"

#include <memory>
#include <vector>

namespace Rapture {

class Scene;

class SkyboxPass : public RenderPass {
  public:
    SkyboxPass(VkFormat depthFormat, VkFormat colorFormat);

    ~SkyboxPass();

    CommandBuffer *record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance) override;
    void onResize(uint32_t width, uint32_t height) override;

  protected:
    void updateAttachments(const RenderPassContext &context) override;

  public:
    void setSkyboxTexture(Texture *skyboxTexture);

    /**
     * @brief Scales the sampled sky radiance, matching what the DDGI miss ray applies
     * @param intensity The multiplier
     */
    void setSkyIntensity(float intensity) { m_skyIntensity = intensity; }

    bool hasActiveSkybox() const { return m_skyboxTexture != nullptr; }

  private:
    void createPipeline();
    void createSkyboxGeometry();

  private:
    const RenderContext *m_rc = nullptr;
    VkDevice m_device;
    VmaAllocator m_vmaAllocator;

    Shader *m_shader = nullptr;
    std::vector<AssetRef> m_shaderAssets;
    std::shared_ptr<GraphicsPipeline> m_pipeline;

    Texture *m_skyboxTexture;
    float m_skyIntensity = 1.0f;
    VkFormat m_depthFormat;
    std::shared_ptr<VertexBuffer> m_skyboxVertexBuffer;
    std::shared_ptr<IndexBuffer> m_skyboxIndexBuffer;

    float m_width;
    float m_height;
    VkFormat m_colorFormat;
};

} // namespace Rapture

#endif // RAPTURE__SKYBOX_PASS_H
