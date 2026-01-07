#ifndef RAPTURE__STENCIL_BORDER_PASS_H
#define RAPTURE__STENCIL_BORDER_PASS_H

#include "AssetManager/AssetManager.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Cameras/CameraCommon.h"
#include "Events/GameEvents.h"
#include "Pipelines/GraphicsPipeline.h"
#include "RenderTargets/SceneRenderTarget.h"
#include "Scenes/Entities/Entity.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"

#include "glm/glm.hpp"
#include <memory>
#include <vector>

namespace Rapture {

class StencilBorderPass {
  public:
    StencilBorderPass(float width, float height, uint32_t framesInFlight,
                      std::vector<std::shared_ptr<Texture>> depthStencilTextures, VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB);

    ~StencilBorderPass();

    CommandBuffer *recordSecondary(SceneRenderTarget &renderTarget, uint32_t currentFrameInFlight,
                                   std::shared_ptr<Scene> activeScene, const SecondaryBufferInheritance &inheritance);

    void beginDynamicRendering(CommandBuffer *commandBuffer, SceneRenderTarget &renderTarget, uint32_t imageIndex);
    void endDynamicRendering(CommandBuffer *commandBuffer);

  private:
    void createPipeline();
    void setupCommandResources();

    void setupDynamicRenderingMemoryBarriers(CommandBuffer *commandBuffer, VkImage targetImage);

  private:
    float m_width;
    float m_height;
    VkFormat m_colorFormat;
    VkDevice m_device;
    VmaAllocator m_vmaAllocator;

    std::shared_ptr<GraphicsPipeline> m_pipeline;

    uint32_t m_framesInFlight;
    uint32_t m_currentImageIndex;

    Shader *m_shader = nullptr;
    std::vector<AssetRef> m_shaderAssets;

    std::vector<std::shared_ptr<Texture>> m_depthStencilTextures;

    std::shared_ptr<Entity> m_selectedEntity;
    size_t m_entitySelectedListenerId = 0;

    CommandPoolHash m_commandPoolHash = 0;
};

} // namespace Rapture

#endif // RAPTURE__STENCIL_BORDER_PASS_H
