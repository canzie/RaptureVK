#include "renderer/SceneQueryRenderer.h"

#include "utils/EnginePaths.h"
#include "buffers/descriptors/DescriptorManager.h"

#include "components/Components.h"
#include "logging/Log.h"
#include "logging/TracyProfiler.h"
#include "pipelines/GraphicsPipeline.h"
#include "renderer/Frustum.h"
#include "renderer/MDIBatch.h"
#include "renderer/SceneRenderData.h"
#include "scenes/Scene.h"
#include "shaders/Shader.h"
#include "window_context/Application.h"
#include "window_context/vulkan_context/VulkanContext.h"

#include <algorithm>
#include <cstring>

namespace Rapture {

struct SceneQueryPushConstants {
    alignas(16) glm::mat4 viewProj;
    alignas(4) uint32_t batchInfoBufferIndex;
    alignas(4) uint32_t meshSSBOIndex;
    alignas(4) uint32_t countBufferIndex;
    alignas(4) uint32_t entryBufferIndex;
    alignas(4) uint32_t regionWidth;
    alignas(4) uint32_t regionHeight;
    alignas(4) uint32_t maxLayers;
};

/**
 * @brief One appended hit as the fragment shader writes it
 */
struct SceneQueryGpuEntry {
    alignas(4) uint32_t entity;
    alignas(4) uint32_t depthBits;
};

/**
 * @brief Projection whose frustum covers only the query's region of the viewport
 *
 * The region's pixel rect is mapped onto the whole of NDC through Vulkan's viewport transform, so
 * the result carries whatever handedness the camera's projection already has.
 *
 * @param projection The camera's projection
 * @param viewportWidth Width of the image the region's pixels are in
 * @param viewportHeight Height of the image the region's pixels are in
 * @param region The region to cover
 * @return The projection narrowed to the region
 */
static glm::mat4 s_regionProjection(const glm::mat4 &projection, uint32_t viewportWidth, uint32_t viewportHeight,
                                    const SceneQuery &region)
{
    const float fullWidth = static_cast<float>(viewportWidth);
    const float fullHeight = static_cast<float>(viewportHeight);

    const float scaleX = fullWidth / static_cast<float>(region.width);
    const float scaleY = fullHeight / static_cast<float>(region.height);

    const float centerX = 2.0f * (static_cast<float>(region.x) + static_cast<float>(region.width) * 0.5f) / fullWidth - 1.0f;
    const float centerY = 2.0f * (static_cast<float>(region.y) + static_cast<float>(region.height) * 0.5f) / fullHeight - 1.0f;

    glm::mat4 narrow(1.0f);
    narrow[0][0] = scaleX;
    narrow[1][1] = scaleY;
    narrow[3][0] = -centerX * scaleX;
    narrow[3][1] = -centerY * scaleY;

    return narrow * projection;
}

std::span<const SceneQueryHit> SceneQueryResult::at(uint32_t x, uint32_t y) const
{
    if (x >= width || y >= height || maxLayers == 0) {
        return {};
    }

    const size_t pixel = static_cast<size_t>(y) * width + x;
    const uint32_t kept = std::min(counts[pixel], maxLayers);

    return std::span<const SceneQueryHit>(hits.data() + pixel * maxLayers, kept);
}

bool SceneQueryResult::overflowed(uint32_t x, uint32_t y) const
{
    if (x >= width || y >= height) {
        return false;
    }

    return counts[static_cast<size_t>(y) * width + x] > maxLayers;
}

SceneQueryRenderer::SceneQueryRenderer(RenderContext renderContext) : m_rc(renderContext)
{
    m_geometry = std::make_unique<SceneGeometryDraw>(m_rc, 1);

    createPipeline();

    CommandPoolConfig config{};
    config.name = "SceneQueryRenderer";
    config.queueFamilyIndex = m_rc.vulkanContext->getGraphicsQueueIndex();
    config.flags = 0;
    m_commandPoolHash = m_rc.commandPoolManager->createCommandPool(config);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    if (vkCreateFence(m_rc.vulkanContext->getLogicalDevice(), &fenceInfo, nullptr, &m_fence) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create the scene query fence");
    }
}

SceneQueryRenderer::~SceneQueryRenderer()
{
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_rc.vulkanContext->getLogicalDevice(), m_fence, nullptr);
    }
}

SceneQueryResult SceneQueryRenderer::query(Scene &scene, Entity camera, uint32_t viewportWidth, uint32_t viewportHeight,
                                           const SceneQuery &region)
{
    RAPTURE_PROFILE_FUNCTION();

    if (region.width == 0 || region.height == 0 || region.maxLayers == 0) {
        RP_CORE_ERROR("Scene query region {}x{} at {} layers covers nothing", region.width, region.height, region.maxLayers);
        return {};
    }

    if (viewportWidth == 0 || viewportHeight == 0) {
        RP_CORE_ERROR("Scene query has no viewport size to place its region in");
        return {};
    }

    if (region.x + region.width > viewportWidth || region.y + region.height > viewportHeight) {
        RP_CORE_ERROR("Scene query region {}x{} at ({}, {}) lies outside the {}x{} viewport", region.width, region.height,
                      region.x, region.y, viewportWidth, viewportHeight);
        return {};
    }

    if (m_pipeline == nullptr || m_fence == VK_NULL_HANDLE) {
        return {};
    }

    CameraComponent *cameraComp = camera.isValid() ? camera.tryGetComponent<CameraComponent>() : nullptr;
    if (cameraComp == nullptr) {
        RP_CORE_ERROR("Scene query needs a camera to render from");
        return {};
    }

    const uint32_t pixelCount = region.width * region.height;
    if (!resizeBuffers(pixelCount, region.maxLayers)) {
        return {};
    }

    const glm::mat4 view = cameraComp->camera.getViewMatrix();
    const glm::mat4 projection = cameraComp->camera.getProjectionMatrix();

    // Always culled, and to the region rather than to the camera. A mesh outside the region cannot
    // cover a pixel inside it, so this narrows what is drawn without narrowing the answer.
    Frustum regionFrustum;
    regionFrustum.update(s_regionProjection(projection, viewportWidth, viewportHeight, region), view);
    m_geometry->populate(scene, &regionFrustum, 0);

    const glm::mat4 viewProj = projection * view;

    CommandPool *pool = m_rc.commandPoolManager->getCommandPool(m_commandPoolHash);
    if (pool == nullptr) {
        return {};
    }

    CommandBuffer *commandBuffer = pool->getPrimaryCommandBuffer();
    if (commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to begin recording the scene query");
        return {};
    }

    recordQuery(commandBuffer, scene, viewProj, viewportWidth, viewportHeight, region);

    if (commandBuffer->end() != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to record the scene query");
        return {};
    }

    VkDevice device = m_rc.vulkanContext->getLogicalDevice();
    vkResetFences(device, 1, &m_fence);

    std::shared_ptr<VulkanQueue> graphicsQueue = m_rc.vulkanContext->getGraphicsQueue();
    graphicsQueue->submitAndFlushQueue(commandBuffer, nullptr, nullptr, nullptr, m_fence);

    {
        RAPTURE_PROFILE_SCOPE("Scene Query Wait");
        vkWaitForFences(device, 1, &m_fence, VK_TRUE, UINT64_MAX);
    }

    return readBack(region);
}

bool SceneQueryRenderer::resizeBuffers(uint32_t pixelCount, uint32_t maxLayers)
{
    const uint32_t entryCount = pixelCount * maxLayers;
    if (pixelCount <= m_pixelCapacity && entryCount <= m_entryCapacity) {
        return true;
    }

    const uint32_t newPixelCapacity = std::max(pixelCount, m_pixelCapacity);
    const uint32_t newEntryCapacity = std::max(entryCount, m_entryCapacity);

    VmaAllocator allocator = m_rc.vulkanContext->getVmaAllocator();

    m_countBuffer = std::make_unique<StorageBuffer>(newPixelCapacity * sizeof(uint32_t), BufferUsage::STREAM, allocator,
                                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    m_entryBuffer = std::make_unique<StorageBuffer>(newEntryCapacity * sizeof(SceneQueryGpuEntry), BufferUsage::STREAM, allocator);

    if (m_countBuffer->getBindlessIndex() == UINT32_MAX || m_entryBuffer->getBindlessIndex() == UINT32_MAX) {
        RP_CORE_ERROR("Failed to bind the scene query result buffers");
        m_countBuffer.reset();
        m_entryBuffer.reset();
        m_pixelCapacity = 0;
        m_entryCapacity = 0;
        return false;
    }

    m_pixelCapacity = newPixelCapacity;
    m_entryCapacity = newEntryCapacity;
    return true;
}

void SceneQueryRenderer::recordQuery(CommandBuffer *commandBuffer, Scene &scene, const glm::mat4 &viewProj,
                                     uint32_t viewportWidth, uint32_t viewportHeight, const SceneQuery &region)
{
    RAPTURE_PROFILE_FUNCTION();

    VkCommandBuffer cmd = commandBuffer->getCommandBufferVk();
    const uint32_t pixelCount = region.width * region.height;

    vkCmdFillBuffer(cmd, m_countBuffer->getBufferVk(), 0, pixelCount * sizeof(uint32_t), 0);

    VkMemoryBarrier clearBarrier{};
    clearBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    clearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &clearBarrier, 0,
                         nullptr, 0, nullptr);

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = {region.width, region.height};
    renderingInfo.layerCount = 1;

    vkCmdBeginRendering(cmd, &renderingInfo);

    m_pipeline->bind(cmd);

    // The region is selected by offsetting the full viewport rather than by narrowing the
    // projection, so the region rasterizes exactly the pixels the frame draws at those coordinates
    VkViewport viewport{};
    viewport.x = -static_cast<float>(region.x);
    viewport.y = -static_cast<float>(region.y);
    viewport.width = static_cast<float>(viewportWidth);
    viewport.height = static_cast<float>(viewportHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {region.width, region.height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    m_rc.descriptorManager->bindSet(0, commandBuffer, m_pipeline);
    m_rc.descriptorManager->bindSet(2, commandBuffer, m_pipeline);
    m_rc.descriptorManager->bindSet(3, commandBuffer, m_pipeline);

    const uint32_t frameInFlight = Application::getInstance().getFrameInFlightIndex();

    SceneQueryPushConstants pushConstants{};
    pushConstants.viewProj = viewProj;
    pushConstants.meshSSBOIndex = scene.getRenderData()->getMeshes().getDescriptorIndex(frameInFlight);
    pushConstants.countBufferIndex = m_countBuffer->getBindlessIndex();
    pushConstants.entryBufferIndex = m_entryBuffer->getBindlessIndex();
    pushConstants.regionWidth = region.width;
    pushConstants.regionHeight = region.height;
    pushConstants.maxLayers = region.maxLayers;

    // Taken from the shader rather than from sizeof, since the struct is padded out to the mat4's
    // sixteen byte alignment and the shader block is not
    VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uint32_t pushSize = sizeof(SceneQueryPushConstants);
    if (m_shader != nullptr && !m_shader->getPushConstantLayouts().empty()) {
        const VkPushConstantRange &pushRange = m_shader->getPushConstantLayouts()[0];
        stageFlags = pushRange.stageFlags;
        pushSize = pushRange.size;
    }

    for (MDIBatch *batch : m_geometry->batches(0)) {
        m_geometry->bindBatch(commandBuffer, batch);

        pushConstants.batchInfoBufferIndex = batch->getBatchInfoBufferIndex();
        vkCmdPushConstants(cmd, m_pipeline->getPipelineLayoutVk(), stageFlags, 0, pushSize, &pushConstants);

        std::shared_ptr<StorageBuffer> indirectBuffer = batch->getIndirectBuffer();
        if (indirectBuffer) {
            vkCmdDrawIndexedIndirect(cmd, indirectBuffer->getBufferVk(), 0, batch->getDrawCount(),
                                     sizeof(VkDrawIndexedIndirectCommand));
        }
    }

    vkCmdEndRendering(cmd);

    VkMemoryBarrier readBarrier{};
    readBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    readBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    readBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &readBarrier, 0, nullptr,
                         0, nullptr);
}

SceneQueryResult SceneQueryRenderer::readBack(const SceneQuery &region)
{
    RAPTURE_PROFILE_FUNCTION();

    const uint32_t pixelCount = region.width * region.height;

    SceneQueryResult result;
    result.width = region.width;
    result.height = region.height;
    result.maxLayers = region.maxLayers;
    result.counts.resize(pixelCount);
    result.hits.resize(static_cast<size_t>(pixelCount) * region.maxLayers);

    m_countBuffer->readData(result.counts.data(), pixelCount * sizeof(uint32_t), 0);

    std::vector<SceneQueryGpuEntry> entries(result.hits.size());
    m_entryBuffer->readData(entries.data(), entries.size() * sizeof(SceneQueryGpuEntry), 0);

    for (uint32_t pixel = 0; pixel < pixelCount; pixel++) {
        const uint32_t kept = std::min(result.counts[pixel], region.maxLayers);
        const size_t base = static_cast<size_t>(pixel) * region.maxLayers;

        for (uint32_t slot = 0; slot < kept; slot++) {
            const SceneQueryGpuEntry &entry = entries[base + slot];

            float depth = 0.0f;
            std::memcpy(&depth, &entry.depthBits, sizeof(float));

            result.hits[base + slot] = {entry.entity, depth};
        }

        std::sort(result.hits.begin() + base, result.hits.begin() + base + kept,
                  [](const SceneQueryHit &lhs, const SceneQueryHit &rhs) { return lhs.depth < rhs.depth; });
    }

    return result;
}

void SceneQueryRenderer::createPipeline()
{
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                 VK_DYNAMIC_STATE_VERTEX_INPUT_EXT};

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};

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
    colorBlending.pAttachments = nullptr;

    std::filesystem::path shaderPath = EnginePaths::shaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl";

    AssetRef asset = AssetManager::importAsset(shaderPath / "glsl/SceneQuery.vs.glsl", shaderConfig);
    m_shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;

    if (m_shader == nullptr) {
        RP_CORE_ERROR("Failed to load the scene query shader");
        return;
    }
    m_shaderAssets.push_back(std::move(asset));

    GraphicsPipelineConfiguration config;
    config.dynamicState = dynamicState;
    config.inputAssemblyState = inputAssembly;
    config.viewportState = viewportState;
    config.rasterizationState = rasterizer;
    config.multisampleState = multisampling;
    config.colorBlendState = colorBlending;
    config.vertexInputState = vertexInputInfo;
    config.shader = m_shader;

    m_pipeline = std::make_shared<GraphicsPipeline>(config);
}

} // namespace Rapture
