#include "LightingPass.h"
#include "WindowContext/Application.h"

#include "Components/FogComponent.h"
#include "Renderer/Shadows/ShadowCommon.h"
#include "Renderer/Shadows/ShadowMapping/ShadowMapping.h"

#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"

namespace Rapture {

struct LightingPushConstants {

    glm::vec4 cameraPos;

    uint32_t lightCount;
    uint32_t shadowCount;

    uint32_t GBufferAlbedoHandle;
    uint32_t GBufferNormalHandle;
    uint32_t GBufferPositionHandle;
    uint32_t GBufferMaterialHandle;
    uint32_t GBufferDepthHandle;

    uint32_t useDDGI;
    uint32_t probeVolumeHandle;
    uint32_t probeIrradianceHandle;
    uint32_t probeVisibilityHandle;
    uint32_t probeOffsetHandle;

    // Fog
    glm::vec4 fogColor;     // .rgb = color, .a = enabled
    glm::vec2 fogDistances; // .x = near, .y = far
};

LightingPass::LightingPass(float width, float height, uint32_t framesInFlight, std::shared_ptr<GBufferPass> gBufferPass,
                           std::shared_ptr<DynamicDiffuseGI> ddgi, VkFormat colorFormat)
    : m_framesInFlight(framesInFlight), m_currentFrame(0), m_colorFormat(colorFormat), m_gBufferPass(gBufferPass), m_ddgi(ddgi),
      m_width(width), m_height(height)
{

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    m_device = vc.getLogicalDevice();
    m_vmaAllocator = vc.getVmaAllocator();

    auto &project = app.getProject();

    auto shaderPath = project.getProjectShaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl/ddgi/";

    auto [shader, handle] = AssetManager::importAsset<Shader>(shaderPath / "glsl/DeferredLighting.fs.glsl", shaderConfig);

    m_shader = shader;
    m_handle = handle;

    createPipeline();
}

LightingPass::~LightingPass()
{
    // Clean up descriptor sets
    m_descriptorSets.clear();

    m_lightUBOs.clear();

    m_pipeline.reset();
}

FramebufferSpecification LightingPass::getFramebufferSpecification()
{
    if (m_colorFormat == VK_FORMAT_UNDEFINED) {
        RP_CORE_ERROR("Invalid color format!");
        return FramebufferSpecification{};
    }

    FramebufferSpecification spec;
    spec.depthAttachment = VK_FORMAT_D32_SFLOAT; // Standard depth format
    spec.colorAttachments.push_back(m_colorFormat);

    return spec;
}

void LightingPass::recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<Scene> activeScene,
                                       SceneRenderTarget &renderTarget, uint32_t imageIndex, uint32_t frameInFlightIndex)
{

    RAPTURE_PROFILE_FUNCTION();

    m_currentFrame = frameInFlightIndex;

    // Get render target properties
    VkImage targetImage = renderTarget.getImage(imageIndex);
    VkImageView targetImageView = renderTarget.getImageView(imageIndex);
    VkExtent2D targetExtent = renderTarget.getExtent();

    // Update dimensions from target extent
    m_width = static_cast<float>(targetExtent.width);
    m_height = static_cast<float>(targetExtent.height);

    setupDynamicRenderingMemoryBarriers(commandBuffer, targetImage);
    beginDynamicRendering(commandBuffer, targetImageView, targetExtent);
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
    scissor.extent = targetExtent;
    vkCmdSetScissor(commandBuffer->getCommandBufferVk(), 0, 1, &scissor);

    auto camera = activeScene->getMainCamera();
    glm::vec3 cameraPos = glm::vec3(0.0f);

    if (camera != nullptr) {
        cameraPos = camera.tryGetComponent<TransformComponent>()->translation();
    } else {
        RP_CORE_WARN("No main camera found!");
    }

    LightingPushConstants pushConstants;
    pushConstants.cameraPos = glm::vec4(cameraPos, 1.0f);

    pushConstants.GBufferAlbedoHandle = m_gBufferPass->getAlbedoTextureIndex();
    pushConstants.GBufferNormalHandle = m_gBufferPass->getNormalTextureIndex();
    pushConstants.GBufferPositionHandle = m_gBufferPass->getPositionTextureIndex();
    pushConstants.GBufferMaterialHandle = m_gBufferPass->getMaterialTextureIndex();
    pushConstants.GBufferDepthHandle = m_gBufferPass->getDepthTextureIndex();

    pushConstants.useDDGI = 1;

    // Query FogComponent from scene
    auto fogView = activeScene->getRegistry().view<FogComponent>();
    if (!fogView.empty()) {
        auto &fogComp = fogView.get<FogComponent>(*fogView.begin());
        pushConstants.fogColor = glm::vec4(fogComp.color, fogComp.enabled ? 1.0f : 0.0f);
        pushConstants.fogDistances = glm::vec2(fogComp.start, fogComp.end);
    } else {
        // No fog component - disable fog
        pushConstants.fogColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        pushConstants.fogDistances = glm::vec2(0.0f, 0.0f);
    }

    auto &reg = activeScene->getRegistry();
    auto lightView = reg.view<LightComponent>();
    auto shadowView = reg.view<ShadowComponent>();
    auto cascadedShadowView = reg.view<CascadedShadowComponent>();

    pushConstants.lightCount = static_cast<uint32_t>(lightView.size());
    pushConstants.shadowCount = static_cast<uint32_t>(shadowView.size() + cascadedShadowView.size());

    // Get probe texture indices from DDGI system
    if (m_ddgi) {
        pushConstants.probeVolumeHandle = 0; // Probe volume data is in set 0, binding 5
        pushConstants.probeIrradianceHandle = m_ddgi->getProbeIrradianceBindlessIndex();
        pushConstants.probeVisibilityHandle = m_ddgi->getProbeVisibilityBindlessIndex();
        pushConstants.probeOffsetHandle = m_ddgi->getProbeOffsetBindlessIndex();
    } else {
        pushConstants.probeVolumeHandle = 0;
        pushConstants.probeIrradianceHandle = 0;
        pushConstants.probeVisibilityHandle = 0;
        pushConstants.probeOffsetHandle = 0;
    }

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_pipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(LightingPushConstants), &pushConstants);

    // light and shadow and probe volume data
    DescriptorManager::getDescriptorSet(0)->bind(commandBuffer->getCommandBufferVk(), m_pipeline);
    // bindless textures for gbuffer textures
    DescriptorManager::getDescriptorSet(3)->bind(commandBuffer->getCommandBufferVk(), m_pipeline);

    // Draw 6 vertices for 2 triangles
    vkCmdDraw(commandBuffer->getCommandBufferVk(), 6, 1, 0, 0);

    vkCmdEndRendering(commandBuffer->getCommandBufferVk());
}

void LightingPass::createPipeline()
{

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

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
    rasterizer.depthBiasClamp = 0.0f;          // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f;    // Optional

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;          // Optional
    multisampling.pSampleMask = nullptr;            // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE;      // Optional

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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

void LightingPass::beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer, VkImageView targetImageView,
                                         VkExtent2D targetExtent)
{

    VkRenderingAttachmentInfo colorAttachmentInfo{};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachmentInfo.imageView = targetImageView;
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentInfo.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = targetExtent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = VK_NULL_HANDLE;
    renderingInfo.pStencilAttachment = VK_NULL_HANDLE;

    vkCmdBeginRendering(commandBuffer->getCommandBufferVk(), &renderingInfo);
}

void LightingPass::setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer, VkImage targetImage)
{

    VkImageMemoryBarrier colorBarrier{};
    // Image layout transitions for dynamic rendering
    colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Always start from undefined for the first transition
    colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.image = targetImage;
    colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorBarrier.subresourceRange.baseMipLevel = 0;
    colorBarrier.subresourceRange.levelCount = 1;
    colorBarrier.subresourceRange.baseArrayLayer = 0;
    colorBarrier.subresourceRange.layerCount = 1;
    colorBarrier.srcAccessMask = 0; // No access before
    colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &colorBarrier);
}
} // namespace Rapture