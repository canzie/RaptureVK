

#include "ForwardRenderer.h"

#include "Logging/Log.h"
#include "WindowContext/VulkanContext/VulkanContext.h"
#include "WindowContext/Application.h"
#include "Components/Components.h"
#include "Scenes/Entities/Entity.h"
#include "Scenes/SceneManager.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"

#include "Events/ApplicationEvents.h"
#include "Events/InputEvents.h"

#include "AssetManager/AssetManager.h"

#include <chrono>
#include <mutex>

namespace Rapture {



    // Static member definitions
    std::shared_ptr<Renderpass> ForwardRenderer::m_renderPass = nullptr;
    std::shared_ptr<Shader> ForwardRenderer::m_shader = nullptr;
    std::shared_ptr<GraphicsPipeline> ForwardRenderer::m_graphicsPipeline = nullptr;
    std::shared_ptr<CommandPool> ForwardRenderer::m_commandPool = nullptr;

    std::shared_ptr<SwapChain> ForwardRenderer::m_swapChain = nullptr;

    std::vector<std::shared_ptr<FrameBuffer>> ForwardRenderer::m_framebuffers = {};
    std::vector<std::shared_ptr<CommandBuffer>> ForwardRenderer::m_commandBuffers = {};

    std::vector<VkSemaphore> ForwardRenderer::m_imageAvailableSemaphores = {};
    std::vector<VkSemaphore> ForwardRenderer::m_renderFinishedSemaphores = {};
    std::vector<VkFence> ForwardRenderer::m_inFlightFences = {};

    // Camera uniform buffers (binding 0)
    std::vector<std::shared_ptr<UniformBuffer>> ForwardRenderer::m_cameraUniformBuffers = {};
    std::vector<CameraUniformBufferObject> ForwardRenderer::m_cameraUbos = {};
    
    // Light uniform buffers (binding 1)
    std::vector<std::shared_ptr<UniformBuffer>> ForwardRenderer::m_lightUniformBuffers = {};
    std::vector<LightUniformBufferObject> ForwardRenderer::m_lightUbos = {};
    
    // Light management
    bool ForwardRenderer::m_lightsNeedUpdate = true;

    VmaAllocator ForwardRenderer::m_vmaAllocator = nullptr;
    VkDevice ForwardRenderer::m_device = nullptr;
    std::shared_ptr<VulkanQueue> ForwardRenderer::m_graphicsQueue = nullptr;
    std::shared_ptr<VulkanQueue> ForwardRenderer::m_presentQueue = nullptr;

    VkDescriptorPool ForwardRenderer::m_descriptorPool = nullptr;
    std::vector<VkDescriptorSet> ForwardRenderer::m_descriptorSets = {};


    bool ForwardRenderer::m_framebufferResized = false;
    uint32_t ForwardRenderer::m_currentFrame = 0;
    
    float ForwardRenderer::m_zoom = 0.0f;

    void ForwardRenderer::init()
    {

        auto &app = Application::getInstance();
        m_vmaAllocator = app.getVulkanContext().getVmaAllocator();
        m_device = app.getVulkanContext().getLogicalDevice();
        m_graphicsQueue = app.getVulkanContext().getGraphicsQueue();
        m_presentQueue = app.getVulkanContext().getPresentQueue();

        ApplicationEvents::onWindowResize().addListener([](unsigned int width, unsigned int height) {
            m_framebufferResized = true;
        });

        InputEvents::onMouseScrolled().addListener([](float x, float y) {
            m_zoom += y;
        });

        m_swapChain = app.getVulkanContext().getSwapChain();

        AssetManager::init();
        MaterialManager::init();

        setupShaders();

        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();

        setupRenderPass();
        setupGraphicsPipeline();
        setupFramebuffers();
        setupCommandPool();
        
        setupCommandBuffers();
        setupSyncObjects();
}

void ForwardRenderer::shutdown()
{
    
    int imageCount = m_swapChain->getImageCount();
    cleanupSwapChain();
    m_swapChain.reset();


    // Now safe to destroy VMA allocator


    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

    m_cameraUniformBuffers.clear();
    m_lightUniformBuffers.clear();
    MaterialManager::shutdown();
    AssetManager::shutdown();
    m_shader.reset();

    for (size_t i = 0; i < imageCount; i++) {
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }

    m_commandPool.reset();
    CommandPoolManager::shutdown();

    m_graphicsQueue.reset();
    m_presentQueue.reset();
}

void ForwardRenderer::drawFrame(std::shared_ptr<Scene> activeScene)
{
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapChain->getSwapChainVk(), UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_framebufferResized = false;
        recreateSwapChain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        RP_CORE_ERROR("failed to acquire swap chain image!");
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    updateUniformBuffers();
    updateLights(activeScene);

    m_commandBuffers[m_currentFrame]->reset();
    recordCommandBuffer(m_commandBuffers[m_currentFrame]->getCommandBufferVk(), imageIndex, activeScene);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    m_graphicsQueue->addCommandBuffer(m_commandBuffers[m_currentFrame]);

    m_graphicsQueue->submitCommandBuffers(submitInfo, m_inFlightFences[m_currentFrame]);


if (SwapChain::renderMode == RenderMode::PRESENTATION) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {m_swapChain->getSwapChainVk()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr; // Optional


    result = m_presentQueue->presentQueue(presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapChain();
        return;
    } else if (result != VK_SUCCESS) {
        RP_CORE_ERROR("failed to present swap chain image!");
        throw std::runtime_error("failed to present swap chain image!");
    }
}else{
    // Offscreen rendering
    // TODO: Implement offscreen rendering
}

    m_currentFrame = (m_currentFrame + 1) % m_swapChain->getImageCount();

}


void ForwardRenderer::setupShaders()
{
    const std::filesystem::path vertShaderPath = "E:/Dev/Games/RaptureVK/Engine/assets/shaders/SPIRV/pbr.vs.spv";


    auto [shader, handle] = AssetManager::importAsset<Shader>(vertShaderPath);
    m_shader = shader;  


}

void ForwardRenderer::setupRenderPass()
{
    if (m_swapChain->getImageFormat() == VK_FORMAT_UNDEFINED) {
        RP_CORE_ERROR("ForwardRenderer - Attempted to create render pass before swap chain was initialized!");
        throw std::runtime_error("ForwardRenderer - Attempted to create render pass before swap chain was initialized!");
    }

    // Create the color attachment description
    SubpassAttachmentUsage colorAttachment{};
    SubpassAttachmentUsage depthAttachment{};

    // Zero out the structures first
    VkAttachmentDescription attachmentDesc = {};
    VkAttachmentReference attachmentRef = {};

    // Fill in the attachment description
    // this is a default swapchain setup for presentation
    attachmentDesc.format = m_swapChain->getImageFormat();
    attachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    attachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachmentDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Fill in the attachment reference
    attachmentRef.attachment = 0;
    attachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Assign the properly initialized structures
    colorAttachment.attachmentDescription = attachmentDesc;
    colorAttachment.attachmentReference = attachmentRef;


    // Create the depth attachment description
    VkAttachmentDescription depthAttachmentDesc = {};
    depthAttachmentDesc.format = m_swapChain->getDepthImageFormat(); // Use format from SwapChain
    depthAttachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // We don't need to store depth data after render pass
    depthAttachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachmentDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1; // Second attachment
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    depthAttachment.attachmentDescription = depthAttachmentDesc;
    depthAttachment.attachmentReference = depthAttachmentRef;

    

    // Create the subpass info
    SubpassInfo subpassInfo{};
    subpassInfo.colorAttachments.push_back(colorAttachment);
    subpassInfo.depthStencilAttachment = depthAttachment;
    subpassInfo.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassInfo.shaderProgram = m_shader;
    subpassInfo.name = "ForwardRenderer presentation subpass";

    // Create the renderpass with the subpass info
    std::vector<SubpassInfo> subpasses = { subpassInfo };
    m_renderPass = std::make_shared<Renderpass>(subpasses);
}

void ForwardRenderer::setupGraphicsPipeline()
{

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_VERTEX_INPUT_EXT
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) m_swapChain->getExtent().width;
    viewport.height = (float) m_swapChain->getExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapChain->getExtent();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f; // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f; // Optional
    multisampling.pSampleMask = nullptr; // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE; // Optional

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f; // Optional
    colorBlending.blendConstants[1] = 0.0f; // Optional
    colorBlending.blendConstants[2] = 0.0f; // Optional
    colorBlending.blendConstants[3] = 0.0f; // Optional

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {};
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    
    

    GraphicsPipelineConfiguration config;
    config.renderPass = m_renderPass;
    config.dynamicState = dynamicState;
    config.inputAssemblyState = inputAssembly;
    config.viewportState = viewportState;
    config.rasterizationState = rasterizer;
    config.multisampleState = multisampling;
    config.colorBlendState = colorBlending;
    config.commonColorBlendAttachmentState = colorBlendAttachment;
    config.vertexInputState = vertexInputInfo;
    config.depthStencilState = depthStencil;


    m_graphicsPipeline = std::make_shared<GraphicsPipeline>(config);

}
void ForwardRenderer::setupFramebuffers()
{
    m_framebuffers.clear();
    m_framebuffers.reserve(m_swapChain->getImageViews().size());

    for (uint32_t i = 0; i < m_swapChain->getImageViews().size(); i++) {
        auto framebuffer = std::make_shared<FrameBuffer>(*m_swapChain, i, m_renderPass->getRenderPassVk());
        m_framebuffers.push_back(framebuffer);
    }
}

void ForwardRenderer::setupCommandPool()
{
    auto& app = Application::getInstance();
    auto queueFamilyIndices = app.getVulkanContext().getQueueFamilyIndices();

    CommandPoolConfig config;
    config.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    config.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    config.threadId = 0;

    m_commandPool = CommandPoolManager::createCommandPool(config);

}


void ForwardRenderer::setupCommandBuffers()
{
    m_commandBuffers = m_commandPool->getCommandBuffers(m_swapChain->getImageCount());

}

void ForwardRenderer::setupSyncObjects()
{

    m_imageAvailableSemaphores.resize(m_swapChain->getImageCount());
    m_renderFinishedSemaphores.resize(m_swapChain->getImageCount());
    m_inFlightFences.resize(m_swapChain->getImageCount());

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < m_swapChain->getImageCount(); i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            RP_CORE_ERROR("failed to create synchronization objects for frame {0}!", i);
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }

}

void ForwardRenderer::cleanupSwapChain()
{
    m_framebuffers.clear();

    // currently closes each command buffer sequentially
    // could make something to close them all at once
    m_commandBuffers.clear();

    m_graphicsPipeline.reset();
    m_renderPass.reset();

    m_swapChain->destroy();
}
void ForwardRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, std::shared_ptr<Scene> activeScene)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr; // Optional

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to begin recording command buffer!");
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass->getRenderPassVk();
    renderPassInfo.framebuffer = m_framebuffers[imageIndex]->getFramebufferVk();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapChain->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    uint32_t subpassIndex = 0;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    m_graphicsPipeline->bind(commandBuffer, subpassIndex);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_swapChain->getExtent().width);
    viewport.height = static_cast<float>(m_swapChain->getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapChain->getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // Get the VulkanContext to check if dynamic vertex input is supported
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    

    // Get entities with TransformComponent and MeshComponent
    auto& registry = activeScene->getRegistry();
    auto view = registry.view<TransformComponent, MeshComponent, MaterialComponent>();
    

    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& meshComp = view.get<MeshComponent>(entity);
        auto& materialComp = view.get<MaterialComponent>(entity);

        // Check if mesh is valid and not loading
        if (!meshComp.mesh || meshComp.isLoading) {
            continue;
        }

        if (!materialComp.material->isReady()) {
            continue;
        }
        
        auto mesh = meshComp.mesh;
        
        // Check if mesh has valid buffers
        if (!mesh->getVertexBuffer() || !mesh->getIndexBuffer()) {
            continue;
        }
        
        // Get the vertex buffer layout
        auto& bufferLayout = mesh->getVertexBuffer()->getBufferLayout();
        
        // Set up dynamic vertex input only if extension is available
        if (vulkanContext.isVertexInputDynamicStateEnabled() && vulkanContext.vkCmdSetVertexInputEXT) {            
            // Convert to EXT variants required by vkCmdSetVertexInputEXT
            auto bindingDescription = bufferLayout.getBindingDescription2EXT();
            
            auto attributeDescriptions = bufferLayout.getAttributeDescriptions2EXT();
            
            // Use the function pointer from VulkanContext
            vulkanContext.vkCmdSetVertexInputEXT(commandBuffer, 
                1, &bindingDescription,
                static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data());
        }
        
        // Push the model matrix as a push constant
        PushConstants pushConstants{};
        pushConstants.model = transform.transformMatrix();
        pushConstants.camPos = transform.translation();

        vkCmdPushConstants(commandBuffer, 
            m_graphicsPipeline->getPipelineLayoutVk(subpassIndex),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
            0, 
            sizeof(PushConstants), 
            &pushConstants);
        
        // Bind vertex buffers
        VkBuffer vertexBuffers[] = {mesh->getVertexBuffer()->getBufferVk()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);


        
        // Bind descriptor sets (only view/proj and material now)materialComp.material->getDescriptorSet()
        VkDescriptorSet descriptorSets[] = {m_descriptorSets[m_currentFrame], materialComp.material->getDescriptorSet()};
        uint32_t descriptorSetCount = sizeof(descriptorSets) / sizeof(descriptorSets[0]);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline->getPipelineLayoutVk(subpassIndex), 
            0, descriptorSetCount, descriptorSets, 0, nullptr);
        
        // Bind index buffer
        vkCmdBindIndexBuffer(commandBuffer, mesh->getIndexBuffer()->getBufferVk(), 0, mesh->getIndexBuffer()->getIndexType());
        
        // Draw the mesh
        vkCmdDrawIndexed(commandBuffer, mesh->getIndexCount(), 1, 0, 0, 0);
    }
    

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to record command buffer!");
        throw std::runtime_error("failed to record command buffer!");
    }

}

void ForwardRenderer::recreateSwapChain()
{
    auto& app = Application::getInstance();
    auto& windowContext = app.getWindowContext();

    int width = 0, height = 0;
    windowContext.getFramebufferSize(&width, &height);
    while (width == 0 || height == 0) {
        windowContext.getFramebufferSize(&width, &height);
        windowContext.waitEvents();
    }

    
    vkDeviceWaitIdle(m_device);

    cleanupSwapChain();


    m_swapChain->recreate();
    setupRenderPass();
    setupGraphicsPipeline();
    setupFramebuffers();
    setupCommandBuffers();
}

void ForwardRenderer::createUniformBuffers()
{
    VkDeviceSize cameraBufferSize = sizeof(CameraUniformBufferObject);
    VkDeviceSize lightBufferSize = sizeof(LightUniformBufferObject);

    for (size_t i = 0; i < m_swapChain->getImageCount(); i++) {
        // Create camera uniform buffer
        auto cameraBuffer = std::make_shared<UniformBuffer>(cameraBufferSize, BufferUsage::STREAM, m_vmaAllocator);
        m_cameraUniformBuffers.push_back(cameraBuffer);
        m_cameraUbos.push_back({});
        m_cameraUniformBuffers[i]->addData((void*)&m_cameraUbos[i], sizeof(m_cameraUbos[i]), 0);

        // Create light uniform buffer
        auto lightBuffer = std::make_shared<UniformBuffer>(lightBufferSize, BufferUsage::STREAM, m_vmaAllocator);
        m_lightUniformBuffers.push_back(lightBuffer);
        m_lightUbos.push_back({});
        m_lightUniformBuffers[i]->addData((void*)&m_lightUbos[i], sizeof(m_lightUbos[i]), 0);
    }
}

void ForwardRenderer::updateUniformBuffers() {
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    CameraUniformBufferObject ubo{};
    
    // Get the active scene from SceneManager
    auto activeScene = SceneManager::getInstance().getActiveScene();
    
    if (activeScene) {
        // Try to find the main camera in the scene
        auto& registry = activeScene->getRegistry();
        auto cameraView = registry.view<TransformComponent, CameraComponent>();
        
        bool foundMainCamera = false;
        for (auto entity : cameraView) {
            auto& camera = cameraView.get<CameraComponent>(entity);
            if (camera.isMainCamera) {
                // Update camera aspect ratio based on current swapchain extent
                float aspectRatio = m_swapChain->getExtent().width / (float)m_swapChain->getExtent().height;
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
            ubo.proj = glm::perspective(glm::radians(45.0f), m_swapChain->getExtent().width / (float) m_swapChain->getExtent().height, 0.1f, 10.0f);
        }
    } else {
        // Fallback if no active scene
        RP_CORE_WARN("No active scene found, using default view matrix");
        ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), m_swapChain->getExtent().width / (float) m_swapChain->getExtent().height, 0.1f, 10.0f);
    }
    
    // Fix projection matrix for Vulkan coordinate system
    ubo.proj[1][1] *= -1;

    m_cameraUniformBuffers[m_currentFrame]->addData((void*)&ubo, sizeof(ubo), 0);
}
void ForwardRenderer::createDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    
    // Camera and Light uniform buffers (per frame) - 2 buffers per frame
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(m_swapChain->getImageCount() * 2);
    
    // Material descriptors (estimate for materials)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 100; // Estimate for material descriptors

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(m_swapChain->getImageCount()) + 100; // Frame sets + material sets

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to create descriptor pool!");
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void ForwardRenderer::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(m_swapChain->getImageCount(), m_shader->getDescriptorSetLayouts()[0]);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(m_swapChain->getImageCount());
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(m_swapChain->getImageCount());
    if (vkAllocateDescriptorSets(m_device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < m_swapChain->getImageCount(); i++) {
        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        // Camera uniform buffer (binding 0)
        VkDescriptorBufferInfo cameraBufferInfo{};
        cameraBufferInfo.buffer = m_cameraUniformBuffers[i]->getBufferVk();
        cameraBufferInfo.offset = 0;
        cameraBufferInfo.range = sizeof(CameraUniformBufferObject);

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &cameraBufferInfo;

        // Light uniform buffer (binding 1)
        VkDescriptorBufferInfo lightBufferInfo{};
        lightBufferInfo.buffer = m_lightUniformBuffers[i]->getBufferVk();
        lightBufferInfo.offset = 0;
        lightBufferInfo.range = sizeof(LightUniformBufferObject);

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = m_descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &lightBufferInfo;

        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void ForwardRenderer::setupLights(std::shared_ptr<Scene> activeScene) {
    if (!activeScene) {
        return;
    }
    
    // Initial light update
    m_lightsNeedUpdate = true;
    updateLights(activeScene);
}

void ForwardRenderer::updateLights(std::shared_ptr<Scene> activeScene) {
    if (!activeScene) {
        return;
    }

    auto& registry = activeScene->getRegistry();
    auto lightView = registry.view<TransformComponent, LightComponent, TagComponent>();
    
    // Check if any lights or transforms have changed
    bool lightsChanged = m_lightsNeedUpdate;
    
    for (auto entity : lightView) {
        auto& transform = lightView.get<TransformComponent>(entity);
        auto& lightComp = lightView.get<LightComponent>(entity);
        auto& tagComp = lightView.get<TagComponent>(entity);
        

        if (lightComp.hasChanged() || transform.hasChanged()) {
            lightsChanged = true;
            break;
        }
    }
    
    if (!lightsChanged) {
        return;
    }
    
    // Update light uniform buffer
    LightUniformBufferObject lightUbo{};
    lightUbo.numLights = 0;
    
    for (auto entity : lightView) {
        if (lightUbo.numLights >= MAX_LIGHTS) {
            RP_CORE_WARN("Maximum number of lights ({}) exceeded. Additional lights will be ignored.", MAX_LIGHTS);
            break;
        }
        
        auto& transform = lightView.get<TransformComponent>(entity);
        auto& lightComp = lightView.get<LightComponent>(entity);
        
        if (!lightComp.isActive) {
            continue;
        }
        
        LightData& lightData = lightUbo.lights[lightUbo.numLights];
        
        // Position and light type
        glm::vec3 position = transform.translation();
        float lightTypeFloat = static_cast<float>(lightComp.type);
        lightData.position = glm::vec4(position, lightTypeFloat);
        
        
        // Direction and range
        glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f); // Default forward direction
        if (lightComp.type == LightType::Directional || lightComp.type == LightType::Spot) {
            // Calculate direction from rotation
            glm::vec3 eulerAngles = transform.rotation();
            glm::mat4 rotationMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(eulerAngles.x), glm::vec3(1.0f, 0.0f, 0.0f));
            rotationMatrix = glm::rotate(rotationMatrix, glm::radians(eulerAngles.y), glm::vec3(0.0f, 1.0f, 0.0f));
            rotationMatrix = glm::rotate(rotationMatrix, glm::radians(eulerAngles.z), glm::vec3(0.0f, 0.0f, 1.0f));
            direction = glm::normalize(glm::vec3(rotationMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        }
        lightData.direction = glm::vec4(direction, lightComp.range);
        
        // Color and intensity
        lightData.color = glm::vec4(lightComp.color, lightComp.intensity);
        
        // Spot light angles
        if (lightComp.type == LightType::Spot) {
            float innerCos = std::cos(lightComp.innerConeAngle);
            float outerCos = std::cos(lightComp.outerConeAngle);
            lightData.spotAngles = glm::vec4(innerCos, outerCos, 0.0f, 0.0f);
        } else {
            lightData.spotAngles = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        }
        
        lightUbo.numLights++;
    }
    
    // Update light uniform buffers for ALL frames in flight
    for (size_t i = 0; i < m_lightUniformBuffers.size(); i++) {
        m_lightUniformBuffers[i]->addData((void*)&lightUbo, sizeof(lightUbo), 0);
    }
    
    m_lightsNeedUpdate = false;
}


}
