#include "renderer/deferred/GBufferPass.h"

#include "core/utils/EnginePaths.h"
#include "gpu/descriptors/DescriptorManager.h"

#include "app/Application.h"
#include "assets/asset_manager/AssetImportConfig.h"
#include "core/ecs/entity_accessor.h"
#include "core/utils/TracyProfiler.h"
#include "renderer/generators/terrain/TerrainGenerator.h"
#include "renderer/generators/terrain/TerrainTypes.h"
#include "scene/components/Components.h"
#include "scene/render_data/SceneRenderData.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Rapture {

struct GBufferPushConstants {
    uint32_t batchInfoBufferIndex;
    uint32_t cameraSSBOIndex;
    uint32_t cameraSlotIndex;
    uint32_t meshSSBOIndex;
    uint32_t skeletonSSBOIndex;
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

static constexpr uint32_t GBUFFER_ATTACHMENT_COUNT = 4;

GBufferPass::GBufferPass(float width, float height, uint32_t framesInFlight)
    : m_width(width), m_height(height), m_framesInFlight(framesInFlight), m_currentFrame(0)
{

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    m_rc = &vc.getRenderContext();
    m_device = vc.getLogicalDevice();
    m_vmaAllocator = vc.getVmaAllocator();

    createPipeline(false);
    createPipeline(true);
    createTerrainPipeline();
    createTextures();

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

    // Clean up pipelines
    m_pipeline.reset();
    m_skinnedPipeline.reset();
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

    return spec;
}

CommandBuffer *GBufferPass::record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance)
{
    RAPTURE_PROFILE_FUNCTION();

    Scene &activeScene = *context.scene;
    ecs::EntityAccessor camera = context.camera;
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

    recordEntityCommands(commandBuffer, context);

    commandBuffer->end();

    return commandBuffer;
}

void GBufferPass::recordEntityCommands(CommandBuffer *secondaryCb, const RenderPassContext &context)
{
    RAPTURE_PROFILE_FUNCTION();

    Scene &activeScene = *context.scene;
    ecs::EntityAccessor camera = context.camera;
    uint32_t currentFrame = context.frameInFlight;
    SceneGeometryDraw *geometry = context.opaqueGeometry;

    m_pipeline->bind(secondaryCb->getCommandBufferVk());

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

    // bind descriptor sets
    m_rc->descriptorManager->bindSet(0, secondaryCb, m_pipeline); // camera stuff
    m_rc->descriptorManager->bindSet(1, secondaryCb, m_pipeline); // materials
    m_rc->descriptorManager->bindSet(2, secondaryCb, m_pipeline); // model data
    m_rc->descriptorManager->bindSet(3, secondaryCb, m_pipeline); // bindless textures for the material stuff

    auto &renderData = *(activeScene.getRenderData());
    uint32_t meshSSBOIndex = renderData.getMeshes().getDescriptorIndex(currentFrame);
    uint32_t cameraSSBOIndex = renderData.getCameras().getDescriptorIndex(currentFrame);
    uint32_t cameraSlot = renderData.getCameraSlot(camera.getEntity());
    uint32_t cameraSlotIndex = (cameraSlot != UINT32_MAX) ? cameraSlot : 0;
    uint32_t skeletonSSBOIndex = renderData.getSkeletonInstanceManager().getBindlessIndex();

    // the initial bind above, so the first batch only rebinds if it is skinned
    GraphicsPipeline *boundPipeline = m_pipeline.get();

    for (MDIBatch *batch : geometry->batches(currentFrame)) {
        RAPTURE_PROFILE_SCOPE("Draw Batch");

        // a batch is keyed on its vertex layout, so it is skinned or unskinned as a whole
        bool isSkinned = batch->getBufferLayout().getAttributeOffset(BufferAttributeID::JOINTS_0) != UINT32_MAX;
        GraphicsPipeline *pipeline = isSkinned ? m_skinnedPipeline.get() : m_pipeline.get();
        if (pipeline == nullptr) {
            continue;
        }

        if (pipeline != boundPipeline) {
            pipeline->bind(secondaryCb->getCommandBufferVk());
            boundPipeline = pipeline;
        }

        geometry->bindBatch(secondaryCb, batch);

        // Set push constants for this batch
        GBufferPushConstants pushConstants{};
        pushConstants.batchInfoBufferIndex = batch->getBatchInfoBufferIndex();
        pushConstants.cameraSSBOIndex = cameraSSBOIndex;
        pushConstants.cameraSlotIndex = cameraSlotIndex;
        pushConstants.meshSSBOIndex = meshSSBOIndex;
        pushConstants.skeletonSSBOIndex = skeletonSSBOIndex;

        const Shader *shader = isSkinned ? m_skinnedShader : m_shader;
        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        if (shader && shader->getPushConstantLayouts().size() > 0) {
            stageFlags = shader->getPushConstantLayouts()[0].stageFlags;
        }

        vkCmdPushConstants(secondaryCb->getCommandBufferVk(), pipeline->getPipelineLayoutVk(), stageFlags, 0,
                           sizeof(GBufferPushConstants), &pushConstants);

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
    m_attachments.colorAttachments.reserve(GBUFFER_ATTACHMENT_COUNT);

    colorAttachment.target = m_normalTextures[frame].get();
    m_attachments.colorAttachments.push_back(colorAttachment);

    colorAttachment.target = m_albedoSpecTextures[frame].get();
    m_attachments.colorAttachments.push_back(colorAttachment);

    colorAttachment.target = m_materialTextures[frame].get();
    m_attachments.colorAttachments.push_back(colorAttachment);

    colorAttachment.target = m_shadingModelTextures[frame].get();
    m_attachments.colorAttachments.push_back(colorAttachment);

    // the prepass laid this frame's depth down already, so it is loaded rather than cleared
    m_attachments.depthAttachment.target = context.targets->depthStencil;
    m_attachments.depthAttachment.loadOp = RenderPassAttachmentLoadOp::LOAD;
    m_attachments.depthAttachment.storeOp = RenderPassAttachmentStoreOp::STORE;

    m_attachments.stencilAttachment = m_attachments.depthAttachment;
}

void GBufferPass::onResize(uint32_t width, uint32_t height)
{
    // TODO: the renderer currently destroys and rebuilds every pass on resize, so only the size is
    // updated here. Recreate the textures and pipelines in place once passes outlive a resize, which
    // is also what shader/pipeline hot reloading needs.
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);
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

    // Create textures for each frame in flight
    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        m_normalTextures.push_back(std::make_unique<Texture>(normalSpec));
        m_albedoSpecTextures.push_back(std::make_unique<Texture>(albedoSpec));
        m_materialTextures.push_back(std::make_unique<Texture>(materialSpec));
        m_shadingModelTextures.push_back(std::make_unique<Texture>(shadingModelSpec));
    }
}

void GBufferPass::bindGBufferTexturesToBindlessSet()
{

    // Resize the index vectors
    m_normalTextureIndices.resize(m_framesInFlight);
    m_albedoTextureIndices.resize(m_framesInFlight);
    m_materialTextureIndices.resize(m_framesInFlight);
    m_shadingModelTextureIndices.resize(m_framesInFlight);

    // Add each texture to the bindless set and store the indices
    for (uint32_t i = 0; i < m_framesInFlight; i++) {
        m_normalTextureIndices[i] = m_normalTextures[i]->getBindlessIndex();
        m_albedoTextureIndices[i] = m_albedoSpecTextures[i]->getBindlessIndex();
        m_materialTextureIndices[i] = m_materialTextures[i]->getBindlessIndex();
        m_shadingModelTextureIndices[i] = m_shadingModelTextures[i]->getBindlessIndex();

        if (m_normalTextureIndices[i] == UINT32_MAX || m_albedoTextureIndices[i] == UINT32_MAX ||
            m_materialTextureIndices[i] == UINT32_MAX || m_shadingModelTextureIndices[i] == UINT32_MAX) {
            RP_CORE_ERROR("Failed to add GBuffer texture(s) to bindless array for frame {}", i);
        }
    }
}

void GBufferPass::createPipeline(bool skinned)
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

    VkPipelineColorBlendAttachmentState colorBlendAttachments[GBUFFER_ATTACHMENT_COUNT];
    for (uint32_t i = 0; i < GBUFFER_ATTACHMENT_COUNT; ++i) {
        colorBlendAttachments[i] = {};
        colorBlendAttachments[i].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[i].blendEnable = VK_FALSE;
        // Other blend factors can be left as default if blendEnable is VK_FALSE
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = GBUFFER_ATTACHMENT_COUNT;
    colorBlending.pAttachments = colorBlendAttachments; // Changed from &colorBlendAttachment
    colorBlending.blendConstants[0] = 0.0f;             // Optional
    colorBlending.blendConstants[1] = 0.0f;             // Optional
    colorBlending.blendConstants[2] = 0.0f;             // Optional
    colorBlending.blendConstants[3] = 0.0f;             // Optional

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    auto shaderPath = EnginePaths::shaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl";
    if (skinned) {
        shaderConfig.compileInfo.macros.emplace_back("IS_SKINNED_MESH");
    }

    auto asset = AssetManager::importAsset(shaderPath / "glsl/GBuffer.vs.glsl", shaderConfig);
    Shader *shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;

    if (!shader) {
        RP_CORE_ERROR("Failed to load GBuffer vertex shader");
        return;
    }
    m_shaderAssets.push_back(std::move(asset));

    if (skinned) {
        m_skinnedShader = shader;
    } else {
        m_shader = shader;
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

    if (skinned) {
        m_skinnedPipeline = std::make_unique<GraphicsPipeline>(config);
        return;
    }

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

    VkPipelineColorBlendAttachmentState colorBlendAttachments[GBUFFER_ATTACHMENT_COUNT];
    for (uint32_t i = 0; i < GBUFFER_ATTACHMENT_COUNT; ++i) {
        colorBlendAttachments[i] = {};
        colorBlendAttachments[i].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[i].blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = GBUFFER_ATTACHMENT_COUNT;
    colorBlending.pAttachments = colorBlendAttachments;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    auto shaderPath = EnginePaths::shaderDirectory();

    ShaderImportConfig terrainShaderConfig;
    terrainShaderConfig.compileInfo.includePath = shaderPath / "glsl";

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

void GBufferPass::recordTerrainCommands(CommandBuffer *commandBuffer, Scene &activeScene, ecs::EntityAccessor camera,
                                        TerrainGenerator &terrain, uint32_t currentFrame)
{
    (void)activeScene;
    RAPTURE_PROFILE_FUNCTION();

    if (!m_terrainPipeline || !terrain.isInitialized()) {
        return;
    }

    if (!camera.isValid()) {
        return;
    }

    auto *cameraComp = camera.tryRead<CameraComponent>();
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
        uint32_t cameraSlot = terrainRenderData.getCameraSlot(camera.getEntity());
        pc.cameraSlotIndex = (cameraSlot != UINT32_MAX) ? cameraSlot : 0;
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
