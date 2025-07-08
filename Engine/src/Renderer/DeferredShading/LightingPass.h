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

#include "RenderTargets/SwapChains/SwapChain.h"
#include "Renderer/DeferredShading/GBufferPass.h"
#include <memory>

#include "Renderer/GI/DDGI/DynamicDiffuseGI.h"

namespace Rapture {

struct FogSettings {
    bool enabled = true;
    glm::vec3 color = glm::vec3(0.5f, 0.6f, 0.7f);
    float nearDistance = 10.0f;
    float farDistance = 180.0f;
};

class LightingPass {
public:
    LightingPass(
        float width, 
        float height, 
        uint32_t framesInFlight, 
        std::shared_ptr<GBufferPass> gBufferPass,
        std::shared_ptr<DynamicDiffuseGI> ddgi);

    ~LightingPass();

    FramebufferSpecification getFramebufferSpecification();

    // NOTE: assumes that the command buffer is already started, and will be ended by the caller
    void recordCommandBuffer(
        std::shared_ptr<CommandBuffer> commandBuffer, 
        std::shared_ptr<Scene> activeScene,
        uint32_t swapchainImageIndex,
        uint32_t frameInFlightIndex
    );

    FogSettings& getFogSettings() { return m_fogSettings; }

private:
    void createPipeline();

    void beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer, uint32_t swapchainImageIndex);
    void setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer, uint32_t swapchainImageIndex);


private:
    std::weak_ptr<Shader> m_shader; 
    AssetHandle m_handle;

    uint32_t m_framesInFlight;
    uint32_t m_currentFrame;

    std::shared_ptr<SwapChain> m_swapChain;
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

    FogSettings m_fogSettings;

};

}