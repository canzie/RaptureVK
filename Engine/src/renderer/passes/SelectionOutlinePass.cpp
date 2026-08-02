#include "renderer/passes/SelectionOutlinePass.h"

#include "asset_manager/AssetImportConfig.h"
#include "buffers/descriptors/DescriptorManager.h"
#include "events/GameEvents.h"
#include "logging/Log.h"
#include "logging/TracyProfiler.h"
#include "textures/Texture.h"
#include "window_context/Application.h"

namespace Rapture {

struct SelectionOutlinePushConstants {
    glm::vec4 outlineColor;
    uint32_t selectedEntityId;
    int32_t thickness;
};

SelectionOutlinePass::SelectionOutlinePass(uint32_t framesInFlight, VkFormat colorFormat) : m_colorFormat(colorFormat)
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    m_rc = &vc.getRenderContext();

    auto shaderPath = app.getProject().getProjectShaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl";

    auto asset = AssetManager::importAsset(shaderPath / "glsl/SelectionOutline.vs.glsl", shaderConfig);
    m_shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_shader) {
        m_shaderAssets.push_back(std::move(asset));
    }

    m_entityIdSets.resize(framesInFlight);

    createPipeline();

    m_entitySelectedListenerId = GameEvents::onEntitySelected().addListener([this](Entity entity) { m_selectedEntity = entity; });

    m_entityDeselectedListenerId = GameEvents::onEntityDeselected().addListener([this](Entity entity) {
        if (m_selectedEntity == entity) {
            m_selectedEntity = Entity();
        }
    });
}

SelectionOutlinePass::~SelectionOutlinePass()
{
    GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);
    GameEvents::onEntityDeselected().removeListener(m_entityDeselectedListenerId);

    m_entityIdSets.clear();
    m_pipeline.reset();
}

void SelectionOutlinePass::updateAttachments(const RenderPassContext &context)
{
    RenderPassAttachment colorAttachment;
    colorAttachment.texture = context.targets->sceneColorHdr;
    colorAttachment.loadOp = RenderPassAttachmentLoadOp::LOAD;
    colorAttachment.storeOp = RenderPassAttachmentStoreOp::STORE;

    m_attachments.colorAttachments.clear();
    m_attachments.colorAttachments.push_back(colorAttachment);

    m_attachments.depthAttachment = {};
    m_attachments.stencilAttachment = {};
}

DescriptorSet *SelectionOutlinePass::obtainEntityIdSet(Texture *entityIdTexture, uint32_t frameInFlight)
{
    if (frameInFlight >= m_entityIdSets.size()) {
        RP_CORE_ERROR("Requested the entity id set for frame {} but only {} exist", frameInFlight, m_entityIdSets.size());
        return nullptr;
    }

    if (m_entityIdSets[frameInFlight] == nullptr) {
        DescriptorSetBindings bindings;
        bindings.setNumber = 4;

        DescriptorSetBinding entityIdBinding = {};
        entityIdBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        entityIdBinding.location = DescriptorSetBindingLocation::CUSTOM_0;
        bindings.bindings.push_back(entityIdBinding);

        auto set = std::make_unique<DescriptorSet>(bindings);
        set->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*entityIdTexture);
        m_entityIdSets[frameInFlight] = std::move(set);
    }

    return m_entityIdSets[frameInFlight].get();
}

CommandBuffer *SelectionOutlinePass::record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance)
{
    RAPTURE_PROFILE_FUNCTION();

    if (!m_selectedEntity.isValid() || m_pipeline == nullptr) {
        return nullptr;
    }

    Texture *entityIdTexture = context.targets->gbufferEntityId;
    Texture *target = context.targets->sceneColorHdr;
    if (entityIdTexture == nullptr || target == nullptr) {
        return nullptr;
    }

    DescriptorSet *entityIdSet = obtainEntityIdSet(entityIdTexture, context.frameInFlight);
    if (entityIdSet == nullptr) {
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

    const TextureSpecification &targetSpec = target->getSpecification();
    VkExtent2D targetExtent = {targetSpec.width, targetSpec.height};

    m_width = static_cast<float>(targetExtent.width);
    m_height = static_cast<float>(targetExtent.height);

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

    SelectionOutlinePushConstants pushConstants{};
    pushConstants.outlineColor = m_outlineColor;
    pushConstants.selectedEntityId = m_selectedEntity.getID();
    pushConstants.thickness = m_thickness;

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_pipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(SelectionOutlinePushConstants), &pushConstants);

    entityIdSet->bind(commandBuffer->getCommandBufferVk(), m_pipeline);

    vkCmdDraw(commandBuffer->getCommandBufferVk(), 6, 1, 0, 0);

    commandBuffer->end();

    return commandBuffer;
}

void SelectionOutlinePass::createPipeline()
{
    if (m_shader == nullptr) {
        RP_CORE_ERROR("Shader is not available for pipeline creation.");
        return;
    }

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
    depthStencil.depthBoundsTestEnable = VK_FALSE;
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
    config.shader = m_shader;

    m_pipeline = std::make_shared<GraphicsPipeline>(config);
}

void SelectionOutlinePass::onResize(uint32_t width, uint32_t height)
{
    // TODO: see GBufferPass::onResize, passes are destroyed and rebuilt on resize today
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);
}

} // namespace Rapture
