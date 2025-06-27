#include "DeferredRenderer.h"

#include "Buffers/CommandBuffers/CommandPool.h"
#include "WindowContext/Application.h"

#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"

#include "Events/ApplicationEvents.h"
#include "Renderer/Shadows/ShadowCommon.h"


namespace Rapture {

// Maximum number of lights supported
static constexpr uint32_t MAX_LIGHTS = 16;

struct PushConstants {
  glm::mat4 model = glm::mat4(1.0f);
  glm::vec3 camPos = glm::vec3(0.0f);
};

// Static member definitions
std::vector<std::shared_ptr<CommandBuffer>> DeferredRenderer::m_commandBuffers;
std::shared_ptr<CommandPool> DeferredRenderer::m_commandPool = nullptr;
std::shared_ptr<Shader> DeferredRenderer::m_shader = nullptr;
VmaAllocator DeferredRenderer::m_vmaAllocator = VK_NULL_HANDLE;
VkDevice DeferredRenderer::m_device = VK_NULL_HANDLE;
std::shared_ptr<SwapChain> DeferredRenderer::m_swapChain = nullptr;
uint32_t DeferredRenderer::m_currentFrame = 0;
std::shared_ptr<VulkanQueue> DeferredRenderer::m_graphicsQueue = nullptr;
std::shared_ptr<VulkanQueue> DeferredRenderer::m_presentQueue = nullptr;
// PASSES
std::shared_ptr<GBufferPass> DeferredRenderer::m_gbufferPass = nullptr;
std::shared_ptr<LightingPass> DeferredRenderer::m_lightingPass = nullptr;
std::shared_ptr<StencilBorderPass> DeferredRenderer::m_stencilBorderPass = nullptr;
std::shared_ptr<SkyboxPass> DeferredRenderer::m_skyboxPass = nullptr;


float DeferredRenderer::m_width = 0.0f;
float DeferredRenderer::m_height = 0.0f;
bool DeferredRenderer::m_framebufferNeedsResize = false;
std::shared_ptr<DynamicDiffuseGI> DeferredRenderer::m_dynamicDiffuseGI = nullptr;

void DeferredRenderer::init() {

  auto &app = Application::getInstance();
  auto &vc = app.getVulkanContext();

  m_device = vc.getLogicalDevice();
  m_swapChain = vc.getSwapChain();
  m_vmaAllocator = vc.getVmaAllocator();

  m_graphicsQueue = vc.getGraphicsQueue();
  m_presentQueue = vc.getPresentQueue();

  m_width = static_cast<float>(m_swapChain->getExtent().width);
  m_height = static_cast<float>(m_swapChain->getExtent().height);


  setupCommandResources();

  m_dynamicDiffuseGI = std::make_shared<DynamicDiffuseGI>(m_swapChain->getImageCount());


  m_gbufferPass = std::make_shared<GBufferPass>(
      static_cast<float>(m_swapChain->getExtent().width),
      static_cast<float>(m_swapChain->getExtent().height),
      m_swapChain->getImageCount());

  m_lightingPass = std::make_shared<LightingPass>(
      static_cast<float>(m_swapChain->getExtent().width),
      static_cast<float>(m_swapChain->getExtent().height),
      m_swapChain->getImageCount(), m_gbufferPass, m_dynamicDiffuseGI);

  m_stencilBorderPass = std::make_shared<StencilBorderPass>(
      static_cast<float>(m_swapChain->getExtent().width),
      static_cast<float>(m_swapChain->getExtent().height),
      m_swapChain->getImageCount(), m_gbufferPass->getDepthTextures());


  m_skyboxPass = std::make_shared<SkyboxPass>(m_gbufferPass->getDepthTextures());

  ApplicationEvents::onWindowResize().addListener(
      [](unsigned int width, unsigned int height) {
        m_framebufferNeedsResize = true;
      });

  ApplicationEvents::onSwapChainRecreated().addListener(
      [](std::shared_ptr<SwapChain> swapChain) { onSwapChainRecreated(); });


}

void DeferredRenderer::shutdown() {
    // Wait for device to finish operations
    vkDeviceWaitIdle(m_device);

    m_skyboxPass.reset();
    m_stencilBorderPass.reset();
    m_lightingPass.reset();
    m_gbufferPass.reset();


    // Clean up command buffers and pool
    m_commandBuffers.clear();
    m_commandPool.reset();

    // Clean up queues
    m_graphicsQueue.reset();
    m_presentQueue.reset();

    // Clean up swapchain
    m_swapChain.reset();
}

void DeferredRenderer::drawFrame(std::shared_ptr<Scene> activeScene) {

  RAPTURE_PROFILE_FUNCTION();

  int imageIndexi = m_swapChain->acquireImage(m_currentFrame);

  if (imageIndexi == -1) {
    return;
  }

  uint32_t imageIndex = static_cast<uint32_t>(imageIndexi);

  m_dynamicDiffuseGI->populateProbesCompute(activeScene, m_currentFrame);

  m_commandBuffers[m_currentFrame]->reset();
  recordCommandBuffer(m_commandBuffers[m_currentFrame], activeScene,
                      imageIndex);

  m_graphicsQueue->addCommandBuffer(m_commandBuffers[m_currentFrame]);

 // --- BEGIN SUBMISSION LOGIC FOR FORWARD RENDERER (COMMON TO BOTH MODES) ---
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore frWaitSemaphores[1]; // Semaphores FR's submission waits on
  VkPipelineStageFlags frWaitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  VkSemaphore frSignalSemaphores[1]; // Semaphores FR's submission signals

  if (SwapChain::renderMode == RenderMode::PRESENTATION) {
    frWaitSemaphores[0] =
        m_swapChain->getImageAvailableSemaphore(m_currentFrame);
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = frWaitSemaphores;
    submitInfo.pWaitDstStageMask = frWaitStages;

    frSignalSemaphores[0] =
        m_swapChain->getRenderFinishedSemaphore(m_currentFrame);
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = frSignalSemaphores;

    m_graphicsQueue->submitCommandBuffers(
        submitInfo, m_swapChain->getInFlightFence(m_currentFrame));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    // Presentation must wait for rendering to be complete.
    // frSignalSemaphores contains the renderFinishedSemaphore for PRESENTATION
    // mode.
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = frSignalSemaphores;

    VkSwapchainKHR swapChains[] = {m_swapChain->getSwapChainVk()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices =
        &imageIndex;                // imageIndex from vkAcquireNextImageKHR
    presentInfo.pResults = nullptr; // Optional

    VkResult result = m_presentQueue->presentQueue(
        presentInfo); // Re-uses 'result' variable from vkAcquireNextImageKHR
    m_swapChain->signalImageAvailability(imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferNeedsResize) {
      ApplicationEvents::onRequestSwapChainRecreation().publish();
      return; // Must return after recreating swap chain, as current frame's
              // resources are invalid.
    } else if (result != VK_SUCCESS) {
      RP_CORE_ERROR("failed to present swap chain image in ForwardRenderer!");
      throw std::runtime_error(
          "failed to present swap chain image in ForwardRenderer!");
    }
  }

  m_currentFrame = (m_currentFrame + 1) % m_swapChain->getImageCount();
}

void DeferredRenderer::onSwapChainRecreated() {
    // Wait for all operations to complete
    vkDeviceWaitIdle(m_device);

    m_skyboxPass.reset();
    m_stencilBorderPass.reset();
    m_lightingPass.reset();
    m_gbufferPass.reset();

    m_width = static_cast<float>(m_swapChain->getExtent().width);
    m_height = static_cast<float>(m_swapChain->getExtent().height);

    m_commandBuffers.clear();

    // Recreate DDGI system with new frame count to ensure correct number of command buffers
    m_dynamicDiffuseGI = std::make_shared<DynamicDiffuseGI>(m_swapChain->getImageCount());

    m_gbufferPass = std::make_shared<GBufferPass>(
        static_cast<float>(m_swapChain->getExtent().width),
        static_cast<float>(m_swapChain->getExtent().height),
        m_swapChain->getImageCount());
    m_lightingPass = std::make_shared<LightingPass>(
        static_cast<float>(m_swapChain->getExtent().width),
        static_cast<float>(m_swapChain->getExtent().height),
        m_swapChain->getImageCount(), m_gbufferPass, m_dynamicDiffuseGI);
    m_stencilBorderPass = std::make_shared<StencilBorderPass>(
        static_cast<float>(m_swapChain->getExtent().width),
        static_cast<float>(m_swapChain->getExtent().height),
        m_swapChain->getImageCount(), m_gbufferPass->getDepthTextures());

    m_skyboxPass = std::make_shared<SkyboxPass>(m_gbufferPass->getDepthTextures());



    setupCommandResources();

    m_currentFrame = 0; // Reset current frame
    m_framebufferNeedsResize = false;
}

void DeferredRenderer::setupCommandResources() {

  auto &app = Application::getInstance();
  auto &vc = app.getVulkanContext();

  CommandPoolConfig config = {};
  config.queueFamilyIndex = vc.getQueueFamilyIndices().graphicsFamily.value();
  config.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  m_commandPool = CommandPoolManager::createCommandPool(config);

  m_commandBuffers =
      m_commandPool->getCommandBuffers(m_swapChain->getImageCount());
}

void DeferredRenderer::recordCommandBuffer(
    std::shared_ptr<CommandBuffer> commandBuffer,
    std::shared_ptr<Scene> activeScene, uint32_t imageIndex) {

    RAPTURE_PROFILE_FUNCTION();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr; // Optional

    if (!m_skyboxPass->hasActiveSkybox() && activeScene->getSkyboxComponent()) {
        m_skyboxPass->setSkyboxTexture(activeScene->getSkyboxComponent()->skyboxTexture);
    }

    if (vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo) !=
        VK_SUCCESS) {
        RP_CORE_ERROR("failed to begin recording command buffer!");
        throw std::runtime_error("failed to begin recording command buffer!");
    }
    {
    RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "DeferredRenderer Frame");

    auto &registry = activeScene->getRegistry();
    auto lightView = registry.view<LightComponent, TransformComponent, ShadowComponent>();
    auto cascadedShadowView = registry.view<LightComponent, TransformComponent, CascadedShadowComponent>();

    {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Shadow Maps");
        for (auto entity : lightView) {
            auto &lightComp = lightView.get<LightComponent>(entity);
            auto &transformComp = lightView.get<TransformComponent>(entity);
            auto &shadowComp = lightView.get<ShadowComponent>(entity);

            // Always update directional light shadows for debugging, others only when changed
            bool shouldUpdateShadow = (lightComp.hasChanged(m_currentFrame) ||
                                    transformComp.hasChanged(m_currentFrame) ||
                                    lightComp.type == LightType::Directional); // Force update for directional lights
            
            if (shadowComp.shadowMap && shouldUpdateShadow) {
                shadowComp.shadowMap->recordCommandBuffer(commandBuffer, activeScene, m_currentFrame);
            }
        }

        for (auto entity : cascadedShadowView) {
            auto &lightComp = cascadedShadowView.get<LightComponent>(entity);
            auto &transformComp = cascadedShadowView.get<TransformComponent>(entity);
            auto &shadowComp = cascadedShadowView.get<CascadedShadowComponent>(entity);

            // Always update directional light shadows for debugging, others only when changed
            bool shouldUpdateShadow = (lightComp.hasChanged(m_currentFrame) ||
                                    transformComp.hasChanged(m_currentFrame) ||
                                    lightComp.type == LightType::Directional); // Force update for directional lights
            
            if (shadowComp.cascadedShadowMap && shouldUpdateShadow) {
                shadowComp.cascadedShadowMap->recordCommandBuffer(commandBuffer, activeScene, m_currentFrame);
            }
        }
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "GBuffer Pass");
        m_gbufferPass->recordCommandBuffer(commandBuffer, activeScene, m_currentFrame);
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Lighting Pass");
        m_lightingPass->recordCommandBuffer(commandBuffer, activeScene, imageIndex, m_currentFrame);
    }
    
    {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Stencil Border Pass");
        m_stencilBorderPass->recordCommandBuffer(commandBuffer, imageIndex, m_currentFrame, activeScene);
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(commandBuffer->getCommandBufferVk(), "Skybox Pass");
        m_skyboxPass->recordCommandBuffer(commandBuffer, m_currentFrame);
    }

    RAPTURE_PROFILE_GPU_COLLECT(commandBuffer->getCommandBufferVk());

    }
    if (vkEndCommandBuffer(commandBuffer->getCommandBufferVk()) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to record command buffer!");
        throw std::runtime_error("failed to record command buffer!");
    }
}


} // namespace Rapture
