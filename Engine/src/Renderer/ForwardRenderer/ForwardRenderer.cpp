#include "ForwardRenderer.h"

#include "Logging/Log.h"
#include "WindowContext/VulkanContext/VulkanContext.h"
#include "WindowContext/Application.h"

#include "Events/ApplicationEvents.h"
#include "Events/InputEvents.h"

#include "AssetManager/AssetManager.h"

#include <chrono>

namespace Rapture {

    const std::vector<Vertex> g_vertices = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
    };

    const std::vector<uint16_t> g_indices = {
        0, 1, 2, 2, 3, 0
    };


    // Static member definitions
    std::shared_ptr<Renderpass> ForwardRenderer::m_renderPass = nullptr;
    std::shared_ptr<SwapChain> ForwardRenderer::m_swapChain = nullptr;
    std::shared_ptr<Shader> ForwardRenderer::m_shader = nullptr;
    std::shared_ptr<GraphicsPipeline> ForwardRenderer::m_graphicsPipeline = nullptr;
    std::shared_ptr<CommandPool> ForwardRenderer::m_commandPool = nullptr;
    std::shared_ptr<VertexBuffer> ForwardRenderer::m_vertexBuffer = nullptr;
    std::shared_ptr<IndexBuffer> ForwardRenderer::m_indexBuffer = nullptr;

    std::vector<std::shared_ptr<FrameBuffer>> ForwardRenderer::m_framebuffers = {};
    std::vector<std::shared_ptr<CommandBuffer>> ForwardRenderer::m_commandBuffers = {};

    std::vector<VkSemaphore> ForwardRenderer::m_imageAvailableSemaphores = {};
    std::vector<VkSemaphore> ForwardRenderer::m_renderFinishedSemaphores = {};
    std::vector<VkFence> ForwardRenderer::m_inFlightFences = {};

    std::vector<std::shared_ptr<UniformBuffer>> ForwardRenderer::m_uniformBuffers = {};
    std::vector<UniformBufferObject> ForwardRenderer::m_ubos = {};

    VmaAllocator ForwardRenderer::m_vmaAllocator = nullptr;
    VkDevice ForwardRenderer::m_device = nullptr;
    VkQueue ForwardRenderer::m_graphicsQueue = nullptr;
    VkQueue ForwardRenderer::m_presentQueue = nullptr;

    VkDescriptorPool ForwardRenderer::m_descriptorPool = nullptr;
    std::vector<VkDescriptorSet> ForwardRenderer::m_descriptorSets = {};

    std::shared_ptr<MaterialInstance> ForwardRenderer::m_defaultMaterial = nullptr;

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


        setupSwapChain();

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
        setupVertexBuffer();
        setupIndexBuffer();
        setupCommandBuffers();
        setupSyncObjects();
}

void ForwardRenderer::shutdown()
{
    int imageCount = m_swapChain->getImageCount();
    cleanupSwapChain();

    m_vertexBuffer.reset();
    m_indexBuffer.reset();

    // Now safe to destroy VMA allocator


    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

    m_uniformBuffers.clear();
    MaterialManager::shutdown();
    AssetManager::shutdown();
    m_defaultMaterial.reset();
    m_shader.reset();

    for (size_t i = 0; i < imageCount; i++) {
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }

    m_commandPool.reset();
    CommandPoolManager::shutdown();
}

void ForwardRenderer::drawFrame()
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


    m_commandBuffers[m_currentFrame]->reset();
    recordCommandBuffer(m_commandBuffers[m_currentFrame]->getCommandBufferVk(), imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffers[] = {m_commandBuffers[m_currentFrame]->getCommandBufferVk()};
    submitInfo.pCommandBuffers = commandBuffers;

    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;


    updateUniformBuffers();


    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to submit draw command buffer!");
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {m_swapChain->getSwapChainVk()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr; // Optional


    result = vkQueuePresentKHR(m_presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapChain();
        return;
    } else if (result != VK_SUCCESS) {
        RP_CORE_ERROR("failed to present swap chain image!");
        throw std::runtime_error("failed to present swap chain image!");
    }

    m_currentFrame = (m_currentFrame + 1) % m_swapChain->getImageCount();
}

void ForwardRenderer::setupSwapChain()
{
    m_swapChain = std::make_shared<SwapChain>();
    m_swapChain->invalidate();

}

void ForwardRenderer::setupShaders()
{
    const std::filesystem::path vertShaderPath = "E:/Dev/Games/RaptureVK/Engine/assets/shaders/SPIRV/default.vs.spv";


    auto [shader, handle] = AssetManager::importAsset<Shader>(vertShaderPath);
    m_shader = shader;  

    MaterialManager::printMaterialNames();
    auto baseMat = MaterialManager::getMaterial("material");
    m_defaultMaterial = std::make_shared<MaterialInstance>(baseMat);

}

void ForwardRenderer::setupRenderPass()
{
    if (m_swapChain->getImageFormat() == VK_FORMAT_UNDEFINED) {
        RP_CORE_ERROR("ForwardRenderer - Attempted to create render pass before swap chain was initialized!");
        throw std::runtime_error("ForwardRenderer - Attempted to create render pass before swap chain was initialized!");
    }

    // Create the color attachment description
    SubpassAttachmentUsage colorAttachment{};
    
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

    // Create the subpass info
    SubpassInfo subpassInfo{};
    subpassInfo.colorAttachments.push_back(colorAttachment);
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
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();



    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

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
void ForwardRenderer::setupVertexBuffer()
{
    VkDeviceSize bufferSize = sizeof(g_vertices[0]) * g_vertices.size();

    m_vertexBuffer = std::make_shared<VertexBuffer>(bufferSize, BufferUsage::STATIC, m_vmaAllocator);
    m_vertexBuffer->addDataGPU((void*)g_vertices.data(), bufferSize, 0);
}

void ForwardRenderer::setupIndexBuffer()
{


    VkDeviceSize bufferSize = sizeof(g_indices[0]) * g_indices.size();

    m_indexBuffer = std::make_shared<IndexBuffer>(bufferSize, BufferUsage::STATIC, m_vmaAllocator);
    m_indexBuffer->addDataGPU((void*)g_indices.data(), bufferSize, 0);
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
void ForwardRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
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

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

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

    VkBuffer vertexBuffers[] = {m_vertexBuffer->getBufferVk()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    VkDescriptorSet descriptorSets[] = {m_descriptorSets[m_currentFrame], m_defaultMaterial->getDescriptorSet()};
    uint32_t descriptorSetCount = sizeof(descriptorSets) / sizeof(descriptorSets[0]);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline->getPipelineLayoutVk(subpassIndex), 
    0, descriptorSetCount, descriptorSets, 0, nullptr);

    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer->getBufferVk(), 0, VK_INDEX_TYPE_UINT16);

    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(g_indices.size()), 1, 0, 0, 0);

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
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    //m_ubos.resize(m_swapChain->getImageCount());

    for (size_t i = 0; i < m_swapChain->getImageCount(); i++) {
        auto buffer = std::make_shared<UniformBuffer>(bufferSize, BufferUsage::STREAM, m_vmaAllocator);
        m_uniformBuffers.push_back(buffer);
        m_ubos.push_back({});
        m_uniformBuffers[i]->addData((void*)&m_ubos[i], sizeof(m_ubos[i]), 0);
    }

}

void ForwardRenderer::updateUniformBuffers() {
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    UniformBufferObject ubo{};
    glm::mat4 translation = glm::mat4(1.0f);
    translation = glm::translate(translation, glm::vec3(m_zoom, 0.0f, 0.0f));
    ubo.model = glm::rotate(translation, time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    ubo.proj = glm::perspective(glm::radians(45.0f), m_swapChain->getExtent().width / (float) m_swapChain->getExtent().height, 0.1f, 10.0f);
    ubo.proj[1][1] *= -1;


    m_uniformBuffers[m_currentFrame]->addData((void*)&ubo, sizeof(ubo), 0);
}
void ForwardRenderer::createDescriptorPool()
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(m_swapChain->getImageCount());

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    poolInfo.maxSets = static_cast<uint32_t>(m_swapChain->getImageCount());

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
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i]->getBufferVk();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;

        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        descriptorWrite.pImageInfo = nullptr; // Optional
        descriptorWrite.pTexelBufferView = nullptr; // Optional

        vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    }

}



}
