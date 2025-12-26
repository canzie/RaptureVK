#include "ShadowMapping.h"

#include "Components/Components.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "Renderer/Shadows/ShadowCommon.h"
#include "WindowContext/Application.h"

namespace Rapture {

struct ShadowMappingPushConstants {
    glm::mat4 model;
    glm::mat4 shadowMatrix;
};

ShadowMap::ShadowMap(float width, float height) : m_width(width), m_height(height), m_lightViewProjection(glm::mat4(1.0f))
{

    createShadowTexture();
    createPipeline();

    // Get the frame count from the application
    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapchain = vulkanContext.getSwapChain();
    m_framesInFlight = swapchain->getImageCount();
    m_allocator = vulkanContext.getVmaAllocator();

    createUniformBuffers();
    setupCommandResources();
}

void ShadowMap::setupCommandResources()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    CommandPoolConfig config = {};
    config.queueFamilyIndex = vc.getGraphicsQueueIndex();
    config.flags = 0;
    m_commandPoolHash = CommandPoolManager::createCommandPool(config);
}

ShadowMap::~ShadowMap() {}

void ShadowMap::createUniformBuffers()
{
    RAPTURE_PROFILE_FUNCTION();

    // Create uniform buffers for each frame in flight
    m_shadowDataBuffer = std::make_shared<ShadowDataBuffer>();
}

void ShadowMap::setupDynamicRenderingMemoryBarriers(CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_FUNCTION();

    // Transition shadow map to depth attachment layout
    VkImageMemoryBarrier barrier =
        m_shadowTexture->getImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0,
                                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void ShadowMap::beginDynamicRendering(CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_FUNCTION();

    setupDynamicRenderingMemoryBarriers(commandBuffer);

    // Configure depth attachment info
    m_depthAttachmentInfo = {};
    m_depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    m_depthAttachmentInfo.imageView = m_shadowTexture->getImageView();
    m_depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    m_depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    m_depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    m_depthAttachmentInfo.clearValue.depthStencil = {1.0f, 0};

    // Configure rendering info
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 0; // Depth-only pass
    renderingInfo.pColorAttachments = nullptr;
    renderingInfo.pDepthAttachment = &m_depthAttachmentInfo;
    renderingInfo.pStencilAttachment = nullptr;
    renderingInfo.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;

    vkCmdBeginRendering(commandBuffer->getCommandBufferVk(), &renderingInfo);
}

void ShadowMap::endDynamicRendering(CommandBuffer *commandBuffer)
{
    vkCmdEndRendering(commandBuffer->getCommandBufferVk());
    transitionToShaderReadableLayout(commandBuffer);
}

void ShadowMap::transitionToShaderReadableLayout(CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_FUNCTION();

    // Transition shadow map to shader readable layout
    VkImageMemoryBarrier barrier = m_shadowTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void ShadowMap::updateViewMatrix(const LightComponent &lightComp, const TransformComponent &transformComp,
                                 const glm::vec3 &cameraPosition)
{
    RAPTURE_PROFILE_FUNCTION();

    ShadowMapData shadowMapData;

    glm::vec3 lightPosition = transformComp.translation();
    glm::vec3 lightDirection = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::mat4 lightProj = glm::mat4(1.0f);

    // Calculate light direction based on light type
    if (lightComp.type == LightType::Directional || lightComp.type == LightType::Spot) {
        // Calculate light direction from rotation
        glm::quat rotationQuat = transformComp.transforms.getRotationQuat();
        lightDirection = glm::normalize(rotationQuat * glm::vec3(0, 0, -1)); // Forward vector
    } else {
        // Point light - use default direction
        lightDirection = glm::vec3(0.0f, 1.0f, 0.0f); // Down direction
    }

    // Calculate light view matrix
    glm::vec3 lightUp = glm::vec3(0.0f, 1.0f, 0.0f);
    if (abs(glm::dot(lightDirection, lightUp)) > 0.99f) {
        // If light is pointing directly up or down, use a different up vector
        lightUp = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    glm::mat4 viewMatrix;

    if (lightComp.type == LightType::Directional) {
        // For directional lights, position the shadow camera to cover the scene properly
        // Use camera position as scene center for better shadow coverage
        glm::vec3 sceneCenter = cameraPosition;

        float sceneBounds = 100.0f;
        float shadowDistance = sceneBounds * 1.5f; // Distance to place shadow camera from scene center

        // Position the shadow camera opposite to the light direction
        // This ensures shadows are cast in the correct direction
        glm::vec3 shadowCamPos = sceneCenter - (lightDirection * shadowDistance);

        // Create view matrix looking from shadow camera position towards scene center
        viewMatrix = glm::lookAt(shadowCamPos, // Shadow camera position (opposite to light direction)
                                 sceneCenter,  // Look at scene center
                                 lightUp       // Up vector
        );

        // Use orthographic projection for directional lights
        float orthoSize = sceneBounds * 0.6f; // Slightly larger to avoid edge artifacts
        float nearPlane = 1.0f;
        float farPlane = shadowDistance + sceneBounds; // Ensure we capture everything

        lightProj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, nearPlane, farPlane);

    } else {
        // Create the light's view matrix for point/spot lights
        viewMatrix = glm::lookAt(lightPosition,                  // Light position
                                 lightPosition + lightDirection, // Look at point along light direction
                                 lightUp                         // Up vector
        );

        // Perspective projection for spot/point light
        float aspect = 1.0f; // Shadow map is square

        if (lightComp.type == LightType::Spot) {
            // For spotlights, use more aggressive near plane scaling
            float nearPlane = glm::max(0.1f, lightComp.range * 0.001f); // Much closer near plane
            float farPlane = lightComp.range * 1.2f;                    // Extend beyond light range for better coverage

            // Use slightly wider angle for shadows to avoid edge artifacts
            float shadowConeAngle = lightComp.outerConeAngle * 1.1f; // 10% wider angle for shadows
            float fovRadians = glm::max(shadowConeAngle * 2.0f, glm::radians(5.0f));

            lightProj = glm::perspective(fovRadians, aspect, nearPlane, farPlane);
        } else {
            // Point light settings (default)
            float nearPlane = 0.1f;
            lightProj = glm::perspective(glm::radians(90.0f), aspect, nearPlane, lightComp.range);
        }
    }

    // Update the frustum for culling using the original matrices (before Vulkan fix)
    m_frustum.update(lightProj, viewMatrix);

    // Apply Vulkan coordinate system fix for Y-axis after frustum update
    lightProj[1][1] *= -1;

    // Store the light's view-projection matrix
    shadowMapData.lightViewProjection = lightProj * viewMatrix;
    m_lightViewProjection = shadowMapData.lightViewProjection;
}

CommandBuffer *ShadowMap::recordSecondary(std::shared_ptr<Scene> activeScene, uint32_t currentFrame)
{
    RAPTURE_PROFILE_FUNCTION();

    m_currentFrame = currentFrame;

    auto pool = CommandPoolManager::getCommandPool(m_commandPoolHash, currentFrame);
    auto commandBuffer = pool->getSecondaryCommandBuffer();

    SecondaryBufferInheritance inheritance{};
    inheritance.depthFormat = m_shadowTexture->getFormat();
    commandBuffer->beginSecondary(inheritance);

    // Bind pipeline
    m_pipeline->bind(commandBuffer->getCommandBufferVk());

    // Configure viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_width);
    viewport.height = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer->getCommandBufferVk(), 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)};
    vkCmdSetScissor(commandBuffer->getCommandBufferVk(), 0, 1, &scissor);

    // Bind descriptor sets
    // via pushconstants for now, since it is only one matrix
    // DescriptorManager::bindSet(DescriptorSetBindingLocation::SHADOW_MATRICES_UBO, commandBuffer, m_pipeline);

    // Get entities with TransformComponent and MeshComponent for rendering
    auto &registry = activeScene->getRegistry();
    auto view = registry.view<TransformComponent, MeshComponent, BoundingBoxComponent>();

    for (auto entity : view) {
        RAPTURE_PROFILE_SCOPE("Draw Shadow Mesh");

        auto &transform = view.get<TransformComponent>(entity);
        auto &meshComp = view.get<MeshComponent>(entity);
        auto &boundingBoxComp = view.get<BoundingBoxComponent>(entity);

        // Skip invalid or loading meshes
        if (!meshComp.mesh || meshComp.isLoading) {
            continue;
        }

        // Check if mesh has valid buffers
        if (!meshComp.mesh->getVertexBuffer() || !meshComp.mesh->getIndexBuffer()) {
            continue;
        }

        // Update world bounding box if transform changed
        if (transform.hasChanged()) {
            boundingBoxComp.updateWorldBoundingBox(transform.transformMatrix());
        }

        // Perform frustum culling
        if (m_frustum.testBoundingBox(boundingBoxComp.worldBoundingBox) == FrustumResult::Outside) {
            continue;
        }

        // Get the vertex buffer layout
        auto &app = Application::getInstance();
        auto &vc = app.getVulkanContext();
        auto &bufferLayout = meshComp.mesh->getVertexBuffer()->getBufferLayout();

        // Convert to EXT variants required by vkCmdSetVertexInputEXT
        auto bindingDescription = bufferLayout.getBindingDescription2EXT();
        auto attributeDescriptions = bufferLayout.getAttributeDescriptions2EXT();

        // Use the function pointer from VulkanContext
        vc.vkCmdSetVertexInputEXT(commandBuffer->getCommandBufferVk(), 1, &bindingDescription,
                                  static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data());

        // Push the model matrix as a push constant
        ShadowMappingPushConstants pushConstants{};
        pushConstants.model = transform.transformMatrix();
        pushConstants.shadowMatrix = m_lightViewProjection;

        // Get push constant stage flags from shader
        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        if (auto shader = m_shader.lock(); shader && shader->getPushConstantLayouts().size() > 0) {
            stageFlags = shader->getPushConstantLayouts()[0].stageFlags;
        }

        vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_pipeline->getPipelineLayoutVk(), stageFlags, 0,
                           sizeof(ShadowMappingPushConstants), &pushConstants);

        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {meshComp.mesh->getVertexBuffer()->getBufferVk()};
        VkDeviceSize offsets[] = {meshComp.mesh->getVertexBuffer()->getOffset()};
        vkCmdBindVertexBuffers(commandBuffer->getCommandBufferVk(), 0, 1, vertexBuffers, offsets);

        // Bind index buffer
        vkCmdBindIndexBuffer(commandBuffer->getCommandBufferVk(), meshComp.mesh->getIndexBuffer()->getBufferVk(),
                             meshComp.mesh->getIndexBuffer()->getOffset(), meshComp.mesh->getIndexBuffer()->getIndexType());

        // Draw the mesh
        vkCmdDrawIndexed(commandBuffer->getCommandBufferVk(), meshComp.mesh->getIndexCount(), 1, 0, 0, 0);
    }

    commandBuffer->end();

    return commandBuffer;
}

void ShadowMap::createPipeline()
{
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
    };

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
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; // Use front face culling for shadow mapping
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;       // Enable depth bias
    rasterizer.depthBiasConstantFactor = 1.25f; // Adjust these values based on your needs
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0; // No color attachments
    colorBlending.pAttachments = nullptr;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    auto &app = Application::getInstance();
    auto &project = app.getProject();

    auto shaderPath = project.getProjectShaderDirectory();

    auto [shader, handle] = AssetManager::importAsset<Shader>(shaderPath / "SPIRV/shadows/ShadowPass.vs.spv");

    if (!shader) {
        RP_CORE_ERROR("Failed to load ShadowPass vertex shader");
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

    FramebufferSpecification framebufferSpec;
    framebufferSpec.depthAttachment = m_shadowTexture->getFormat();

    config.framebufferSpec = framebufferSpec;
    config.shader = shader;

    m_shader = shader;
    m_handle = handle;

    m_pipeline = std::make_shared<GraphicsPipeline>(config);
}

void ShadowMap::createShadowTexture()
{
    TextureSpecification spec;
    spec.width = static_cast<uint32_t>(m_width);
    spec.height = static_cast<uint32_t>(m_height);
    spec.format = TextureFormat::D32F;
    spec.filter = TextureFilter::Linear;
    spec.type = TextureType::TEXTURE2D;
    spec.wrap = TextureWrap::ClampToEdge;
    spec.srgb = false;
    spec.shadowComparison = true; // Enable shadow comparison sampling
    spec.storageImage = true;

    m_shadowTexture = std::make_shared<Texture>(spec);
}
} // namespace Rapture
