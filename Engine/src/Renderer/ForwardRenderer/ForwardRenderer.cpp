#include "ForwardRenderer.h"

#include "Logging/Log.h"
#include "WindowContext/VulkanContext/VulkanContext.h"
#include "WindowContext/Application.h"
#include "Scenes/Entities/Entity.h"
#include "Scenes/SceneManager.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"

#include "Events/ApplicationEvents.h"
#include "Events/InputEvents.h"

#include "AssetManager/AssetManager.h"
#include "Logging/TracyProfiler.h"

#include <chrono>
#include <mutex>

namespace Rapture {



    // Static member definitions
    std::shared_ptr<Shader> ForwardRenderer::m_shader = nullptr;
    std::shared_ptr<GraphicsPipeline> ForwardRenderer::m_graphicsPipeline = nullptr;
    std::shared_ptr<CommandPool> ForwardRenderer::m_commandPool = nullptr;

    std::shared_ptr<SwapChain> ForwardRenderer::m_swapChain = nullptr;

    std::vector<std::shared_ptr<CommandBuffer>> ForwardRenderer::m_commandBuffers = {};

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


    uint32_t ForwardRenderer::m_currentFrame = 0;
    
    float ForwardRenderer::m_zoom = 0.0f;

    void ForwardRenderer::init()
    {

        auto &app = Application::getInstance();
        m_vmaAllocator = app.getVulkanContext().getVmaAllocator();
        m_device = app.getVulkanContext().getLogicalDevice();
        m_graphicsQueue = app.getVulkanContext().getGraphicsQueue();
        m_presentQueue = app.getVulkanContext().getPresentQueue();



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

        setupGraphicsPipeline();
        setupCommandPool();
        
        setupCommandBuffers();
}

void ForwardRenderer::shutdown()
{
    
    cleanupSwapChain();


    // Now safe to destroy VMA allocator


    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

    m_cameraUniformBuffers.clear();
    m_lightUniformBuffers.clear();
    MaterialManager::shutdown();
    AssetManager::shutdown();
    m_shader.reset();

    m_commandPool.reset();

    m_graphicsQueue.reset();
    m_presentQueue.reset();
}

void ForwardRenderer::drawFrame(std::shared_ptr<Scene> activeScene)
{
    RAPTURE_PROFILE_FUNCTION();
    
    int imageIndexi = m_swapChain->acquireImage(m_currentFrame);

    if (imageIndexi == -1) {
        recreateSwapChain();
        return;
    }
    uint32_t imageIndex = static_cast<uint32_t>(imageIndexi);


    updateUniformBuffers();
    updateLights(activeScene);

    m_commandBuffers[m_currentFrame]->reset();
    recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex, activeScene);


    m_graphicsQueue->addCommandBuffer(m_commandBuffers[m_currentFrame]);


    // --- BEGIN SUBMISSION LOGIC FOR FORWARD RENDERER (COMMON TO BOTH MODES) ---
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore frWaitSemaphores[1]; // Semaphores FR's submission waits on
    VkPipelineStageFlags frWaitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore frSignalSemaphores[1]; // Semaphores FR's submission signals

    if (SwapChain::renderMode == RenderMode::PRESENTATION) {
        frWaitSemaphores[0] = m_swapChain->getImageAvailableSemaphore(m_currentFrame);
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = frWaitSemaphores;
        submitInfo.pWaitDstStageMask = frWaitStages;

        frSignalSemaphores[0] = m_swapChain->getRenderFinishedSemaphore(m_currentFrame);
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = frSignalSemaphores;

        m_graphicsQueue->submitCommandBuffers(submitInfo, m_swapChain->getInFlightFence(m_currentFrame));


        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        // Presentation must wait for rendering to be complete.
        // frSignalSemaphores contains the renderFinishedSemaphore for PRESENTATION mode.
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = frSignalSemaphores; 

        VkSwapchainKHR swapChains[] = {m_swapChain->getSwapChainVk()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex; // imageIndex from vkAcquireNextImageKHR
        presentInfo.pResults = nullptr; // Optional

        VkResult result = m_presentQueue->presentQueue(presentInfo); // Re-uses 'result' variable from vkAcquireNextImageKHR
        m_swapChain->signalImageAvailability(imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {

            recreateSwapChain();
            return; // Must return after recreating swap chain, as current frame's resources are invalid.
        } else if (result != VK_SUCCESS) {
            RP_CORE_ERROR("failed to present swap chain image in ForwardRenderer!");
            throw std::runtime_error("failed to present swap chain image in ForwardRenderer!");
        }
    }
    // --- END PRESENTATION LOGIC ---

    m_currentFrame = (m_currentFrame + 1) % m_swapChain->getImageCount();

}


void ForwardRenderer::setupShaders()
{
    const std::filesystem::path vertShaderPath = "E:/Dev/Games/RaptureVK/Engine/assets/shaders/SPIRV/pbr.vs.spv";


    auto [shader, handle] = AssetManager::importAsset<Shader>(vertShaderPath);
    m_shader = shader;  


}

FramebufferSpecification ForwardRenderer::getMainFramebufferSpecification()
{
    if (m_swapChain->getImageFormat() == VK_FORMAT_UNDEFINED) {
        RP_CORE_ERROR("ForwardRenderer - Attempted to create render pass before swap chain was initialized!");
        throw std::runtime_error("ForwardRenderer - Attempted to create render pass before swap chain was initialized!");
    }

    FramebufferSpecification spec;
    spec.depthAttachment = m_swapChain->getDepthImageFormat();
    //spec.stencilAttachment = m_swapChain->getDepthImageFormat();
    spec.colorAttachments.push_back(m_swapChain->getImageFormat());

    return spec;

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
    config.dynamicState = dynamicState;
    config.inputAssemblyState = inputAssembly;
    config.viewportState = viewportState;
    config.rasterizationState = rasterizer;
    config.multisampleState = multisampling;
    config.colorBlendState = colorBlending;
    config.vertexInputState = vertexInputInfo;
    config.depthStencilState = depthStencil;
    config.framebufferSpec = getMainFramebufferSpecification();
    config.shader = m_shader;

    m_graphicsPipeline = std::make_shared<GraphicsPipeline>(config);

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

void ForwardRenderer::setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer)
{
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();

    VkImageMemoryBarrier colorBarrier{};
    // Image layout transitions for dynamic rendering
    colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // Always start from undefined for the first transition
    colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.image = m_swapChain->getImages()[m_currentFrame];
    colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorBarrier.subresourceRange.baseMipLevel = 0;
    colorBarrier.subresourceRange.levelCount = 1;
    colorBarrier.subresourceRange.baseArrayLayer = 0;
    colorBarrier.subresourceRange.layerCount = 1;
    colorBarrier.srcAccessMask = 0;  // No access before
    colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    

    VkImageMemoryBarrier depthBarrier{};
    if (m_swapChain->getDepthTexture() && m_swapChain->getDepthTexture()->getImage()) {
        depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // Always start from undefined for depth
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = m_swapChain->getDepthTexture()->getImage();
        depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (m_swapChain->getDepthImageFormat() == VK_FORMAT_D32_SFLOAT_S8_UINT || m_swapChain->getDepthImageFormat() == VK_FORMAT_D24_UNORM_S8_UINT) {
            depthBarrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;
        depthBarrier.srcAccessMask = 0;
        depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkImageMemoryBarrier barriers[2] = {depthBarrier, colorBarrier};

        vkCmdPipelineBarrier(
            commandBuffer->getCommandBufferVk(),
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0,
            0, nullptr,
            0, nullptr,
            2, barriers
        );
    } else {
         vkCmdPipelineBarrier(
            commandBuffer->getCommandBufferVk(),
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &colorBarrier
        );
    }
}

void ForwardRenderer::beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer)
{

    VkRenderingAttachmentInfoKHR colorAttachmentInfo{};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    colorAttachmentInfo.imageView = m_swapChain->getImageViews()[m_currentFrame];
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentInfo.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    


    VkRenderingAttachmentInfoKHR depthAttachmentInfo{};
    bool hasDepth = m_swapChain->getDepthTexture() && m_swapChain->getDepthTexture()->getImageView() != VK_NULL_HANDLE;
    if (hasDepth) {
        depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
        depthAttachmentInfo.imageView = m_swapChain->getDepthTexture()->getImageView();
        depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // Or STORE if needed later
        depthAttachmentInfo.clearValue.depthStencil = {1.0f, 0};
    }


    VkRenderingInfoKHR renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = m_swapChain->getExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = hasDepth ? &depthAttachmentInfo : VK_NULL_HANDLE;
    renderingInfo.pStencilAttachment = VK_NULL_HANDLE; // Not used currently

    vkCmdBeginRendering(commandBuffer->getCommandBufferVk(), &renderingInfo);

}

void ForwardRenderer::cleanupSwapChain() {

    vkDeviceWaitIdle(m_device);


    m_commandBuffers.clear();

    m_graphicsPipeline.reset();

    m_swapChain->destroy();

    m_currentFrame = 0;
}

void ForwardRenderer::recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, uint32_t imageIndex, std::shared_ptr<Scene> activeScene)
{

    RAPTURE_PROFILE_FUNCTION();


    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr; // Optional

    if (vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to begin recording command buffer!");
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    // Get the VulkanContext to check if dynamic vertex input is supported
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();


    setupDynamicRenderingMemoryBarriers(commandBuffer);
    beginDynamicRendering(commandBuffer);
    m_graphicsPipeline->bind(commandBuffer->getCommandBufferVk());

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_swapChain->getExtent().width);
    viewport.height = static_cast<float>(m_swapChain->getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer->getCommandBufferVk(), 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapChain->getExtent();
    vkCmdSetScissor(commandBuffer->getCommandBufferVk(), 0, 1, &scissor);



    // Get entities with TransformComponent and MeshComponent
    auto& registry = activeScene->getRegistry();
    auto view = registry.view<TransformComponent, MeshComponent, MaterialComponent>();
    

    for (auto entity : view) {

        RAPTURE_PROFILE_SCOPE("Draw Mesh");


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
            vulkanContext.vkCmdSetVertexInputEXT(commandBuffer->getCommandBufferVk(), 
                1, &bindingDescription,
                static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data());
        }
        
        // Push the model matrix as a push constant
        PushConstants pushConstants{};
        pushConstants.model = transform.transformMatrix();
        pushConstants.camPos = transform.translation();

        vkCmdPushConstants(commandBuffer->getCommandBufferVk(), 
            m_graphicsPipeline->getPipelineLayoutVk(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
            0, 
            sizeof(PushConstants), 
            &pushConstants);
        
        // Bind vertex buffers
        VkBuffer vertexBuffers[] = {mesh->getVertexBuffer()->getBufferVk()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer->getCommandBufferVk(), 0, 1, vertexBuffers, offsets);


        
        // Bind descriptor sets (only view/proj and material now)materialComp.material->getDescriptorSet()
        VkDescriptorSet descriptorSets[] = {m_descriptorSets[m_currentFrame], materialComp.material->getDescriptorSet()};
        uint32_t descriptorSetCount = sizeof(descriptorSets) / sizeof(descriptorSets[0]);
        vkCmdBindDescriptorSets(commandBuffer->getCommandBufferVk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline->getPipelineLayoutVk(), 
            0, descriptorSetCount, descriptorSets, 0, nullptr);
        
        // Bind index buffer
        vkCmdBindIndexBuffer(commandBuffer->getCommandBufferVk(), mesh->getIndexBuffer()->getBufferVk(), 0, mesh->getIndexBuffer()->getIndexType());
        
        // Draw the mesh
        vkCmdDrawIndexed(commandBuffer->getCommandBufferVk(), mesh->getIndexCount(), 1, 0, 0, 0);
    }
    

    vkCmdEndRendering(commandBuffer->getCommandBufferVk());

    // only transition to present layout if in presentation mode
if (SwapChain::renderMode == RenderMode::PRESENTATION) {
    // Add pipeline barrier to transition to present layout
    VkImageMemoryBarrier presentBarrier{};
    presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.image = m_swapChain->getImages()[imageIndex];  
    presentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    presentBarrier.subresourceRange.baseMipLevel = 0;
    presentBarrier.subresourceRange.levelCount = 1;
    presentBarrier.subresourceRange.baseArrayLayer = 0;
    presentBarrier.subresourceRange.layerCount = 1;
    presentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    presentBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &presentBarrier
    );
}

    if (vkEndCommandBuffer(commandBuffer->getCommandBufferVk()) != VK_SUCCESS) {
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

    
    cleanupSwapChain();


    m_swapChain->recreate();
    setupGraphicsPipeline();
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
