#pragma once

#include "Shaders/Shader.h"
#include "Pipelines/GraphicsPipeline.h"
#include "Textures/Texture.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/VertexBuffers/VertexBuffer.h"
#include "Buffers/IndexBuffers/IndexBuffer.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "WindowContext/Application.h"

#include <memory>
#include <vector>

namespace Rapture {

class SkyboxPass {
public:
    SkyboxPass(std::shared_ptr<Texture> skyboxTexture, std::vector<std::shared_ptr<Texture>> depthTextures);
    SkyboxPass(std::vector<std::shared_ptr<Texture>> depthTextures);

    ~SkyboxPass();

    // NOTE: assumes that the command buffer is already started, and will be ended by the caller
    void recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, uint32_t frameInFlightIndex);

    // Set the skybox texture
    void setSkyboxTexture(std::shared_ptr<Texture> skyboxTexture);

    bool hasActiveSkybox() const { return m_skyboxTexture != nullptr; }

private:
    void createPipeline();
    void createSkyboxGeometry();

    void beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer, uint32_t frameInFlightIndex);
    void setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer, uint32_t frameInFlightIndex);

private:
    VkDevice m_device;
    VmaAllocator m_vmaAllocator;
    std::shared_ptr<SwapChain> m_swapChain;

    std::weak_ptr<Shader> m_shader;
    std::shared_ptr<GraphicsPipeline> m_pipeline;

    std::shared_ptr<Texture> m_skyboxTexture;
    std::vector<std::shared_ptr<Texture>> m_depthTextures;
    std::shared_ptr<VertexBuffer> m_skyboxVertexBuffer;
    std::shared_ptr<IndexBuffer> m_skyboxIndexBuffer;

    float m_width;
    float m_height;
};

} // namespace Rapture 

