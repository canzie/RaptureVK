#pragma once

#include "Shaders/Shader.h"
#include "Pipelines/GraphicsPipeline.h"

#include "AssetManager/AssetManager.h"
#include "Scenes/Scene.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Cameras/CameraCommon.h"

#include <memory>

namespace Rapture {

class GBufferPass {
public:
    GBufferPass(float width, float height, uint32_t framesInFlight);
    ~GBufferPass();

    static FramebufferSpecification getFramebufferSpecification();

    // NOTE: assumes that the command buffer is already started, and will be ended by the caller
    void recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<Scene> activeScene, uint32_t currentFrame);


private:
    void createTextures();
    void createPipeline();
    void createDescriptorSets(uint32_t framesInFlight);
    void createCameraUBO(uint32_t framesInFlight);

    void beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer);

    void setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer);

    void transitionToShaderReadableLayout(std::shared_ptr<CommandBuffer> commandBuffer);

private:
    std::weak_ptr<Shader> m_shader; 
    AssetHandle m_handle;
    float m_width;
    float m_height;

    VmaAllocator m_vmaAllocator;

    VkDevice m_device;

    std::shared_ptr<Texture> m_positionDepthTexture;
    std::shared_ptr<Texture> m_normalTexture;
    std::shared_ptr<Texture> m_albedoSpecTexture;
    std::shared_ptr<Texture> m_materialTexture;
    std::shared_ptr<Texture> m_depthStencilTexture;

    std::shared_ptr<GraphicsPipeline> m_pipeline;

    VkRenderingAttachmentInfoKHR m_colorAttachmentInfo[4];
    VkRenderingAttachmentInfoKHR m_depthAttachmentInfo;

    std::vector<std::shared_ptr<UniformBuffer>> m_cameraUBOs;
    std::vector<std::shared_ptr<DescriptorSet>> m_descriptorSets;
    std::vector<CameraUniformBufferObject> m_cameraUBOData;

};

}