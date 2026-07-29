#include "StochasticScreenSpaceReflectionsPass.h"

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

#include <algorithm>
#include <glm/glm.hpp>

namespace Rapture {

struct TracePushConstants {
    alignas(4) uint32_t cameraSSBOIndex;
    alignas(4) uint32_t cameraSlotIndex;
    alignas(4) uint32_t depthTextureIndex;
    alignas(4) uint32_t normalTextureIndex;
    alignas(4) uint32_t materialTextureIndex;
    alignas(4) uint32_t linearDepthTextureIndex;
    alignas(4) uint32_t frameIndex;
    alignas(4) float maxDistance;
    alignas(4) float thickness;
    alignas(4) int32_t stepCount;
    alignas(4) int32_t hiZMaxLevel;
    alignas(8) glm::ivec2 outputSize;
    alignas(8) glm::ivec2 fullResSize;
};

struct ResolvePushConstants {
    alignas(4) uint32_t cameraSSBOIndex;
    alignas(4) uint32_t cameraSlotIndex;
    alignas(4) uint32_t hitTextureIndex;
    alignas(4) uint32_t historyColorTextureIndex;
    alignas(4) uint32_t depthTextureIndex;
    alignas(4) uint32_t normalTextureIndex;
    alignas(4) uint32_t materialTextureIndex;
    alignas(4) uint32_t frameIndex;
    alignas(4) float historyMipCount;
    alignas(8) glm::ivec2 outputSize;
    alignas(8) glm::ivec2 hitSize;
};

struct TemporalPushConstants {
    alignas(4) uint32_t cameraSSBOIndex;
    alignas(4) uint32_t cameraSlotIndex;
    alignas(4) uint32_t resolvedTextureIndex;
    alignas(4) uint32_t previousAccumulatedTextureIndex;
    alignas(4) uint32_t depthTextureIndex;
    alignas(4) uint32_t normalTextureIndex;
    alignas(4) uint32_t hasHistory;
    alignas(4) float hysteresis;
    alignas(8) glm::ivec2 outputSize;
};

struct DownsamplePushConstants {
    alignas(4) uint32_t sourceTextureIndex;
    alignas(4) int32_t sourceMip;
    alignas(8) glm::ivec2 sourceSize;
    alignas(8) glm::ivec2 outputSize;
};

static constexpr uint32_t SSSR_LOCAL_SIZE = 8;

static glm::ivec2 s_mipSize(uint32_t width, uint32_t height, uint32_t mip)
{
    return glm::ivec2(std::max(1u, width >> mip), std::max(1u, height >> mip));
}

// Makes a just-written mip visible to the dispatch that reduces it into the next one
static VkImageMemoryBarrier s_mipReadBarrier(Texture *texture, uint32_t mip)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->getImage();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = mip;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    return barrier;
}

StochasticScreenSpaceReflectionsPass::StochasticScreenSpaceReflectionsPass(uint32_t width, uint32_t height,
                                                                          uint32_t framesInFlight,
                                                                          std::vector<Texture *> sceneColorTextures)
    : m_sceneColorTextures(std::move(sceneColorTextures)), m_width(width), m_height(height), m_framesInFlight(framesInFlight)
{
    m_rc = &Application::getInstance().getVulkanContext().getRenderContext();

    m_halfWidth = std::max(1u, (m_width + 1) / 2);
    m_halfHeight = std::max(1u, (m_height + 1) / 2);

    if (!m_sceneColorTextures.empty() && m_sceneColorTextures[0] != nullptr) {
        m_sceneColorMipLevels = m_sceneColorTextures[0]->getSpecification().mipLevels;
    }

    loadShaders();
    createTextures();
    createDescriptorSets();
}

StochasticScreenSpaceReflectionsPass::~StochasticScreenSpaceReflectionsPass()
{
    m_hitSets.clear();
    m_resolvedSets.clear();
    m_accumulatedSets.clear();
    m_sceneColorMipSets.clear();
    m_hitTextures.clear();
    m_resolvedTextures.clear();
    m_accumulatedTextures.clear();
    m_tracePipeline.reset();
    m_resolvePipeline.reset();
    m_downsamplePipeline.reset();
    m_temporalPipeline.reset();
}

void StochasticScreenSpaceReflectionsPass::loadShaders()
{
    auto &project = Application::getInstance().getProject();
    auto shaderPath = project.getProjectShaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl";

    auto traceAsset = AssetManager::importAsset(shaderPath / "glsl/StochasticScreenSpaceReflectionsTrace.cs.glsl", shaderConfig);
    m_traceShader = traceAsset ? traceAsset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_traceShader != nullptr) {
        m_shaderAssets.push_back(std::move(traceAsset));
    }

    auto resolveAsset =
        AssetManager::importAsset(shaderPath / "glsl/StochasticScreenSpaceReflectionsResolve.cs.glsl", shaderConfig);
    m_resolveShader = resolveAsset ? resolveAsset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_resolveShader != nullptr) {
        m_shaderAssets.push_back(std::move(resolveAsset));
    }

    // No reduction macro, so the chain averages rather than taking a minimum the way Hi-Z does
    auto downsampleAsset = AssetManager::importAsset(shaderPath / "glsl/DownsampleMip.cs.glsl", shaderConfig);
    m_downsampleShader = downsampleAsset ? downsampleAsset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_downsampleShader != nullptr) {
        m_shaderAssets.push_back(std::move(downsampleAsset));
    }

    auto temporalAsset =
        AssetManager::importAsset(shaderPath / "glsl/StochasticScreenSpaceReflectionsTemporal.cs.glsl", shaderConfig);
    m_temporalShader = temporalAsset ? temporalAsset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_temporalShader != nullptr) {
        m_shaderAssets.push_back(std::move(temporalAsset));
    }

    RP_ASSERT(m_traceShader != nullptr && m_resolveShader != nullptr && m_downsampleShader != nullptr &&
                  m_temporalShader != nullptr,
              "Reflection shaders failed to load");
    if (m_traceShader == nullptr || m_resolveShader == nullptr || m_downsampleShader == nullptr ||
        m_temporalShader == nullptr) {
        RP_CORE_ERROR("Reflection shaders failed to load");
        return;
    }

    ComputePipelineConfiguration tracePipelineConfig;
    tracePipelineConfig.shader = m_traceShader;
    m_tracePipeline = std::make_shared<ComputePipeline>(tracePipelineConfig);

    ComputePipelineConfiguration resolvePipelineConfig;
    resolvePipelineConfig.shader = m_resolveShader;
    m_resolvePipeline = std::make_shared<ComputePipeline>(resolvePipelineConfig);

    ComputePipelineConfiguration downsamplePipelineConfig;
    downsamplePipelineConfig.shader = m_downsampleShader;
    m_downsamplePipeline = std::make_shared<ComputePipeline>(downsamplePipelineConfig);

    ComputePipelineConfiguration temporalPipelineConfig;
    temporalPipelineConfig.shader = m_temporalShader;
    m_temporalPipeline = std::make_shared<ComputePipeline>(temporalPipelineConfig);
}

void StochasticScreenSpaceReflectionsPass::createTextures()
{
    TextureSpecification hitSpec;
    hitSpec.width = m_halfWidth;
    hitSpec.height = m_halfHeight;
    // The sampling density spans several orders of magnitude, reaching 5e5 at the roughness floor,
    // and half floats top out at 65504. A hit uv also needs finer steps than a half float has left
    // near 1.0, where its spacing grows to about a pixel.
    hitSpec.format = TextureFormat::RGBA32F;
    hitSpec.type = TextureType::TEXTURE2D;
    hitSpec.filter = TextureFilter::Nearest;
    hitSpec.wrap = TextureWrap::ClampToEdge;
    hitSpec.srgb = false;
    hitSpec.storageImage = true;

    TextureSpecification resolvedSpec = hitSpec;
    resolvedSpec.width = m_width;
    resolvedSpec.height = m_height;
    resolvedSpec.format = TextureFormat::RGBA16F;
    resolvedSpec.filter = TextureFilter::Linear;

    m_hitTextures.reserve(m_framesInFlight);
    m_resolvedTextures.reserve(m_framesInFlight);
    m_accumulatedTextures.reserve(m_framesInFlight);
    for (uint32_t frame = 0; frame < m_framesInFlight; ++frame) {
        m_hitTextures.push_back(std::make_unique<Texture>(hitSpec));
        m_hitTextures.back()->getBindlessIndex();

        m_resolvedTextures.push_back(std::make_unique<Texture>(resolvedSpec));
        m_resolvedTextures.back()->getBindlessIndex();

        m_accumulatedTextures.push_back(std::make_unique<Texture>(resolvedSpec));
        m_accumulatedTextures.back()->getBindlessIndex();
    }
}

void StochasticScreenSpaceReflectionsPass::createDescriptorSets()
{
    DescriptorSetBindings bindings;
    bindings.setNumber = 4;

    DescriptorSetBinding outputBinding = {};
    outputBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputBinding.location = DescriptorSetBindingLocation::CUSTOM_0;
    outputBinding.useStorageImageInfo = true;
    bindings.bindings.push_back(outputBinding);

    m_hitSets.reserve(m_framesInFlight);
    m_resolvedSets.reserve(m_framesInFlight);
    for (uint32_t frame = 0; frame < m_framesInFlight; ++frame) {
        auto hitSet = std::make_unique<DescriptorSet>(bindings);
        hitSet->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*m_hitTextures[frame]);
        m_hitSets.push_back(std::move(hitSet));

        auto resolvedSet = std::make_unique<DescriptorSet>(bindings);
        resolvedSet->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*m_resolvedTextures[frame]);
        m_resolvedSets.push_back(std::move(resolvedSet));

        auto accumulatedSet = std::make_unique<DescriptorSet>(bindings);
        accumulatedSet->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*m_accumulatedTextures[frame]);
        m_accumulatedSets.push_back(std::move(accumulatedSet));
    }

    m_sceneColorMipSets.resize(m_sceneColorTextures.size());
    for (size_t target = 0; target < m_sceneColorTextures.size(); ++target) {
        if (m_sceneColorTextures[target] == nullptr) {
            continue;
        }

        m_sceneColorMipSets[target].reserve(m_sceneColorMipLevels);
        for (uint32_t mip = 0; mip < m_sceneColorMipLevels; ++mip) {
            auto set = std::make_unique<DescriptorSet>(bindings);
            set->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->addStorageMip(*m_sceneColorTextures[target], mip);
            m_sceneColorMipSets[target].push_back(std::move(set));
        }
    }
}


Texture *StochasticScreenSpaceReflectionsPass::getHitTexture(uint32_t frameInFlight) const
{
    if (frameInFlight >= m_hitTextures.size()) {
        RP_CORE_ERROR("Requested hit texture for frame {} but only {} exist", frameInFlight, m_hitTextures.size());
        return nullptr;
    }
    return m_hitTextures[frameInFlight].get();
}

Texture *StochasticScreenSpaceReflectionsPass::getResolvedTexture(uint32_t frameInFlight) const
{
    if (frameInFlight >= m_resolvedTextures.size()) {
        RP_CORE_ERROR("Requested resolved texture for frame {} but only {} exist", frameInFlight, m_resolvedTextures.size());
        return nullptr;
    }
    return m_resolvedTextures[frameInFlight].get();
}

Texture *StochasticScreenSpaceReflectionsPass::getAccumulatedTexture(uint32_t frameInFlight) const
{
    if (frameInFlight >= m_accumulatedTextures.size()) {
        RP_CORE_ERROR("Requested accumulated texture for frame {} but only {} exist", frameInFlight,
                      m_accumulatedTextures.size());
        return nullptr;
    }
    return m_accumulatedTextures[frameInFlight].get();
}

void StochasticScreenSpaceReflectionsPass::recordTemporal(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    const uint32_t count = static_cast<uint32_t>(m_accumulatedTextures.size());
    const uint32_t previousFrame = (context.frameInFlight + count - 1) % count;

    auto &renderData = *(context.scene->getRenderData());
    auto *cameraComp = context.camera.isValid() ? context.camera.tryGetComponent<CameraComponent>() : nullptr;

    m_temporalPipeline->bind(commandBuffer->getCommandBufferVk());
    m_rc->descriptorManager->bindSet(0, commandBuffer, m_temporalPipeline);
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_temporalPipeline);
    m_accumulatedSets[context.frameInFlight]->bind(commandBuffer->getCommandBufferVk(), m_temporalPipeline);

    TemporalPushConstants pushConstants{};
    pushConstants.cameraSSBOIndex = renderData.getCameras().getDescriptorIndex(context.frameInFlight);
    pushConstants.cameraSlotIndex =
        (cameraComp != nullptr && cameraComp->renderDataSlot != UINT32_MAX) ? cameraComp->renderDataSlot : 0;
    pushConstants.resolvedTextureIndex = getResolvedTexture(context.frameInFlight)->getBindlessIndex();
    pushConstants.previousAccumulatedTextureIndex = m_accumulatedTextures[previousFrame]->getBindlessIndex();
    pushConstants.depthTextureIndex = context.targets->depthStencil->getBindlessIndex();
    pushConstants.normalTextureIndex = context.targets->gbufferNormalMotion->getBindlessIndex();
    pushConstants.hasHistory = m_hasAccumulatedHistory ? 1u : 0u;
    pushConstants.hysteresis = m_hysteresis;
    pushConstants.outputSize = glm::ivec2(m_width, m_height);

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_temporalPipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TemporalPushConstants), &pushConstants);

    vkCmdDispatch(commandBuffer->getCommandBufferVk(), groupCount(m_width, SSSR_LOCAL_SIZE),
                  groupCount(m_height, SSSR_LOCAL_SIZE), 1);
}

void StochasticScreenSpaceReflectionsPass::onResize(uint32_t width, uint32_t height)
{
    // TODO: the renderer currently destroys and rebuilds every pass on resize, so only the size is
    // updated here. Recreate the textures and descriptor sets in place once passes outlive a resize.
    m_width = width;
    m_height = height;
    m_halfWidth = std::max(1u, (m_width + 1) / 2);
    m_halfHeight = std::max(1u, (m_height + 1) / 2);
}

void StochasticScreenSpaceReflectionsPass::updateResources(const RenderPassContext &context)
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

    ComputeResource material;
    material.texture = context.targets->gbufferMaterial;
    material.access = ComputeResourceAccess::READ;
    m_resources.push_back(material);

    ComputeResource hiZ;
    hiZ.texture = context.targets->hiZ;
    hiZ.access = ComputeResourceAccess::READ;
    m_resources.push_back(hiZ);

    ComputeResource history;
    history.texture = context.targets->historyColor;
    history.access = ComputeResourceAccess::READ;
    m_resources.push_back(history);

    // Written by the trace and read back by the resolve, so it stays in GENERAL across both
    ComputeResource hit;
    hit.texture = getHitTexture(context.frameInFlight);
    hit.access = ComputeResourceAccess::READ_WRITE;
    hit.discardContents = true;
    hit.readableAfter = true;
    m_resources.push_back(hit);

    // Written by the resolve and read straight back by the temporal accumulation
    ComputeResource resolved;
    resolved.texture = getResolvedTexture(context.frameInFlight);
    resolved.access = ComputeResourceAccess::READ_WRITE;
    resolved.discardContents = true;
    resolved.readableAfter = true;
    m_resources.push_back(resolved);

    const uint32_t count = static_cast<uint32_t>(m_accumulatedTextures.size());
    ComputeResource previousAccumulated;
    previousAccumulated.texture = getAccumulatedTexture((context.frameInFlight + count - 1) % count);
    previousAccumulated.access = ComputeResourceAccess::READ;
    m_resources.push_back(previousAccumulated);

    ComputeResource accumulated;
    accumulated.texture = getAccumulatedTexture(context.frameInFlight);
    accumulated.access = ComputeResourceAccess::WRITE;
    accumulated.discardContents = true;
    accumulated.readableAfter = true;
    m_resources.push_back(accumulated);
}

void StochasticScreenSpaceReflectionsPass::recordTrace(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    auto &renderData = *(context.scene->getRenderData());
    auto *cameraComp = context.camera.isValid() ? context.camera.tryGetComponent<CameraComponent>() : nullptr;

    m_tracePipeline->bind(commandBuffer->getCommandBufferVk());
    m_rc->descriptorManager->bindSet(0, commandBuffer, m_tracePipeline);
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_tracePipeline);
    m_hitSets[context.frameInFlight]->bind(commandBuffer->getCommandBufferVk(), m_tracePipeline);

    TracePushConstants pushConstants{};
    pushConstants.cameraSSBOIndex = renderData.getCameras().getDescriptorIndex(context.frameInFlight);
    pushConstants.cameraSlotIndex =
        (cameraComp != nullptr && cameraComp->renderDataSlot != UINT32_MAX) ? cameraComp->renderDataSlot : 0;
    pushConstants.depthTextureIndex = context.targets->depthStencil->getBindlessIndex();
    pushConstants.normalTextureIndex = context.targets->gbufferNormalMotion->getBindlessIndex();
    pushConstants.materialTextureIndex = context.targets->gbufferMaterial->getBindlessIndex();
    pushConstants.linearDepthTextureIndex = context.targets->hiZ->getBindlessIndex();
    pushConstants.frameIndex = static_cast<uint32_t>(Application::getInstance().getFrameCount());
    pushConstants.maxDistance = m_maxDistance;
    pushConstants.thickness = m_thickness;
    pushConstants.stepCount = m_stepCount;
    pushConstants.hiZMaxLevel = static_cast<int32_t>(context.targets->hiZ->getSpecification().mipLevels) - 1;
    pushConstants.outputSize = glm::ivec2(m_halfWidth, m_halfHeight);
    pushConstants.fullResSize = glm::ivec2(m_width, m_height);

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_tracePipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(TracePushConstants), &pushConstants);

    vkCmdDispatch(commandBuffer->getCommandBufferVk(), groupCount(m_halfWidth, SSSR_LOCAL_SIZE),
                  groupCount(m_halfHeight, SSSR_LOCAL_SIZE), 1);
}

void StochasticScreenSpaceReflectionsPass::recordSceneColorMipChain(const RenderPassContext &context,
                                                                   CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_FUNCTION();

    Texture *sceneColor = context.targets->sceneColorHdr;
    const uint32_t target = context.frameInFlight;
    if (!m_downsamplePipeline || sceneColor == nullptr || target >= m_sceneColorMipSets.size() ||
        m_sceneColorMipSets[target].empty()) {
        return;
    }

    const TextureSpecification &spec = sceneColor->getSpecification();

    VkImageMemoryBarrier toGeneral = sceneColor->getImageMemoryBarrier(VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
                                                                       VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toGeneral);

    m_downsamplePipeline->bind(commandBuffer->getCommandBufferVk());
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_downsamplePipeline);

    for (uint32_t mip = 1; mip < m_sceneColorMipLevels; ++mip) {
        VkImageMemoryBarrier barrier = s_mipReadBarrier(sceneColor, mip - 1);
        vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        m_sceneColorMipSets[target][mip]->bind(commandBuffer->getCommandBufferVk(), m_downsamplePipeline);

        DownsamplePushConstants pushConstants{};
        pushConstants.sourceTextureIndex = sceneColor->getBindlessIndex();
        pushConstants.sourceMip = static_cast<int32_t>(mip - 1);
        pushConstants.sourceSize = s_mipSize(spec.width, spec.height, mip - 1);
        pushConstants.outputSize = s_mipSize(spec.width, spec.height, mip);

        vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_downsamplePipeline->getPipelineLayoutVk(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DownsamplePushConstants), &pushConstants);

        vkCmdDispatch(commandBuffer->getCommandBufferVk(),
                      groupCount(static_cast<uint32_t>(pushConstants.outputSize.x), SSSR_LOCAL_SIZE),
                      groupCount(static_cast<uint32_t>(pushConstants.outputSize.y), SSSR_LOCAL_SIZE), 1);
    }

    // Left sampleable, which is both what the next frame's resolve needs and what the bindless
    // descriptor for this texture declares
    VkImageMemoryBarrier toReadable = sceneColor->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &toReadable);
}

void StochasticScreenSpaceReflectionsPass::recordResolve(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    m_resolvePipeline->bind(commandBuffer->getCommandBufferVk());
    m_rc->descriptorManager->bindSet(0, commandBuffer, m_resolvePipeline);
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_resolvePipeline);
    m_resolvedSets[context.frameInFlight]->bind(commandBuffer->getCommandBufferVk(), m_resolvePipeline);

    auto &renderData = *(context.scene->getRenderData());
    auto *cameraComp = context.camera.isValid() ? context.camera.tryGetComponent<CameraComponent>() : nullptr;

    ResolvePushConstants pushConstants{};
    pushConstants.cameraSSBOIndex = renderData.getCameras().getDescriptorIndex(context.frameInFlight);
    pushConstants.cameraSlotIndex =
        (cameraComp != nullptr && cameraComp->renderDataSlot != UINT32_MAX) ? cameraComp->renderDataSlot : 0;
    pushConstants.hitTextureIndex = getHitTexture(context.frameInFlight)->getBindlessIndex();
    pushConstants.historyColorTextureIndex = context.targets->historyColor->getBindlessIndex();
    pushConstants.depthTextureIndex = context.targets->depthStencil->getBindlessIndex();
    pushConstants.normalTextureIndex = context.targets->gbufferNormalMotion->getBindlessIndex();
    pushConstants.materialTextureIndex = context.targets->gbufferMaterial->getBindlessIndex();
    pushConstants.frameIndex = static_cast<uint32_t>(Application::getInstance().getFrameCount());
    pushConstants.historyMipCount = static_cast<float>(m_sceneColorMipLevels);
    pushConstants.outputSize = glm::ivec2(m_width, m_height);
    pushConstants.hitSize = glm::ivec2(m_halfWidth, m_halfHeight);

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_resolvePipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(ResolvePushConstants), &pushConstants);

    vkCmdDispatch(commandBuffer->getCommandBufferVk(), groupCount(m_width, SSSR_LOCAL_SIZE),
                  groupCount(m_height, SSSR_LOCAL_SIZE), 1);
}

void StochasticScreenSpaceReflectionsPass::record(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_FUNCTION();

    if (!m_tracePipeline || !m_resolvePipeline) {
        RP_CORE_ERROR("Pipelines are not initialized!");
        return;
    }

    if (context.targets->depthStencil == nullptr || context.targets->gbufferNormalMotion == nullptr ||
        context.targets->gbufferMaterial == nullptr || context.targets->hiZ == nullptr ||
        context.targets->historyColor == nullptr || getHitTexture(context.frameInFlight) == nullptr ||
        getResolvedTexture(context.frameInFlight) == nullptr || getAccumulatedTexture(context.frameInFlight) == nullptr) {
        return;
    }

    recordTrace(context, commandBuffer);

    // The resolve reads hit records the trace just wrote, including its neighbours'
    VkImageMemoryBarrier barrier = getHitTexture(context.frameInFlight)
                                       ->getImageMemoryBarrier(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                                                               VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    recordResolve(context, commandBuffer);

    // The accumulation reads the resolve at its own pixel and across its neighbourhood
    VkImageMemoryBarrier resolvedBarrier = s_mipReadBarrier(getResolvedTexture(context.frameInFlight), 0);
    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &resolvedBarrier);

    recordTemporal(context, commandBuffer);

    // Only true once a full accumulation buffer exists to reproject into
    m_hasAccumulatedHistory = true;
}

} // namespace Rapture
