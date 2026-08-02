#include "GBufferPass.h"

#include "asset_manager/AssetImportConfig.h"
#include "components/Components.h"
#include "generators/terrain/TerrainGenerator.h"
#include "generators/terrain/TerrainTypes.h"
#include "logging/TracyProfiler.h"
#include "renderer/SceneRenderData.h"
#include "scenes/entities/Entity.h"
#include "window_context/Application.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Rapture {

struct GBufferPushConstants {
    uint32_t batchInfoBufferIndex;
    uint32_t cameraSSBOIndex;
    uint32_t cameraSlotIndex;
    uint32_t meshSSBOIndex;
};

struct TerrainGBufferPushConstants {
    uint32_t cameraSSBOIndex;
    uint32_t cameraSlotIndex;
    uint32_t chunkDataBufferIndex;
    uint32_t continentalnessIndex; // Also used for single heightmap when useMultiNoise = 0
    uint32_t erosionIndex;
    uint32_t peaksValleysIndex;
    uint32_t splineCurveIndex;
    uint32_t useMultiNoise;
    uint32_t lodResolution;
    float heightScale;
    float terrainWorldSize;
    uint32_t materialIndex;
};

// The targets every device is guaranteed to give one pass
static constexpr uint32_t GBUFFER_ATTACHMENT_COUNT_MIN = 4;
// Every target the G-buffer wants, optional slots included
static constexpr uint32_t GBUFFER_ATTACHMENT_COUNT_ALL = 5;

// Targets past the guaranteed four are a hard drop: a device that cannot afford them loses the
// features they carry rather than getting a substitute.
static bool s_hasAllGBufferAttachments()
{
    return Application::getInstance().getVulkanContext().getMaxColorAttachments() >= GBUFFER_ATTACHMENT_COUNT_ALL;
}

GBufferPass::GBufferPass(float width, float height, uint32_t framesInFlight)
    : m_width(width), m_height(height), m_framesInFlight(framesInFlight), m_currentFrame(0)
{

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    m_rc = &vc.getRenderContext();
    m_device = vc.getLogicalDevice();
    m_vmaAllocator = vc.getVmaAllocator();

    createPipeline();
    createTerrainPipeline();
    createTextures();

    // Initialize MDI batching system - one set per frame in flight
    m_mdiBatchMaps.resize(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; i++) {
        m_mdiBatchMaps[i] = std::make_unique<MDIBatchMap>(*m_rc);
    }

    // Bind GBuffer textures to bindless set
    bindGBufferTexturesToBindlessSet();

    setupCommandResources();
}

void GBufferPass::setupCommandResources()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    CommandPoolConfig config = {};
    config.queueFamilyIndex = vc.getGraphicsQueueIndex();
    config.flags = 0;
    size_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    config.threadId = threadId;
    m_commandPoolHash = m_rc->commandPoolManager->createCommandPool(config);
}

GBufferPass::~GBufferPass()
{
    // Wait for device to finish operations
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    vc.waitIdle();

    // Clean up textures
    m_normalTextures.clear();
    m_albedoSpecTextures.clear();
    m_materialTextures.clear();
    m_shadingModelTextures.clear();
    m_gbufferE.clear();
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
    spec.colorAttachments.push_back(VK_FORMAT_R16G16B16A16_SFLOAT); // rg=octahedral world normal ba=motion vector
    spec.colorAttachments.push_back(VK_FORMAT_R8G8B8A8_SRGB);       // rgb=base color a=packed emissive intensity
    spec.colorAttachments.push_back(VK_FORMAT_R8G8B8A8_UNORM);      // r=metallic g=roughness b=AO a=specular
    spec.colorAttachments.push_back(VK_FORMAT_R8G8B8A8_UNORM);      // r=shading model id gba=custom data
    if (s_hasAllGBufferAttachments()) {
        spec.colorAttachments.push_back(VK_FORMAT_R32_UINT); // r=entity id, biased by one
    }

    return spec;
}

std::vector<Texture *> GBufferPass::getDepthTextures() const
{
    std::vector<Texture *> depthTextures;
    depthTextures.reserve(m_depthStencilTextures.size());
    for (const std::unique_ptr<Texture> &texture : m_depthStencilTextures) {
        depthTextures.push_back(texture.get());
    }

    return depthTextures;
}

CommandBuffer *GBufferPass::record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance)
{
    RAPTURE_PROFILE_FUNCTION();

    Scene &activeScene = *context.scene;
    Entity camera = context.camera;
    TerrainGenerator *terrain = context.terrain;
    uint32_t currentFrame = context.frameInFlight;

    m_currentFrame = currentFrame;

    // updates the hash according to the current thread
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    CommandPoolConfig config = {};
    config.queueFamilyIndex = vc.getGraphicsQueueIndex();
    config.flags = 0;
    size_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    config.threadId = threadId;
    auto hash = m_rc->commandPoolManager->createCommandPool(config);

    auto pool = m_rc->commandPoolManager->getCommandPool(hash);
    auto commandBuffer = pool->getSecondaryCommandBuffer();

    commandBuffer->beginSecondary(inheritance);

    if (terrain && terrain->isInitialized()) {
        recordTerrainCommands(commandBuffer, activeScene, camera, *terrain, currentFrame);
    }

    recordEntityCommands(commandBuffer, activeScene, camera, currentFrame);

    commandBuffer->end();

    return commandBuffer;
}

void GBufferPass::recordEntityCommands(CommandBuffer *secondaryCb, Scene &activeScene, Entity camera, uint32_t currentFrame)
{
    RAPTURE_PROFILE_FUNCTION();

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
    auto &registry = activeScene.getRegistry();
    auto view = registry.view<TransformComponent, MeshComponent, MaterialComponent, BoundingBoxComponent>();
    CameraComponent *cameraComp = nullptr;

    if (camera.isValid()) {
        cameraComp = camera.tryGetComponent<CameraComponent>();
    }

    // Begin frame for MDI batching - use current frame's batch maps
    m_mdiBatchMaps[currentFrame]->beginFrame();

    // bind descriptor sets
    m_rc->descriptorManager->bindSet(0, secondaryCb, m_pipeline); // camera stuff
    m_rc->descriptorManager->bindSet(1, secondaryCb, m_pipeline); // materials
    m_rc->descriptorManager->bindSet(2, secondaryCb, m_pipeline); // model data
    m_rc->descriptorManager->bindSet(3, secondaryCb, m_pipeline); // bindless textures for the material stuff

    auto &renderData = *(activeScene.getRenderData());
    uint32_t meshSSBOIndex = renderData.getMeshes().getDescriptorIndex(currentFrame);
    uint32_t cameraSSBOIndex = renderData.getCameras().getDescriptorIndex(currentFrame);
    uint32_t cameraSlotIndex = (cameraComp != nullptr && cameraComp->renderDataSlot != UINT32_MAX) ? cameraComp->renderDataSlot : 0;

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

        boundingBoxComp.updateWorldBoundingBox(transform);

        if (cameraComp && activeScene.getSettings().frustumCullingEnabled) {
            if (cameraComp->frustum.testBoundingBox(boundingBoxComp.worldBoundingBox) == FrustumResult::Outside) {
                continue;
            }
        }

        // Get buffer allocation info to determine batch
        auto vboAlloc = meshComp.mesh->getVertexAllocation();
        auto iboAlloc = meshComp.mesh->getIndexAllocation();

        if (!vboAlloc || !iboAlloc) {
            continue;
        }

        // Get or create batch for this VBO/IBO arena combination
        MDIBatch *batch =
            m_mdiBatchMaps[currentFrame]->obtainBatch(vboAlloc, iboAlloc, meshComp.mesh->getVertexBuffer()->getBufferLayout(),
                                                      meshComp.mesh->getIndexBuffer()->getIndexType());

        uint32_t meshSlotIndex = meshComp.renderDataSlot;
        uint32_t materialIndex = materialComp.material ? materialComp.material->getBindlessIndex() : 0;

        batch->addObject(*meshComp.mesh, meshSlotIndex, materialIndex);
    }

    // Second pass: Render the batches using MDI
    for (const auto &[batchKey, batch] : m_mdiBatchMaps[currentFrame]->getBatches()) {
        if (batch->getDrawCount() == 0) {
            continue;
        }

        RAPTURE_PROFILE_SCOPE("Draw Batch");

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
        pushConstants.cameraSSBOIndex = cameraSSBOIndex;
        pushConstants.cameraSlotIndex = cameraSlotIndex;
        pushConstants.meshSSBOIndex = meshSSBOIndex;

        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        if (m_shader && m_shader->getPushConstantLayouts().size() > 0) {
            stageFlags = m_shader->getPushConstantLayouts()[0].stageFlags;
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

void GBufferPass::updateAttachments(const RenderPassContext &context)
{
    uint32_t frame = context.frameInFlight;
    m_currentFrame = frame;

    RenderPassAttachment colorAttachment;
    colorAttachment.loadOp = RenderPassAttachmentLoadOp::CLEAR;
    colorAttachment.storeOp = RenderPassAttachmentStoreOp::STORE;
    colorAttachment.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    m_attachments.colorAttachments.clear();
    m_attachments.colorAttachments.reserve(m_gbufferE.empty() ? 4 : 5);

    colorAttachment.target = m_normalTextures[frame].get();
    m_attachments.colorAttachments.push_back(colorAttachment);

    colorAttachment.target = m_albedoSpecTextures[frame].get();
    m_attachments.colorAttachments.push_back(colorAttachment);

    colorAttachment.target = m_materialTextures[frame].get();
    m_attachments.colorAttachments.push_back(colorAttachment);

    colorAttachment.target = m_shadingModelTextures[frame].get();
    m_attachments.colorAttachments.push_back(colorAttachment);

    if (!m_gbufferE.empty()) {
        colorAttachment.target = m_gbufferE[frame].get();
        m_attachments.colorAttachments.push_back(colorAttachment);
    }

    m_attachments.depthAttachment.target = m_depthStencilTextures[frame].get();
    m_attachments.depthAttachment.loadOp = RenderPassAttachmentLoadOp::CLEAR;
    m_attachments.depthAttachment.storeOp = RenderPassAttachmentStoreOp::STORE;
    m_attachments.depthAttachment.clearDepth = 1.0f;
    m_attachments.depthAttachment.clearStencil = 0;

    m_attachments.stencilAttachment = m_attachments.depthAttachment;
}

EntityID GBufferPass::readEntityId(uint32_t x, uint32_t y, uint32_t frameInFlight) const
{
    if (m_gbufferE.empty() || frameInFlight >= m_gbufferE.size()) {
        return INVALID_ENTITY_ID;
    }

    Texture *entityIdTexture = m_gbufferE[frameInFlight].get();
    std::vector<uint8_t> pixel = entityIdTexture->readbackRegion(x, y, 1, 1);
    if (pixel.size() < sizeof(uint32_t)) {
        return INVALID_ENTITY_ID;
    }

    uint32_t biased = 0;
    std::memcpy(&biased, pixel.data(), sizeof(uint32_t));

    // Ids are stored biased by one so the cleared value reads as nothing drawn
    if (biased == 0) {
        return INVALID_ENTITY_ID;
    }
    return biased - 1;
}

void GBufferPass::endRendering(CommandBuffer *primaryCb)
{
    RenderPass::endRendering(primaryCb);
    transitionToShaderReadableLayout(primaryCb, m_currentFrame);
}

void GBufferPass::onResize(uint32_t width, uint32_t height)
{
    // TODO: the renderer currently destroys and rebuilds every pass on resize, so only the size is
    // updated here. Recreate the textures and pipelines in place once passes outlive a resize, which
    // is also what shader/pipeline hot reloading needs.
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);
}

void GBufferPass::transitionToShaderReadableLayout(CommandBuffer *primaryCb, uint32_t currentFrame)
{
    RAPTURE_PROFILE_FUNCTION();

    VkImageMemoryBarrier barriers[6];
    barriers[0] = m_normalTextures[currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    barriers[1] = m_albedoSpecTextures[currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    barriers[2] = m_materialTextures[currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    barriers[3] = m_shadingModelTextures[currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    uint32_t barrierCount = 4;
    if (!m_gbufferE.empty()) {
        barriers[barrierCount++] = m_gbufferE[currentFrame]->getImageMemoryBarrier(
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    barriers[barrierCount++] = m_depthStencilTextures[currentFrame]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    vkCmdPipelineBarrier(primaryCb->getCommandBufferVk(),
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         barrierCount, barriers);
}

void GBufferPass::createTextures()
{
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

    TextureSpecification shadingModelSpec;
    shadingModelSpec.width = static_cast<uint32_t>(m_width);
    shadingModelSpec.height = static_cast<uint32_t>(m_height);
    shadingModelSpec.format = TextureFormat::RGBA8;
    shadingModelSpec.type = TextureType::TEXTURE2D;
    shadingModelSpec.srgb = false;

    TextureSpecification entityIdSpec;
    entityIdSpec.width = static_cast<uint32_t>(m_width);
    entityIdSpec.height = static_cast<uint32_t>(m_height);
    entityIdSpec.format = TextureFormat::R32UI;
    entityIdSpec.type = TextureType::TEXTURE2D;
    entityIdSpec.srgb = false;
    entityIdSpec.allowReadback = true;
    // Integer formats support no filtering, and neighbouring ids must not be blended in any case
    entityIdSpec.filter = TextureFilter::Nearest;
    entityIdSpec.wrap = TextureWrap::ClampToEdge;

    TextureSpecification depthStencilSpec;
    depthStencilSpec.width = static_cast<uint32_t>(m_width);
    depthStencilSpec.height = static_cast<uint32_t>(m_height);
    depthStencilSpec.format = TextureFormat::D24S8;
    depthStencilSpec.type = TextureType::TEXTURE2D;
    depthStencilSpec.srgb = false;

    const bool entityIdSlot = s_hasAllGBufferAttachments();

    // Create textures for each frame in flight
    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        m_normalTextures.push_back(std::make_unique<Texture>(normalSpec));
        m_albedoSpecTextures.push_back(std::make_unique<Texture>(albedoSpec));
        m_materialTextures.push_back(std::make_unique<Texture>(materialSpec));
        m_shadingModelTextures.push_back(std::make_unique<Texture>(shadingModelSpec));
        if (entityIdSlot) {
            m_gbufferE.push_back(std::make_unique<Texture>(entityIdSpec));
        }
        m_depthStencilTextures.push_back(std::make_unique<Texture>(depthStencilSpec));
    }
}

void GBufferPass::bindGBufferTexturesToBindlessSet()
{

    // Resize the index vectors
    m_normalTextureIndices.resize(m_framesInFlight);
    m_albedoTextureIndices.resize(m_framesInFlight);
    m_materialTextureIndices.resize(m_framesInFlight);
    m_shadingModelTextureIndices.resize(m_framesInFlight);
    m_depthTextureIndices.resize(m_framesInFlight);

    // Add each texture to the bindless set and store the indices
    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        m_normalTextureIndices[i] = m_normalTextures[i]->getBindlessIndex();
        m_albedoTextureIndices[i] = m_albedoSpecTextures[i]->getBindlessIndex();
        m_materialTextureIndices[i] = m_materialTextures[i]->getBindlessIndex();
        m_shadingModelTextureIndices[i] = m_shadingModelTextures[i]->getBindlessIndex();
        m_depthTextureIndices[i] = m_depthStencilTextures[i]->getBindlessIndex();

        if (m_normalTextureIndices[i] == UINT32_MAX || m_albedoTextureIndices[i] == UINT32_MAX ||
            m_materialTextureIndices[i] == UINT32_MAX || m_shadingModelTextureIndices[i] == UINT32_MAX ||
            m_depthTextureIndices[i] == UINT32_MAX) {
            RP_CORE_ERROR("Failed to add GBuffer texture(s) to bindless array for frame {}", i);
        }
    }
}

void GBufferPass::createPipeline()
{
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

    const uint32_t colorAttachmentCount =
        s_hasAllGBufferAttachments() ? GBUFFER_ATTACHMENT_COUNT_ALL : GBUFFER_ATTACHMENT_COUNT_MIN;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[5];
    for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
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
    colorBlending.attachmentCount = colorAttachmentCount;
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
    depthStencil.stencilTestEnable = VK_FALSE;

    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    auto &app = Application::getInstance();
    auto &project = app.getProject();

    auto shaderPath = project.getProjectShaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl";
    shaderConfig.compileInfo.macros.emplace_back(s_hasAllGBufferAttachments() ? "GBUFFER_ATTACHMENT_COUNT_ALL"
                                                                             : "GBUFFER_ATTACHMENT_COUNT_MIN");

    auto asset = AssetManager::importAsset(shaderPath / "glsl/GBuffer.vs.glsl", shaderConfig);
    m_shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;

    if (!m_shader) {
        RP_CORE_ERROR("Failed to load GBuffer vertex shader");
        return;
    }
    if (m_shader) m_shaderAssets.push_back(std::move(asset));

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

    const uint32_t colorAttachmentCount =
        s_hasAllGBufferAttachments() ? GBUFFER_ATTACHMENT_COUNT_ALL : GBUFFER_ATTACHMENT_COUNT_MIN;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[5];
    for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
        colorBlendAttachments[i] = {};
        colorBlendAttachments[i].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[i].blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = colorAttachmentCount;
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

    ShaderImportConfig terrainShaderConfig;
    terrainShaderConfig.compileInfo.includePath = shaderPath / "glsl";
    terrainShaderConfig.compileInfo.macros.emplace_back(s_hasAllGBufferAttachments() ? "GBUFFER_ATTACHMENT_COUNT_ALL"
                                                                                    : "GBUFFER_ATTACHMENT_COUNT_MIN");

    auto asset = AssetManager::importAsset(shaderPath / "glsl/terrain/terrain_gbuffer.vs.glsl", terrainShaderConfig);
    m_terrainShader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (!m_terrainShader) {
        RP_CORE_WARN("Failed to load terrain GBuffer shader - terrain rendering disabled");
        return;
    }
    m_shaderAssets.push_back(std::move(asset));

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
    config.shader = m_terrainShader;

    m_terrainPipeline = std::make_shared<GraphicsPipeline>(config);

    RP_CORE_TRACE("GBufferPass: Terrain pipeline created");
}

void GBufferPass::recordTerrainCommands(CommandBuffer *commandBuffer, Scene &activeScene, Entity camera, TerrainGenerator &terrain,
                                        uint32_t currentFrame)
{
    (void)activeScene;
    RAPTURE_PROFILE_FUNCTION();

    if (!m_terrainPipeline || !terrain.isInitialized()) {
        return;
    }

    if (!camera.isValid()) {
        return;
    }

    auto *cameraComp = camera.tryGetComponent<CameraComponent>();
    if (cameraComp == nullptr) {
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

    m_rc->descriptorManager->bindSet(0, commandBuffer, m_terrainPipeline); // Camera + chunk data SSBOs
    m_rc->descriptorManager->bindSet(1, commandBuffer, m_terrainPipeline); // Materials
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_terrainPipeline); // Bindless textures

    const auto &terrainConfig = terrain.getConfig();

    uint32_t chunkDataBufferIndex = terrain.getChunkDataBuffer()->getBindlessIndex();
    uint32_t continentalnessIndex = terrain.getNoiseTexture(CONTINENTALNESS)->getBindlessIndex();
    uint32_t erosionIndex = 0;
    uint32_t peaksValleysIndex = 0;
    uint32_t splineCurveIndex = 0;

    if (terrainConfig.hmType == HM_CEPV) {
        erosionIndex = terrain.getNoiseTexture(EROSION)->getBindlessIndex();
        peaksValleysIndex = terrain.getNoiseTexture(PEAKS_VALLEYS)->getBindlessIndex();
        splineCurveIndex = terrain.getSplineCurveTexture()->getBindlessIndex();
    }
    auto *cullBuffers = terrain.getCullBuffers(currentFrame);
    if (!cullBuffers || !cullBuffers->drawCountBuffer) {
        return;
    }

    VkBuffer countBuffer = cullBuffers->drawCountBuffer->getBufferVk();

    // Without a material there is no graph to shade with, and the index would read past the buffer
    uint32_t materialIndex = terrain.getMaterialIndex();
    if (materialIndex == UINT32_MAX) {
        return;
    }

    for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod) {
        if (!cullBuffers->indirectBuffers[lod]) {
            continue;
        }

        vkCmdBindIndexBuffer(commandBuffer->getCommandBufferVk(), terrain.getIndexBuffer(lod), 0, VK_INDEX_TYPE_UINT32);

        TerrainGBufferPushConstants pc{};
        auto &terrainRenderData = *(activeScene.getRenderData());
        pc.cameraSSBOIndex = terrainRenderData.getCameras().getDescriptorIndex(currentFrame);
        pc.cameraSlotIndex = (cameraComp->renderDataSlot != UINT32_MAX) ? cameraComp->renderDataSlot : 0;
        pc.chunkDataBufferIndex = chunkDataBufferIndex;
        pc.continentalnessIndex = continentalnessIndex;
        pc.erosionIndex = erosionIndex;
        pc.peaksValleysIndex = peaksValleysIndex;
        pc.splineCurveIndex = splineCurveIndex;
        pc.useMultiNoise = terrainConfig.hmType == HM_CEPV ? 1u : 0u;
        pc.lodResolution = getTerrainLODResolution(lod);
        pc.heightScale = terrainConfig.heightScale;
        pc.terrainWorldSize = terrainConfig.terrainWorldSize;
        pc.materialIndex = materialIndex;

        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        if (m_terrainShader && m_terrainShader->getPushConstantLayouts().size() > 0) {
            stageFlags = m_terrainShader->getPushConstantLayouts()[0].stageFlags;
        }

        vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_terrainPipeline->getPipelineLayoutVk(), stageFlags, 0,
                           sizeof(TerrainGBufferPushConstants), &pc);

        VkBuffer indirectBuffer = cullBuffers->indirectBuffers[lod]->getBufferVk();
        VkDeviceSize countOffset = lod * sizeof(uint32_t);
        uint32_t maxDrawCount = cullBuffers->indirectCapacities[lod];

        vkCmdDrawIndexedIndirectCount(commandBuffer->getCommandBufferVk(), indirectBuffer, 0, countBuffer, countOffset,
                                      maxDrawCount, sizeof(VkDrawIndexedIndirectCommand));
    }
}

} // namespace Rapture
