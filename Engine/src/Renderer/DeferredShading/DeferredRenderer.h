#pragma once

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/IndexBuffers/IndexBuffer.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/VertexBuffers/VertexBuffer.h"
#include "Cameras/CameraCommon.h"
#include "Components/Components.h"
#include "Materials/MaterialInstance.h"
#include "Pipelines/GraphicsPipeline.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "Scenes/Scene.h"
#include "Shaders/Shader.h"
#include "WindowContext/VulkanContext/VulkanQueue.h"

#include "Renderer/DeferredShading/GBufferPass.h"
#include "Renderer/DeferredShading/LightingPass.h"
#include "Renderer/StencilBorderPass.h"
#include "Renderer/GI/DDGI/DynamicDiffuseGI.h"
#include "Renderer/GI/RadianceCascades/RadianceCascades.h"
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

  // Getter for GBuffer pass
  static std::shared_ptr<GBufferPass> getGBufferPass() { return m_gbufferPass; }

  static std::shared_ptr<LightingPass> getLightingPass() { return m_lightingPass; }

  static std::shared_ptr<DynamicDiffuseGI> getDynamicDiffuseGI() { return m_dynamicDiffuseGI; }

  static std::shared_ptr<RadianceCascades> getRadianceCascades() { return m_radianceCascades; }

private:
  // sets up command pools and buffers
  static void setupCommandResources();

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

  static std::shared_ptr<Shader> m_shader;


  static VmaAllocator m_vmaAllocator;
  static VkDevice m_device;
  static std::shared_ptr<SwapChain> m_swapChain;

  static uint32_t m_currentFrame;

  static std::shared_ptr<VulkanQueue> m_graphicsQueue;
  static std::shared_ptr<VulkanQueue> m_presentQueue;

  static bool m_framebufferNeedsResize;
  static float m_width;
  static float m_height;

  static std::shared_ptr<DynamicDiffuseGI> m_dynamicDiffuseGI;
  static std::shared_ptr<RadianceCascades> m_radianceCascades;
};

} // namespace Rapture
