#include "SkyboxPass.h"

#include "core/utils/EnginePaths.h"
#include "assets/asset_manager/AssetImportConfig.h"
#include "assets/asset_manager/AssetManager.h"
#include "gpu/descriptors/DescriptorManager.h"
#include "scene/components/Components.h"
#include "core/utils/Log.h"
#include "scene/render_data/SceneRenderData.h"
#include "gpu/textures/TextureCommon.h"
#include "app/Application.h"

namespace Rapture {

struct SkyboxPushConstants {
    uint32_t cameraSSBOIndex;
    uint32_t cameraSlotIndex;
    uint32_t skyboxTextureIndex;
    float skyIntensity;
};

SkyboxPass::SkyboxPass(VkFormat depthFormat, VkFormat colorFormat)
    : m_skyboxTexture(nullptr), m_depthFormat(depthFormat), m_width(0.0f), m_height(0.0f), m_colorFormat(colorFormat)
{

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    m_rc = &vc.getRenderContext();
    m_device = vc.getLogicalDevice();
    m_vmaAllocator = vc.getVmaAllocator();

    auto shaderPath = EnginePaths::shaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl";

    auto asset = AssetManager::importAsset(shaderPath / "glsl/Skybox.vs.glsl", shaderConfig);
    m_shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_shader) m_shaderAssets.push_back(std::move(asset));

    createSkyboxGeometry();
    createPipeline();
}

SkyboxPass::~SkyboxPass()
{
    m_pipeline.reset();
    m_skyboxVertexBuffer.reset();
    m_skyboxIndexBuffer.reset();
}

CommandBuffer *SkyboxPass::record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance)
{
    Scene &activeScene = *context.scene;
    ecs::EntityAccessor camera = context.camera;
    Texture *target = context.targets->sceneColorHdr;
    uint32_t frameInFlightIndex = context.frameInFlight;

    if (!m_skyboxTexture || !m_skyboxTexture->isReady()) {
        return nullptr;
    }

    if (!m_pipeline) {
        RP_CORE_ERROR("Pipeline is not initialized!");
        return nullptr;
    }

    CommandPoolConfig config = {};
    config.queueFamilyIndex = Application::getInstance().getVulkanContext().getGraphicsQueueIndex();
    config.flags = 0;
    size_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    config.threadId = threadId;
    auto hash = m_rc->commandPoolManager->createCommandPool(config);

    auto pool = m_rc->commandPoolManager->getCommandPool(hash);
    auto commandBuffer = pool->getSecondaryCommandBuffer();

    commandBuffer->beginSecondary(inheritance);

    const TextureSpecification &targetSpec = target->getSpecification();
    VkExtent2D targetExtent = {targetSpec.width, targetSpec.height};

    // Update dimensions from target extent
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

    VkBuffer vertexBuffers[] = {m_skyboxVertexBuffer->getBufferVk()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer->getCommandBufferVk(), 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer->getCommandBufferVk(), m_skyboxIndexBuffer->getBufferVk(), 0, VK_INDEX_TYPE_UINT32);

    auto &renderData = *(activeScene.getRenderData());
    uint32_t cameraSlot = renderData.getCameraSlot(camera.getEntity());

    SkyboxPushConstants pushConstants{};
    pushConstants.cameraSSBOIndex = renderData.getCameras().getDescriptorIndex(frameInFlightIndex);
    pushConstants.cameraSlotIndex = (cameraSlot != UINT32_MAX) ? cameraSlot : 0;
    pushConstants.skyboxTextureIndex = m_skyboxTexture->getBindlessIndex();
    pushConstants.skyIntensity = m_skyIntensity;

    VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    if (m_shader && m_shader->getPushConstantLayouts().size() > 0) {
        stageFlags = m_shader->getPushConstantLayouts()[0].stageFlags;
    }

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_pipeline->getPipelineLayoutVk(), stageFlags, 0,
                       sizeof(SkyboxPushConstants), &pushConstants);

    auto cameraSet = m_rc->descriptorManager->getDescriptorSet(DescriptorSetBindingLocation::CAMERA_DATA_SSBO);
    if (cameraSet) {
        cameraSet->bind(commandBuffer->getCommandBufferVk(), m_pipeline);
    }

    auto skyboxSet = m_rc->descriptorManager->getDescriptorSet(DescriptorSetBindingLocation::BINDLESS_TEXTURES);
    if (skyboxSet) {
        skyboxSet->bind(commandBuffer->getCommandBufferVk(), m_pipeline);
    }

    vkCmdDrawIndexed(commandBuffer->getCommandBufferVk(), 36, 1, 0, 0, 0);

    commandBuffer->end();

    return commandBuffer;
}

void SkyboxPass::setSkyboxTexture(Texture *skyboxTexture)
{
    // null is a scene with no sky, which the pass skips rather than draws
    m_skyboxTexture = skyboxTexture;
    // Re-create descriptor sets with the new texture
}

void SkyboxPass::createPipeline()
{
    if (!m_shader) {
        RP_CORE_ERROR("Shader is not available for pipeline creation.");
        return;
    }

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(glm::vec3);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription.offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

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
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
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
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
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
    fbSpec.depthAttachment = m_depthFormat;
    fbSpec.colorAttachments.push_back(m_colorFormat);
    config.framebufferSpec = fbSpec;
    config.shader = m_shader;

    m_pipeline = std::make_shared<GraphicsPipeline>(config);
}

void SkyboxPass::createSkyboxGeometry()
{
    std::vector<glm::vec3> vertices = {{-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},  {1.0f, 1.0f, 1.0f},  {-1.0f, 1.0f, 1.0f},
                                       {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f}};

    std::vector<uint32_t> indices = {
        0, 1, 2, 2, 3, 0, // Front
        1, 5, 6, 6, 2, 1, // Right
        7, 6, 5, 5, 4, 7, // Back
        4, 0, 3, 3, 7, 4, // Left
        3, 2, 6, 6, 7, 3, // Top
        4, 5, 1, 1, 0, 4  // Bottom
    };

    m_skyboxVertexBuffer = std::make_shared<VertexBuffer>(vertices.size() * sizeof(glm::vec3), BufferUsage::STATIC, m_vmaAllocator);
    m_skyboxVertexBuffer->addDataGPU(vertices.data(), vertices.size() * sizeof(glm::vec3), 0);

    m_skyboxIndexBuffer =
        std::make_shared<IndexBuffer>(indices.size() * sizeof(uint32_t), BufferUsage::STATIC, m_vmaAllocator, VK_INDEX_TYPE_UINT32);
    m_skyboxIndexBuffer->addDataGPU(indices.data(), indices.size() * sizeof(uint32_t), 0);
}

void SkyboxPass::updateAttachments(const RenderPassContext &context)
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

void SkyboxPass::onResize(uint32_t width, uint32_t height)
{
    // TODO: see GBufferPass::onResize, passes are destroyed and rebuilt on resize today
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);
}

} // namespace Rapture
