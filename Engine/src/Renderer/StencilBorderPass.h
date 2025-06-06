#pragma once

#include "Pipelines/GraphicsPipeline.h"
#include "Shaders/Shader.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Textures/Texture.h"
#include "Events/GameEvents.h"
#include "Scenes/Entities/Entity.h"
#include "AssetManager/AssetManager.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "Cameras/CameraCommon.h"
#include "Buffers/Descriptors/DescriptorSet.h"

#include <memory>
#include <vector>
#include "glm/glm.hpp"

namespace Rapture {

class StencilBorderPass {
public:
    StencilBorderPass(float width, float height, 
        uint32_t framesInFlight, 
        std::vector<std::shared_ptr<Texture>> depthStencilTextures,
        std::vector<std::shared_ptr<UniformBuffer>> cameraUBOs);

    ~StencilBorderPass();

    void recordCommandBuffer(
        std::shared_ptr<CommandBuffer> commandBuffer,
        uint32_t swapchainImageIndex,
        uint32_t currentFrameInFlight,
        std::shared_ptr<Scene> activeScene
    );

private:
    void createPipeline();
    void createDescriptorSets(uint32_t framesInFlight);

    void beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer);

    void setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer);

private:
    float m_width;
    float m_height;
    std::shared_ptr<SwapChain> m_swapChain;
    VkDevice m_device;
    VmaAllocator m_vmaAllocator;

    std::shared_ptr<GraphicsPipeline> m_pipeline;

    uint32_t m_framesInFlight;
    uint32_t m_currentImageIndex; // Current frame in flight


    std::weak_ptr<Shader> m_shader;
    AssetHandle m_shaderHandle;

    std::vector<std::shared_ptr<Texture>> m_depthStencilTextures;

    std::vector<std::shared_ptr<DescriptorSet>> m_descriptorSets;
    std::vector<std::shared_ptr<UniformBuffer>> m_cameraUBOs;



    std::shared_ptr<Entity> m_selectedEntity;
    size_t m_entitySelectedListenerId;
};

} // namespace Rapture
