#pragma once

#include "Scenes/Scene.h"
#include "Pipelines/GraphicsPipeline.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "Textures/Texture.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "AssetManager/Asset.h"

#include <memory>
#include <vector>

namespace Rapture {

class Shader;

class InstancedShapesPass {
public:
    InstancedShapesPass(
        float width, float height,
        uint32_t framesInFlight,
        std::vector<std::shared_ptr<Texture>> depthStencilTextures
    );
    ~InstancedShapesPass();

    void recordCommandBuffer(
        const std::shared_ptr<CommandBuffer>& commandBuffer,
        const std::shared_ptr<Scene>& scene,
        uint32_t imageIndex,
        uint32_t frameInFlight
    );

private:
    void createPipeline();
    void beginDynamicRendering(const std::shared_ptr<CommandBuffer>& commandBuffer, uint32_t imageIndex);
    void setupDynamicRenderingMemoryBarriers(const std::shared_ptr<CommandBuffer>& commandBuffer, uint32_t imageIndex);

private:
    float m_width;
    float m_height;
    uint32_t m_framesInFlight;
    uint32_t m_currentImageIndex = 0;

    std::vector<std::shared_ptr<Texture>> m_depthStencilTextures;
    std::weak_ptr<Shader> m_shader;
    AssetHandle m_shaderHandle;

    std::shared_ptr<GraphicsPipeline> m_pipelineFilled;
    std::shared_ptr<GraphicsPipeline> m_pipelineWireframe;

    VkDevice m_device;
    VmaAllocator m_vmaAllocator;
    std::shared_ptr<SwapChain> m_swapChain;
};

}