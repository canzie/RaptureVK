#include "ImmediateShapesDrawPass.h"

#include "app/Application.h"
#include "assets/asset_manager/AssetManager.h"
#include "core/utils/EnginePaths.h"
#include "core/utils/TracyProfiler.h"
#include "core/utils/rp_assert.h"
#include "gpu/descriptors/DescriptorManager.h"
#include "scene/Scene.h"
#include "scene/render_data/SceneRenderData.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <bit>
#include <thread>

namespace Rapture {

static constexpr VkDeviceSize SHAPE_BUFFER_INITIAL_BYTES = 64 * 1024;

// Producers resubmit much the same shapes every frame, so a drop that survives a window is a real
// drop rather than a gap between two busy frames
static constexpr uint32_t SHAPE_BUFFER_WINDOW_FRAMES = 60;

struct ImmediateShapesPushConstants {
    glm::vec2 viewportSize;
    uint32_t cameraSSBOIndex;
    uint32_t cameraSlotIndex;
    uint32_t shapeOffset;
};

ImmediateShapesDrawPass::ImmediateShapesDrawPass(const ImmediateShapesDrawPassConfig &config,
                                                 const ImmediateDrawList *drawList)
    : m_drawList(drawList), m_config(config), m_targetBytes(SHAPE_BUFFER_INITIAL_BYTES),
      m_width(static_cast<float>(config.width)), m_height(static_cast<float>(config.height))
{
    RP_ASSERT(drawList != nullptr, "Immediate shapes draw pass needs a draw list to draw");

    m_rc = &Application::getInstance().getVulkanContext().getRenderContext();

    const auto shaderPath = EnginePaths::shaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl/";

    m_segmentShader =
        AssetPtr<Shader>(AssetManager::importAsset(shaderPath / "glsl/ImmediateSegment.fs.glsl", shaderConfig));
    m_triangleShader =
        AssetPtr<Shader>(AssetManager::importAsset(shaderPath / "glsl/ImmediateTriangle.fs.glsl", shaderConfig));

    const uint32_t slotCount = m_config.framesInFlight + 1;
    m_shapeBuffers.resize(slotCount);
    m_shapeSets.resize(slotCount);
    m_bufferBytes.resize(slotCount, 0);

    for (uint32_t slot = 0; slot < slotCount; ++slot) {
        buildBuffer(slot, m_targetBytes);
    }

    createPipelines();
}

ImmediateShapesDrawPass::~ImmediateShapesDrawPass()
{
    m_segmentPipeline.reset();
    m_trianglePipeline.reset();
}

void ImmediateShapesDrawPass::buildBuffer(uint32_t slot, VkDeviceSize bytes)
{
    VmaAllocator allocator = m_rc->vulkanContext->getVmaAllocator();

    m_shapeSets[slot].reset();
    m_shapeBuffers[slot] = std::make_unique<StorageBuffer>(bytes, BufferUsage::STREAM, allocator);
    m_bufferBytes[slot] = bytes;

    DescriptorSetBindings bindings;
    bindings.setNumber = 4;

    DescriptorSetBinding shapeBinding = {};
    shapeBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shapeBinding.location = DescriptorSetBindingLocation::CUSTOM_0;
    bindings.bindings.push_back(shapeBinding);

    auto set = std::make_unique<DescriptorSet>(bindings);
    set->getSSBOBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*m_shapeBuffers[slot]);
    m_shapeSets[slot] = std::move(set);
}

VkDeviceSize ImmediateShapesDrawPass::requiredBytes() const
{
    VkDeviceSize bytes = 0;

    for (uint32_t mode = 0; mode < DEPTH_MODE_COUNT; ++mode) {
        const DepthMode depthMode = static_cast<DepthMode>(mode);
        bytes += m_drawList->getSegments(depthMode).size() * sizeof(LineSegment);
        bytes += m_drawList->getTriangleVertices(depthMode).size() * sizeof(ShapeVertex);
    }

    return bytes;
}

void ImmediateShapesDrawPass::updateTargetBytes(VkDeviceSize required)
{
    m_windowPeakBytes = std::max(m_windowPeakBytes, required);
    ++m_windowFrames;

    if (required > m_targetBytes) {
        m_targetBytes = std::bit_ceil(required);
    }

    if (m_windowFrames >= SHAPE_BUFFER_WINDOW_FRAMES) {
        const VkDeviceSize fitted = std::max(SHAPE_BUFFER_INITIAL_BYTES, std::bit_ceil(m_windowPeakBytes));
        m_targetBytes = std::min(m_targetBytes, fitted);
        m_windowPeakBytes = 0;
        m_windowFrames = 0;
    }
}

void ImmediateShapesDrawPass::createPipelines()
{
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                 VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE};

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

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
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

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
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    FramebufferSpecification framebufferSpec;
    framebufferSpec.colorAttachments.push_back(m_config.colorFormat);
    framebufferSpec.depthAttachment = m_config.depthFormat;

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

    config.shader = m_segmentShader.get();
    m_segmentPipeline = std::make_shared<GraphicsPipeline>(config);

    config.shader = m_triangleShader.get();
    m_trianglePipeline = std::make_shared<GraphicsPipeline>(config);
}

void ImmediateShapesDrawPass::updateAttachments(const RenderPassContext &context)
{
    RenderPassAttachment colorAttachment;
    colorAttachment.target = RenderTargetImage{context.renderTarget, context.imageIndex};
    colorAttachment.loadOp = RenderPassAttachmentLoadOp::LOAD;
    colorAttachment.storeOp = RenderPassAttachmentStoreOp::STORE;

    m_attachments.colorAttachments.clear();
    m_attachments.colorAttachments.push_back(colorAttachment);

    m_attachments.depthAttachment = {};
    if (context.targets != nullptr && context.targets->depthStencil != nullptr) {
        m_attachments.depthAttachment.target = context.targets->depthStencil;
        m_attachments.depthAttachment.loadOp = RenderPassAttachmentLoadOp::LOAD;
        m_attachments.depthAttachment.storeOp = RenderPassAttachmentStoreOp::STORE;
    }

    m_attachments.stencilAttachment = {};
}

SecondaryBufferInheritance ImmediateShapesDrawPass::getInheritance(const RenderPassContext &context)
{
    (void)context;

    SecondaryBufferInheritance inheritance;
    inheritance.colorFormats = {m_config.colorFormat};
    inheritance.depthFormat = m_config.depthFormat;

    return inheritance;
}

void ImmediateShapesDrawPass::beginRendering(const RenderPassContext &context, CommandBuffer *primaryCb)
{
    // The presented image is picked by image index rather than by frame in flight, and the two do
    // not advance together, so the cached attachments cannot be reused across frames
    invalidateAttachments();

    RenderPass::beginRendering(context, primaryCb);
}

void ImmediateShapesDrawPass::uploadDrawList(uint32_t slot, std::array<DepthModeRange, DEPTH_MODE_COUNT> &ranges,
                                             uint32_t &vertexBase)
{
    StorageBuffer &buffer = *m_shapeBuffers[slot];

    uint32_t segmentCursor = 0;
    for (uint32_t mode = 0; mode < DEPTH_MODE_COUNT; ++mode) {
        const std::vector<LineSegment> &segments = m_drawList->getSegments(static_cast<DepthMode>(mode));

        ranges[mode].firstSegment = segmentCursor;
        ranges[mode].segmentCount = static_cast<uint32_t>(segments.size());

        if (!segments.empty()) {
            buffer.addData(const_cast<LineSegment *>(segments.data()), segments.size() * sizeof(LineSegment),
                           segmentCursor * sizeof(LineSegment));
        }

        segmentCursor += ranges[mode].segmentCount;
    }

    const VkDeviceSize segmentBytes = static_cast<VkDeviceSize>(segmentCursor) * sizeof(LineSegment);
    vertexBase = static_cast<uint32_t>(segmentBytes / sizeof(ShapeVertex));

    uint32_t vertexCursor = 0;
    for (uint32_t mode = 0; mode < DEPTH_MODE_COUNT; ++mode) {
        const std::vector<ShapeVertex> &vertices = m_drawList->getTriangleVertices(static_cast<DepthMode>(mode));

        ranges[mode].firstVertex = vertexCursor;
        ranges[mode].vertexCount = static_cast<uint32_t>(vertices.size());

        if (!vertices.empty()) {
            buffer.addData(const_cast<ShapeVertex *>(vertices.data()), vertices.size() * sizeof(ShapeVertex),
                           segmentBytes + vertexCursor * sizeof(ShapeVertex));
        }

        vertexCursor += ranges[mode].vertexCount;
    }
}

CommandBuffer *ImmediateShapesDrawPass::record(const RenderPassContext &context,
                                               const SecondaryBufferInheritance &inheritance)
{
    RAPTURE_PROFILE_FUNCTION();

    if (m_drawList->empty() || context.scene == nullptr) {
        return nullptr;
    }

    SceneRenderData *renderData = context.scene->getRenderData();
    if (renderData == nullptr) {
        return nullptr;
    }

    m_currentSlot = (m_currentSlot + 1) % static_cast<uint32_t>(m_shapeBuffers.size());

    updateTargetBytes(requiredBytes());
    if (m_bufferBytes[m_currentSlot] != m_targetBytes) {
        buildBuffer(m_currentSlot, m_targetBytes);
    }

    std::array<DepthModeRange, DEPTH_MODE_COUNT> ranges;
    uint32_t vertexBase = 0;
    uploadDrawList(m_currentSlot, ranges, vertexBase);

    CommandPoolConfig poolConfig = {};
    poolConfig.queueFamilyIndex = m_rc->vulkanContext->getGraphicsQueueIndex();
    poolConfig.flags = 0;
    poolConfig.threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    auto hash = m_rc->commandPoolManager->createCommandPool(poolConfig);

    auto pool = m_rc->commandPoolManager->getCommandPool(hash);
    auto commandBuffer = pool->getSecondaryCommandBuffer();
    VkCommandBuffer cb = commandBuffer->getCommandBufferVk();

    commandBuffer->beginSecondary(inheritance);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = m_width;
    viewport.height = m_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)};
    vkCmdSetScissor(cb, 0, 1, &scissor);

    const uint32_t cameraSlot = renderData->getCameraSlot(context.camera.getEntity());

    ImmediateShapesPushConstants pushConstants{};
    pushConstants.viewportSize = glm::vec2(m_width, m_height);
    pushConstants.cameraSSBOIndex = renderData->getCameras().getDescriptorIndex(context.frameInFlight);
    pushConstants.cameraSlotIndex = (cameraSlot != UINT32_MAX) ? cameraSlot : 0;

    for (uint32_t mode = 0; mode < DEPTH_MODE_COUNT; ++mode) {
        const DepthModeRange &range = ranges[mode];
        if (range.segmentCount == 0 && range.vertexCount == 0) {
            continue;
        }

        vkCmdSetDepthTestEnable(cb, mode == DEPTH_MODE_TESTED ? VK_TRUE : VK_FALSE);

        if (range.vertexCount > 0) {
            m_trianglePipeline->bind(cb);
            m_rc->descriptorManager->bindSet(0, commandBuffer, m_trianglePipeline);
            m_shapeSets[m_currentSlot]->bind(cb, m_trianglePipeline);

            pushConstants.shapeOffset = vertexBase + range.firstVertex;
            vkCmdPushConstants(cb, m_trianglePipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(ImmediateShapesPushConstants), &pushConstants);
            vkCmdDraw(cb, range.vertexCount, 1, 0, 0);
        }

        if (range.segmentCount > 0) {
            m_segmentPipeline->bind(cb);
            m_rc->descriptorManager->bindSet(0, commandBuffer, m_segmentPipeline);
            m_shapeSets[m_currentSlot]->bind(cb, m_segmentPipeline);

            pushConstants.shapeOffset = range.firstSegment;
            vkCmdPushConstants(cb, m_segmentPipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(ImmediateShapesPushConstants), &pushConstants);
            vkCmdDraw(cb, 6, range.segmentCount, 0, 0);
        }
    }

    commandBuffer->end();

    return commandBuffer;
}

void ImmediateShapesDrawPass::onResize(uint32_t width, uint32_t height)
{
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);
}

} // namespace Rapture
