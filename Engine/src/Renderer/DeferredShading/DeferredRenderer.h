#ifndef RAPTURE__DEFERRED_RENDERER_H
#define RAPTURE__DEFERRED_RENDERER_H

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/IndexBuffers/IndexBuffer.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/VertexBuffers/VertexBuffer.h"
#include "Cameras/CameraCommon.h"
#include "Components/Components.h"
#include "Materials/MaterialInstance.h"
#include "Pipelines/GraphicsPipeline.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "RenderTargets/SceneRenderTarget.h"
#include "Scenes/Scene.h"
#include "Shaders/Shader.h"
#include "WindowContext/VulkanContext/VulkanQueue.h"

#include "Renderer/DeferredShading/GBufferPass.h"
#include "Renderer/DeferredShading/LightingPass.h"
#include "Renderer/StencilBorderPass.h"
#include "Renderer/GI/DDGI/DynamicDiffuseGI.h"
#include "Renderer/SkyboxPass.h"
#include "Renderer/InstancedShapesPass.h"

#include <memory>
#include <vector>

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "Meshes/Mesh.h"

namespace Rapture {

// Forward declarations
struct MeshComponent;
struct TransformComponent;
struct LightComponent;

class DeferredRenderer {

public:
  static void init();
  static void shutdown();

  static void drawFrame(std::shared_ptr<Scene> activeScene);

  static void onSwapChainRecreated();

  /**
   * @brief Resize the render target (for Editor viewport resize, independent of window size)
   * Only effective when using offscreen render target.
   * @param width New width
   * @param height New height
   */

  // Getter for GBuffer pass
  static std::shared_ptr<GBufferPass> getGBufferPass() { return m_gbufferPass; }

  static std::shared_ptr<LightingPass> getLightingPass() { return m_lightingPass; }

  static std::shared_ptr<DynamicDiffuseGI> getDynamicDiffuseGI() { return m_dynamicDiffuseGI; }

  /**
   * @brief Get the scene render target (for ImGui to sample in Editor mode)
   * @return The scene render target
   */
  static std::shared_ptr<SceneRenderTarget> getSceneRenderTarget() { return m_sceneRenderTarget; }

  /**
   * @brief Get the current frame index for synchronization
   */
  static uint32_t getCurrentFrame() { return m_currentFrame; }

private:
  // sets up command pools and buffers
  static void setupCommandResources();
  static void createRenderTarget();
  static void recreateRenderPasses();
  static void processPendingViewportResize();

  static void recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer,
                                  std::shared_ptr<Scene> activeScene,
                                  uint32_t imageIndex);


private:
  static std::shared_ptr<GBufferPass> m_gbufferPass;
  static std::shared_ptr<LightingPass> m_lightingPass;
  static std::shared_ptr<StencilBorderPass> m_stencilBorderPass;
  static std::shared_ptr<SkyboxPass> m_skyboxPass;
  static std::shared_ptr<InstancedShapesPass> m_instancedShapesPass;

  static std::vector<std::shared_ptr<CommandBuffer>> m_commandBuffers;
  static std::shared_ptr<CommandPool> m_commandPool;


  static VmaAllocator m_vmaAllocator;
  static VkDevice m_device;
  static std::shared_ptr<SwapChain> m_swapChain;
  static std::shared_ptr<SceneRenderTarget> m_sceneRenderTarget;

  static uint32_t m_currentFrame;

  static std::shared_ptr<VulkanQueue> m_graphicsQueue;
  static std::shared_ptr<VulkanQueue> m_presentQueue;

  static bool m_framebufferNeedsResize;
  static float m_width;
  static float m_height;
  
  // Pending viewport resize (deferred to start of next frame)
  static uint32_t m_pendingViewportWidth;
  static uint32_t m_pendingViewportHeight;
  static bool m_viewportResizePending;

  static std::shared_ptr<DynamicDiffuseGI> m_dynamicDiffuseGI;
};

} // namespace Rapture

#endif // RAPTURE__DEFERRED_RENDERER_H