#include "CascadedShadowMapping.h"

#include "WindowContext/Application.h"
#include "Logging/Log.h"
#include "Renderer/Shadows/ShadowCommon.h"
#include "Components/Components.h"
#include "RenderTargets/SwapChains/SwapChain.h"

#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <algorithm>

namespace Rapture {

struct PushConstantsCSM {
    glm::mat4 model;
};

std::unique_ptr<DescriptorSubAllocationBase<Texture>> CascadedShadowMap::s_bindlessCSMs = nullptr;

CascadedShadowMap::CascadedShadowMap(float width, float height, uint32_t numCascades, float lambda) :
    m_width(width),
    m_height(height),
    m_NumCascades(static_cast<uint8_t>(std::clamp(numCascades, 1u, MAX_CASCADES))),
    m_lambda(lambda),
    m_shadowTextureArray(nullptr),
    m_shadowMapIndex(UINT32_MAX)    {

    // Initialize static bindless descriptor array if not already done
    if (s_bindlessCSMs == nullptr) {
        s_bindlessCSMs = DescriptorArrayManager::createTextureSubAllocation(512, "Bindless CSM Descriptor Array Sub-Allocation");
    }

    // Get VMA allocator and frames in flight from application
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto swapchain = vulkanContext.getSwapChain();
    m_framesInFlight = swapchain->getImageCount();
    m_allocator = vulkanContext.getVmaAllocator();

    createShadowTexture();
    createPipeline();
    createUniformBuffers();
    createDescriptorSets();
    
    // Create flattened texture for debugging/visualization
    m_flattenedShadowTexture = TextureFlattener::createFlattenTexture(m_shadowTextureArray, "[CSM] Flattened Shadow Map Array");
}

CascadedShadowMap::~CascadedShadowMap() {
    if (s_bindlessCSMs && m_shadowMapIndex != UINT32_MAX) {
        s_bindlessCSMs->free(m_shadowMapIndex);
    }

}

std::vector<float> CascadedShadowMap::calculateCascadeSplits(float nearPlane, float farPlane, float lambda) {

    // Validate input parameters
    if (nearPlane <= 0.0f) {
        RP_CORE_ERROR("CascadedShadowMaping::calculateCascadeSplits: Near plane must be positive, got {0}", nearPlane);
        nearPlane = 0.1f; // Default fallback
    }
    
    if (farPlane <= nearPlane) {
        RP_CORE_ERROR("CascadedShadowMaping::calculateCascadeSplits: Far plane ({0}) must be greater than near plane ({1})", 
            farPlane, nearPlane);
        farPlane = nearPlane * 100.0f; // Default fallback
    }
    
    
    std::vector<float> splitDepths(m_NumCascades + 1);
    
    // First split is always at near plane
    splitDepths[0] = nearPlane;
    
    // Calculate splits using hybrid approach
    for (uint8_t i = 1; i < m_NumCascades; i++) {
        float p = static_cast<float>(i) / m_NumCascades;
        
        // Logarithmic split calculation
        float log = nearPlane * std::pow(farPlane / nearPlane, p);
        
        // Linear split calculation
        float linear = nearPlane + (farPlane - nearPlane) * p;
        
        // Blend between logarithmic and linear based on lambda
        splitDepths[i] = lambda * log + (1.0f - lambda) * linear;
    }
    
    // Last split is always at far plane
    splitDepths[m_NumCascades] = farPlane;
    
    return splitDepths;
}

std::vector<CascadeData> CascadedShadowMap::calculateCascades(
    const glm::vec3 &lightDir, const glm::mat4 &viewMatrix, 
    const glm::mat4 &projMatrix, 
    float nearPlane, 
    float farPlane, 
    ProjectionType cameraProjectionType) {


     // Calculate cascade splits
    std::vector<float> cascadeSplits = calculateCascadeSplits(nearPlane, farPlane, m_lambda); 
    m_cascadeSplits = cascadeSplits;

    std::vector<CascadeData> cascadeData(m_NumCascades);
    m_lightViewProjections.resize(m_NumCascades);
    
    // Light direction and up vector for view matrix
    glm::vec3 lightDirection = glm::normalize(lightDir);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(lightDirection, up)) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    for (uint8_t cascadeIdx = 0; cascadeIdx < m_NumCascades; cascadeIdx++) {
        cascadeData[cascadeIdx].nearPlane = cascadeSplits[cascadeIdx];
        cascadeData[cascadeIdx].farPlane = cascadeSplits[cascadeIdx + 1];

        // 1. Extract World-Space Frustum Corners for the current cascade
        std::array<glm::vec3, 8> frustumCorners = extractFrustumCorners(
            projMatrix, viewMatrix,
            cascadeSplits[cascadeIdx], cascadeSplits[cascadeIdx + 1],
            cameraProjectionType
        );

        // 2. Calculate Frustum Center
        glm::vec3 frustumCenter = glm::vec3(0.0f);
        for (const auto& corner : frustumCorners) {
            frustumCenter += corner;
        }
        frustumCenter /= 8.0f;

        // 3. Create Light View Matrix, looking at the cascade's center
        glm::mat4 lightViewMatrix = glm::lookAt(
            frustumCenter - lightDirection,
            frustumCenter,
            up
        );

        // 4. Find AABB of the cascade frustum in light-space
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();

        for (const auto& corner : frustumCorners) {
            glm::vec4 trf = lightViewMatrix * glm::vec4(corner, 1.0f);
            minX = std::min(minX, trf.x);
            maxX = std::max(maxX, trf.x);
            minY = std::min(minY, trf.y);
            maxY = std::max(maxY, trf.y);
            minZ = std::min(minZ, trf.z);
            maxZ = std::max(maxZ, trf.z);
        }
        
        // Add padding to avoid clipping issues
        constexpr float zMult = 10.0f;
        if (minZ < 0) minZ *= zMult; else minZ /= zMult;
        if (maxZ < 0) maxZ /= zMult; else maxZ *= zMult;

        // 5. Create the orthographic projection for this cascade
        glm::mat4 lightProjectionMatrix = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
        
        lightProjectionMatrix[1][1] *= -1;

        // 6. Store Final Matrix
        cascadeData[cascadeIdx].lightViewProj = lightProjectionMatrix * lightViewMatrix;
        m_lightViewProjections[cascadeIdx] = cascadeData[cascadeIdx].lightViewProj;
    }


    return cascadeData;
}

void CascadedShadowMap::recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<Scene> activeScene, uint32_t currentFrame) {

    setupDynamicRenderingMemoryBarriers(commandBuffer);
    beginDynamicRendering(commandBuffer);

    m_currentFrame = currentFrame;

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
    
    // Bind descriptor sets (contains all cascade matrices)
    VkDescriptorSet sets[] = {m_descriptorSets[currentFrame]->getDescriptorSet()};
    vkCmdBindDescriptorSets(commandBuffer->getCommandBufferVk(),
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline->getPipelineLayoutVk(),
                          0, 1, sets, 0, nullptr);
    
    // Get entities with TransformComponent and MeshComponent for rendering
    auto& registry = activeScene->getRegistry();
    auto view = registry.view<TransformComponent, MeshComponent, BoundingBoxComponent>();
    
    for (auto entity : view) {
        
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
        //if (m_frustum.testBoundingBox(boundingBoxComp.worldBoundingBox) == FrustumResult::Outside) {
        //    continue;
        //}
        
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
        PushConstantsCSM pushConstants{};
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
                           sizeof(PushConstantsCSM),
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
        
        // Draw the mesh once - multiview extension will handle rendering to all cascade layers
        vkCmdDrawIndexed(commandBuffer->getCommandBufferVk(), 
                        meshComp.mesh->getIndexCount(), 
                        1, 0, 0, 0);
    }
    
    // End rendering and transition image for shader reading
    vkCmdEndRendering(commandBuffer->getCommandBufferVk());

    transitionToShaderReadableLayout(commandBuffer);
}

std::vector<CascadeData> CascadedShadowMap::updateViewMatrix(const LightComponent &lightComp, const TransformComponent &transformComp, const CameraComponent &cameraComp)
{

    if (lightComp.type != LightType::Directional) {
        RP_CORE_ERROR("CascadedShadowMap::updateViewMatrix: Light is not a directional light");
        return std::vector<CascadeData>();
    }

    if (!lightComp.isActive) {
        return std::vector<CascadeData>();
    }

    // Calculate light direction
    glm::quat rotationQuat = transformComp.transforms.getRotationQuat();
    glm::vec3 lightDir = glm::normalize(rotationQuat * glm::vec3(0, 0, -1)); // Forward vector


    auto cascadeData = calculateCascades(
            lightDir,
            cameraComp.camera.getViewMatrix(),
            cameraComp.camera.getProjectionMatrix(),
            cameraComp.nearPlane,
            cameraComp.farPlane,
            ProjectionType::Perspective // Assuming perspective camera
    );

    // Update all uniform buffers with the new cascade matrices
    CSMData csmData;
    for (size_t i = 0; i < MAX_CASCADES; i++) {
        if (i < m_NumCascades && i < m_lightViewProjections.size()) {
            csmData.lightViewProjection[i] = m_lightViewProjections[i];
        } else {
            // Fill unused cascades with identity matrices
            csmData.lightViewProjection[i] = glm::mat4(1.0f);
        }
    }

    // Update all frame uniform buffers
    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_shadowUBOs[i]->addData((void*)&csmData, sizeof(CSMData), 0);
    }

    return cascadeData;
}

std::array<glm::vec3, 8> CascadedShadowMap::extractFrustumCorners(
    const glm::mat4 &cameraProjectionMatrix, 
    const glm::mat4 &cameraViewMatrix, 
    float cascadeNearPlane, 
    float cascadeFarPlane, 
    ProjectionType cameraProjectionType) {

    // Validate input parameters
    if (glm::any(glm::isnan(cameraProjectionMatrix[0])) || 
        glm::any(glm::isnan(cameraViewMatrix[0]))) {
        RP_CORE_ERROR("CascadedShadowMaping::extractFrustumCorners: Received NaN in input matrices");
    }
    
    if (cascadeNearPlane <= 0.0f) {
        RP_CORE_ERROR("CascadedShadowMaping::extractFrustumCorners: Near plane must be positive, got {0}", 
            cascadeNearPlane);
        cascadeNearPlane = 0.1f; // Fallback
    }
    
    if (cascadeFarPlane <= cascadeNearPlane) {
        RP_CORE_ERROR("CascadedShadowMaping::extractFrustumCorners: Far plane ({0}) must be greater than near plane ({1})", 
            cascadeFarPlane, cascadeNearPlane);
        cascadeFarPlane = cascadeNearPlane * 10.0f; // Fallback
    }
    
    // Define the 8 corners of a canonical view frustum in NDC space
    // These are the same for both projection types (perspective and orthographic)
    // NDC space is a cube from (-1,-1,-1) to (1,1,1)
    std::array<glm::vec4, 8> ndcCorners = {
        // Near face corners (z = -1 in NDC)
        glm::vec4(-1.0f, -1.0f, -1.0f, 1.0f), // Near-bottom-left
        glm::vec4( 1.0f, -1.0f, -1.0f, 1.0f), // Near-bottom-right
        glm::vec4( 1.0f,  1.0f, -1.0f, 1.0f), // Near-top-right
        glm::vec4(-1.0f,  1.0f, -1.0f, 1.0f), // Near-top-left
        
        // Far face corners (z = 1 in NDC)
        glm::vec4(-1.0f, -1.0f,  1.0f, 1.0f), // Far-bottom-left
        glm::vec4( 1.0f, -1.0f,  1.0f, 1.0f), // Far-bottom-right
        glm::vec4( 1.0f,  1.0f,  1.0f, 1.0f), // Far-top-right
        glm::vec4(-1.0f,  1.0f,  1.0f, 1.0f)  // Far-top-left
    };
    
    // Create a new projection matrix specific to this cascade's depth range
    glm::mat4 cascadeProjectionMatrix;
    
    if (cameraProjectionType == ProjectionType::Perspective)
    {
        // Extract FOV and aspect ratio from projection matrix
        float fovY = 0.0f;
        float aspectRatio = 1.0f;
        
        try {
            // For perspective: extract parameters from projection matrix
            fovY = 2.0f * atan(1.0f / cameraProjectionMatrix[1][1]);
            aspectRatio = cameraProjectionMatrix[1][1] / cameraProjectionMatrix[0][0];
            
            if (fovY <= 0.0f || fovY > glm::radians(180.0f)) {
                RP_CORE_ERROR("CascadedShadowMaping: Invalid FOV extracted: {0} radians", fovY);
                fovY = glm::radians(45.0f); // Default fallback
            }
            
            if (aspectRatio <= 0.0f) {
                RP_CORE_ERROR("CascadedShadowMaping: Invalid aspect ratio extracted: {0}", aspectRatio);
                aspectRatio = 1.0f; // Default fallback
            }
        }
        catch (const std::exception& e) {
            RP_CORE_ERROR("CascadedShadowMaping: Exception extracting perspective parameters: {0}", e.what());
            // Use fallback values
            fovY = glm::radians(45.0f);
            aspectRatio = 1.0f;
        }

        // Create perspective projection matrix with the cascade's depth range
        cascadeProjectionMatrix = glm::perspective(
            fovY, 
            aspectRatio, 
            cascadeNearPlane, 
            cascadeFarPlane
        );
        
        // Apply Vulkan Y-axis flip
        cascadeProjectionMatrix[1][1] *= -1;
    }
    else // Orthographic
    {
        float right = 0.0f;
        float top = 0.0f;
        
        try {
            // Extract orthographic dimensions from projection matrix
            right = 1.0f / cameraProjectionMatrix[0][0];
            top = 1.0f / cameraProjectionMatrix[1][1];
            
            if (right <= 0.0f) {
                RP_CORE_ERROR("CascadedShadowMaping: Invalid right value extracted: {0}", right);
                right = 10.0f; // Default fallback
            }
            
            if (top <= 0.0f) {
                RP_CORE_ERROR("CascadedShadowMaping: Invalid top value extracted: {0}", top);
                top = 10.0f; // Default fallback
            }
        }
        catch (const std::exception& e) {
            RP_CORE_ERROR("CascadedShadowMaping: Exception extracting orthographic parameters: {0}", e.what());
            // Use fallback values
            right = 10.0f;
            top = 10.0f;
        }


        // Create orthographic projection matrix with the cascade's depth range
        cascadeProjectionMatrix = glm::ortho(
            -right, right,
            -top, top,
            cascadeNearPlane, 
            cascadeFarPlane
        );
        
        // Apply Vulkan Y-axis flip
        cascadeProjectionMatrix[1][1] *= -1;
    }
    
    // Check for invalid transform matrix
    if (glm::any(glm::isnan(cascadeProjectionMatrix[0]))) {
        RP_CORE_ERROR("CascadedShadowMaping: Generated cascade projection matrix contains NaN");
        return std::array<glm::vec3, 8>{}; // Return empty corners
    }
    
    // Calculate the inverse of the combined view-projection matrix for this cascade
    // This transforms from NDC space to world space
    glm::mat4 inverseViewProj;
    
    try {
        inverseViewProj = glm::inverse(cascadeProjectionMatrix * cameraViewMatrix);
        
        // Check for invalid inverse matrix
        if (glm::any(glm::isnan(inverseViewProj[0]))) {
            RP_CORE_ERROR("CascadedShadowMaping: Inverse view-projection matrix contains NaN");
            return std::array<glm::vec3, 8>{}; // Return empty corners
        }
    }
    catch (const std::exception& e) {
        RP_CORE_ERROR("CascadedShadowMaping: Exception calculating inverse matrix: {0}", e.what());
        return std::array<glm::vec3, 8>{}; // Return empty corners
    }
    
    // Transform each NDC corner to world space
    std::array<glm::vec3, 8> worldSpaceCorners;
    for (size_t i = 0; i < 8; i++) {
        // Transform the corner from NDC to world space
        glm::vec4 worldSpaceCorner = inverseViewProj * ndcCorners[i];
        
        // Check for invalid transformed corner
        if (glm::any(glm::isnan(worldSpaceCorner)) || worldSpaceCorner.w == 0.0f) {
            RP_CORE_ERROR("CascadedShadowMaping: Invalid frustum corner calculated (NaN or w=0)");
            worldSpaceCorners[i] = glm::vec3(0.0f); // Fallback
        } else {
            // Apply perspective divide to get the actual world position
            worldSpaceCorners[i] = glm::vec3(worldSpaceCorner) / worldSpaceCorner.w;
        }
    }
    
    return worldSpaceCorners;
}

void CascadedShadowMap::createPipeline() {

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

    auto [shader, handle] = AssetManager::importAsset<Shader>(shaderPath / "SPIRV/shadows/CascadedShadowPass.vs.spv");

    if (!shader) {
        RP_CORE_ERROR("Failed to load CascadedShadowPass vertex shader");
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
    framebufferSpec.depthAttachment = m_shadowTextureArray->getFormat();
    
    // Add multiview support
    framebufferSpec.viewMask = (1u << m_NumCascades) - 1;  // Set bits for each cascade view
    framebufferSpec.correlationMask = (1u << m_NumCascades) - 1;  // All views are correlated

    config.framebufferSpec = framebufferSpec;
    config.shader = shader;

    m_shader = shader;
    m_handle = handle;

    m_pipeline = std::make_shared<GraphicsPipeline>(config);


}
void CascadedShadowMap::createShadowTexture() {

    TextureSpecification spec;
    spec.width = static_cast<uint32_t>(m_width);
    spec.height = static_cast<uint32_t>(m_height);
    spec.depth = m_NumCascades;
    spec.format = TextureFormat::D32F;
    spec.filter = TextureFilter::Linear;
    spec.type = TextureType::TEXTURE2D_ARRAY;
    spec.wrap = TextureWrap::ClampToEdge;
    spec.srgb = false;
    spec.shadowComparison = true; // Enable shadow comparison sampling
    spec.storageImage = true;

    m_shadowTextureArray = std::make_shared<Texture>(spec);

    if (s_bindlessCSMs) {
        m_shadowMapIndex = s_bindlessCSMs->allocate(m_shadowTextureArray);
    } else {
        RP_CORE_ERROR("CascadedShadowMaping::createShadowTexture - s_bindlessCSMs is nullptr");
    }

}
void CascadedShadowMap::createUniformBuffers() {
    m_shadowUBOs.resize(m_framesInFlight);

    CSMData csmData;
    // Initialize with identity matrices
    for (size_t i = 0; i < MAX_CASCADES; i++) {
        if (i < m_NumCascades) {
            csmData.lightViewProjection[i] = (i < m_lightViewProjections.size()) ? 
                m_lightViewProjections[i] : glm::mat4(1.0f);
        } else {
            // Fill unused cascades with identity matrices
            csmData.lightViewProjection[i] = glm::mat4(1.0f);
        }
    }

    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_shadowUBOs[i] = std::make_shared<UniformBuffer>(sizeof(CSMData), BufferUsage::STREAM, m_allocator);
        m_shadowUBOs[i]->addData((void*)&csmData, sizeof(CSMData), 0);
    }
}
void CascadedShadowMap::createDescriptorSets() {
    // Get descriptor set layout from shader
    VkDescriptorSetLayout layout;
    if (auto shader = m_shader.lock()) {
        if (shader->getDescriptorSetLayouts().size() > 0) {
            layout = shader->getDescriptorSetLayouts()[static_cast<uint32_t>(DESCRIPTOR_SET_INDICES::COMMON_RESOURCES)];
        } else {
            RP_CORE_ERROR("CascadedShadowMap::createDescriptorSets: Shader has no descriptor set layouts");
            return;
        }
    } else {
        RP_CORE_ERROR("CascadedShadowMap::createDescriptorSets: Shader is nullptr");
        return;
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
void CascadedShadowMap::setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer) {

    // Transition shadow map to depth attachment layout
    VkImageMemoryBarrier barrier = m_shadowTextureArray->getImageMemoryBarrier(
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
void CascadedShadowMap::beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer) {
    // Configure depth attachment info
    m_depthAttachmentInfo = {};
    m_depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    m_depthAttachmentInfo.imageView = m_shadowTextureArray->getImageView();
    m_depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    m_depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    m_depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    m_depthAttachmentInfo.clearValue.depthStencil = {1.0f, 0};
    
    // Configure rendering info for multiview rendering
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)};
    renderingInfo.layerCount = 1;  // Always 1 for multiview
    renderingInfo.viewMask = (1u << m_NumCascades) - 1;  // Set bits for each cascade view
    renderingInfo.colorAttachmentCount = 0; // Depth-only pass
    renderingInfo.pColorAttachments = nullptr;
    renderingInfo.pDepthAttachment = &m_depthAttachmentInfo;
    renderingInfo.pStencilAttachment = nullptr;
    
    vkCmdBeginRendering(commandBuffer->getCommandBufferVk(), &renderingInfo);
}

void CascadedShadowMap::transitionToShaderReadableLayout(std::shared_ptr<CommandBuffer> commandBuffer) {
    // Transition shadow map to shader readable layout
    VkImageMemoryBarrier barrier = m_shadowTextureArray->getImageMemoryBarrier(
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
    
    // Update flattened texture for debugging/visualization
    if (m_flattenedShadowTexture) {
        m_flattenedShadowTexture->update(commandBuffer);
    }
}
}