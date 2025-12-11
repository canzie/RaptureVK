#include "Renderer/InstancedShapesPass.h"

#include "AssetManager/AssetManager.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Components/Components.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "Shaders/Shader.h"
#include "WindowContext/Application.h"

namespace Rapture {

struct InstancedShapesPushConstants {
    glm::mat4 globalTransform;
    glm::vec4 color;
    uint32_t cameraUBOIndex;
    uint32_t instanceDataSSBOIndex;
};

InstancedShapesPass::InstancedShapesPass(float width, float height, uint32_t framesInFlight,
                                         std::vector<std::shared_ptr<Texture>> depthStencilTextures, VkFormat colorFormat)
    : m_width(width), m_height(height), m_framesInFlight(framesInFlight), m_depthStencilTextures(depthStencilTextures),
      m_colorFormat(colorFormat)
{

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    m_device = vc.getLogicalDevice();
    m_vmaAllocator = vc.getVmaAllocator();

    auto &project = app.getProject();
    auto shaderPath = project.getProjectShaderDirectory();

    auto [shader, handle] = AssetManager::importAsset<Shader>(shaderPath / "glsl/InstancedShapes.vs.glsl");

    m_shader = shader;
    m_shaderHandle = handle;

    createPipeline();
}

InstancedShapesPass::~InstancedShapesPass() {}

void InstancedShapesPass::recordCommandBuffer(const std::shared_ptr<CommandBuffer> &commandBuffer,
                                              const std::shared_ptr<Scene> &scene, SceneRenderTarget &renderTarget,
                                              uint32_t imageIndex, uint32_t frameInFlight)
{

    RAPTURE_PROFILE_FUNCTION();

    m_currentImageIndex = frameInFlight;

    // Get render target properties
    VkImage targetImage = renderTarget.getImage(imageIndex);
    VkImageView targetImageView = renderTarget.getImageView(imageIndex);
    VkExtent2D targetExtent = renderTarget.getExtent();

    m_width = static_cast<float>(targetExtent.width);
    m_height = static_cast<float>(targetExtent.height);

    setupDynamicRenderingMemoryBarriers(commandBuffer, targetImage);
    beginDynamicRendering(commandBuffer, targetImageView, targetExtent);

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

    auto camera = scene->getSettings().mainCamera;
    if (camera == nullptr) {
        vkCmdEndRendering(commandBuffer->getCommandBufferVk());
        return;
    }
    auto cameraComp = camera->tryGetComponent<CameraComponent>();
    if (cameraComp == nullptr) {
        vkCmdEndRendering(commandBuffer->getCommandBufferVk());
        return;
    }

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    auto &registry = scene->getRegistry();
    auto view = registry.view<TransformComponent, MeshComponent, InstanceShapeComponent>();

    for (auto entity : view) {
        auto [transformComp, meshComp, instanceShapeComp] =
            view.get<TransformComponent, MeshComponent, InstanceShapeComponent>(entity);

        if (meshComp.mesh == nullptr || instanceShapeComp.instanceSSBO == nullptr) {
            continue;
        }

        if (instanceShapeComp.useWireMode) {
            m_pipelineWireframe->bind(commandBuffer->getCommandBufferVk());
        } else {
            m_pipelineFilled->bind(commandBuffer->getCommandBufferVk());
        }

        InstancedShapesPushConstants pushConstants;
        pushConstants.globalTransform = transformComp.transformMatrix();
        pushConstants.color = instanceShapeComp.color;
        pushConstants.cameraUBOIndex = cameraComp->cameraDataBuffer->getDescriptorIndex(frameInFlight);
        pushConstants.instanceDataSSBOIndex = instanceShapeComp.instanceSSBO->getBindlessIndex();

        vkCmdPushConstants(
            commandBuffer->getCommandBufferVk(),
            instanceShapeComp.useWireMode ? m_pipelineWireframe->getPipelineLayoutVk() : m_pipelineFilled->getPipelineLayoutVk(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(InstancedShapesPushConstants), &pushConstants);

        DescriptorManager::getDescriptorSet(0)->bind(commandBuffer->getCommandBufferVk(),
                                                     instanceShapeComp.useWireMode ? m_pipelineWireframe : m_pipelineFilled);
        DescriptorManager::getDescriptorSet(3)->bind(commandBuffer->getCommandBufferVk(),
                                                     instanceShapeComp.useWireMode ? m_pipelineWireframe : m_pipelineFilled);

        auto &bufferLayout = meshComp.mesh->getVertexBuffer()->getBufferLayout();
        auto bindingDescription = bufferLayout.getBindingDescription2EXT();
        auto attributeDescriptions = bufferLayout.getAttributeDescriptions2EXT();
        vc.vkCmdSetVertexInputEXT(commandBuffer->getCommandBufferVk(), 1, &bindingDescription,
                                  static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data());

        VkBuffer vertexBuffers[] = {meshComp.mesh->getVertexBuffer()->getBufferVk()};
        VkDeviceSize offsets[] = {meshComp.mesh->getVertexBuffer()->getOffset()};
        vkCmdBindVertexBuffers(commandBuffer->getCommandBufferVk(), 0, 1, vertexBuffers, offsets);

        vkCmdBindIndexBuffer(commandBuffer->getCommandBufferVk(), meshComp.mesh->getIndexBuffer()->getBufferVk(),
                             meshComp.mesh->getIndexBuffer()->getOffset(), meshComp.mesh->getIndexBuffer()->getIndexType());

        uint32_t instanceCount = instanceShapeComp.instanceCount;
        vkCmdDrawIndexed(commandBuffer->getCommandBufferVk(), meshComp.mesh->getIndexCount(), instanceCount, 0, 0, 0);
    }

    vkCmdEndRendering(commandBuffer->getCommandBufferVk());
}

void InstancedShapesPass::createPipeline()
{
    RAPTURE_PROFILE_FUNCTION();

    if (m_shader.expired()) {
        RP_CORE_ERROR("InstancedShapesPass: Shader not loaded, cannot create pipeline.");
        return;
    }
    auto shaderShared = m_shader.lock();
    if (!shaderShared) {
        RP_CORE_ERROR("InstancedShapesPass: Shader is null after lock, cannot create pipeline.");
        return;
    }

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                 VK_DYNAMIC_STATE_VERTEX_INPUT_EXT};

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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
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
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.stencilTestEnable = VK_FALSE;

    FramebufferSpecification spec;
    spec.colorAttachments.push_back(m_colorFormat);
    spec.depthAttachment = m_depthStencilTextures[0]->getFormat();

    GraphicsPipelineConfiguration config;
    config.dynamicState = dynamicState;
    config.inputAssemblyState = inputAssembly;
    config.viewportState = viewportState;
    config.rasterizationState = rasterizer;
    config.multisampleState = multisampling;
    config.colorBlendState = colorBlending;
    config.vertexInputState = vertexInputInfo;
    config.depthStencilState = depthStencil;
    config.framebufferSpec = spec;
    config.shader = shaderShared;

    // Filled pipeline
    m_pipelineFilled = std::make_shared<GraphicsPipeline>(config);

    // Wireframe pipeline
    config.rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
    m_pipelineWireframe = std::make_shared<GraphicsPipeline>(config);
}

void InstancedShapesPass::beginDynamicRendering(const std::shared_ptr<CommandBuffer> &commandBuffer, VkImageView targetImageView,
                                                VkExtent2D targetExtent)
{
    VkRenderingAttachmentInfo colorAttachmentInfo{};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachmentInfo.imageView = targetImageView;
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Preserve LightingPass output
    colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachmentInfo{};
    depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachmentInfo.imageView = m_depthStencilTextures[m_currentImageIndex]->getImageView();
    depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = targetExtent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;
    renderingInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(commandBuffer->getCommandBufferVk(), &renderingInfo);
}

void InstancedShapesPass::setupDynamicRenderingMemoryBarriers(const std::shared_ptr<CommandBuffer> &commandBuffer,
                                                              VkImage targetImage)
{
    // Execution barrier to ensure previous pass color writes are complete
    // before this pass starts writing (write-after-write synchronization)
    VkImageMemoryBarrier colorBarrier{};
    colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    colorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.image = targetImage;
    colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorBarrier.subresourceRange.baseMipLevel = 0;
    colorBarrier.subresourceRange.levelCount = 1;
    colorBarrier.subresourceRange.baseArrayLayer = 0;
    colorBarrier.subresourceRange.layerCount = 1;
    colorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &colorBarrier);
}

} // namespace Rapture
