#include "LightingPass.h"
#include "window_context/Application.h"

#include "buffers/descriptors/DescriptorManager.h"
#include "components/Components.h"
#include "components/systems/Environment.h"
#include "renderer/ImageBasedLighting.h"
#include "renderer/RenderSettings.h"
#include "renderer/SceneRenderData.h"
#include "renderer/shadows/ShadowMapping.h"

#include "logging/Log.h"
#include "logging/TracyProfiler.h"

namespace Rapture {

struct LightingPushConstants {

    glm::vec4 cameraPos;

    uint32_t lightDataSSBOIndex;
    uint32_t lightStaticCount;
    uint32_t lightDynamicOffset;
    uint32_t lightDynamicCount;
    uint32_t shadowDataSSBOIndex;
    uint32_t shadowStaticCount;
    uint32_t shadowDynamicOffset;
    uint32_t shadowDynamicCount;

    uint32_t GBufferAlbedoHandle;
    uint32_t GBufferNormalHandle;
    uint32_t cameraSSBOIndex;
    uint32_t cameraSlotIndex;
    uint32_t GBufferMaterialHandle;
    uint32_t GBufferDepthHandle;
    uint32_t GBufferShadingModelHandle;

    uint32_t lightingFlags;
    uint32_t probeVolumeHandle;
    uint32_t probeIrradianceHandle;
    uint32_t probeVisibilityHandle;
    uint32_t probeOffsetHandle;
    uint32_t probeClassificationHandle;
    uint32_t brdfLutHandle;
    uint32_t prefilteredEnvHandle;
    float prefilteredEnvMipCount;
    float skyIntensity;
};

LightingPass::LightingPass(float width, float height, DynamicDiffuseGI *ddgi, VkFormat colorFormat)
    : m_colorFormat(colorFormat), m_ddgi(ddgi), m_width(width), m_height(height)
{

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    m_rc = &vc.getRenderContext();
    m_device = vc.getLogicalDevice();
    m_vmaAllocator = vc.getVmaAllocator();

    auto &project = app.getProject();

    auto shaderPath = project.getProjectShaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl/";

    auto asset = AssetManager::importAsset(shaderPath / "glsl/DeferredLighting.fs.glsl", shaderConfig);
    m_shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_shader) m_shaderAssets.push_back(std::move(asset));

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
    // spec.depthAttachment = VK_FORMAT_D32_SFLOAT; // Standard depth format
    spec.colorAttachments.push_back(m_colorFormat);

    return spec;
}

CommandBuffer *LightingPass::record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance)
{
    RAPTURE_PROFILE_FUNCTION();

    Scene &activeScene = *context.scene;
    Entity camera = context.camera;
    Texture *target = context.targets->sceneColorHdr;
    uint32_t frameIndex = context.frameInFlight;
    uint32_t lightingFlags = context.settings->flags;

    auto &vc = Application::getInstance().getVulkanContext();

    CommandPoolConfig config = {};
    config.queueFamilyIndex = vc.getGraphicsQueueIndex();
    config.flags = 0;
    size_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    config.threadId = threadId;
    auto hash = m_rc->commandPoolManager->createCommandPool(config);

    auto pool = m_rc->commandPoolManager->getCommandPool(hash);
    auto commandBuffer = pool->getSecondaryCommandBuffer();

    commandBuffer->beginSecondary(inheritance);

    const TextureSpecification &targetSpec = target->getSpecification();

    // Update dimensions from target extent
    m_width = static_cast<float>(targetSpec.width);
    m_height = static_cast<float>(targetSpec.height);

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
    scissor.extent = {targetSpec.width, targetSpec.height};
    vkCmdSetScissor(commandBuffer->getCommandBufferVk(), 0, 1, &scissor);

    glm::vec3 cameraPos = glm::vec3(0.0f);
    uint32_t cameraSlotIndex = 0;

    if (camera.isValid()) {
        cameraPos = camera.tryGetComponent<TransformComponent>()->translation();
        auto *cameraComp = camera.tryGetComponent<CameraComponent>();
        if (cameraComp != nullptr && cameraComp->renderDataSlot != UINT32_MAX) {
            cameraSlotIndex = cameraComp->renderDataSlot;
        }
    } else {
        RP_CORE_WARN("No main camera found!");
    }

    LightingPushConstants pushConstants;
    pushConstants.cameraPos = glm::vec4(cameraPos, 1.0f);

    pushConstants.GBufferAlbedoHandle = context.targets->gbufferBaseColor->getBindlessIndex();
    pushConstants.GBufferNormalHandle = context.targets->gbufferNormalMotion->getBindlessIndex();
    pushConstants.cameraSSBOIndex = activeScene.getRenderData()->getCameras().getDescriptorIndex(frameIndex);
    pushConstants.cameraSlotIndex = cameraSlotIndex;
    pushConstants.GBufferMaterialHandle = context.targets->gbufferMaterial->getBindlessIndex();
    pushConstants.GBufferDepthHandle = context.targets->depthStencil->getBindlessIndex();
    pushConstants.GBufferShadingModelHandle = context.targets->gbufferShadingModel->getBindlessIndex();

    // DDGI indirect requires the GI system to exist; fall back to ambient otherwise
    pushConstants.lightingFlags = m_ddgi ? lightingFlags : (lightingFlags & ~RENDER_USE_GLOBAL_ILLUMINATION);

    // Already 0 until the bake finishes, which the shader samples harmlessly
    Environment *environment = activeScene.environment();
    ImageBasedLighting *ibl = environment != nullptr ? environment->getImageBasedLighting() : nullptr;
    pushConstants.brdfLutHandle = ibl != nullptr ? ibl->getBrdfLutBindlessIndex() : 0;

    pushConstants.prefilteredEnvHandle = ibl != nullptr ? ibl->getPrefilteredBindlessIndex() : 0;
    pushConstants.prefilteredEnvMipCount = ibl != nullptr ? static_cast<float>(ibl->getPrefilteredMipCount()) : 1.0f;

    Entity environmentEntity = activeScene.environmentEntity();
    pushConstants.skyIntensity =
        environmentEntity.hasComponent<SkyboxComponent>() ? environmentEntity.getComponent<SkyboxComponent>().skyIntensity : 1.0f;

    auto &renderData = *(activeScene.getRenderData());
    auto &lightStore = renderData.getLights();
    pushConstants.lightDataSSBOIndex = lightStore.getDescriptorIndex(frameIndex);
    pushConstants.lightStaticCount = lightStore.getPartition(MOBILITY_STATIC).getCount();
    pushConstants.lightDynamicOffset = lightStore.getGlobalSlot(MOBILITY_DYNAMIC, 0);
    pushConstants.lightDynamicCount = lightStore.getPartition(MOBILITY_DYNAMIC).getCount();

    auto &shadowStore = renderData.getShadows();
    pushConstants.shadowDataSSBOIndex = shadowStore.getDescriptorIndex(frameIndex);
    pushConstants.shadowStaticCount = shadowStore.getPartition(MOBILITY_STATIC).getCount();
    pushConstants.shadowDynamicOffset = shadowStore.getGlobalSlot(MOBILITY_DYNAMIC, 0);
    pushConstants.shadowDynamicCount = shadowStore.getPartition(MOBILITY_DYNAMIC).getCount();

    // Get probe texture indices from DDGI system
    if (m_ddgi) {
        pushConstants.probeVolumeHandle = 0; // Probe volume data is in set 0, binding 5
        pushConstants.probeIrradianceHandle = m_ddgi->getProbeIrradianceBindlessIndex();
        pushConstants.probeVisibilityHandle = m_ddgi->getProbeVisibilityBindlessIndex();
        pushConstants.probeOffsetHandle = m_ddgi->getProbeOffsetBindlessIndex();
        pushConstants.probeClassificationHandle = m_ddgi->getProbeClassificationBindlessIndex();
    } else {
        pushConstants.probeVolumeHandle = 0;
        pushConstants.probeIrradianceHandle = 0;
        pushConstants.probeVisibilityHandle = 0;
        pushConstants.probeOffsetHandle = 0;
        pushConstants.probeClassificationHandle = 0;
    }

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_pipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(LightingPushConstants), &pushConstants);

    // light and shadow and probe volume data
    m_rc->descriptorManager->getDescriptorSet(0)->bind(commandBuffer->getCommandBufferVk(), m_pipeline);
    // bindless textures for gbuffer textures
    m_rc->descriptorManager->getDescriptorSet(3)->bind(commandBuffer->getCommandBufferVk(), m_pipeline);

    // Draw 6 vertices for 2 triangles
    vkCmdDraw(commandBuffer->getCommandBufferVk(), 6, 1, 0, 0);

    commandBuffer->end();

    return commandBuffer;
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
    config.shader = m_shader;

    m_pipeline = std::make_shared<GraphicsPipeline>(config);
}

void LightingPass::updateAttachments(const RenderPassContext &context)
{
    RenderPassAttachment colorAttachment;
    colorAttachment.texture = context.targets->sceneColorHdr;
    colorAttachment.loadOp = RenderPassAttachmentLoadOp::CLEAR;
    colorAttachment.storeOp = RenderPassAttachmentStoreOp::STORE;
    colorAttachment.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    m_attachments.colorAttachments.clear();
    m_attachments.colorAttachments.push_back(colorAttachment);

    m_attachments.depthAttachment = {};
    m_attachments.stencilAttachment = {};
}

void LightingPass::onResize(uint32_t width, uint32_t height)
{
    // TODO: see GBufferPass::onResize, passes are destroyed and rebuilt on resize today
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);
}
} // namespace Rapture
