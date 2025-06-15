#include "ShadowMapping.h"

#include "Components/Components.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "WindowContext/Application.h"
#include "Renderer/Shadows/ShadowCommon.h"
#include "RenderTargets/SwapChains/SwapChain.h"


namespace Rapture {

struct PushConstants {
    glm::mat4 model;
};

std::unique_ptr<DescriptorSubAllocationBase<Texture>> ShadowMap::s_bindlessShadowMaps = nullptr;


ShadowMap::ShadowMap(float width, float height) 
    : m_width(width), m_height(height), m_shadowMapIndex(UINT32_MAX), m_lightViewProjection(glm::mat4(1.0f)) {

    if (s_bindlessShadowMaps == nullptr) {
        s_bindlessShadowMaps = DescriptorArrayManager::createTextureSubAllocation(512, "Bindless Shadow Map Descriptor Array Sub-Allocation");
    }

    createShadowTexture();
    createPipeline();
    
    // Get the frame count from the application
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto swapchain = vulkanContext.getSwapChain();
    m_framesInFlight = swapchain->getImageCount();
    m_allocator = vulkanContext.getVmaAllocator();
    



    createUniformBuffers();
    createDescriptorSets();
}

ShadowMap::~ShadowMap() {
    if (m_shadowMapIndex != UINT32_MAX && s_bindlessShadowMaps) {
        s_bindlessShadowMaps->free(m_shadowMapIndex);
    }
}

void ShadowMap::createUniformBuffers() {
    RAPTURE_PROFILE_FUNCTION();
    
    // Create uniform buffers for each frame in flight
    m_shadowUBOs.resize(m_framesInFlight);
    
    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        m_shadowUBOs[i] = std::make_shared<UniformBuffer>(
            sizeof(ShadowMapData),
            BufferUsage::STREAM,
            m_allocator
        );
    }
}

void ShadowMap::createDescriptorSets() {
    RAPTURE_PROFILE_FUNCTION();
    
    // Get descriptor set layout from shader
    VkDescriptorSetLayout layout;
    if (auto shader = m_shader.lock()) {
        if (shader->getDescriptorSetLayouts().size() > 0) {
            layout = shader->getDescriptorSetLayouts()[static_cast<uint32_t>(DESCRIPTOR_SET_INDICES::COMMON_RESOURCES)];
        }
    }
    
    m_descriptorSets.resize(m_framesInFlight);
    
    // For each frame in flight, create a descriptor set
    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        DescriptorSetBindings bindings;
        bindings.layout = layout;
        
        DescriptorSetBinding shadowBinding;
        shadowBinding.binding = 0;
        shadowBinding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        shadowBinding.count = 1;
        shadowBinding.resource = m_shadowUBOs[i];
        bindings.bindings.push_back(shadowBinding);

        
        m_descriptorSets[i] = std::make_shared<DescriptorSet>(bindings);
    }
}

void ShadowMap::setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer) {
    RAPTURE_PROFILE_FUNCTION();
    
    // Transition shadow map to depth attachment layout
    VkImageMemoryBarrier barrier = m_shadowTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    );
    
    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

void ShadowMap::beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer) {
    RAPTURE_PROFILE_FUNCTION();
    
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
    
    vkCmdBeginRendering(commandBuffer->getCommandBufferVk(), &renderingInfo);
}

void ShadowMap::transitionToShaderReadableLayout(std::shared_ptr<CommandBuffer> commandBuffer) {
    RAPTURE_PROFILE_FUNCTION();
    
    // Transition shadow map to shader readable layout
    VkImageMemoryBarrier barrier = m_shadowTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT
    );
    
    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

void ShadowMap::updateViewMatrix(const LightComponent& lightComp, const TransformComponent& transformComp, const glm::vec3& cameraPosition) {
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
    } 
    else {
        // Point light - use default direction
        lightDirection = glm::vec3(0.0f, 1.0f, 0.0f); // Down direction
    }

    // Calculate light view matrix
    glm::vec3 lightUp = glm::vec3(0.0f, 1.0f, 0.0f);
    if (abs(glm::dot(lightDirection, lightUp)) > 0.99f) {
        // If light is pointing directly up or down, use a different up vector
        lightUp = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    
    // Create the light's view matrix
    glm::mat4 viewMatrix = glm::lookAt(
        lightPosition,               // Light position
        lightPosition + lightDirection,    // Look at center (Point along the light direction)
        lightUp                 // Up vector
    );
    
    if (lightComp.type == LightType::Directional) {
        // For directional lights, center the shadow map around the camera's XZ position  
        glm::vec3 sceneCenter = glm::vec3(0.0f);
        
        float sceneBounds = 100.0f;

        // Position the light based on scene center and direction
        glm::vec3 shadowCamPos = lightPosition;
        shadowCamPos.y = sceneBounds;
        shadowCamPos += lightDirection*10.0f;

        // Create view matrix centered on the scene, not on the light entity
        viewMatrix = glm::lookAt(
            shadowCamPos,          // Position light relative to scene center
            sceneCenter,           // Look at scene center
            lightUp                // Up vector
        );
        
        // Use appropriate size for your scene
        float orthoSize = sceneBounds * 0.5f;
        // Use near/far planes that encompass your entire scene
        lightProj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 1.0f, sceneBounds*1.5f);


    } else {
        // Perspective projection for spot/point light
        float aspect = 1.0f; // Shadow map is square
        
        if (lightComp.type == LightType::Spot) {
            // For spotlights, use more aggressive near plane scaling
            float nearPlane = glm::max(0.1f, lightComp.range * 0.001f); // Much closer near plane
            float farPlane = lightComp.range * 1.2f; // Extend beyond light range for better coverage
            
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
    

    

    // Update all UBOs with the new projection matrix
    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        m_shadowUBOs[i]->addData(&shadowMapData, sizeof(ShadowMapData), 0);
    }
}

void ShadowMap::recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, 
                                       std::shared_ptr<Scene> activeScene,
                                       uint32_t currentFrame) {
    RAPTURE_PROFILE_FUNCTION();
    
    m_currentFrame = currentFrame;
    
    // Setup pipeline barriers and begin rendering
    setupDynamicRenderingMemoryBarriers(commandBuffer);
    beginDynamicRendering(commandBuffer);
    
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
    
    // Get entities with TransformComponent and MeshComponent for rendering
    auto& registry = activeScene->getRegistry();
    auto view = registry.view<TransformComponent, MeshComponent, BoundingBoxComponent>();
    
    for (auto entity : view) {
        RAPTURE_PROFILE_SCOPE("Draw Shadow Mesh");
        
        auto& transform = view.get<TransformComponent>(entity);
        auto& meshComp = view.get<MeshComponent>(entity);
        auto& boundingBoxComp = view.get<BoundingBoxComponent>(entity);
        
        // Skip invalid or loading meshes
        if (!meshComp.mesh || meshComp.isLoading) {
            continue;
        }
        
        // Check if mesh has valid buffers
        if (!meshComp.mesh->getVertexBuffer() || !meshComp.mesh->getIndexBuffer()) {
            continue;
        }
        
        // Update world bounding box if transform changed
        if (transform.hasChanged(m_currentFrame)) {
            boundingBoxComp.updateWorldBoundingBox(transform.transformMatrix());
        }
        
        // Perform frustum culling
        if (m_frustum.testBoundingBox(boundingBoxComp.worldBoundingBox) == FrustumResult::Outside) {
            continue;
        }
        
        // Get the vertex buffer layout
        auto& app = Application::getInstance();
        auto& vc = app.getVulkanContext();
        auto& bufferLayout = meshComp.mesh->getVertexBuffer()->getBufferLayout();
        
        // Convert to EXT variants required by vkCmdSetVertexInputEXT
        auto bindingDescription = bufferLayout.getBindingDescription2EXT();
        auto attributeDescriptions = bufferLayout.getAttributeDescriptions2EXT();
        
        // Use the function pointer from VulkanContext
        vc.vkCmdSetVertexInputEXT(commandBuffer->getCommandBufferVk(),
            1, &bindingDescription,
            static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data());
        
        // Push the model matrix as a push constant
        PushConstants pushConstants{};
        pushConstants.model = transform.transformMatrix();
        
        // Get push constant stage flags from shader
        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        if (auto shader = m_shader.lock(); shader && shader->getPushConstantLayouts().size() > 0) {
            stageFlags = shader->getPushConstantLayouts()[0].stageFlags;
        }
        
        vkCmdPushConstants(commandBuffer->getCommandBufferVk(),
                           m_pipeline->getPipelineLayoutVk(),
                           stageFlags,
                           0,
                           sizeof(PushConstants),
                           &pushConstants);
        
        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {meshComp.mesh->getVertexBuffer()->getBufferVk()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer->getCommandBufferVk(), 0, 1, vertexBuffers, offsets);
        
        // Bind index buffer
        vkCmdBindIndexBuffer(commandBuffer->getCommandBufferVk(), 
                            meshComp.mesh->getIndexBuffer()->getBufferVk(), 
                            0, 
                            meshComp.mesh->getIndexBuffer()->getIndexType());
        
        // Bind descriptor sets
        VkDescriptorSet sets[] = {m_descriptorSets[currentFrame]->getDescriptorSet()};
        vkCmdBindDescriptorSets(commandBuffer->getCommandBufferVk(),
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_pipeline->getPipelineLayoutVk(),
                              0, 1, sets, 0, nullptr);
        
        // Draw the mesh
        vkCmdDrawIndexed(commandBuffer->getCommandBufferVk(), 
                        meshComp.mesh->getIndexCount(), 
                        1, 0, 0, 0);
    }
    
    // End rendering and transition image for shader reading
    vkCmdEndRendering(commandBuffer->getCommandBufferVk());
    transitionToShaderReadableLayout(commandBuffer);
}

void ShadowMap::createPipeline() {
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
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;  // Use front face culling for shadow mapping
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;  // Enable depth bias
    rasterizer.depthBiasConstantFactor = 2.0f;  // Adjust these values based on your needs
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 2.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // For depth-only pass, we don't need color attachments
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0;  // Disable color writes
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;  // No color attachments
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
    
    auto& app = Application::getInstance();
    auto& project = app.getProject();

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

void ShadowMap::createShadowTexture() {
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

    if (s_bindlessShadowMaps) {
        m_shadowMapIndex = s_bindlessShadowMaps->allocate(m_shadowTexture);
    } else {
        RP_CORE_ERROR("ShadowMap::createShadowTexture - s_bindlessShadowMaps is nullptr");
    }
}
}
