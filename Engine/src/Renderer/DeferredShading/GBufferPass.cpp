#include "GBufferPass.h"

#include "WindowContext/Application.h"
#include "Components/Components.h"
#include "Logging/TracyProfiler.h"



namespace Rapture {

struct PushConstants {
    glm::mat4 model;
};

GBufferPass::GBufferPass(float width, float height, uint32_t framesInFlight, std::vector<std::shared_ptr<UniformBuffer>> cameraUBOs)
    : m_width(width), m_height(height), 
    m_framesInFlight(framesInFlight), m_currentFrame(0), 
    m_selectedEntity(nullptr),
    m_cameraUBOs(cameraUBOs) {

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();
    
    m_device = vc.getLogicalDevice();
    m_vmaAllocator = vc.getVmaAllocator();
    
    createPipeline();
    createTextures();
    createDescriptorSets(framesInFlight);
    
    m_entitySelectedListenerId = GameEvents::onEntitySelected().addListener(
        [this](std::shared_ptr<Rapture::Entity> entity) {
            m_selectedEntity = entity;
        }
    );
}

GBufferPass::~GBufferPass() {
    // Wait for device to finish operations
    vkDeviceWaitIdle(m_device);

    GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId); 

    // Clean up descriptor sets
    m_descriptorSets.clear();

    // Clean up camera UBOs
    m_cameraUBOs.clear();

    // Clean up textures
    m_positionDepthTextures.clear();
    m_normalTextures.clear();
    m_albedoSpecTextures.clear();
    m_materialTextures.clear();
    m_depthStencilTextures.clear();

    // Clean up pipeline
    m_pipeline.reset();
}

// order of the color attachments is important, it NEEDS to be the same order as the fragment shaders output attachments
FramebufferSpecification GBufferPass::getFramebufferSpecification() {
    FramebufferSpecification spec;
    spec.depthAttachment = VK_FORMAT_D24_UNORM_S8_UINT;
    spec.stencilAttachment = VK_FORMAT_D24_UNORM_S8_UINT;
    spec.colorAttachments.push_back(VK_FORMAT_R32G32B32A32_SFLOAT); // position
    spec.colorAttachments.push_back(VK_FORMAT_R16G16B16A16_SFLOAT ); // normal a=???
    spec.colorAttachments.push_back(VK_FORMAT_R8G8B8A8_SRGB); // albedo + specular
    spec.colorAttachments.push_back(VK_FORMAT_R8G8B8A8_UNORM ); // r=metallic g=roughness b=AO a=???

    return spec;
}

void GBufferPass::recordCommandBuffer(
    std::shared_ptr<CommandBuffer> commandBuffer, 
    std::shared_ptr<Scene> activeScene, 
    uint32_t currentFrame) {

    RAPTURE_PROFILE_FUNCTION();

    m_currentFrame = currentFrame;  // Update current frame index

    setupDynamicRenderingMemoryBarriers(commandBuffer);
    beginDynamicRendering(commandBuffer);
    m_pipeline->bind(commandBuffer->getCommandBufferVk());

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();

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

    // Get entities with TransformComponent and MeshComponent
    auto& registry = activeScene->getRegistry();
    auto view = registry.view<TransformComponent, MeshComponent, MaterialComponent, BoundingBoxComponent>();
    auto mainCamera = activeScene->getSettings().mainCamera;

    CameraComponent* cameraComp = nullptr;

    if (mainCamera) {
        cameraComp = mainCamera->tryGetComponent<CameraComponent>();
    }

    for (auto entity : view) {

        RAPTURE_PROFILE_SCOPE("Draw Mesh");


        auto& transform = view.get<TransformComponent>(entity);
        auto& meshComp = view.get<MeshComponent>(entity);
        auto& materialComp = view.get<MaterialComponent>(entity);
        auto& boundingBoxComp = view.get<BoundingBoxComponent>(entity);

        // Check if mesh is valid and not loading
        if (!meshComp.mesh || meshComp.isLoading) {
            continue;
        }

        if (!materialComp.material->isReady()) {
            continue;
        }
        
        auto mesh = meshComp.mesh;
        
        // Check if mesh has valid buffers
        if (!mesh->getVertexBuffer() || !mesh->getIndexBuffer()) {
            continue;
        }
        if (transform.hasChanged(m_currentFrame)){
            boundingBoxComp.updateWorldBoundingBox(transform.transformMatrix());
        }

        if (cameraComp && activeScene->getSettings().frustumCullingEnabled){
            if (cameraComp->frustum.testBoundingBox(boundingBoxComp.worldBoundingBox) == FrustumResult::Outside){
                continue;
            }
        }
        
        // Check if current entity is the selected one
        bool isSelected = false;
        if (m_selectedEntity) {
            // Assuming m_selectedEntity->getHandle() returns entt::entity
            // and 'entity' is of type entt::entity
            if (m_selectedEntity->getHandle() == entity) {
                isSelected = true;
            }
        }

        if (isSelected) {
            // Set stencil reference to 1 for the selected entity
            vkCmdSetStencilReference(commandBuffer->getCommandBufferVk(), VK_STENCIL_FACE_FRONT_AND_BACK, 1);
            // Enable stencil writing for selected entity
            vkCmdSetStencilWriteMask(commandBuffer->getCommandBufferVk(), VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
        } else {
            // Set stencil reference to 0 for non-selected entities
            vkCmdSetStencilReference(commandBuffer->getCommandBufferVk(), VK_STENCIL_FACE_FRONT_AND_BACK, 0);
            // Disable stencil writing for non-selected entities
            vkCmdSetStencilWriteMask(commandBuffer->getCommandBufferVk(), VK_STENCIL_FACE_FRONT_AND_BACK, 0x00);
        }

        // Get the vertex buffer layout
        auto& bufferLayout = mesh->getVertexBuffer()->getBufferLayout();
        
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

        // for now assume only 1 set of pushconstants in a full shader
        VkShaderStageFlags stageFlags;
        if (auto shader = m_shader.lock(); shader->getPushConstantLayouts().size() > 0) {
            stageFlags = shader->getPushConstantLayouts()[0].stageFlags;
        }


        vkCmdPushConstants(commandBuffer->getCommandBufferVk(), 
            m_pipeline->getPipelineLayoutVk(),
            stageFlags, 
            0, 
            sizeof(PushConstants), 
            &pushConstants);
        
        // Bind vertex buffers
        VkBuffer vertexBuffers[] = {mesh->getVertexBuffer()->getBufferVk()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer->getCommandBufferVk(), 0, 1, vertexBuffers, offsets);

        
        // Bind descriptor sets (only view/proj and material now)materialComp.material->getDescriptorSet()
        VkDescriptorSet descriptorSets[] = {m_descriptorSets[currentFrame]->getDescriptorSet(), materialComp.material->getDescriptorSet()};
        uint32_t descriptorSetCount = sizeof(descriptorSets) / sizeof(descriptorSets[0]);
        vkCmdBindDescriptorSets(commandBuffer->getCommandBufferVk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipelineLayoutVk(), 
            0, descriptorSetCount, descriptorSets, 0, nullptr);
        
        // Bind index buffer
        vkCmdBindIndexBuffer(commandBuffer->getCommandBufferVk(), mesh->getIndexBuffer()->getBufferVk(), 0, mesh->getIndexBuffer()->getIndexType());
        
        // Draw the mesh
        vkCmdDrawIndexed(commandBuffer->getCommandBufferVk(), mesh->getIndexCount(), 1, 0, 0, 0);
    
    }
    

    vkCmdEndRendering(commandBuffer->getCommandBufferVk());

    transitionToShaderReadableLayout(commandBuffer);

}

void GBufferPass::beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer) {
    RAPTURE_PROFILE_FUNCTION();

    // Update color attachment infos for current frame
    m_colorAttachmentInfo[0] = {};
    m_colorAttachmentInfo[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    m_colorAttachmentInfo[0].imageView = m_positionDepthTextures[m_currentFrame]->getImageView();
    m_colorAttachmentInfo[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    m_colorAttachmentInfo[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    m_colorAttachmentInfo[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    m_colorAttachmentInfo[0].clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    m_colorAttachmentInfo[1] = {};
    m_colorAttachmentInfo[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    m_colorAttachmentInfo[1].imageView = m_normalTextures[m_currentFrame]->getImageView();
    m_colorAttachmentInfo[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    m_colorAttachmentInfo[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    m_colorAttachmentInfo[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    m_colorAttachmentInfo[1].clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    m_colorAttachmentInfo[2] = {};
    m_colorAttachmentInfo[2].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    m_colorAttachmentInfo[2].imageView = m_albedoSpecTextures[m_currentFrame]->getImageView();
    m_colorAttachmentInfo[2].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    m_colorAttachmentInfo[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    m_colorAttachmentInfo[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    m_colorAttachmentInfo[2].clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    m_colorAttachmentInfo[3] = {};
    m_colorAttachmentInfo[3].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    m_colorAttachmentInfo[3].imageView = m_materialTextures[m_currentFrame]->getImageView();
    m_colorAttachmentInfo[3].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    m_colorAttachmentInfo[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    m_colorAttachmentInfo[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    m_colorAttachmentInfo[3].clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    // Depth-stencil attachment configuration
    m_depthAttachmentInfo = {};
    m_depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    m_depthAttachmentInfo.imageView = m_depthStencilTextures[m_currentFrame]->getImageView();
    m_depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    m_depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    m_depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // Clear depth to 1.0f (far) and stencil to 0
    m_depthAttachmentInfo.clearValue.depthStencil = {1.0f, 0};

    // Configure dynamic rendering info
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 4;
    renderingInfo.pColorAttachments = m_colorAttachmentInfo;
    renderingInfo.pDepthAttachment = &m_depthAttachmentInfo;
    renderingInfo.pStencilAttachment = &m_depthAttachmentInfo;

    vkCmdBeginRendering(commandBuffer->getCommandBufferVk(), &renderingInfo);
}

void GBufferPass::setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer) {
    RAPTURE_PROFILE_FUNCTION();

    VkImageMemoryBarrier barriers[5];
    barriers[0] = m_positionDepthTextures[m_currentFrame]->getImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    barriers[1] = m_normalTextures[m_currentFrame]->getImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    barriers[2] = m_albedoSpecTextures[m_currentFrame]->getImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    barriers[3] = m_materialTextures[m_currentFrame]->getImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    barriers[4] = m_depthStencilTextures[m_currentFrame]->getImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(), 
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 
        0, 
        0, nullptr, 
        0, nullptr, 
        5, barriers);
}

void GBufferPass::transitionToShaderReadableLayout(std::shared_ptr<CommandBuffer> commandBuffer) {
    RAPTURE_PROFILE_FUNCTION();

    VkImageMemoryBarrier barriers[5];
    barriers[0] = m_positionDepthTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
        VK_ACCESS_SHADER_READ_BIT);
    barriers[1] = m_normalTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
        VK_ACCESS_SHADER_READ_BIT);
    barriers[2] = m_albedoSpecTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
        VK_ACCESS_SHADER_READ_BIT);
    barriers[3] = m_materialTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 
        VK_ACCESS_SHADER_READ_BIT);
    barriers[4] = m_depthStencilTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
        VK_ACCESS_SHADER_READ_BIT);

    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(), 
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
        0, 
        0, nullptr, 
        0, nullptr, 
        5, barriers);  
}




void GBufferPass::createTextures()
{
    TextureSpecification posDepthSpec;
    posDepthSpec.width = m_width;
    posDepthSpec.height = m_height;
    posDepthSpec.format = TextureFormat::RGBA32F;
    posDepthSpec.type = TextureType::TEXTURE2D;
    posDepthSpec.srgb = false;

    TextureSpecification normalSpec;
    normalSpec.width = m_width;
    normalSpec.height = m_height;
    normalSpec.format = TextureFormat::RGBA16F;
    normalSpec.type = TextureType::TEXTURE2D;
    normalSpec.srgb = false;

    TextureSpecification albedoSpec;
    albedoSpec.width = m_width;
    albedoSpec.height = m_height;
    albedoSpec.format = TextureFormat::RGBA8;
    albedoSpec.type = TextureType::TEXTURE2D;
    albedoSpec.srgb = true;

    TextureSpecification materialSpec;
    materialSpec.width = m_width;
    materialSpec.height = m_height;
    materialSpec.format = TextureFormat::RGBA8;
    materialSpec.type = TextureType::TEXTURE2D;
    materialSpec.srgb = false;

    TextureSpecification depthStencilSpec;
    depthStencilSpec.width = m_width;
    depthStencilSpec.height = m_height;
    depthStencilSpec.format = TextureFormat::D24S8;
    depthStencilSpec.type = TextureType::TEXTURE2D;
    depthStencilSpec.srgb = false;

    // Create textures for each frame in flight
    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        m_positionDepthTextures.push_back(std::make_shared<Texture>(posDepthSpec));
        m_normalTextures.push_back(std::make_shared<Texture>(normalSpec));
        m_albedoSpecTextures.push_back(std::make_shared<Texture>(albedoSpec));
        m_materialTextures.push_back(std::make_shared<Texture>(materialSpec));
        m_depthStencilTextures.push_back(std::make_shared<Texture>(depthStencilSpec));
    }
}

void GBufferPass::createPipeline()
{
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE, // Added for dynamic stencil reference
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK // Added for dynamic stencil write mask
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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f; // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;


    VkPipelineColorBlendAttachmentState colorBlendAttachments[4];
    for (int i = 0; i < 4; ++i) {
        colorBlendAttachments[i] = {};
        colorBlendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[i].blendEnable = VK_FALSE;
        // Other blend factors can be left as default if blendEnable is VK_FALSE
    }


    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = 4; // Changed from 1
    colorBlending.pAttachments = colorBlendAttachments; // Changed from &colorBlendAttachment
    colorBlending.blendConstants[0] = 0.0f; // Optional
    colorBlending.blendConstants[1] = 0.0f; // Optional
    colorBlending.blendConstants[2] = 0.0f; // Optional
    colorBlending.blendConstants[3] = 0.0f; // Optional

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_TRUE;
    
    // Front face stencil operations
    depthStencil.front.failOp = VK_STENCIL_OP_KEEP;      // Keep current value if stencil test fails
    depthStencil.front.passOp = VK_STENCIL_OP_REPLACE;   // Replace with reference value when stencil test passes
    depthStencil.front.depthFailOp = VK_STENCIL_OP_REPLACE; // Replace with reference value even if depth test fails
    depthStencil.front.compareOp = VK_COMPARE_OP_ALWAYS; // Always pass the stencil test
    depthStencil.front.compareMask = 0xFF;               // Compare all bits
    depthStencil.front.writeMask = 0xFF;                 // Write all bits in stencil buffer
    depthStencil.front.reference = 0;                    // Default reference value (will be overridden by vkCmdSetStencilReference)
    
    // Back face stencil operations (same as front face)
    depthStencil.back = depthStencil.front;
    
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    
    
    auto& app = Application::getInstance();
    auto& project = app.getProject();

    auto shaderPath = project.getProjectShaderDirectory();

    auto [shader, handle] = AssetManager::importAsset<Shader>(shaderPath / "SPIRV/GBuffer.vs.spv");

    if (!shader) {
        RP_CORE_ERROR("Failed to load GBuffer vertex shader");
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
    config.framebufferSpec = getFramebufferSpecification();
    config.shader = shader;

    m_shader = shader;
    m_handle = handle;

    m_pipeline = std::make_shared<GraphicsPipeline>(config);
}



void GBufferPass::createDescriptorSets(uint32_t framesInFlight)
{
    VkDescriptorSetLayout layout;
    uint32_t descriptorSetIndex = static_cast<uint32_t>(DESCRIPTOR_SET_INDICES::COMMON_RESOURCES);
    if (auto shader = m_shader.lock(); shader->getDescriptorSetLayouts().size() > descriptorSetIndex) {
        layout = shader->getDescriptorSetLayouts()[descriptorSetIndex];
    }

    std::vector<DescriptorSetBindings> bindings(framesInFlight);
    for (int i = 0; i < framesInFlight; i++) {
        // TODO: binding index, currently set to 0, should change to use a constant DESCRIPTOR SET CAMERA INDEX 
        bindings[i].bindings.push_back({0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, TextureViewType::DEFAULT, m_cameraUBOs[i], false});
        bindings[i].layout = layout;
    }

    for (int i = 0; i < framesInFlight; i++) {
        m_descriptorSets.push_back(std::make_shared<DescriptorSet>(bindings[i]));
    }
}


} // namespace Rapture
