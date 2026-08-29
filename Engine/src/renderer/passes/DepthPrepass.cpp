#include "renderer/passes/DepthPrepass.h"

#include "app/Application.h"
#include "core/utils/EnginePaths.h"
#include "core/utils/Log.h"
#include "core/utils/TracyProfiler.h"
#include "gpu/descriptors/DescriptorManager.h"
#include "renderer/MDIBatch.h"
#include "renderer/SceneGeometryDraw.h"
#include "scene/Scene.h"
#include "scene/render_data/SceneRenderData.h"

#include <thread>

namespace Rapture {

struct DepthPrepassPushConstants {
    uint32_t batchInfoBufferIndex;
    uint32_t cameraSSBOIndex;
    uint32_t cameraSlotIndex;
    uint32_t meshSSBOIndex;
    uint32_t skeletonSSBOIndex;
};

DepthPrepass::DepthPrepass(float width, float height, VkFormat depthFormat)
    : m_width(width), m_height(height), m_depthFormat(depthFormat)
{
    m_rc = &Application::getInstance().getVulkanContext().getRenderContext();

    createPipeline(false);
    createPipeline(true);

    setupCommandResources();
}

DepthPrepass::~DepthPrepass()
{
    m_pipeline.reset();
    m_skinnedPipeline.reset();
}

FramebufferSpecification DepthPrepass::getFramebufferSpecification() const
{
    FramebufferSpecification spec;
    spec.depthAttachment = m_depthFormat;

    return spec;
}

void DepthPrepass::setupCommandResources()
{
    CommandPoolConfig config = {};
    config.queueFamilyIndex = m_rc->vulkanContext->getGraphicsQueueIndex();
    config.flags = 0;

    m_commandPoolHash = m_rc->commandPoolManager->createCommandPool(config);
}

void DepthPrepass::createPipeline(bool skinned)
{
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                 VK_DYNAMIC_STATE_VERTEX_INPUT_EXT};

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // the batch sets its own layout through vkCmdSetVertexInputEXT
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    auto shaderPath = EnginePaths::shaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl";
    if (skinned) {
        shaderConfig.compileInfo.macros.emplace_back("IS_SKINNED_MESH");
    }

    Ref<AShader> shader = AssetManager::importAsset<AShader>(shaderPath / "glsl/DepthPrepass.vs.glsl", shaderConfig);

    if (!shader) {
        RP_CORE_ERROR("Failed to load depth prepass vertex shader");
        return;
    }

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
    config.shader = shader.operator->();

    if (skinned) {
        m_skinnedShader = shader;
        m_skinnedPipeline = std::make_shared<GraphicsPipeline>(config);
        return;
    }

    m_shader = shader;
    m_pipeline = std::make_shared<GraphicsPipeline>(config);
}

void DepthPrepass::updateAttachments(const RenderPassContext &context)
{
    m_attachments.colorAttachments.clear();

    m_attachments.depthAttachment = {};
    m_attachments.depthAttachment.target = context.targets->depthStencil;
    m_attachments.depthAttachment.loadOp = RenderPassAttachmentLoadOp::CLEAR;
    m_attachments.depthAttachment.storeOp = RenderPassAttachmentStoreOp::STORE;
    m_attachments.depthAttachment.clearDepth = 1.0f;
    m_attachments.depthAttachment.clearStencil = 0;

    m_attachments.stencilAttachment = {};
}

void DepthPrepass::onResize(uint32_t width, uint32_t height)
{
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);
}

CommandBuffer *DepthPrepass::record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance)
{
    RAPTURE_PROFILE_FUNCTION();

    SceneGeometryDraw *geometry = context.opaqueGeometry;
    if (m_pipeline == nullptr || geometry == nullptr) {
        return nullptr;
    }

    Scene &activeScene = *context.scene;
    SceneRenderData *renderData = activeScene.getRenderData();
    if (renderData == nullptr) {
        return nullptr;
    }

    CommandPoolConfig config = {};
    config.queueFamilyIndex = m_rc->vulkanContext->getGraphicsQueueIndex();
    config.flags = 0;
    config.threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    auto hash = m_rc->commandPoolManager->createCommandPool(config);

    auto pool = m_rc->commandPoolManager->getCommandPool(hash);
    auto commandBuffer = pool->getSecondaryCommandBuffer();

    commandBuffer->beginSecondary(inheritance);

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

    m_rc->descriptorManager->bindSet(0, commandBuffer, m_pipeline);
    m_rc->descriptorManager->bindSet(2, commandBuffer, m_pipeline);

    const uint32_t currentFrame = context.frameInFlight;
    uint32_t cameraSlot = renderData->getCameraSlot(context.camera.getEntity());

    DepthPrepassPushConstants pushConstants{};
    pushConstants.cameraSSBOIndex = renderData->getCameras().getDescriptorIndex(currentFrame);
    pushConstants.cameraSlotIndex = (cameraSlot != UINT32_MAX) ? cameraSlot : 0;
    pushConstants.meshSSBOIndex = renderData->getMeshes().getDescriptorIndex(currentFrame);
    pushConstants.skeletonSSBOIndex = renderData->getSkeletonInstanceManager().getBindlessIndex();

    GraphicsPipeline *boundPipeline = m_pipeline.get();

    for (MDIBatch *batch : geometry->batches(currentFrame)) {
        RAPTURE_PROFILE_SCOPE("Draw Batch");

        // a batch is keyed on its vertex layout, so it is skinned or unskinned as a whole
        bool isSkinned = batch->getBufferLayout().getAttributeOffset(BufferAttributeID::JOINTS_0) != UINT32_MAX;
        GraphicsPipeline *pipeline = isSkinned ? m_skinnedPipeline.get() : m_pipeline.get();
        if (pipeline == nullptr) {
            continue;
        }

        if (pipeline != boundPipeline) {
            pipeline->bind(commandBuffer->getCommandBufferVk());
            boundPipeline = pipeline;
        }

        geometry->bindBatch(commandBuffer, batch);

        pushConstants.batchInfoBufferIndex = batch->getBatchInfoBufferIndex();

        const Shader *shader = isSkinned ? m_skinnedShader.operator->() : m_shader.operator->();
        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        if (shader != nullptr && shader->getPushConstantLayouts().size() > 0) {
            stageFlags = shader->getPushConstantLayouts()[0].stageFlags;
        }

        vkCmdPushConstants(commandBuffer->getCommandBufferVk(), pipeline->getPipelineLayoutVk(), stageFlags, 0,
                           sizeof(DepthPrepassPushConstants), &pushConstants);

        auto indirectBuffer = batch->getIndirectBuffer();
        if (indirectBuffer) {
            vkCmdDrawIndexedIndirect(commandBuffer->getCommandBufferVk(), indirectBuffer->getBufferVk(), 0, batch->getDrawCount(),
                                     sizeof(VkDrawIndexedIndirectCommand));
        }
    }

    commandBuffer->end();

    return commandBuffer;
}

} // namespace Rapture
