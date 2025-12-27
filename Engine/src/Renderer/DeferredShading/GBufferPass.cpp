#include "GBufferPass.h"

#include "Components/Components.h"
#include "Generators/Terrain/TerrainTypes.h"
#include "Logging/TracyProfiler.h"
#include "WindowContext/Application.h"

namespace Rapture {

struct GBufferPushConstants {
    uint32_t batchInfoBufferIndex;
    uint32_t cameraBindlessIndex;
};

struct TerrainGBufferPushConstants {
    uint32_t cameraBindlessIndex;
    uint32_t chunkDataBufferIndex;
    uint32_t continentalnessIndex;
    uint32_t erosionIndex;
    uint32_t peaksValleysIndex;
    uint32_t noiseLUTIndex;
    uint32_t lodResolution;
    float heightScale;
    float terrainWorldSize;
};

GBufferPass::GBufferPass(float width, float height, uint32_t framesInFlight)
    : m_width(width), m_height(height), m_framesInFlight(framesInFlight), m_currentFrame(0), m_selectedEntity(nullptr)
{

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    m_device = vc.getLogicalDevice();
    m_vmaAllocator = vc.getVmaAllocator();

    createPipeline();
    createTerrainPipeline();
    createTextures();

    // Initialize MDI batching system - one set per frame in flight
    m_mdiBatchMaps.resize(framesInFlight);
    m_selectedEntityBatchMaps.resize(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; i++) {
        m_mdiBatchMaps[i] = std::make_unique<MDIBatchMap>();
        m_selectedEntityBatchMaps[i] = std::make_unique<MDIBatchMap>();
    }

    // Bind GBuffer textures to bindless set
    bindGBufferTexturesToBindlessSet();

    setupCommandResources();

    m_entitySelectedListenerId =
        GameEvents::onEntitySelected().addListener([this](std::shared_ptr<Rapture::Entity> entity) { m_selectedEntity = entity; });
}

void GBufferPass::setupCommandResources()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    CommandPoolConfig config = {};
    config.queueFamilyIndex = vc.getGraphicsQueueIndex();
    config.flags = 0;
    m_commandPoolHash = CommandPoolManager::createCommandPool(config);
}

GBufferPass::~GBufferPass()
{
    // Wait for device to finish operations
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    vc.waitIdle();

    GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);

    // Clean up textures
    m_positionDepthTextures.clear();
    m_normalTextures.clear();
    m_albedoSpecTextures.clear();
    m_materialTextures.clear();
    m_depthStencilTextures.clear();

    // Clean up pipelines
    m_pipeline.reset();
    m_terrainPipeline.reset();
}

// order of the color attachments is important, it NEEDS to be the same order as the fragment shaders output attachments
FramebufferSpecification GBufferPass::getFramebufferSpecification()
{
    FramebufferSpecification spec;
    spec.depthAttachment = VK_FORMAT_D24_UNORM_S8_UINT;
    spec.stencilAttachment = VK_FORMAT_D24_UNORM_S8_UINT;
    spec.colorAttachments.push_back(VK_FORMAT_R32G32B32A32_SFLOAT); // position
    spec.colorAttachments.push_back(VK_FORMAT_R16G16B16A16_SFLOAT); // normal a=???
    spec.colorAttachments.push_back(VK_FORMAT_R8G8B8A8_SRGB);       // albedo + specular
    spec.colorAttachments.push_back(VK_FORMAT_R8G8B8A8_UNORM);      // r=metallic g=roughness b=AO a=???

    return spec;
}

CommandBuffer *GBufferPass::recordSecondary(std::shared_ptr<Scene> activeScene, uint32_t currentFrame,
                                            const SecondaryBufferInheritance &inheritance, TerrainGenerator *terrain)
{
    RAPTURE_PROFILE_FUNCTION();

    m_currentFrame = currentFrame;

    auto pool = CommandPoolManager::getCommandPool(m_commandPoolHash, currentFrame);
    auto commandBuffer = pool->getSecondaryCommandBuffer();

    commandBuffer->beginSecondary(inheritance);

    if (terrain && terrain->isInitialized()) {
        recordTerrainCommands(commandBuffer, activeScene, *terrain, currentFrame);
    }

    recordEntityCommands(commandBuffer, activeScene, currentFrame);

    commandBuffer->end();

    return commandBuffer;
}

void GBufferPass::recordEntityCommands(CommandBuffer *secondaryCb, std::shared_ptr<Scene> activeScene, uint32_t currentFrame)
{
    RAPTURE_PROFILE_FUNCTION();

    m_currentFrame = currentFrame;

    m_pipeline->bind(secondaryCb->getCommandBufferVk());

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_width);
    viewport.height = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(secondaryCb->getCommandBufferVk(), 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)};
    vkCmdSetScissor(secondaryCb->getCommandBufferVk(), 0, 1, &scissor);

    // Get entities with TransformComponent and MeshComponent
    auto &registry = activeScene->getRegistry();
    auto view = registry.view<TransformComponent, MeshComponent, MaterialComponent, BoundingBoxComponent>();
    auto mainCamera = activeScene->getMainCamera();

    CameraComponent *cameraComp = nullptr;

    if (mainCamera) {
        cameraComp = mainCamera.tryGetComponent<CameraComponent>();
    }

    // Begin frame for MDI batching - use current frame's batch maps
    m_mdiBatchMaps[m_currentFrame]->beginFrame();
    m_selectedEntityBatchMaps[m_currentFrame]->beginFrame();

    // bind descriptor sets
    DescriptorManager::bindSet(0, secondaryCb, m_pipeline); // camera stuff
    DescriptorManager::bindSet(1, secondaryCb, m_pipeline); // materials
    DescriptorManager::bindSet(2, secondaryCb, m_pipeline); // model data
    DescriptorManager::bindSet(3, secondaryCb, m_pipeline); // bindless textures for the material stuff

    // First pass: Populate MDI batches with mesh data
    for (auto entity : view) {
        RAPTURE_PROFILE_SCOPE("Populate Batch");

        auto &transform = view.get<TransformComponent>(entity);
        auto &meshComp = view.get<MeshComponent>(entity);
        auto &materialComp = view.get<MaterialComponent>(entity);
        auto &boundingBoxComp = view.get<BoundingBoxComponent>(entity);

        // Check if mesh is valid and not loading
        if (!meshComp.mesh || meshComp.isLoading) {
            continue;
        }

        auto mesh = meshComp.mesh;

        // Check if mesh has valid buffers
        if (!mesh->getVertexBuffer() || !mesh->getIndexBuffer()) {
            continue;
        }

        if (transform.hasChanged()) {
            boundingBoxComp.updateWorldBoundingBox(transform.transformMatrix());
        }

        if (cameraComp && activeScene->getSettings().frustumCullingEnabled) {
            if (cameraComp->frustum.testBoundingBox(boundingBoxComp.worldBoundingBox) == FrustumResult::Outside) {
                continue;
            }
        }

        // Check if current entity is the selected one
        bool isSelected = false;
        if (m_selectedEntity) {
            if (m_selectedEntity->getHandle() == entity) {
                isSelected = true;
            }
        }

        // Get buffer allocation info to determine batch
        auto vboAlloc = meshComp.mesh->getVertexAllocation();
        auto iboAlloc = meshComp.mesh->getIndexAllocation();

        if (!vboAlloc || !iboAlloc) {
            continue;
        }

        // Choose the appropriate batch map based on selection state
        MDIBatchMap *batchMap = isSelected ? m_selectedEntityBatchMaps[m_currentFrame].get() : m_mdiBatchMaps[m_currentFrame].get();

        // Get or create batch for this VBO/IBO arena combination
        MDIBatch *batch = batchMap->obtainBatch(vboAlloc, iboAlloc, meshComp.mesh->getVertexBuffer()->getBufferLayout(),
                                                meshComp.mesh->getIndexBuffer()->getIndexType());

        // Get mesh buffer index from the MeshComponent
        uint32_t meshBufferIndex = meshComp.meshDataBuffer ? meshComp.meshDataBuffer->getDescriptorIndex(currentFrame) : 0;
        uint32_t materialIndex = materialComp.material ? materialComp.material->getBindlessIndex() : 0;

        // Add mesh to batch
        batch->addObject(*meshComp.mesh, meshBufferIndex, materialIndex);
    }

    // Second pass: Render non-selected entities using MDI
    // Set stencil reference to 0 for non-selected entities
    vkCmdSetStencilReference(secondaryCb->getCommandBufferVk(), VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    // Disable stencil writing for non-selected entities
    vkCmdSetStencilWriteMask(secondaryCb->getCommandBufferVk(), VK_STENCIL_FACE_FRONT_AND_BACK, 0x00);

    for (const auto &[batchKey, batch] : m_mdiBatchMaps[m_currentFrame]->getBatches()) {
        if (batch->getDrawCount() == 0) {
            continue;
        }

        RAPTURE_PROFILE_SCOPE("Draw Non-Selected Batch");

        // Upload batch data to GPU
        batch->uploadBuffers();

        // Get layout from batch
        auto bindingDescription = batch->getBufferLayout().getBindingDescription2EXT();
        auto attributeDescriptions = batch->getBufferLayout().getAttributeDescriptions2EXT();

        vc.vkCmdSetVertexInputEXT(secondaryCb->getCommandBufferVk(), 1, &bindingDescription,
                                  static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data());

        // Set push constants for this batch
        GBufferPushConstants pushConstants{};
        pushConstants.batchInfoBufferIndex = batch->getBatchInfoBufferIndex();
        pushConstants.cameraBindlessIndex = cameraComp ? cameraComp->cameraDataBuffer->getDescriptorIndex(m_currentFrame) : 0;

        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        if (auto shader = m_shader.lock(); shader && shader->getPushConstantLayouts().size() > 0) {
            stageFlags = shader->getPushConstantLayouts()[0].stageFlags;
        }

        vkCmdPushConstants(secondaryCb->getCommandBufferVk(), m_pipeline->getPipelineLayoutVk(), stageFlags, 0,
                           sizeof(GBufferPushConstants), &pushConstants);

        // Bind vertex buffer from the arena
        VkBuffer vertexBuffer = batch->getVertexBuffer();
        VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(secondaryCb->getCommandBufferVk(), 0, 1, &vertexBuffer, &vertexOffset);

        // Bind index buffer from the arena
        VkBuffer indexBuffer = batch->getIndexBuffer();
        vkCmdBindIndexBuffer(secondaryCb->getCommandBufferVk(), indexBuffer, 0, batch->getIndexType());

        // Execute multi-draw indirect
        auto indirectBuffer = batch->getIndirectBuffer();
        if (indirectBuffer) {
            vkCmdDrawIndexedIndirect(secondaryCb->getCommandBufferVk(), indirectBuffer->getBufferVk(), 0, batch->getDrawCount(),
                                     sizeof(VkDrawIndexedIndirectCommand));
        }
    }

    // Third pass: Render selected entities using MDI with different stencil settings
    // Set stencil reference to 1 for the selected entity
    vkCmdSetStencilReference(secondaryCb->getCommandBufferVk(), VK_STENCIL_FACE_FRONT_AND_BACK, 1);
    // Enable stencil writing for selected entity
    vkCmdSetStencilWriteMask(secondaryCb->getCommandBufferVk(), VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);

    for (const auto &[batchKey, batch] : m_selectedEntityBatchMaps[m_currentFrame]->getBatches()) {
        if (batch->getDrawCount() == 0) {
            continue;
        }

        RAPTURE_PROFILE_SCOPE("Draw Selected Batch");

        // Upload batch data to GPU
        batch->uploadBuffers();

        // Get layout from batch
        auto bindingDescription = batch->getBufferLayout().getBindingDescription2EXT();
        auto attributeDescriptions = batch->getBufferLayout().getAttributeDescriptions2EXT();

        vc.vkCmdSetVertexInputEXT(secondaryCb->getCommandBufferVk(), 1, &bindingDescription,
                                  static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data());

        // Set push constants for this batch
        GBufferPushConstants pushConstants{};
        pushConstants.batchInfoBufferIndex = batch->getBatchInfoBufferIndex();
        pushConstants.cameraBindlessIndex = cameraComp ? cameraComp->cameraDataBuffer->getDescriptorIndex(m_currentFrame) : 0;

        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        if (auto shader = m_shader.lock(); shader && shader->getPushConstantLayouts().size() > 0) {
            stageFlags = shader->getPushConstantLayouts()[0].stageFlags;
        }

        vkCmdPushConstants(secondaryCb->getCommandBufferVk(), m_pipeline->getPipelineLayoutVk(), stageFlags, 0,
                           sizeof(GBufferPushConstants), &pushConstants);

        // Bind vertex buffer from the arena
        VkBuffer vertexBuffer = batch->getVertexBuffer();
        VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(secondaryCb->getCommandBufferVk(), 0, 1, &vertexBuffer, &vertexOffset);

        // Bind index buffer from the arena
        VkBuffer indexBuffer = batch->getIndexBuffer();
        vkCmdBindIndexBuffer(secondaryCb->getCommandBufferVk(), indexBuffer, 0, batch->getIndexType());

        // Execute multi-draw indirect
        auto indirectBuffer = batch->getIndirectBuffer();
        if (indirectBuffer) {
            vkCmdDrawIndexedIndirect(secondaryCb->getCommandBufferVk(), indirectBuffer->getBufferVk(), 0, batch->getDrawCount(),
                                     sizeof(VkDrawIndexedIndirectCommand));
        }
    }
}

void GBufferPass::beginDynamicRendering(CommandBuffer *primaryCb)
{
    RAPTURE_PROFILE_FUNCTION();

    setupDynamicRenderingMemoryBarriers(primaryCb);

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
    renderingInfo.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;

    vkCmdBeginRendering(primaryCb->getCommandBufferVk(), &renderingInfo);
}

void GBufferPass::endDynamicRendering(CommandBuffer *primaryCb)
{
    vkCmdEndRendering(primaryCb->getCommandBufferVk());
    transitionToShaderReadableLayout(primaryCb);
}

void GBufferPass::setupDynamicRenderingMemoryBarriers(CommandBuffer *primaryCb)
{
    RAPTURE_PROFILE_FUNCTION();

    VkImageMemoryBarrier barriers[5];
    barriers[0] = m_positionDepthTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    barriers[1] = m_normalTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    barriers[2] = m_albedoSpecTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    barriers[3] = m_materialTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    barriers[4] = m_depthStencilTextures[m_currentFrame]->getImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED,
                                                                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0,
                                                                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    vkCmdPipelineBarrier(primaryCb->getCommandBufferVk(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr,
                         0, nullptr, 5, barriers);
}

void GBufferPass::transitionToShaderReadableLayout(CommandBuffer *primaryCb)
{
    RAPTURE_PROFILE_FUNCTION();

    VkImageMemoryBarrier barriers[5];
    barriers[0] = m_positionDepthTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    barriers[1] = m_normalTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    barriers[2] = m_albedoSpecTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    barriers[3] = m_materialTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    barriers[4] = m_depthStencilTextures[m_currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    vkCmdPipelineBarrier(primaryCb->getCommandBufferVk(),
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 5, barriers);
}

void GBufferPass::createTextures()
{
    TextureSpecification posDepthSpec;
    posDepthSpec.width = static_cast<uint32_t>(m_width);
    posDepthSpec.height = static_cast<uint32_t>(m_height);
    posDepthSpec.format = TextureFormat::RGBA32F;
    posDepthSpec.type = TextureType::TEXTURE2D;
    posDepthSpec.srgb = false;

    TextureSpecification normalSpec;
    normalSpec.width = static_cast<uint32_t>(m_width);
    normalSpec.height = static_cast<uint32_t>(m_height);
    normalSpec.format = TextureFormat::RGBA16F;
    normalSpec.type = TextureType::TEXTURE2D;
    normalSpec.srgb = false;

    TextureSpecification albedoSpec;
    albedoSpec.width = static_cast<uint32_t>(m_width);
    albedoSpec.height = static_cast<uint32_t>(m_height);
    albedoSpec.format = TextureFormat::RGBA8;
    albedoSpec.type = TextureType::TEXTURE2D;
    albedoSpec.srgb = true;

    TextureSpecification materialSpec;
    materialSpec.width = static_cast<uint32_t>(m_width);
    materialSpec.height = static_cast<uint32_t>(m_height);
    materialSpec.format = TextureFormat::RGBA8;
    materialSpec.type = TextureType::TEXTURE2D;
    materialSpec.srgb = false;

    TextureSpecification depthStencilSpec;
    depthStencilSpec.width = static_cast<uint32_t>(m_width);
    depthStencilSpec.height = static_cast<uint32_t>(m_height);
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

void GBufferPass::bindGBufferTexturesToBindlessSet()
{

    // Resize the index vectors
    m_positionTextureIndices.resize(m_framesInFlight);
    m_normalTextureIndices.resize(m_framesInFlight);
    m_albedoTextureIndices.resize(m_framesInFlight);
    m_materialTextureIndices.resize(m_framesInFlight);
    m_depthTextureIndices.resize(m_framesInFlight);

    // Add each texture to the bindless set and store the indices
    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        m_positionTextureIndices[i] = m_positionDepthTextures[i]->getBindlessIndex();
        m_normalTextureIndices[i] = m_normalTextures[i]->getBindlessIndex();
        m_albedoTextureIndices[i] = m_albedoSpecTextures[i]->getBindlessIndex();
        m_materialTextureIndices[i] = m_materialTextures[i]->getBindlessIndex();
        m_depthTextureIndices[i] = m_depthStencilTextures[i]->getBindlessIndex();

        if (m_positionTextureIndices[i] == UINT32_MAX || m_normalTextureIndices[i] == UINT32_MAX ||
            m_albedoTextureIndices[i] == UINT32_MAX || m_materialTextureIndices[i] == UINT32_MAX ||
            m_depthTextureIndices[i] == UINT32_MAX) {
            RP_CORE_ERROR("Failed to add GBuffer texture(s) to bindless array for frame {}", i);
        }
    }
}

void GBufferPass::createPipeline()
{
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
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
    rasterizer.depthBiasClamp = 0.0f;          // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f;    // Optional

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[4];
    for (int i = 0; i < 4; ++i) {
        colorBlendAttachments[i] = {};
        colorBlendAttachments[i].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[i].blendEnable = VK_FALSE;
        // Other blend factors can be left as default if blendEnable is VK_FALSE
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;           // Optional
    colorBlending.attachmentCount = 4;                  // Changed from 1
    colorBlending.pAttachments = colorBlendAttachments; // Changed from &colorBlendAttachment
    colorBlending.blendConstants[0] = 0.0f;             // Optional
    colorBlending.blendConstants[1] = 0.0f;             // Optional
    colorBlending.blendConstants[2] = 0.0f;             // Optional
    colorBlending.blendConstants[3] = 0.0f;             // Optional

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_TRUE;

    // Front face stencil operations
    depthStencil.front.failOp = VK_STENCIL_OP_KEEP;         // Keep current value if stencil test fails
    depthStencil.front.passOp = VK_STENCIL_OP_REPLACE;      // Replace with reference value when stencil test passes
    depthStencil.front.depthFailOp = VK_STENCIL_OP_REPLACE; // Replace with reference value even if depth test fails
    depthStencil.front.compareOp = VK_COMPARE_OP_ALWAYS;    // Always pass the stencil test
    depthStencil.front.compareMask = 0xFF;                  // Compare all bits
    depthStencil.front.writeMask = 0xFF;                    // Write all bits in stencil buffer
    depthStencil.front.reference = 0; // Default reference value (will be overridden by vkCmdSetStencilReference)

    // Back face stencil operations (same as front face)
    depthStencil.back = depthStencil.front;

    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    auto &app = Application::getInstance();
    auto &project = app.getProject();

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

void GBufferPass::createTerrainPipeline()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    if (vc.isExtendedDynamicState3Enabled()) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_POLYGON_MODE_EXT);
    }

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // No vertex input - terrain generates vertices from gl_VertexIndex + heightmap
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

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[4];
    for (int i = 0; i < 4; ++i) {
        colorBlendAttachments[i] = {};
        colorBlendAttachments[i].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[i].blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 4;
    colorBlending.pAttachments = colorBlendAttachments;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    auto &project = app.getProject();
    auto shaderPath = project.getProjectShaderDirectory();

    auto [shader, handle] = AssetManager::importAsset<Shader>(shaderPath / "glsl/terrain/terrain_gbuffer.vs.glsl");

    if (!shader) {
        RP_CORE_WARN("Failed to load terrain GBuffer shader - terrain rendering disabled");
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

    m_terrainShader = shader;
    m_terrainShaderHandle = handle;

    m_terrainPipeline = std::make_shared<GraphicsPipeline>(config);

    RP_CORE_TRACE("GBufferPass: Terrain pipeline created");
}

void GBufferPass::recordTerrainCommands(CommandBuffer *commandBuffer, std::shared_ptr<Scene> activeScene, TerrainGenerator &terrain,
                                        uint32_t currentFrame)
{
    RAPTURE_PROFILE_FUNCTION();

    if (!m_terrainPipeline || !terrain.isInitialized()) {
        return;
    }

    m_currentFrame = currentFrame;

    auto mainCamera = activeScene->getMainCamera();
    if (!mainCamera) {
        return;
    }

    auto *cameraComp = mainCamera.tryGetComponent<CameraComponent>();
    if (!cameraComp) {
        return;
    }

    m_terrainPipeline->bind(commandBuffer->getCommandBufferVk());

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    if (vc.isExtendedDynamicState3Enabled() && vc.vkCmdSetPolygonModeEXT) {
        VkPolygonMode mode = terrain.isWireframe() ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        vc.vkCmdSetPolygonModeEXT(commandBuffer->getCommandBufferVk(), mode);
    }

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

    DescriptorManager::bindSet(0, commandBuffer, m_terrainPipeline); // Camera + chunk data SSBOs
    DescriptorManager::bindSet(3, commandBuffer, m_terrainPipeline); // Bindless textures

    uint32_t chunkDataBufferIndex = terrain.getChunkDataBuffer()->getBindlessIndex();
    uint32_t continentalnessIndex = terrain.getNoiseTexture(CONTINENTALNESS)->getBindlessIndex();
    uint32_t erosionIndex = terrain.getNoiseTexture(EROSION)->getBindlessIndex();
    uint32_t peaksValleysIndex = terrain.getNoiseTexture(PEAKS_VALLEYS)->getBindlessIndex();
    uint32_t noiseLUTIndex = terrain.getNoiseLUT()->getBindlessIndex();

    const auto &terrainConfig = terrain.getConfig();
    VkBuffer countBuffer = terrain.getDrawCountBuffer()->getBufferVk();

    for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod) {
        vkCmdBindIndexBuffer(commandBuffer->getCommandBufferVk(), terrain.getIndexBuffer(lod), 0, VK_INDEX_TYPE_UINT32);

        TerrainGBufferPushConstants pc{};
        pc.cameraBindlessIndex = cameraComp->cameraDataBuffer->getDescriptorIndex(m_currentFrame);
        pc.chunkDataBufferIndex = chunkDataBufferIndex;
        pc.continentalnessIndex = continentalnessIndex;
        pc.erosionIndex = erosionIndex;
        pc.peaksValleysIndex = peaksValleysIndex;
        pc.noiseLUTIndex = noiseLUTIndex;
        pc.lodResolution = getTerrainLODResolution(lod);
        pc.heightScale = terrainConfig.heightScale;
        pc.terrainWorldSize = terrainConfig.terrainWorldSize;

        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        if (auto shader = m_terrainShader.lock(); shader && shader->getPushConstantLayouts().size() > 0) {
            stageFlags = shader->getPushConstantLayouts()[0].stageFlags;
        }

        vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_terrainPipeline->getPipelineLayoutVk(), stageFlags, 0,
                           sizeof(TerrainGBufferPushConstants), &pc);

        VkBuffer indirectBuffer = terrain.getIndirectBuffer(lod)->getBufferVk();
        VkDeviceSize countOffset = lod * sizeof(uint32_t);
        uint32_t maxDrawCount = terrain.getIndirectBufferCapacity(lod);

        vkCmdDrawIndexedIndirectCount(commandBuffer->getCommandBufferVk(), indirectBuffer, 0, countBuffer, countOffset,
                                      maxDrawCount, sizeof(VkDrawIndexedIndirectCommand));
    }
}

} // namespace Rapture
