#pragma once

#include "Pipelines/GraphicsPipeline.h"
#include "AssetManager/AssetManager.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/Descriptors/BindlessDescriptorArray.h"

#include "Renderer/Frustum/Frustum.h"

#include "Scenes/Scene.h"

#include <memory>
#include <glm/glm.hpp>

namespace Rapture {

// Forward declarations
struct LightComponent;
struct TransformComponent;

class ShadowMap {
public:
    ShadowMap(float width, float height, std::shared_ptr<BindlessDescriptorArray> bindlessShadowMaps);
    ShadowMap(float width, float height);

    ~ShadowMap();

    void createPipeline();
    void createShadowTexture();
    void createUniformBuffers();
    void createDescriptorSets();
    
    void recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, 
                            std::shared_ptr<Scene> activeScene, 
                            uint32_t currentFrame);
    
    void updateViewMatrix(const LightComponent& lightComp, const TransformComponent& transformComp);

    std::shared_ptr<Texture> getShadowTexture() const { return m_shadowTexture; }

private:
    void setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer);
    void beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer);
    void transitionToShaderReadableLayout(std::shared_ptr<CommandBuffer> commandBuffer);

private:
    float m_width;
    float m_height;
    uint32_t m_currentFrame = 0;
    uint32_t m_framesInFlight = 3; // Default, will be updated

    std::shared_ptr<Texture> m_shadowTexture;
    std::shared_ptr<GraphicsPipeline> m_pipeline;
    std::vector<std::shared_ptr<UniformBuffer>> m_shadowUBOs;
    std::vector<std::shared_ptr<DescriptorSet>> m_descriptorSets;
    
    Frustum m_frustum;

    // Rendering attachments info
    VkRenderingAttachmentInfo m_depthAttachmentInfo{};
    
    std::weak_ptr<Shader> m_shader;
    AssetHandle m_handle;

    VmaAllocator m_allocator;

    uint32_t m_shadowMapIndex = 0;

    static std::unique_ptr<BindlessDescriptorSubAllocation> s_bindlessShadowMaps;
    
    };

}