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
    std::shared_ptr<LightingPass> DeferredRenderer::m_lightingPass = nullptr;
    std::shared_ptr<StencilBorderPass> DeferredRenderer::m_stencilBorderPass = nullptr;
    std::vector<std::shared_ptr<UniformBuffer>> DeferredRenderer::m_cameraUBOs = {};
    std::vector<std::shared_ptr<UniformBuffer>> DeferredRenderer::m_shadowDataUBOs = {};
    float DeferredRenderer::m_width = 0.0f;
    float DeferredRenderer::m_height = 0.0f;
    std::shared_ptr<BindlessDescriptorArray> DeferredRenderer::m_bindlessDescriptorArray = nullptr;


void DeferredRenderer::init() {

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();
    
    m_device = vc.getLogicalDevice();
    m_swapChain = vc.getSwapChain();
    m_vmaAllocator = vc.getVmaAllocator();

    m_graphicsQueue = vc.getGraphicsQueue();
    m_presentQueue = vc.getPresentQueue();

    m_width = static_cast<float>(m_swapChain->getExtent().width);
    m_height = static_cast<float>(m_swapChain->getExtent().height);

    m_bindlessDescriptorArray = BindlessDescriptorManager::getPool(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    setupCommandResources();
    createUniformBuffers(m_swapChain->getImageCount());

    m_gbufferPass = std::make_shared<GBufferPass>(
        static_cast<float>(m_swapChain->getExtent().width), 
        static_cast<float>(m_swapChain->getExtent().height), 
        m_swapChain->getImageCount(),
        m_cameraUBOs
    );

    m_lightingPass = std::make_shared<LightingPass>(
        static_cast<float>(m_swapChain->getExtent().width), 
        static_cast<float>(m_swapChain->getExtent().height), 
        m_swapChain->getImageCount(), m_gbufferPass, m_shadowDataUBOs);

    m_stencilBorderPass = std::make_shared<StencilBorderPass>(
        static_cast<float>(m_swapChain->getExtent().width), 
        static_cast<float>(m_swapChain->getExtent().height), 
        m_swapChain->getImageCount(), 
        m_gbufferPass->getDepthTextures(),
        m_cameraUBOs
    );

    ApplicationEvents::onSwapChainRecreated().addListener([](std::shared_ptr<SwapChain> swapChain) {
        onSwapChainRecreated();
    });
}

void DeferredRenderer::shutdown() {
    // Wait for device to finish operations
    vkDeviceWaitIdle(m_device);


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



    m_commandBuffers[m_currentFrame]->reset();
    recordCommandBuffer(m_commandBuffers[m_currentFrame], activeScene, imageIndex);


    m_graphicsQueue->addCommandBuffer(m_commandBuffers[m_currentFrame]);


    m_currentFrame = (m_currentFrame + 1) % m_swapChain->getImageCount();

}

void DeferredRenderer::onSwapChainRecreated() {
    // Wait for all operations to complete
    vkDeviceWaitIdle(m_device);

    m_stencilBorderPass.reset();
    m_lightingPass.reset();
    m_gbufferPass.reset();

    m_width = static_cast<float>(m_swapChain->getExtent().width);
    m_height = static_cast<float>(m_swapChain->getExtent().height);

    m_cameraUBOs.clear();
    m_shadowDataUBOs.clear();
    createUniformBuffers(m_swapChain->getImageCount());

    m_commandBuffers.clear();

    m_gbufferPass = std::make_shared<GBufferPass>(
        static_cast<float>(m_swapChain->getExtent().width), 
        static_cast<float>(m_swapChain->getExtent().height), 
        m_swapChain->getImageCount(),
        m_cameraUBOs
    );    
    m_lightingPass = std::make_shared<LightingPass>(
        static_cast<float>(m_swapChain->getExtent().width), 
        static_cast<float>(m_swapChain->getExtent().height), 
        m_swapChain->getImageCount(), 
        m_gbufferPass, 
        m_shadowDataUBOs
    );
    m_stencilBorderPass = std::make_shared<StencilBorderPass>(
        static_cast<float>(m_swapChain->getExtent().width), 
        static_cast<float>(m_swapChain->getExtent().height), 
        m_swapChain->getImageCount(), 
        m_gbufferPass->getDepthTextures(), 
        m_cameraUBOs
    );

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

    updateCameraUBOs(activeScene, m_currentFrame);
    updateShadowMaps(activeScene);
    
    if (vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to begin recording command buffer!");
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    auto& registry = activeScene->getRegistry();
    auto lightView = registry.view<LightComponent, TransformComponent, ShadowComponent>();
    for (auto entity : lightView) {
        auto& lightComp = lightView.get<LightComponent>(entity);
        auto& transformComp = lightView.get<TransformComponent>(entity);
        auto& shadowComp = lightView.get<ShadowComponent>(entity);
    
        if (shadowComp.shadowMap && (lightComp.hasChanged(m_currentFrame) || transformComp.hasChanged(m_currentFrame))) {
            shadowComp.shadowMap->recordCommandBuffer(commandBuffer, activeScene, m_currentFrame);
        }
    }

    m_gbufferPass->recordCommandBuffer(commandBuffer, activeScene, m_currentFrame);
    
    m_lightingPass->recordCommandBuffer(commandBuffer, activeScene, imageIndex, m_currentFrame);

    m_stencilBorderPass->recordCommandBuffer(commandBuffer, imageIndex, m_currentFrame, activeScene);


    if (vkEndCommandBuffer(commandBuffer->getCommandBufferVk()) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to record command buffer!");
        throw std::runtime_error("failed to record command buffer!");
    }


}
void DeferredRenderer::updateCameraUBOs(std::shared_ptr<Scene> activeScene, uint32_t currentFrame) {

    RAPTURE_PROFILE_FUNCTION();

    CameraUniformBufferObject ubo{};

    // Try to find the main camera in the scene
    auto& registry = activeScene->getRegistry();
    auto cameraView = registry.view<TransformComponent, CameraComponent>();
    
    bool foundMainCamera = false;
    for (auto entity : cameraView) {
        auto& camera = cameraView.get<CameraComponent>(entity);
        if (camera.isMainCamera) {
            // Update camera aspect ratio based on current swapchain extent
            float aspectRatio = m_width / m_height;
            if (camera.aspectRatio != aspectRatio) {
                camera.updateProjectionMatrix(camera.fov, aspectRatio, camera.nearPlane, camera.farPlane);
            }
            
            // Use the camera's view matrix
            ubo.view = camera.camera.getViewMatrix();
            ubo.proj = camera.camera.getProjectionMatrix();
            foundMainCamera = true;
            break;
        }
    }
    
    // Fallback to default camera if no main camera found
    if (!foundMainCamera) {
        RP_CORE_WARN("No main camera found in scene, using default view matrix");
        ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), m_width / m_height, 0.1f, 10.0f);
    }

    
    // Fix projection matrix for Vulkan coordinate system
    ubo.proj[1][1] *= -1;

    m_cameraUBOs[currentFrame]->addData((void*)&ubo, sizeof(ubo), 0);


}

void DeferredRenderer::createUniformBuffers(uint32_t framesInFlight) {
    for (int i = 0; i < framesInFlight; i++) {
        m_cameraUBOs.push_back(std::make_shared<UniformBuffer>(sizeof(CameraUniformBufferObject), BufferUsage::STREAM, m_vmaAllocator));
        m_shadowDataUBOs.push_back(std::make_shared<UniformBuffer>(sizeof(ShadowStorageLayout), BufferUsage::DYNAMIC, m_vmaAllocator));
    
        ShadowStorageLayout shadowDataLayout{};
        shadowDataLayout.shadowCount = 0;
        m_shadowDataUBOs[i]->addData((void*)&shadowDataLayout, sizeof(shadowDataLayout), 0);
    }

    

}

void DeferredRenderer::updateShadowMaps(std::shared_ptr<Scene> activeScene) {

    auto& registry = activeScene->getRegistry();
    auto lightView = registry.view<LightComponent, TransformComponent, ShadowComponent>();

    ShadowStorageLayout shadowDataLayout{};
    uint32_t shadowIndex = 0;

    for (auto entity : lightView) {
        auto& lightComp = lightView.get<LightComponent>(entity);
        auto& transformComp = lightView.get<TransformComponent>(entity);
        auto& shadowComp = lightView.get<ShadowComponent>(entity);

        if (shadowComp.shadowMap) {
            shadowComp.shadowMap->updateViewMatrix(lightComp, transformComp);
            

            // Always populate shadow data for existing shadow maps
            ShadowBufferData shadowBufferData{};
            shadowBufferData.type = static_cast<int>(lightComp.type);
            shadowBufferData.cascadeCount = 1;
            shadowBufferData.lightIndex = (uint32_t)entity;
            shadowBufferData.textureHandle = shadowComp.shadowMap->getTextureHandle();
            shadowBufferData.cascadeMatrices[0] = shadowComp.shadowMap->getLightViewProjection();
            shadowBufferData.cascadeSplitsViewSpace[0] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

            // Store in the array
            shadowDataLayout.shadowData[shadowIndex] = shadowBufferData;
            shadowIndex++;
        }
    }

    shadowDataLayout.shadowCount = shadowIndex;

    // Update all shadow data UBOs
    for (int i = 0; i < m_swapChain->getImageCount(); i++) {
        m_shadowDataUBOs[i]->addData((void*)&shadowDataLayout, sizeof(shadowDataLayout), 0);
    }
}
}
