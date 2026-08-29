#include "DDGIProbeDebugPass.h"
#include "assets/shaders/AShader.h"

#include "app/Application.h"
#include "assets/asset_manager/AssetManager.h"
#include "core/utils/EnginePaths.h"
#include "core/utils/TracyProfiler.h"
#include "core/utils/rp_assert.h"
#include "gpu/command_buffers/CommandPool.h"
#include "gpu/descriptors/DescriptorManager.h"
#include "renderer/RenderSettings.h"
#include "renderer/gi/ddgi/DynamicDiffuseGI.h"
#include "scene/Scene.h"
#include "scene/render_data/SceneRenderData.h"

#include <thread>

namespace Rapture {

// rings and sectors of the sphere the vertex shader builds, matched by PROBE_SPHERE_* there
static constexpr uint32_t PROBE_SPHERE_RINGS = 8;
static constexpr uint32_t PROBE_SPHERE_SECTORS = 16;
static constexpr uint32_t PROBE_SPHERE_VERTEX_COUNT = PROBE_SPHERE_RINGS * PROBE_SPHERE_SECTORS * 6;

struct DDGIProbeDebugPushConstants {
    uint32_t cameraSSBOIndex;
    uint32_t cameraSlotIndex;
    uint32_t volumeSlot;
    uint32_t probeIrradianceIndex;
    uint32_t probeDistanceIndex;
    uint32_t probeOffsetIndex;
    uint32_t probeClassificationIndex;
    uint32_t mode;
    float probeRadius;
};

DDGIProbeDebugPass::DDGIProbeDebugPass(const DDGIProbeDebugPassConfig &config, const DynamicDiffuseGI *gi)
    : m_gi(gi), m_config(config), m_width(static_cast<float>(config.width)), m_height(static_cast<float>(config.height))
{
    RP_ASSERT(gi != nullptr, "Probe debug pass needs a volume to draw");

    m_rc = &Application::getInstance().getVulkanContext().getRenderContext();

    const auto shaderPath = EnginePaths::shaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl/";

    m_shader = AssetManager::importAsset(shaderPath / "glsl/ddgi/ProbeDebug.fs.glsl", shaderConfig).as<AShader>();

    createPipeline();
}

DDGIProbeDebugPass::~DDGIProbeDebugPass()
{
    m_pipeline.reset();
}

void DDGIProbeDebugPass::createPipeline()
{
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

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
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    FramebufferSpecification framebufferSpec;
    framebufferSpec.colorAttachments = {m_config.colorFormat};
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
    config.shader = &m_shader.get()->shader();

    m_pipeline = std::make_shared<GraphicsPipeline>(config);
}

void DDGIProbeDebugPass::updateAttachments(const RenderPassContext &context)
{
    RenderPassAttachment colorAttachment;
    colorAttachment.target = context.targets->sceneColorHdr;
    colorAttachment.loadOp = RenderPassAttachmentLoadOp::LOAD;
    colorAttachment.storeOp = RenderPassAttachmentStoreOp::STORE;

    m_attachments.colorAttachments.clear();
    m_attachments.colorAttachments.push_back(colorAttachment);

    m_attachments.depthAttachment = {};
    m_attachments.depthAttachment.target = context.targets->depthStencil;
    m_attachments.depthAttachment.loadOp = RenderPassAttachmentLoadOp::LOAD;
    m_attachments.depthAttachment.storeOp = RenderPassAttachmentStoreOp::STORE;

    m_attachments.stencilAttachment = {};
}

SecondaryBufferInheritance DDGIProbeDebugPass::getInheritance(const RenderPassContext &context)
{
    (void)context;

    SecondaryBufferInheritance inheritance;
    inheritance.colorFormats = {m_config.colorFormat};
    inheritance.depthFormat = m_config.depthFormat;

    return inheritance;
}

CommandBuffer *DDGIProbeDebugPass::record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance)
{
    RAPTURE_PROFILE_FUNCTION();

    if (context.settings == nullptr || !context.settings->showDDGIProbes()) {
        return nullptr;
    }

    if (context.scene == nullptr) {
        return nullptr;
    }

    SceneRenderData *renderData = context.scene->getRenderData();
    if (renderData == nullptr) {
        return nullptr;
    }

    const ProbeVolume &volume = m_gi->getProbeVolume();
    const uint32_t probeCount = volume.gridDimensions.x * volume.gridDimensions.y * volume.gridDimensions.z;
    if (probeCount == 0) {
        return nullptr;
    }

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

    m_pipeline->bind(cb);
    m_rc->descriptorManager->bindSet(0, commandBuffer, m_pipeline);
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_pipeline);

    const uint32_t cameraSlot = renderData->getCameraSlot(context.camera.getEntity());

    DDGIProbeDebugPushConstants pushConstants{};
    pushConstants.cameraSSBOIndex = renderData->getCameras().getDescriptorIndex(context.frameInFlight);
    pushConstants.cameraSlotIndex = (cameraSlot != UINT32_MAX) ? cameraSlot : 0;
    pushConstants.volumeSlot = context.frameInFlight;
    pushConstants.probeIrradianceIndex = m_gi->getProbeIrradianceBindlessIndex();
    pushConstants.probeDistanceIndex = m_gi->getProbeVisibilityBindlessIndex();
    pushConstants.probeOffsetIndex = m_gi->getProbeOffsetBindlessIndex();
    pushConstants.probeClassificationIndex = m_gi->getProbeClassificationBindlessIndex();
    pushConstants.mode = static_cast<uint32_t>(context.settings->probeDebugMode);
    pushConstants.probeRadius = context.settings->probeDebugRadius;

    vkCmdPushConstants(cb, m_pipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(DDGIProbeDebugPushConstants), &pushConstants);

    vkCmdDraw(cb, PROBE_SPHERE_VERTEX_COUNT, probeCount, 0, 0);

    commandBuffer->end();

    return commandBuffer;
}

void DDGIProbeDebugPass::onResize(uint32_t width, uint32_t height)
{
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);
}

} // namespace Rapture
