#pragma once

#include "AssetManager/AssetManager.h"
#include "Pipelines/GraphicsPipeline.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/Descriptors/DescriptorBinding.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"

#include "Renderer/Frustum/Frustum.h"

#include "Scenes/Scene.h"

#include "Components/Systems/ObjectDataBuffers/ShadowDataBuffer.h"

#include <glm/glm.hpp>
#include <memory>

namespace Rapture {

// Forward declarations
struct LightComponent;
struct TransformComponent;

class ShadowMap {
  public:
    ShadowMap(float width, float height);
    ~ShadowMap();

    void createPipeline();
    void createShadowTexture();
    void createUniformBuffers();

    void recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<Scene> activeScene,
                             uint32_t currentFrame);

    void updateViewMatrix(const LightComponent &lightComp, const TransformComponent &transformComp,
                          const glm::vec3 &cameraPosition);

    std::shared_ptr<Texture> getShadowTexture() const { return m_shadowTexture; }

    uint32_t getTextureHandle() { return m_shadowTexture->getBindlessIndex(); }

    glm::mat4 getLightViewProjection() const { return m_lightViewProjection; }

    std::shared_ptr<ShadowDataBuffer> getShadowDataBuffer() { return m_shadowDataBuffer; }

  private:
    void setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer);
    void beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer);
    void transitionToShaderReadableLayout(std::shared_ptr<CommandBuffer> commandBuffer);

  private:
    float m_width;
    float m_height;
    uint32_t m_currentFrame = 0;
    uint32_t m_framesInFlight = 3; // Default, will be updated

    glm::mat4 m_lightViewProjection;

    std::shared_ptr<Texture> m_shadowTexture;
    std::shared_ptr<ShadowDataBuffer> m_shadowDataBuffer;

    // Rendering attachments info
    VkRenderingAttachmentInfo m_depthAttachmentInfo{};

    Frustum m_frustum;

    std::weak_ptr<Shader> m_shader;
    AssetHandle m_handle;

    std::shared_ptr<GraphicsPipeline> m_pipeline;
    VmaAllocator m_allocator;
};

} // namespace Rapture