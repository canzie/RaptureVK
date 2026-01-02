#ifndef RAPTURE__INSTANCED_SHAPES_PASS_H
#define RAPTURE__INSTANCED_SHAPES_PASS_H

#include "AssetManager/Asset.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Pipelines/GraphicsPipeline.h"
#include "RenderTargets/SceneRenderTarget.h"
#include "Scenes/Scene.h"
#include "Textures/Texture.h"

#include <memory>
#include <vector>

namespace Rapture {

class Shader;

class InstancedShapesPass {
  public:
    InstancedShapesPass(float width, float height, uint32_t framesInFlight,
                        std::vector<std::shared_ptr<Texture>> depthStencilTextures, VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB);
    ~InstancedShapesPass();

    CommandBuffer *recordSecondary(const std::shared_ptr<Scene> &scene, SceneRenderTarget &renderTarget, uint32_t frameInFlight,
                                   const SecondaryBufferInheritance &inheritance);

    void beginDynamicRendering(CommandBuffer *commandBuffer, SceneRenderTarget &renderTarget, uint32_t imageIndex, uint32_t frameInFlightIndex);
    void endDynamicRendering(CommandBuffer *commandBuffer);

  private:
    void createPipeline();
    void setupDynamicRenderingMemoryBarriers(CommandBuffer *commandBuffer, VkImage targetImage, uint32_t frameInFlightIndex);

  private:
    float m_width;
    float m_height;
    uint32_t m_framesInFlight;

    std::vector<std::shared_ptr<Texture>> m_depthStencilTextures;
    std::weak_ptr<Shader> m_shader;
    AssetHandle m_shaderHandle;

    std::shared_ptr<GraphicsPipeline> m_pipelineFilled;
    std::shared_ptr<GraphicsPipeline> m_pipelineWireframe;

    VkDevice m_device;
    VmaAllocator m_vmaAllocator;
    VkFormat m_colorFormat;
};

} // namespace Rapture

#endif // RAPTURE__INSTANCED_SHAPES_PASS_H