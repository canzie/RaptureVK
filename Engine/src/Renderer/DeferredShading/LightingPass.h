#ifndef RAPTURE__LIGHTING_PASS_H
#define RAPTURE__LIGHTING_PASS_H

#include "Pipelines/GraphicsPipeline.h"
#include "Shaders/Shader.h"

#include "AssetManager/AssetManager.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Cameras/CameraCommon.h"
#include "Components/Components.h"
#include "Scenes/Scene.h"
#include "Textures/Texture.h"
#include "WindowContext/VulkanContext/VulkanContext.h"

#include "RenderTargets/SceneRenderTarget.h"
#include "Renderer/DeferredShading/GBufferPass.h"
#include <memory>

#include "Renderer/GI/DDGI/DynamicDiffuseGI.h"

namespace Rapture {

class LightingPass {
  public:
    LightingPass(float width, float height, uint32_t framesInFlight, std::shared_ptr<GBufferPass> gBufferPass,
                 std::shared_ptr<DynamicDiffuseGI> ddgi, VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB);
    ~LightingPass();

    void beginDynamicRendering(CommandBuffer *commandBuffer, SceneRenderTarget &renderTarget, uint32_t imageIndex);
    void endDynamicRendering(CommandBuffer *commandBuffer);

    FramebufferSpecification getFramebufferSpecification();

    CommandBuffer *recordSecondary(std::shared_ptr<Scene> activeScene, SceneRenderTarget &renderTarget, uint32_t frameInFlightIndex,
                                   const SecondaryBufferInheritance &inheritance);

  private:
    void createPipeline();
    void setupCommandResources();

    void setupDynamicRenderingMemoryBarriers(CommandBuffer *commandBuffer, VkImage targetImage);

  private:
    std::weak_ptr<Shader> m_shader;
    AssetHandle m_handle;

    uint32_t m_framesInFlight;
    uint32_t m_currentFrame;

    VkFormat m_colorFormat;
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

    CommandPoolHash m_commandPoolHash = 0;
};

} // namespace Rapture

#endif // RAPTURE__LIGHTING_PASS_H