#ifndef RAPTURE__STENCIL_BORDER_PASS_H
#define RAPTURE__STENCIL_BORDER_PASS_H

#include "Pipelines/GraphicsPipeline.h"
#include "Shaders/Shader.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Textures/Texture.h"
#include "Events/GameEvents.h"
#include "Scenes/Entities/Entity.h"
#include "AssetManager/AssetManager.h"
#include "RenderTargets/SceneRenderTarget.h"
#include "Cameras/CameraCommon.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"

#include <memory>
#include <vector>
#include "glm/glm.hpp"

namespace Rapture {

class StencilBorderPass {
public:
    StencilBorderPass(float width, float height, 
        uint32_t framesInFlight, 
        std::vector<std::shared_ptr<Texture>> depthStencilTextures,
        VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB);

    ~StencilBorderPass();

    void recordCommandBuffer(
        std::shared_ptr<CommandBuffer> commandBuffer,
        SceneRenderTarget& renderTarget,
        uint32_t imageIndex,
        uint32_t currentFrameInFlight,
        std::shared_ptr<Scene> activeScene
    );

private:
    void createPipeline();

    void setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer,
                                              VkImage targetImage);
    void beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer,
                               VkImageView targetImageView,
                               VkExtent2D targetExtent);

private:
    float m_width;
    float m_height;
    VkFormat m_colorFormat;
    VkDevice m_device;
    VmaAllocator m_vmaAllocator;

    std::shared_ptr<GraphicsPipeline> m_pipeline;

    uint32_t m_framesInFlight;
    uint32_t m_currentImageIndex;


    std::weak_ptr<Shader> m_shader;
    AssetHandle m_shaderHandle;

    std::vector<std::shared_ptr<Texture>> m_depthStencilTextures;

    std::shared_ptr<Entity> m_selectedEntity;
    size_t m_entitySelectedListenerId = 0;
};

} // namespace Rapture

#endif // RAPTURE__STENCIL_BORDER_PASS_H
