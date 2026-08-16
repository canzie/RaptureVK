#ifndef RAPTURE__SHADOWMAPPING_H
#define RAPTURE__SHADOWMAPPING_H

#include "assets/asset_manager/AssetManager.h"
#include "gpu/pipelines/GraphicsPipeline.h"
#include "gpu/shaders/Shader.h"
#include "gpu/textures/Texture.h"

#include "gpu/command_buffers/CommandBuffer.h"
#include "gpu/command_buffers/CommandPool.h"
#include "gpu/descriptors/DescriptorBinding.h"
#include "gpu/descriptors/DescriptorSet.h"

#include "renderer/Frustum.h"

#include "scene/Scene.h"

#include "gpu/vulkan_context/RenderContext.h"

#include "core/ecs/entity_accessor.h"

#include <glm/glm.hpp>
#include <memory>

namespace Rapture {

// Forward declarations
struct TransformComponent;

class ShadowMap {
  public:
    ShadowMap(float width, float height);
    ~ShadowMap();

    CommandBuffer *recordSecondary(Scene &activeScene, uint32_t currentFrame);
    void beginDynamicRendering(CommandBuffer *commandBuffer);
    void endDynamicRendering(CommandBuffer *commandBuffer);

    void updateViewMatrix(ecs::EntityAccessor light, const TransformComponent &transformComp, const glm::vec3 &cameraPosition);

    std::shared_ptr<Texture> getShadowTexture() const { return m_shadowTexture; }

    uint32_t getTextureHandle() { return m_shadowTexture->getBindlessIndex(); }

    glm::mat4 getLightViewProjection() const { return m_lightViewProjection; }

  private:
    void setupDynamicRenderingMemoryBarriers(CommandBuffer *commandBuffer);
    void transitionToShaderReadableLayout(CommandBuffer *commandBuffer);

    void createPipeline();
    void createShadowTexture();
    void setupCommandResources();

  private:
    const RenderContext *m_rc = nullptr;
    float m_width;
    float m_height;
    uint32_t m_currentFrame = 0;

    glm::mat4 m_lightViewProjection;

    std::shared_ptr<Texture> m_shadowTexture;

    // Rendering attachments info
    VkRenderingAttachmentInfo m_depthAttachmentInfo{};

    Frustum m_frustum;

    Shader *m_shader = nullptr;
    std::vector<AssetRef> m_shaderAssets;

    std::shared_ptr<GraphicsPipeline> m_pipeline;

    CommandPoolHash m_commandPoolHash = 0;
};

} // namespace Rapture

#endif // RAPTURE__SHADOWMAPPING_H
