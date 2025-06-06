#include "Renderer/StencilBorderPass.h"

#include "WindowContext/Application.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "StencilBorderPass.h"
#include "Components/Components.h"

namespace Rapture {


struct PushConstants {
    glm::mat4 model;
    glm::vec4 color;
    float borderWidth;
};

StencilBorderPass::StencilBorderPass(
    float width, float height, 
    uint32_t framesInFlight, 
    std::vector<std::shared_ptr<Texture>> depthStencilTextures,
    std::vector<std::shared_ptr<UniformBuffer>> cameraUBOs)
    : m_width(width), 
      m_height(height), 
      m_framesInFlight(framesInFlight), 
      m_depthStencilTextures(depthStencilTextures),
      m_currentImageIndex(0), 
      m_selectedEntity(nullptr),
      m_pipeline(nullptr),
      m_swapChain(nullptr),
      m_cameraUBOs(cameraUBOs) {

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();
    
    m_device = vc.getLogicalDevice();
    m_vmaAllocator = vc.getVmaAllocator();
    m_swapChain = vc.getSwapChain();

    auto& project = app.getProject();

    auto shaderPath = project.getProjectShaderDirectory();

    m_width = m_swapChain->getExtent().width;
    m_height = m_swapChain->getExtent().height;

    auto [shader, handle] = AssetManager::importAsset<Shader>(shaderPath / "SPIRV/StencilBorder.vs.spv");


    m_shader = shader;
    m_shaderHandle = handle;

    createPipeline();
    createDescriptorSets(framesInFlight);

    m_entitySelectedListenerId = GameEvents::onEntitySelected().addListener(
        [this](std::shared_ptr<Rapture::Entity> entity) {
            m_selectedEntity = entity;
        }
    );
}

StencilBorderPass::~StencilBorderPass() {
    GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);
}

void StencilBorderPass::recordCommandBuffer(
    std::shared_ptr<CommandBuffer> commandBuffer, 
    uint32_t swapchainImageIndex,
    uint32_t currentFrameInFlight, 
    std::shared_ptr<Scene> activeScene) {

    RAPTURE_PROFILE_FUNCTION();

    if (m_selectedEntity == nullptr) {
        return;
    }

    auto [transformComp, meshComp] = m_selectedEntity->tryGetComponents<TransformComponent, MeshComponent>();
   
   
    if (transformComp == nullptr || meshComp == nullptr || meshComp->mesh == nullptr) {
        return;
    }

    m_currentImageIndex = swapchainImageIndex;
    
    setupDynamicRenderingMemoryBarriers(commandBuffer);
    beginDynamicRendering(commandBuffer);
    m_pipeline->bind(commandBuffer->getCommandBufferVk());

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();

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



    auto& model = transformComp->transforms.getTransform();
    auto mesh = meshComp->mesh;



    PushConstants pushConstants;
    pushConstants.model = model;
    pushConstants.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    pushConstants.borderWidth = 1.4f;

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), 
        m_pipeline->getPipelineLayoutVk(),
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, 
        0,
        sizeof(PushConstants), 
        &pushConstants);

    // Bind descriptor sets using frameInFlightIndex for GBuffer/UBO consistency
    VkDescriptorSet descriptorSetsToBind[] = {m_descriptorSets[currentFrameInFlight]->getDescriptorSet()};
    uint32_t descriptorSetCount = sizeof(descriptorSetsToBind) / sizeof(descriptorSetsToBind[0]);
    vkCmdBindDescriptorSets(commandBuffer->getCommandBufferVk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipelineLayoutVk(), 
        0, descriptorSetCount, descriptorSetsToBind, 0, nullptr);
    
    // Get the vertex buffer layout
    auto& bufferLayout = mesh->getVertexBuffer()->getBufferLayout();
    
    // Convert to EXT variants required by vkCmdSetVertexInputEXT
    auto bindingDescription = bufferLayout.getBindingDescription2EXT();
        
    auto attributeDescriptions = bufferLayout.getAttributeDescriptions2EXT();
        
    // Use the function pointer from VulkanContext
    vc.vkCmdSetVertexInputEXT(commandBuffer->getCommandBufferVk(), 
        1, &bindingDescription,
        static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data());
    

    
    // Bind vertex buffers
    VkBuffer vertexBuffers[] = {mesh->getVertexBuffer()->getBufferVk()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBufferVk(), 0, 1, vertexBuffers, offsets);


    // Bind index buffer
    vkCmdBindIndexBuffer(commandBuffer->getCommandBufferVk(), mesh->getIndexBuffer()->getBufferVk(), 0, mesh->getIndexBuffer()->getIndexType());
    
    // Draw the mesh
    vkCmdDrawIndexed(commandBuffer->getCommandBufferVk(), mesh->getIndexCount(), 1, 0, 0, 0);


    vkCmdEndRendering(commandBuffer->getCommandBufferVk());

}

void StencilBorderPass::createPipeline() {
    RAPTURE_PROFILE_FUNCTION();


    if (m_shader.expired()) {
        RP_CORE_ERROR("StencilBorderPass: Shader not loaded, cannot create pipeline.");
        return;
    }
    auto shaderShared = m_shader.lock();
    if (!shaderShared) {
        RP_CORE_ERROR("StencilBorderPass: Shader is null after lock, cannot create pipeline.");
        return;
    }

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
    viewport.width = m_width;
    viewport.height = m_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)};

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

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
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS; // No depth test

    GraphicsPipelineConfiguration config;
    config.dynamicState = dynamicState;
    config.inputAssemblyState = inputAssembly;
    config.viewportState = viewportState;
    config.rasterizationState = rasterizer;
    config.multisampleState = multisampling;
    config.colorBlendState = colorBlending;
    config.vertexInputState = vertexInputInfo;
    config.depthStencilState = depthStencil;

    FramebufferSpecification spec;
    spec.colorAttachments.push_back(m_swapChain->getImageFormat());

    config.framebufferSpec = spec;

    config.shader = shaderShared;

    m_pipeline = std::make_shared<GraphicsPipeline>(config);
}

void StencilBorderPass::createDescriptorSets(uint32_t framesInFlight) {

    VkDescriptorSetLayout layout = m_shader.lock()->getDescriptorSetLayouts()[static_cast<uint32_t>(DESCRIPTOR_SET_INDICES::COMMON_RESOURCES)];
    m_descriptorSets.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {

        DescriptorSetBindings bindings;
        bindings.layout = layout;

        // First binding: Stencil texture sampler (binding 0)
        DescriptorSetBinding stencilBinding;
        stencilBinding.binding = 1;
        stencilBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        stencilBinding.count = 1;
        stencilBinding.viewType = TextureViewType::STENCIL;
        stencilBinding.resource = m_depthStencilTextures[i];
        bindings.bindings.push_back(stencilBinding);

        // Second binding: Camera UBO (binding 1)
        DescriptorSetBinding cameraBinding;
        cameraBinding.binding = 0;
        cameraBinding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cameraBinding.count = 1;
        cameraBinding.resource = m_cameraUBOs[i];
        bindings.bindings.push_back(cameraBinding);

        m_descriptorSets[i] = std::make_shared<DescriptorSet>(bindings);
    }

}

void StencilBorderPass::beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer) {

if (m_swapChain != nullptr) {
    VkRenderingAttachmentInfo colorAttachmentInfo{};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachmentInfo.imageView = m_swapChain->getImageViews()[m_currentImageIndex];
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentInfo.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    


    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = m_swapChain->getExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = VK_NULL_HANDLE;
    renderingInfo.pStencilAttachment = VK_NULL_HANDLE;

    vkCmdBeginRendering(commandBuffer->getCommandBufferVk(), &renderingInfo);

}

}

void StencilBorderPass::setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer) {

    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();

    VkImageMemoryBarrier colorBarrier{};
    // Image layout transitions for dynamic rendering
    colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    colorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; 
    colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.image = m_swapChain->getImages()[m_currentImageIndex];
    colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorBarrier.subresourceRange.baseMipLevel = 0;
    colorBarrier.subresourceRange.levelCount = 1;
    colorBarrier.subresourceRange.baseArrayLayer = 0;
    colorBarrier.subresourceRange.layerCount = 1;
    colorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    

    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &colorBarrier
    );

}



} // namespace Rapture

