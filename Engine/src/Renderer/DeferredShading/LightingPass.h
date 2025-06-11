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

namespace Rapture {

class LightingPass {
public:
    LightingPass(float width, float height, uint32_t framesInFlight, std::shared_ptr<GBufferPass> gBufferPass, std::vector<std::shared_ptr<UniformBuffer>> shadowDataUBOs);
    ~LightingPass();

    FramebufferSpecification getFramebufferSpecification();

    // NOTE: assumes that the command buffer is already started, and will be ended by the caller
    void recordCommandBuffer(
        std::shared_ptr<CommandBuffer> commandBuffer, 
        std::shared_ptr<Scene> activeScene,
        uint32_t swapchainImageIndex,
        uint32_t frameInFlightIndex
    );


private:
    void createPipeline();
    void createDescriptorSets(uint32_t framesInFlight);
    void updateLightUBOs(std::shared_ptr<Scene> activeScene);
    void createLightUBOs(uint32_t framesInFlight);

    void beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer);
    void setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer);


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

    float m_width;
    float m_height;

    bool m_lightsChanged = true;

};

}