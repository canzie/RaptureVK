#include "LightingPass.h"
#include "WindowContext/Application.h"

#include "Renderer/Shadows/ShadowCommon.h"
#include "Renderer/Shadows/ShadowMapping/ShadowMapping.h"

#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"

namespace Rapture {

struct PushConstants {

    glm::vec3 cameraPos;
};

LightingPass::LightingPass(
    float width, 
    float height, 
    uint32_t framesInFlight, 
    std::shared_ptr<GBufferPass> gBufferPass, 
    std::vector<std::shared_ptr<UniformBuffer>> shadowDataUBOs)
    : m_framesInFlight(framesInFlight), 
    m_currentFrame(0), 
    m_width(width), 
    m_height(height), 
    m_gBufferPass(gBufferPass), 
    m_shadowDataUBOs(shadowDataUBOs) {

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();
    
    m_device = vc.getLogicalDevice();
    m_vmaAllocator = vc.getVmaAllocator();
    m_swapChain = vc.getSwapChain();

    auto& project = app.getProject();

    auto shaderPath = project.getProjectShaderDirectory();

    m_width = m_swapChain->getExtent().width;
    m_height = m_swapChain->getExtent().height;

    auto [shader, handle] = AssetManager::importAsset<Shader>(shaderPath / "SPIRV/DeferredLighting.vs.spv");


    m_shader = shader;
    m_handle = handle;

    createPipeline();
    createLightUBOs(framesInFlight);
    createDescriptorSets(framesInFlight);

}

LightingPass::~LightingPass() {
    // Clean up descriptor sets
    m_descriptorSets.clear();

    m_lightUBOs.clear();

    m_pipeline.reset();

    m_swapChain.reset();
}

// TODO: Remove swapchain dependency
FramebufferSpecification LightingPass::getFramebufferSpecification()
{
    if (m_swapChain->getImageFormat() == VK_FORMAT_UNDEFINED) {
        RP_CORE_ERROR("LightingPass - Attempted to create render pass before swap chain was initialized!");
        throw std::runtime_error("LightingPass - Attempted to create render pass before swap chain was initialized!");
    }

    FramebufferSpecification spec;
    spec.depthAttachment = m_swapChain->getDepthImageFormat();
    //spec.stencilAttachment = m_swapChain->getDepthImageFormat();
    spec.colorAttachments.push_back(m_swapChain->getImageFormat());

    return spec;
}

void LightingPass::recordCommandBuffer(
    std::shared_ptr<CommandBuffer> commandBuffer, 
    std::shared_ptr<Scene> activeScene,
    uint32_t swapchainImageIndex,
    uint32_t frameInFlightIndex) {

    RAPTURE_PROFILE_FUNCTION();

    m_currentFrame = swapchainImageIndex;  

    updateLightUBOs(activeScene); 

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

    auto camera = activeScene->getSettings().mainCamera;
    glm::vec3 cameraPos = glm::vec3(0.0f);

    if (camera != nullptr) {
        cameraPos = camera->getComponent<TransformComponent>().translation();
    } else {
        RP_CORE_WARN("LightingPass - No main camera found!");
    }

    PushConstants pushConstants;
    pushConstants.cameraPos = cameraPos;

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), 
        m_pipeline->getPipelineLayoutVk(),
        VK_SHADER_STAGE_FRAGMENT_BIT, 
        0,
        sizeof(PushConstants), 
        &pushConstants);

    // Bind descriptor sets using frameInFlightIndex for GBuffer/UBO consistency
    // First bind set 0 (common resources)
    VkDescriptorSet commonResourcesSet = m_descriptorSets[frameInFlightIndex]->getDescriptorSet();
    vkCmdBindDescriptorSets(commandBuffer->getCommandBufferVk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipelineLayoutVk(), 
        static_cast<uint32_t>(DESCRIPTOR_SET_INDICES::COMMON_RESOURCES), 1, &commonResourcesSet, 0, nullptr);
    
    // Then bind set 3 (bindless shadow textures)
    VkDescriptorSet bindlessSet = ShadowMap::getBindlessShadowMaps()->getDescriptorSet();
    vkCmdBindDescriptorSets(commandBuffer->getCommandBufferVk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipelineLayoutVk(), 
        static_cast<uint32_t>(DESCRIPTOR_SET_INDICES::EXTRA_RESOURCES), 1, &bindlessSet, 0, nullptr);
    
        
    // Draw 6 vertices for 2 triangles
    vkCmdDraw(commandBuffer->getCommandBufferVk(), 6, 1, 0, 0);

    vkCmdEndRendering(commandBuffer->getCommandBufferVk());

}

void LightingPass::createPipeline() {


    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
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
    scissor.extent = {(uint32_t)m_width, (uint32_t)m_height};

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
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;


    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
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
    config.framebufferSpec = getFramebufferSpecification();
    config.shader = m_shader.lock();

    m_pipeline = std::make_shared<GraphicsPipeline>(config);

}

// need gbuffer textures
// camera position
// light data
// shadow data
void LightingPass::createDescriptorSets(uint32_t framesInFlight) {

    if (m_gBufferPass == nullptr) {
        RP_CORE_ERROR("LightingPass - GBufferPass is not set!");
        return;
    }

    if (m_shader.expired()) {
        RP_CORE_ERROR("LightingPass - Shader is not set!");
        return;
    }

    m_descriptorSets.resize(framesInFlight);

    VkDescriptorSetLayout layout = m_shader.lock()->getDescriptorSetLayouts()[static_cast<uint32_t>(DESCRIPTOR_SET_INDICES::COMMON_RESOURCES)];

    for (uint32_t i = 0; i < framesInFlight; i++) {

        DescriptorSetBindings bindings;
        bindings.layout = layout;

        DescriptorSetBinding binding;
        binding.binding = 0;
        binding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.count = 1;
        binding.resource = m_gBufferPass->getPositionDepthTextures()[i];
        bindings.bindings.push_back(binding);

        binding.binding = 1;
        binding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.count = 1;
        binding.resource = m_gBufferPass->getNormalTextures()[i];
        bindings.bindings.push_back(binding);

        binding.binding = 2;
        binding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.count = 1;
        binding.resource = m_gBufferPass->getAlbedoSpecTextures()[i];
        bindings.bindings.push_back(binding);

        binding.binding = 3;
        binding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.count = 1;
        binding.resource = m_gBufferPass->getMaterialTextures()[i];
        bindings.bindings.push_back(binding);

        binding.binding = 4;
        binding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.count = 1;
        binding.resource = m_lightUBOs[i];
        bindings.bindings.push_back(binding);

        binding.binding = 5;
        binding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.count = 1;
        binding.resource = m_shadowDataUBOs[i];
        bindings.bindings.push_back(binding);

        m_descriptorSets[i] = std::make_shared<DescriptorSet>(bindings);
    
    }

}

void LightingPass::updateLightUBOs(std::shared_ptr<Scene> activeScene) {
    if (!activeScene) {
        return;
    }

    auto& registry = activeScene->getRegistry();
    auto lightView = registry.view<TransformComponent, LightComponent>();
    
    // Check if any lights or transforms have changed
    bool lightsChanged = m_lightsChanged;
    
    for (auto entity : lightView) {
        auto& transform = lightView.get<TransformComponent>(entity);
        auto& lightComp = lightView.get<LightComponent>(entity);
        

        if (true) { // to force true for testing
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
            glm::quat rotationQuat = transform.transforms.getRotationQuat();
            direction = glm::normalize(rotationQuat * glm::vec3(0, 0, -1)); // Forward vector
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
        
        lightData.spotAngles.z = (uint32_t)entity;
        
        lightUbo.numLights++;
    }
    
    // Update light uniform buffers for ALL frames in flight
    for (size_t i = 0; i < m_lightUBOs.size(); i++) {
        m_lightUBOs[i]->addData((void*)&lightUbo, sizeof(lightUbo), 0);
    }

    m_lightsChanged = false;
    
}

void LightingPass::createLightUBOs(uint32_t framesInFlight) {

    m_lightUBOs.resize(framesInFlight);
    LightUniformBufferObject initialLightUbo{}; // Zero-initialize
    initialLightUbo.numLights = 0; // Explicitly set numLights to 0

    for (uint32_t i = 0; i < framesInFlight; i++) {
        m_lightUBOs[i] = std::make_shared<UniformBuffer>(sizeof(LightUniformBufferObject), BufferUsage::DYNAMIC, m_vmaAllocator);
        // Initialize the UBO with the default data
        m_lightUBOs[i]->addData(&initialLightUbo, sizeof(initialLightUbo), 0);
    }

}

void LightingPass::beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer) {

if (m_swapChain != nullptr) {
    VkRenderingAttachmentInfo colorAttachmentInfo{};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachmentInfo.imageView = m_swapChain->getImageViews()[m_currentFrame];
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
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

void LightingPass::setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer) {

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