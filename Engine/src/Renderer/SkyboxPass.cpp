#include "SkyboxPass.h"
#include "WindowContext/Application.h"
#include "AssetManager/AssetManager.h"
#include "Logging/Log.h"
#include "Buffers/Descriptors/DescriptorManager.h"

namespace Rapture {

struct PushConstants {
    uint32_t frameIndex;
    uint32_t skyboxTextureIndex;
};

SkyboxPass::SkyboxPass(std::shared_ptr<Texture> skyboxTexture, std::vector<std::shared_ptr<Texture>> depthTextures)
    : m_skyboxTexture(skyboxTexture), m_depthTextures(depthTextures) {
    
    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();
    
    m_device = vc.getLogicalDevice();
    m_vmaAllocator = vc.getVmaAllocator();
    m_swapChain = vc.getSwapChain();

    m_width = static_cast<float>(m_swapChain->getExtent().width);
    m_height = static_cast<float>(m_swapChain->getExtent().height);
    
    auto& project = app.getProject();
    auto shaderPath = project.getProjectShaderDirectory();
    
    auto [shader, handle] = AssetManager::importAsset<Shader>(shaderPath / "SPIRV/SkyboxPass.vs.spv");
    m_shader = shader;

    createSkyboxGeometry();
    createPipeline();
}

SkyboxPass::SkyboxPass(std::vector<std::shared_ptr<Texture>> depthTextures)
    : m_skyboxTexture(nullptr), m_depthTextures(depthTextures) {
    
    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();
    
    m_device = vc.getLogicalDevice();
    m_vmaAllocator = vc.getVmaAllocator();
    m_swapChain = vc.getSwapChain();

    m_width = static_cast<float>(m_swapChain->getExtent().width);
    m_height = static_cast<float>(m_swapChain->getExtent().height);
    
    auto& project = app.getProject();
    auto shaderPath = project.getProjectShaderDirectory();
    
    auto [shader, handle] = AssetManager::importAsset<Shader>(shaderPath / "SPIRV/SkyboxPass.vs.spv");
    m_shader = shader;

    createSkyboxGeometry();
    createPipeline();
}

SkyboxPass::~SkyboxPass() {
    m_pipeline.reset();
    m_skyboxVertexBuffer.reset();
    m_skyboxIndexBuffer.reset();
}

void SkyboxPass::recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, uint32_t frameInFlightIndex) {
    
    if (!m_skyboxTexture && !m_skyboxTexture->isReadyForSampling()) { // TODO: create a log type to only log once
        //RP_CORE_WARN("SkyboxPass - Skybox texture is not ready for sampling, skipping command buffer.");
        return;
    }

    setupDynamicRenderingMemoryBarriers(commandBuffer, frameInFlightIndex);
    beginDynamicRendering(commandBuffer, frameInFlightIndex);

    m_pipeline->bind(commandBuffer->getCommandBufferVk());

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = m_width;
    viewport.height = m_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer->getCommandBufferVk(), 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)};
    vkCmdSetScissor(commandBuffer->getCommandBufferVk(), 0, 1, &scissor);

    VkBuffer vertexBuffers[] = { m_skyboxVertexBuffer->getBufferVk() };
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBufferVk(), 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer->getCommandBufferVk(), m_skyboxIndexBuffer->getBufferVk(), 0, VK_INDEX_TYPE_UINT32);

    PushConstants pushConstants{};
    pushConstants.frameIndex = frameInFlightIndex;
    pushConstants.skyboxTextureIndex = m_skyboxTexture->getBindlessIndex();

    // Get push constant stage flags from shader reflection data
    VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    if (auto shader = m_shader.lock(); shader && shader->getPushConstantLayouts().size() > 0) {
        stageFlags = shader->getPushConstantLayouts()[0].stageFlags;
    }

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), 
        m_pipeline->getPipelineLayoutVk(),
        stageFlags,
        0,
        sizeof(PushConstants),
        &pushConstants);



    auto cameraSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::CAMERA_UBO);
    if (cameraSet) {
        cameraSet->bind(commandBuffer->getCommandBufferVk(), m_pipeline);
    }

    auto skyboxSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::BINDLESS_TEXTURES);
    if (skyboxSet) {
        skyboxSet->bind(commandBuffer->getCommandBufferVk(), m_pipeline);
    }

    vkCmdDrawIndexed(commandBuffer->getCommandBufferVk(), 36, 1, 0, 0, 0);

    vkCmdEndRendering(commandBuffer->getCommandBufferVk());

    // Transition depth image for shader reading in subsequent passes
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_depthTextures[frameInFlightIndex]->getImage();
    barrier.subresourceRange.aspectMask = getImageAspectFlags(m_depthTextures[frameInFlightIndex]->getSpecification().format);
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}


void SkyboxPass::setSkyboxTexture(std::shared_ptr<Texture> skyboxTexture) {
    if (!skyboxTexture) {
        RP_CORE_ERROR("SkyboxPass - Skybox texture is not set!");
        return;
    }
    m_skyboxTexture = skyboxTexture;
    // Re-create descriptor sets with the new texture
}


void SkyboxPass::createPipeline() {
    auto shader = m_shader.lock();
    if (!shader) {
        RP_CORE_ERROR("SkyboxPass - Shader is not available for pipeline creation.");
        return;
    }

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(glm::vec3);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription.offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    GraphicsPipelineConfiguration config;
    config.dynamicState = dynamicState;
    config.inputAssemblyState = inputAssembly;
    config.viewportState = viewportState;
    config.rasterizationState = rasterizer;
    config.multisampleState = multisampling;
    config.colorBlendState = colorBlending;
    config.vertexInputState = vertexInputInfo;
    config.depthStencilState = depthStencil;
    
    FramebufferSpecification fbSpec;
    fbSpec.depthAttachment = m_depthTextures[0]->getFormat();
    fbSpec.colorAttachments.push_back(m_swapChain->getImageFormat());
    config.framebufferSpec = fbSpec;
    config.shader = shader;
    

    m_pipeline = std::make_shared<GraphicsPipeline>(config);
}


void SkyboxPass::createSkyboxGeometry() {
    std::vector<glm::vec3> vertices = {
        {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f},
        {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f}
    };

    std::vector<uint32_t> indices = {
        0, 1, 2, 2, 3, 0,  // Front
        1, 5, 6, 6, 2, 1,  // Right
        7, 6, 5, 5, 4, 7,  // Back
        4, 0, 3, 3, 7, 4,  // Left
        3, 2, 6, 6, 7, 3,  // Top
        4, 5, 1, 1, 0, 4   // Bottom
    };

    m_skyboxVertexBuffer = std::make_shared<VertexBuffer>(vertices.size() * sizeof(glm::vec3), BufferUsage::STATIC, m_vmaAllocator);
    m_skyboxVertexBuffer->addDataGPU(vertices.data(), vertices.size() * sizeof(glm::vec3), 0);

    m_skyboxIndexBuffer = std::make_shared<IndexBuffer>(indices.size() * sizeof(uint32_t), BufferUsage::STATIC, m_vmaAllocator, VK_INDEX_TYPE_UINT32);
    m_skyboxIndexBuffer->addDataGPU(indices.data(), indices.size() * sizeof(uint32_t), 0);
}

void SkyboxPass::beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer, uint32_t frameInFlightIndex) {
    if (m_swapChain == nullptr) return;

    VkRenderingAttachmentInfo colorAttachmentInfo{};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachmentInfo.imageView = m_swapChain->getImageViews()[frameInFlightIndex];
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachmentInfo{};
    depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachmentInfo.imageView = m_depthTextures[frameInFlightIndex]->getImageView();
    depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = m_swapChain->getExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;
    renderingInfo.pStencilAttachment = VK_NULL_HANDLE;

    vkCmdBeginRendering(commandBuffer->getCommandBufferVk(), &renderingInfo);
}

void SkyboxPass::setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer, uint32_t frameInFlightIndex) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    // We are transitioning the same image, so no need for queue family transfers
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    // The layout is not changing, but we need to synchronize access
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    barrier.image = m_depthTextures[frameInFlightIndex]->getImage();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // G-Buffer pass wrote to the depth buffer
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // Skybox pass will read from it for depth testing
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        // Wait for the G-Buffer's depth writes to complete
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        // Before the skybox pass performs its depth tests
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

} // namespace Rapture 

