#pragma once

#include "Shaders/Shader.h"
#include "Pipelines/GraphicsPipeline.h"

#include "AssetManager/AssetManager.h"
#include "Scenes/Scene.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Cameras/CameraCommon.h"
#include "Textures/Texture.h"
#include "WindowContext/VulkanContext/VulkanContext.h"
#include "Components/Components.h"
#include "Events/GameEvents.h"

#include <memory>

namespace Rapture {

class GBufferPass {
public:
    GBufferPass(float width, float height, uint32_t framesInFlight, std::vector<std::shared_ptr<UniformBuffer>> cameraUBOs);
    ~GBufferPass();

    static FramebufferSpecification getFramebufferSpecification();

    // NOTE: assumes that the command buffer is already started, and will be ended by the caller
    void recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<Scene> activeScene, uint32_t currentFrame);

    // Getters for current frame's GBuffer textures
    std::shared_ptr<Texture> getPositionTexture() const { return m_positionDepthTextures[m_currentFrame]; }
    std::shared_ptr<Texture> getNormalTexture() const { return m_normalTextures[m_currentFrame]; }
    std::shared_ptr<Texture> getAlbedoTexture() const { return m_albedoSpecTextures[m_currentFrame]; }
    std::shared_ptr<Texture> getMaterialTexture() const { return m_materialTextures[m_currentFrame]; }
    std::shared_ptr<Texture> getDepthTexture() const { return m_depthStencilTextures[m_currentFrame]; }

    std::vector<std::shared_ptr<Texture>> getPositionDepthTextures() const { return m_positionDepthTextures; }
    std::vector<std::shared_ptr<Texture>> getNormalTextures() const { return m_normalTextures; }
    std::vector<std::shared_ptr<Texture>> getAlbedoSpecTextures() const { return m_albedoSpecTextures; }
    std::vector<std::shared_ptr<Texture>> getMaterialTextures() const { return m_materialTextures; }
    std::vector<std::shared_ptr<Texture>> getDepthTextures() const { return m_depthStencilTextures; }



private:
    void createTextures();
    void createPipeline();
    void createDescriptorSets(uint32_t framesInFlight);

    void beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer);

    void setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer);

    void transitionToShaderReadableLayout(std::shared_ptr<CommandBuffer> commandBuffer);



private:
    std::weak_ptr<Shader> m_shader; 
    AssetHandle m_handle;
    float m_width;
    float m_height;
    uint32_t m_framesInFlight;
    uint32_t m_currentFrame;

    VmaAllocator m_vmaAllocator;
    VkDevice m_device;

    // Multiple textures for each frame in flight
    std::vector<std::shared_ptr<Texture>> m_positionDepthTextures;
    std::vector<std::shared_ptr<Texture>> m_normalTextures;
    std::vector<std::shared_ptr<Texture>> m_albedoSpecTextures;
    std::vector<std::shared_ptr<Texture>> m_materialTextures;
    std::vector<std::shared_ptr<Texture>> m_depthStencilTextures;

    std::shared_ptr<GraphicsPipeline> m_pipeline;

    VkRenderingAttachmentInfo m_colorAttachmentInfo[4];
    VkRenderingAttachmentInfo m_depthAttachmentInfo;

    std::vector<std::shared_ptr<UniformBuffer>> m_cameraUBOs;
    std::vector<std::shared_ptr<DescriptorSet>> m_descriptorSets;

    bool m_isFirstFrame = true;

    std::shared_ptr<Entity> m_selectedEntity;
    size_t m_entitySelectedListenerId;
};

}