#ifndef RAPTURE__STENCIL_BORDER_PASS_H
#define RAPTURE__STENCIL_BORDER_PASS_H

#include "asset_manager/AssetManager.h"
#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/command_buffers/CommandPool.h"
#include "buffers/descriptors/DescriptorSet.h"
#include "buffers/UniformBuffer.h"
#include "cameras/CameraCommon.h"
#include "events/GameEvents.h"
#include "pipelines/GraphicsPipeline.h"
#include "render_targets/SceneRenderTarget.h"
#include "scenes/entities/Entity.h"
#include "shaders/Shader.h"
#include "textures/Texture.h"

#include "window_context/vulkan_context/RenderContext.h"

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
                                   std::shared_ptr<Scene> activeScene, Entity camera, const SecondaryBufferInheritance &inheritance);

    void beginDynamicRendering(CommandBuffer *commandBuffer, SceneRenderTarget &renderTarget, uint32_t imageIndex);
    void endDynamicRendering(CommandBuffer *commandBuffer);

  private:
    void createPipeline();
    void setupCommandResources();

    void setupDynamicRenderingMemoryBarriers(CommandBuffer *commandBuffer, VkImage targetImage);

  private:
    const RenderContext *m_rc = nullptr;
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
