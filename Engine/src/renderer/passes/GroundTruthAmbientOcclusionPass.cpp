#include "GroundTruthAmbientOcclusionPass.h"

#include "asset_manager/AssetImportConfig.h"
#include "asset_manager/AssetManager.h"
#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/descriptors/DescriptorManager.h"
#include "components/Components.h"
#include "logging/Log.h"
#include "logging/TracyProfiler.h"
#include "renderer/SceneRenderData.h"
#include "scenes/Scene.h"
#include "utils/rp_assert.h"
#include "window_context/Application.h"

#include <glm/glm.hpp>

namespace Rapture {

struct OcclusionPushConstants {
    alignas(4) uint32_t cameraSSBOIndex;
    alignas(4) uint32_t cameraSlotIndex;
    alignas(4) uint32_t depthTextureIndex;
    alignas(4) uint32_t normalTextureIndex;
    alignas(4) uint32_t linearDepthTextureIndex;
    alignas(4) uint32_t frameIndex;
    alignas(4) float radius;
    alignas(4) float maxScreenRadius;
    alignas(4) float falloffRange;
    alignas(4) int32_t sliceCount;
    alignas(4) int32_t stepCount;
    alignas(8) glm::ivec2 outputSize;
};

struct DenoisePushConstants {
    alignas(4) uint32_t cameraSSBOIndex;
    alignas(4) uint32_t cameraSlotIndex;
    alignas(4) uint32_t occlusionTextureIndex;
    alignas(4) uint32_t previousDenoisedTextureIndex;
    alignas(4) uint32_t depthTextureIndex;
    alignas(4) uint32_t normalTextureIndex;
    alignas(4) uint32_t linearDepthTextureIndex;
    alignas(4) uint32_t historyLinearDepthTextureIndex;
    alignas(4) uint32_t hasHistory;
    alignas(4) float depthRejection;
    alignas(4) float hysteresis;
    alignas(8) glm::ivec2 outputSize;
};

static constexpr uint32_t GTAO_LOCAL_SIZE = 8;

GroundTruthAmbientOcclusionPass::GroundTruthAmbientOcclusionPass(uint32_t width, uint32_t height, uint32_t framesInFlight)
    : m_width(width), m_height(height), m_framesInFlight(framesInFlight)
{
    m_rc = &Application::getInstance().getVulkanContext().getRenderContext();

    loadShaders();
    createTextures();
    createDescriptorSets();
}

GroundTruthAmbientOcclusionPass::~GroundTruthAmbientOcclusionPass()
{
    m_occlusionSets.clear();
    m_denoisedSets.clear();
    m_occlusionTextures.clear();
    m_denoisedTextures.clear();
    m_occlusionPipeline.reset();
    m_denoisePipeline.reset();
}

void GroundTruthAmbientOcclusionPass::loadShaders()
{
    auto &project = Application::getInstance().getProject();
    auto shaderPath = project.getProjectShaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl";

    auto occlusionAsset = AssetManager::importAsset(shaderPath / "glsl/GroundTruthAmbientOcclusion.cs.glsl", shaderConfig);
    m_occlusionShader = occlusionAsset ? occlusionAsset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_occlusionShader != nullptr) {
        m_shaderAssets.push_back(std::move(occlusionAsset));
    }

    auto denoiseAsset =
        AssetManager::importAsset(shaderPath / "glsl/GroundTruthAmbientOcclusionDenoise.cs.glsl", shaderConfig);
    m_denoiseShader = denoiseAsset ? denoiseAsset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_denoiseShader != nullptr) {
        m_shaderAssets.push_back(std::move(denoiseAsset));
    }

    RP_ASSERT(m_occlusionShader != nullptr && m_denoiseShader != nullptr, "Ambient occlusion shaders failed to load");
    if (m_occlusionShader == nullptr || m_denoiseShader == nullptr) {
        RP_CORE_ERROR("Ambient occlusion shaders failed to load");
        return;
    }

    ComputePipelineConfiguration occlusionPipelineConfig;
    occlusionPipelineConfig.shader = m_occlusionShader;
    m_occlusionPipeline = std::make_shared<ComputePipeline>(occlusionPipelineConfig);

    ComputePipelineConfiguration denoisePipelineConfig;
    denoisePipelineConfig.shader = m_denoiseShader;
    m_denoisePipeline = std::make_shared<ComputePipeline>(denoisePipelineConfig);
}

void GroundTruthAmbientOcclusionPass::createTextures()
{
    TextureSpecification spec;
    spec.width = m_width;
    spec.height = m_height;
    spec.format = TextureFormat::RGBA16F;
    spec.type = TextureType::TEXTURE2D;
    spec.filter = TextureFilter::Linear;
    spec.wrap = TextureWrap::ClampToEdge;
    spec.srgb = false;
    spec.storageImage = true;

    m_occlusionTextures.reserve(m_framesInFlight);
    m_denoisedTextures.reserve(m_framesInFlight);
    for (uint32_t frame = 0; frame < m_framesInFlight; ++frame) {
        m_occlusionTextures.push_back(std::make_unique<Texture>(spec));
        m_occlusionTextures.back()->getBindlessIndex();

        m_denoisedTextures.push_back(std::make_unique<Texture>(spec));
        m_denoisedTextures.back()->getBindlessIndex();
    }
}

void GroundTruthAmbientOcclusionPass::createDescriptorSets()
{
    DescriptorSetBindings bindings;
    bindings.setNumber = 4;

    DescriptorSetBinding outputBinding = {};
    outputBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputBinding.location = DescriptorSetBindingLocation::CUSTOM_0;
    outputBinding.useStorageImageInfo = true;
    bindings.bindings.push_back(outputBinding);

    m_occlusionSets.reserve(m_framesInFlight);
    m_denoisedSets.reserve(m_framesInFlight);
    for (uint32_t frame = 0; frame < m_framesInFlight; ++frame) {
        auto occlusionSet = std::make_unique<DescriptorSet>(bindings);
        occlusionSet->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*m_occlusionTextures[frame]);
        m_occlusionSets.push_back(std::move(occlusionSet));

        auto denoisedSet = std::make_unique<DescriptorSet>(bindings);
        denoisedSet->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*m_denoisedTextures[frame]);
        m_denoisedSets.push_back(std::move(denoisedSet));
    }
}

Texture *GroundTruthAmbientOcclusionPass::getOcclusionTexture(uint32_t frameInFlight) const
{
    if (frameInFlight >= m_occlusionTextures.size()) {
        RP_CORE_ERROR("Requested occlusion texture for frame {} but only {} exist", frameInFlight, m_occlusionTextures.size());
        return nullptr;
    }
    return m_occlusionTextures[frameInFlight].get();
}

Texture *GroundTruthAmbientOcclusionPass::getDenoisedTexture(uint32_t frameInFlight) const
{
    if (frameInFlight >= m_denoisedTextures.size()) {
        RP_CORE_ERROR("Requested denoised texture for frame {} but only {} exist", frameInFlight, m_denoisedTextures.size());
        return nullptr;
    }
    return m_denoisedTextures[frameInFlight].get();
}

void GroundTruthAmbientOcclusionPass::onResize(uint32_t width, uint32_t height)
{
    // TODO: the renderer currently destroys and rebuilds every pass on resize, so only the size is
    // updated here. Recreate the textures and descriptor sets in place once passes outlive a resize.
    m_width = width;
    m_height = height;
}

void GroundTruthAmbientOcclusionPass::updateResources(const RenderPassContext &context)
{
    m_resources.clear();

    ComputeResource depth;
    depth.texture = context.targets->depthStencil;
    depth.access = ComputeResourceAccess::READ;
    m_resources.push_back(depth);

    ComputeResource normal;
    normal.texture = context.targets->gbufferNormalMotion;
    normal.access = ComputeResourceAccess::READ;
    m_resources.push_back(normal);

    ComputeResource hiZ;
    hiZ.texture = context.targets->hiZ;
    hiZ.access = ComputeResourceAccess::READ;
    m_resources.push_back(hiZ);

    ComputeResource historyLinearDepth;
    historyLinearDepth.texture = context.targets->historyLinearDepth;
    historyLinearDepth.access = ComputeResourceAccess::READ;
    m_resources.push_back(historyLinearDepth);

    // Written by the horizon search and read straight back by the filter, so it stays in GENERAL
    // across both
    ComputeResource occlusion;
    occlusion.texture = getOcclusionTexture(context.frameInFlight);
    occlusion.access = ComputeResourceAccess::READ_WRITE;
    occlusion.discardContents = true;
    occlusion.readableAfter = true;
    m_resources.push_back(occlusion);

    const uint32_t count = static_cast<uint32_t>(m_denoisedTextures.size());
    ComputeResource previousDenoised;
    previousDenoised.texture = getDenoisedTexture((context.frameInFlight + count - 1) % count);
    previousDenoised.access = ComputeResourceAccess::READ;
    m_resources.push_back(previousDenoised);

    ComputeResource denoised;
    denoised.texture = getDenoisedTexture(context.frameInFlight);
    denoised.access = ComputeResourceAccess::WRITE;
    denoised.discardContents = true;
    denoised.readableAfter = true;
    m_resources.push_back(denoised);
}

void GroundTruthAmbientOcclusionPass::recordOcclusion(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    auto &renderData = *(context.scene->getRenderData());
    auto *cameraComp = context.camera.isValid() ? context.camera.tryGetComponent<CameraComponent>() : nullptr;

    m_occlusionPipeline->bind(commandBuffer->getCommandBufferVk());
    m_rc->descriptorManager->bindSet(0, commandBuffer, m_occlusionPipeline);
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_occlusionPipeline);
    m_occlusionSets[context.frameInFlight]->bind(commandBuffer->getCommandBufferVk(), m_occlusionPipeline);

    OcclusionPushConstants pushConstants{};
    pushConstants.cameraSSBOIndex = renderData.getCameras().getDescriptorIndex(context.frameInFlight);
    pushConstants.cameraSlotIndex =
        (cameraComp != nullptr && cameraComp->renderDataSlot != UINT32_MAX) ? cameraComp->renderDataSlot : 0;
    pushConstants.depthTextureIndex = context.targets->depthStencil->getBindlessIndex();
    pushConstants.normalTextureIndex = context.targets->gbufferNormalMotion->getBindlessIndex();
    pushConstants.linearDepthTextureIndex = context.targets->hiZ->getBindlessIndex();
    pushConstants.frameIndex = static_cast<uint32_t>(Application::getInstance().getFrameCount());
    pushConstants.radius = m_radius;
    pushConstants.maxScreenRadius = m_maxScreenRadius;
    pushConstants.falloffRange = m_falloffRange;
    pushConstants.sliceCount = m_sliceCount;
    pushConstants.stepCount = m_stepCount;
    pushConstants.outputSize = glm::ivec2(m_width, m_height);

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_occlusionPipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(OcclusionPushConstants), &pushConstants);

    vkCmdDispatch(commandBuffer->getCommandBufferVk(), groupCount(m_width, GTAO_LOCAL_SIZE),
                  groupCount(m_height, GTAO_LOCAL_SIZE), 1);
}

void GroundTruthAmbientOcclusionPass::recordDenoise(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    const uint32_t count = static_cast<uint32_t>(m_denoisedTextures.size());
    const uint32_t previousFrame = (context.frameInFlight + count - 1) % count;

    auto &renderData = *(context.scene->getRenderData());
    auto *cameraComp = context.camera.isValid() ? context.camera.tryGetComponent<CameraComponent>() : nullptr;

    m_denoisePipeline->bind(commandBuffer->getCommandBufferVk());
    m_rc->descriptorManager->bindSet(0, commandBuffer, m_denoisePipeline);
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_denoisePipeline);
    m_denoisedSets[context.frameInFlight]->bind(commandBuffer->getCommandBufferVk(), m_denoisePipeline);

    DenoisePushConstants pushConstants{};
    pushConstants.cameraSSBOIndex = renderData.getCameras().getDescriptorIndex(context.frameInFlight);
    pushConstants.cameraSlotIndex =
        (cameraComp != nullptr && cameraComp->renderDataSlot != UINT32_MAX) ? cameraComp->renderDataSlot : 0;
    pushConstants.occlusionTextureIndex = getOcclusionTexture(context.frameInFlight)->getBindlessIndex();
    pushConstants.previousDenoisedTextureIndex = m_denoisedTextures[previousFrame]->getBindlessIndex();
    pushConstants.depthTextureIndex = context.targets->depthStencil->getBindlessIndex();
    pushConstants.normalTextureIndex = context.targets->gbufferNormalMotion->getBindlessIndex();
    pushConstants.linearDepthTextureIndex = context.targets->hiZ->getBindlessIndex();
    pushConstants.historyLinearDepthTextureIndex = context.targets->historyLinearDepth->getBindlessIndex();
    pushConstants.hasHistory = m_hasHistory ? 1u : 0u;
    pushConstants.depthRejection = m_depthRejection;
    pushConstants.hysteresis = m_hysteresis;
    pushConstants.outputSize = glm::ivec2(m_width, m_height);

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_denoisePipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DenoisePushConstants), &pushConstants);

    vkCmdDispatch(commandBuffer->getCommandBufferVk(), groupCount(m_width, GTAO_LOCAL_SIZE),
                  groupCount(m_height, GTAO_LOCAL_SIZE), 1);
}

void GroundTruthAmbientOcclusionPass::record(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_FUNCTION();

    if (!m_occlusionPipeline || !m_denoisePipeline) {
        RP_CORE_ERROR("Pipelines are not initialized!");
        return;
    }

    if (context.targets->depthStencil == nullptr || context.targets->gbufferNormalMotion == nullptr ||
        context.targets->hiZ == nullptr || context.targets->historyLinearDepth == nullptr ||
        getOcclusionTexture(context.frameInFlight) == nullptr || getDenoisedTexture(context.frameInFlight) == nullptr) {
        return;
    }

    recordOcclusion(context, commandBuffer);

    // The filter reads the horizon search at its own pixel and across its neighbourhood
    VkImageMemoryBarrier barrier = getOcclusionTexture(context.frameInFlight)
                                       ->getImageMemoryBarrier(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                                                               VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    recordDenoise(context, commandBuffer);

    // Only true once a full accumulation buffer exists to reproject into
    m_hasHistory = true;
}

} // namespace Rapture
