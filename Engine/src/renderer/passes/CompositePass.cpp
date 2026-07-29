#include "CompositePass.h"
#include "window_context/Application.h"

#include "asset_manager/AssetManager.h"
#include "buffers/descriptors/DescriptorManager.h"
#include "render_targets/SceneRenderTarget.h"
#include "renderer/RenderSettings.h"
#include "textures/Texture.h"

#include "logging/Log.h"
#include "logging/TracyProfiler.h"

#include <thread>

namespace Rapture {

struct CompositePushConstants {
    uint32_t sceneColorHandle;
    float exposureStops;
    uint32_t renderFlags;
    uint32_t debugTextureHandle;
};

/**
 * @brief The texture a display override wants shown in place of the scene
 * @param settings The view's display overrides, or nullptr for none
 * @param targets The frame's pass targets
 * @return The texture to show, or nullptr if no override is active
 */
static Texture *Composite_selectDebugTexture(const RenderSettings *settings, const RenderPassTargets &targets)
{
    if (settings == nullptr) {
        return nullptr;
    }

    if ((settings->flags & RENDER_SHOW_SSSR_HIT) != 0u) {
        return targets.sssrHit;
    }
    if ((settings->flags & RENDER_SHOW_SSSR_RESOLVED) != 0u) {
        return targets.sssrResolved;
    }
    if ((settings->flags & (RENDER_SHOW_SSSR_ACCUMULATED | RENDER_SHOW_SSSR_CONFIDENCE)) != 0u) {
        return targets.sssrAccumulated;
    }
    if ((settings->flags & (RENDER_SHOW_AMBIENT_OCCLUSION | RENDER_SHOW_BENT_NORMALS)) != 0u) {
        return targets.ambientOcclusion;
    }
    return nullptr;
}

CompositePass::CompositePass(float width, float height, VkFormat colorFormat)
    : m_colorFormat(colorFormat), m_width(width), m_height(height)
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    m_rc = &vc.getRenderContext();

    auto shaderPath = app.getProject().getProjectShaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl/";
    if (!Texture_isSrgbFormat(m_colorFormat)) {
        shaderConfig.compileInfo.macros.push_back({"COMPOSITE_APPLY_SRGB_ENCODE"});
    }

    auto asset = AssetManager::importAsset(shaderPath / "glsl/Composite.fs.glsl", shaderConfig);
    m_shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_shader) {
        m_shaderAssets.push_back(std::move(asset));
    }

    createPipeline();
}

CompositePass::~CompositePass()
{
    m_pipeline.reset();
}

void CompositePass::updateAttachments(const RenderPassContext &context)
{
    // The presented image is a raw VkImage, not a Texture, so this pass drives its own rendering info
    (void)context;
}

SecondaryBufferInheritance CompositePass::getInheritance(const RenderPassContext &context)
{
    (void)context;

    SecondaryBufferInheritance inheritance;
    inheritance.colorFormats = {m_colorFormat};

    return inheritance;
}

CommandBuffer *CompositePass::record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance)
{
    RAPTURE_PROFILE_FUNCTION();

    if (context.targets == nullptr || context.targets->sceneColorHdr == nullptr) {
        RP_CORE_ERROR("No HDR scene colour to composite");
        return nullptr;
    }

    auto &vc = Application::getInstance().getVulkanContext();

    CommandPoolConfig config = {};
    config.queueFamilyIndex = vc.getGraphicsQueueIndex();
    config.flags = 0;
    config.threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    auto hash = m_rc->commandPoolManager->createCommandPool(config);

    auto pool = m_rc->commandPoolManager->getCommandPool(hash);
    auto commandBuffer = pool->getSecondaryCommandBuffer();

    commandBuffer->beginSecondary(inheritance);

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

    m_pipeline->bind(commandBuffer->getCommandBufferVk());

    Texture *debugTexture = Composite_selectDebugTexture(context.settings, *context.targets);

    CompositePushConstants pushConstants;
    pushConstants.sceneColorHandle = context.targets->sceneColorHdr->getBindlessIndex();
    pushConstants.exposureStops = m_exposureStops;
    // A view with no texture behind it falls back to the scene rather than sampling a stale handle
    pushConstants.renderFlags = debugTexture != nullptr ? context.settings->flags : RENDER_NONE;
    pushConstants.debugTextureHandle = debugTexture != nullptr ? debugTexture->getBindlessIndex() : 0;

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_pipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(CompositePushConstants), &pushConstants);

    m_rc->descriptorManager->getDescriptorSet(3)->bind(commandBuffer->getCommandBufferVk(), m_pipeline);

    vkCmdDraw(commandBuffer->getCommandBufferVk(), 6, 1, 0, 0);

    commandBuffer->end();

    return commandBuffer;
}

void CompositePass::beginRendering(const RenderPassContext &context, CommandBuffer *primaryCb)
{
    SceneRenderTarget *renderTarget = context.renderTarget;
    if (renderTarget == nullptr) {
        RP_CORE_ERROR("No render target to composite into");
        return;
    }

    VkImage targetImage = renderTarget->getImage(context.imageIndex);
    VkImageView targetImageView = renderTarget->getImageView(context.imageIndex);

    setupMemoryBarriers(primaryCb, targetImage, context.targets->sceneColorHdr);

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
    renderingInfo.renderArea.extent = renderTarget->getExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = nullptr;
    renderingInfo.pStencilAttachment = nullptr;
    renderingInfo.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;

    vkCmdBeginRendering(primaryCb->getCommandBufferVk(), &renderingInfo);
}

void CompositePass::endRendering(CommandBuffer *primaryCb)
{
    vkCmdEndRendering(primaryCb->getCommandBufferVk());
}

void CompositePass::onResize(uint32_t width, uint32_t height)
{
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);
}

void CompositePass::setupMemoryBarriers(CommandBuffer *primaryCb, VkImage targetImage, Texture *sceneColor)
{
    VkImageMemoryBarrier sceneColorBarrier =
        sceneColor->getImageMemoryBarrier(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    vkCmdPipelineBarrier(primaryCb->getCommandBufferVk(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &sceneColorBarrier);

    VkImageMemoryBarrier colorBarrier{};
    colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.image = targetImage;
    colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorBarrier.subresourceRange.baseMipLevel = 0;
    colorBarrier.subresourceRange.levelCount = 1;
    colorBarrier.subresourceRange.baseArrayLayer = 0;
    colorBarrier.subresourceRange.layerCount = 1;
    colorBarrier.srcAccessMask = 0;
    colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(primaryCb->getCommandBufferVk(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &colorBarrier);
}

void CompositePass::createPipeline()
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
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

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

    FramebufferSpecification framebufferSpec;
    framebufferSpec.colorAttachments.push_back(m_colorFormat);

    GraphicsPipelineConfiguration config;
    config.dynamicState = dynamicState;
    config.inputAssemblyState = inputAssembly;
    config.viewportState = viewportState;
    config.rasterizationState = rasterizer;
    config.multisampleState = multisampling;
    config.colorBlendState = colorBlending;
    config.vertexInputState = vertexInputInfo;
    config.depthStencilState = depthStencil;
    config.framebufferSpec = framebufferSpec;
    config.shader = m_shader;

    m_pipeline = std::make_shared<GraphicsPipeline>(config);
}

} // namespace Rapture
