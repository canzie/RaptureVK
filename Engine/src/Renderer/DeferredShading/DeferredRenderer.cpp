#include "DeferredRenderer.h"

#include "Buffers/CommandBuffers/CommandPool.h"
#include "WindowContext/Application.h"

#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"

#include "Events/ApplicationEvents.h"

namespace Rapture {

    // Maximum number of lights supported
    static constexpr uint32_t MAX_LIGHTS = 16;

    struct PushConstants {
        glm::mat4 model = glm::mat4(1.0f);
        glm::vec3 camPos = glm::vec3(0.0f);
    };

    // Static member definitions
    std::shared_ptr<GBufferPass> DeferredRenderer::m_gbufferPass = nullptr;
    std::vector<std::shared_ptr<CommandBuffer>> DeferredRenderer::m_commandBuffers;
    std::shared_ptr<CommandPool> DeferredRenderer::m_commandPool = nullptr;
    std::shared_ptr<Shader> DeferredRenderer::m_shader = nullptr;
    VmaAllocator DeferredRenderer::m_vmaAllocator = VK_NULL_HANDLE;
    VkDevice DeferredRenderer::m_device = VK_NULL_HANDLE;
    std::shared_ptr<SwapChain> DeferredRenderer::m_swapChain = nullptr;
    uint32_t DeferredRenderer::m_currentFrame = 0;
    std::shared_ptr<VulkanQueue> DeferredRenderer::m_graphicsQueue = nullptr;
    std::shared_ptr<VulkanQueue> DeferredRenderer::m_presentQueue = nullptr;


void DeferredRenderer::init() {

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();
    
    m_device = vc.getLogicalDevice();
    m_swapChain = vc.getSwapChain();
    m_vmaAllocator = vc.getVmaAllocator();

    m_graphicsQueue = vc.getGraphicsQueue();
    m_presentQueue = vc.getPresentQueue();

    setupCommandResources();

    m_gbufferPass = std::make_shared<GBufferPass>(static_cast<float>(m_swapChain->getExtent().width), static_cast<float>(m_swapChain->getExtent().height), m_swapChain->getImageCount());

    ApplicationEvents::onSwapChainRecreated().addListener([](std::shared_ptr<SwapChain> swapChain) {
        onSwapChainRecreated();
    });
}

void DeferredRenderer::shutdown() {
    // Wait for device to finish operations
    vkDeviceWaitIdle(m_device);

    // Clean up GBuffer pass
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



    m_commandBuffers[m_currentFrame]->reset();
    recordCommandBuffer(m_commandBuffers[m_currentFrame], activeScene, imageIndex);


    m_graphicsQueue->addCommandBuffer(m_commandBuffers[m_currentFrame]);


    m_currentFrame = (m_currentFrame + 1) % m_swapChain->getImageCount();

}

void DeferredRenderer::onSwapChainRecreated()
{
    m_gbufferPass.reset();
    m_gbufferPass = std::make_shared<GBufferPass>(static_cast<float>(m_swapChain->getExtent().width), static_cast<float>(m_swapChain->getExtent().height), m_swapChain->getImageCount());

    m_commandBuffers.clear();
    setupCommandResources();
    m_currentFrame = 0;  // Reset current frame
}

void DeferredRenderer::setupCommandResources()
{

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();

    CommandPoolConfig config = {};
    config.queueFamilyIndex = vc.getQueueFamilyIndices().graphicsFamily.value();
    config.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    m_commandPool = CommandPoolManager::createCommandPool(config);

    m_commandBuffers = m_commandPool->getCommandBuffers(m_swapChain->getImageCount());
}



void DeferredRenderer::recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<Scene> activeScene, uint32_t imageIndex) {

    RAPTURE_PROFILE_FUNCTION();


    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr; // Optional

    if (vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to begin recording command buffer!");
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    m_gbufferPass->recordCommandBuffer(commandBuffer, activeScene, m_currentFrame); // does not need imageIndex because it does not use the swapchain images
    //m_lightingPass->recordCommandBuffer(commandBuffer, activeScene, imageIndex); // <-- this one does

    if (vkEndCommandBuffer(commandBuffer->getCommandBufferVk()) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to record command buffer!");
        throw std::runtime_error("failed to record command buffer!");
    }


}
}
