#include "renderer/atmosphere/SkyAndAerialPerspectivePass.h"

#include "gpu/descriptors/DescriptorManager.h"
#include "renderer/atmosphere/ViewAtmosphereLookupTablesPass.h"
#include <rapture/app/Application.h>
#include <rapture/assets/asset_manager/AssetImportConfig.h>
#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/core/utils/EnginePaths.h>
#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/TracyProfiler.h>
#include <rapture/core/utils/rp_assert.h>
#include <rapture/gpu/command_buffers/CommandPool.h>
#include <rapture/gpu/textures/Texture.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/render_data/SceneRenderData.h>

namespace Rapture {

struct SkyAndAerialPerspectivePushConstants {
    alignas(4) glm::vec4 cameraPosition; ///< xyz world metres, w = altitude in km
    alignas(4) uint32_t cameraSSBOIndex;
    alignas(4) uint32_t cameraSlotIndex;
    alignas(4) uint32_t depthTexture;
    alignas(4) uint32_t skyViewLut;
    alignas(4) uint32_t aerialPerspectiveVolume;
    alignas(4) float skyIntensity;
};

SkyAndAerialPerspectivePass::SkyAndAerialPerspectivePass(VkFormat colorFormat, const ViewAtmosphereLookupTablesPass &lookupTables)
    : m_lookupTables(lookupTables), m_colorFormat(colorFormat)
{
    m_rc = &Application::getInstance().getVulkanContext().getRenderContext();

    auto shaderPath = EnginePaths::shaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl";

    m_shader = AssetManager::importAsset<AShader>(shaderPath / "glsl/atmosphere/SkyAndAerialPerspective.fs.glsl", shaderConfig);
    RP_ASSERT(m_shader, "Sky and aerial perspective shader failed to load");
    if (!m_shader) {
        RP_CORE_ERROR("Sky and aerial perspective shader failed to load");
        return;
    }

    createPipeline();
}

SkyAndAerialPerspectivePass::~SkyAndAerialPerspectivePass()
{
    m_pipeline.reset();
}

CommandBufferLease SkyAndAerialPerspectivePass::record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance)
{
    RAPTURE_PROFILE_FUNCTION();

    if (!m_pipeline || m_lookupTables.getAtmosphere() == nullptr) {
        return {};
    }

    auto &vc = Application::getInstance().getVulkanContext();
    CommandBufferLease commandBufferLease = m_rc->commandPoolManager->leaseSecondary(vc.getGraphicsQueueIndex());
    CommandBuffer *commandBuffer = commandBufferLease.get();
    VkCommandBuffer cmd = commandBuffer->getCommandBufferVk();

    commandBuffer->beginSecondary(inheritance);

    const TextureSpecification &targetSpec = context.targets->sceneColorHdr->getSpecification();

    m_pipeline->bind(cmd);

    VkViewport viewport{};
    viewport.width = static_cast<float>(targetSpec.width);
    viewport.height = static_cast<float>(targetSpec.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = {targetSpec.width, targetSpec.height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    auto &renderData = *context.scene->getRenderData();
    uint32_t cameraSlot = renderData.getCameraSlot(context.camera.getEntity());

    SkyAndAerialPerspectivePushConstants pushConstants{};
    pushConstants.cameraPosition = glm::vec4(m_lookupTables.getCameraPosition(), m_lookupTables.getSkyViewAltitude());
    pushConstants.cameraSSBOIndex = renderData.getCameras().getDescriptorIndex(context.frameInFlight);
    pushConstants.cameraSlotIndex = cameraSlot != UINT32_MAX ? cameraSlot : 0;
    pushConstants.depthTexture = context.targets->depthStencil->getBindlessIndex();
    pushConstants.skyViewLut = m_lookupTables.getSkyViewLut()->getBindlessIndex();
    pushConstants.aerialPerspectiveVolume = m_lookupTables.getAerialPerspectiveVolume()->getBindlessIndex();
    pushConstants.skyIntensity = m_skyIntensity;

    vkCmdPushConstants(cmd, m_pipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyAndAerialPerspectivePushConstants),
                       &pushConstants);

    m_rc->descriptorManager->bindSet(0, commandBuffer, m_pipeline);
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_pipeline);
    m_lookupTables.getUniformSet(context.frameInFlight)->bind(cmd, m_pipeline);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    commandBuffer->end();

    return commandBufferLease;
}

void SkyAndAerialPerspectivePass::createPipeline()
{
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

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

    // color = source + destination * (1 - source alpha), the destination alpha kept
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
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
    fbSpec.colorAttachments.push_back(m_colorFormat);
    config.framebufferSpec = fbSpec;
    config.shader = m_shader.operator->();

    m_pipeline = std::make_shared<GraphicsPipeline>(config);
}

void SkyAndAerialPerspectivePass::updateAttachments(const RenderPassContext &context)
{
    RenderPassAttachment colorAttachment;
    colorAttachment.target = context.targets->sceneColorHdr;
    colorAttachment.loadOp = RenderPassAttachmentLoadOp::LOAD;
    colorAttachment.storeOp = RenderPassAttachmentStoreOp::STORE;

    m_attachments.colorAttachments.clear();
    m_attachments.colorAttachments.push_back(colorAttachment);

    m_attachments.depthAttachment = {};
    m_attachments.stencilAttachment = {};
}

void SkyAndAerialPerspectivePass::fillInputs(const RenderPassContext &context)
{
    m_inputs.push_back({context.targets->depthStencil, TEXTURE_USAGE_SAMPLED_FRAGMENT});
    m_inputs.push_back({m_lookupTables.getSkyViewLut(), TEXTURE_USAGE_SAMPLED_FRAGMENT});
    m_inputs.push_back({m_lookupTables.getAerialPerspectiveVolume(), TEXTURE_USAGE_SAMPLED_FRAGMENT});

    const SkyAtmosphere *atmosphere = m_lookupTables.getAtmosphere();
    if (atmosphere != nullptr) {
        m_inputs.push_back({atmosphere->getTransmittanceLut(), TEXTURE_USAGE_SAMPLED_FRAGMENT});
    }
}

void SkyAndAerialPerspectivePass::onResize(uint32_t width, uint32_t height)
{
    // nothing the pass owns is sized to the view
    (void)width;
    (void)height;
}

} // namespace Rapture
