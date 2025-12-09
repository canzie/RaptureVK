#ifndef RAPTURE__INSTANCED_SHAPES_PASS_H
#define RAPTURE__INSTANCED_SHAPES_PASS_H

#include "Scenes/Scene.h"
#include "Pipelines/GraphicsPipeline.h"
#include "RenderTargets/SceneRenderTarget.h"
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
        std::vector<std::shared_ptr<Texture>> depthStencilTextures,
        VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB
    );
    ~InstancedShapesPass();

    void recordCommandBuffer(
        const std::shared_ptr<CommandBuffer>& commandBuffer,
        const std::shared_ptr<Scene>& scene,
        SceneRenderTarget& renderTarget,
        uint32_t imageIndex,
        uint32_t frameInFlight
    );

private:
    void createPipeline();
    void setupDynamicRenderingMemoryBarriers(const std::shared_ptr<CommandBuffer>& commandBuffer,
                                              VkImage targetImage);
    void beginDynamicRendering(const std::shared_ptr<CommandBuffer>& commandBuffer, 
                               VkImageView targetImageView,
                               VkExtent2D targetExtent);

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
    VkFormat m_colorFormat;
};

}

#endif // RAPTURE__INSTANCED_SHAPES_PASS_H