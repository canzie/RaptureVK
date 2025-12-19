#include "DynamicDiffuseGI.h"

#include "AssetManager/Asset.h"
#include "AssetManager/AssetManager.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorBinding.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Components/Components.h"
#include "Components/IndirectLightingComponent.h"
#include "Events/AssetEvents.h"
#include "Materials/MaterialParameters.h"
#include "Renderer/GI/DDGI/DDGICommon.h"
#include "Renderer/RtInstanceData.h"
#include "Scenes/Entities/Entity.h"
#include "Scenes/Scene.h"
#include "Textures/Texture.h"
#include "Textures/TextureCommon.h"
#include "WindowContext/Application.h"

#include "Logging/TracyProfiler.h"

#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <random>

namespace Rapture {

// Push constants for DDGI compute shaders
struct DDGITracePushConstants {

    uint32_t skyboxTextureIndex;
    uint32_t sunLightDataIndex;
    uint32_t lightCount;
    uint32_t prevRadianceIndex;
    uint32_t prevVisibilityIndex;
    uint32_t tlasIndex;
    uint32_t probeOffsetHandle;
    // alignas(16) glm::vec3 cameraPosition;
};

struct DDGIBlendPushConstants {
    uint32_t prevTextureIndex;
    uint32_t rayDataIndex;
};

struct DDGIClassifyPushConstants {
    uint32_t rayDataIndex;
    uint32_t probeOffsetHandle;
};

struct DDGIRelocatePushConstants {
    uint32_t rayDataIndex;
};

std::shared_ptr<Texture> DynamicDiffuseGI::s_defaultSkyboxTexture = nullptr;

DynamicDiffuseGI::DynamicDiffuseGI(uint32_t framesInFlight)
    : m_DDGI_ProbeTraceShader(nullptr), m_DDGI_ProbeIrradianceBlendingShader(nullptr), m_DDGI_ProbeDistanceBlendingShader(nullptr),
      m_DDGI_ProbeRelocationShader(nullptr), m_DDGI_ProbeClassificationShader(nullptr), m_DDGI_ProbeTracePipeline(nullptr),
      m_DDGI_ProbeIrradianceBlendingPipeline(nullptr), m_DDGI_ProbeDistanceBlendingPipeline(nullptr),
      m_DDGI_ProbeRelocationPipeline(nullptr), m_DDGI_ProbeClassificationPipeline(nullptr), m_ProbeInfoBuffer(nullptr),
      m_framesInFlight(framesInFlight), m_isFirstFrame(true), m_meshCount(0), m_probeIrradianceBindlessIndex(UINT32_MAX),
      m_probeVisibilityBindlessIndex(UINT32_MAX), m_probeOffsetBindlessIndex(UINT32_MAX), m_skyboxTexture(nullptr),
      m_probeTraceDescriptorSet(nullptr), m_probeIrradianceBlendingDescriptorSet(nullptr),
      m_probeDistanceBlendingDescriptorSet(nullptr), m_probeClassificationDescriptorSet(nullptr),
      m_probeRelocationDescriptorSet(nullptr)
{

    if (!s_defaultSkyboxTexture) {
        s_defaultSkyboxTexture = Texture::createDefaultWhiteCubemapTexture();
    }
    m_skyboxTexture = s_defaultSkyboxTexture;

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    m_allocator = vc.getVmaAllocator();
    m_computeQueue = vc.getComputeQueue();

    createPipelines();

    CommandPoolConfig poolConfig;
    poolConfig.name = "DDGI Command Pool";
    poolConfig.queueFamilyIndex = vc.getComputeQueueIndex();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    auto pool = CommandPoolManager::createCommandPool(poolConfig);

    m_CommandBuffers = pool->getCommandBuffers(m_framesInFlight, "DDGI");

    initProbeInfoBuffer();
    initTextures();
    setupProbeTextures();
}

DynamicDiffuseGI::~DynamicDiffuseGI() {}

void DynamicDiffuseGI::createPipelines()
{

    auto &app = Application::getInstance();
    auto &proj = app.getProject();
    auto shaderDir = proj.getProjectShaderDirectory();

    ShaderImportConfig shaderIrradianceBlendConfig;
    shaderIrradianceBlendConfig.compileInfo.macros.push_back("DDGI_BLEND_RADIANCE");
    shaderIrradianceBlendConfig.compileInfo.includePath = shaderDir / "glsl/ddgi/";
    ShaderImportConfig shaderDistanceBlendConfig;
    shaderDistanceBlendConfig.compileInfo.macros.push_back("DDGI_BLEND_DISTANCE");
    shaderDistanceBlendConfig.compileInfo.includePath = shaderDir / "glsl/ddgi/";

    ShaderImportConfig shaderBaseProbeConfig;
    shaderBaseProbeConfig.compileInfo.includePath = shaderDir / "glsl/ddgi/";

    auto [probeTraceShader, probeTraceShaderHandle] =
        AssetManager::importAsset<Shader>(shaderDir / "glsl/ddgi/ProbeTrace.cs.glsl", shaderBaseProbeConfig);
    auto [probeIrradianceBlendShader, probeIrradianceBlendShaderHandle] =
        AssetManager::importAsset<Shader>(shaderDir / "glsl/ddgi/ProbeBlending.cs.glsl", shaderIrradianceBlendConfig);
    auto [probeDistanceBlendShader, probeDistanceBlendShaderHandle] =
        AssetManager::importAsset<Shader>(shaderDir / "glsl/ddgi/ProbeBlending.cs.glsl", shaderDistanceBlendConfig);
    auto [probeRelocationShader, probeRelocationShaderHandle] =
        AssetManager::importAsset<Shader>(shaderDir / "glsl/ddgi/ProbeRelocation.cs.glsl", shaderBaseProbeConfig);
    auto [probeClassificationShader, probeClassificationShaderHandle] =
        AssetManager::importAsset<Shader>(shaderDir / "glsl/ddgi/ProbeClassification.cs.glsl", shaderBaseProbeConfig);

    m_DDGI_ProbeTraceShader = probeTraceShader;
    m_DDGI_ProbeIrradianceBlendingShader = probeIrradianceBlendShader;
    m_DDGI_ProbeDistanceBlendingShader = probeDistanceBlendShader;
    m_DDGI_ProbeRelocationShader = probeRelocationShader;
    m_DDGI_ProbeClassificationShader = probeClassificationShader;

    ComputePipelineConfiguration probeTraceConfig;
    probeTraceConfig.shader = m_DDGI_ProbeTraceShader;
    ComputePipelineConfiguration probeIrradianceBlendingConfig;
    probeIrradianceBlendingConfig.shader = m_DDGI_ProbeIrradianceBlendingShader;
    ComputePipelineConfiguration probeDistanceBlendingConfig;
    probeDistanceBlendingConfig.shader = m_DDGI_ProbeDistanceBlendingShader;
    ComputePipelineConfiguration probeRelocationConfig;
    probeRelocationConfig.shader = m_DDGI_ProbeRelocationShader;
    ComputePipelineConfiguration probeClassificationConfig;
    probeClassificationConfig.shader = m_DDGI_ProbeClassificationShader;

    m_DDGI_ProbeTracePipeline = std::make_shared<ComputePipeline>(probeTraceConfig);
    m_DDGI_ProbeIrradianceBlendingPipeline = std::make_shared<ComputePipeline>(probeIrradianceBlendingConfig);
    m_DDGI_ProbeDistanceBlendingPipeline = std::make_shared<ComputePipeline>(probeDistanceBlendingConfig);
    m_DDGI_ProbeRelocationPipeline = std::make_shared<ComputePipeline>(probeRelocationConfig);
    m_DDGI_ProbeClassificationPipeline = std::make_shared<ComputePipeline>(probeClassificationConfig);
}

void DynamicDiffuseGI::setupProbeTextures()
{
    RP_CORE_TRACE("Setting up probe textures for bindless access");

    // Get bindless indices for probe textures (these will be used in lighting pass)
    if (m_RadianceTexture) {
        m_probeIrradianceBindlessIndex = m_RadianceTexture->getBindlessIndex();
    }
    if (m_VisibilityTexture) {
        m_probeVisibilityBindlessIndex = m_VisibilityTexture->getBindlessIndex();
    }
    if (m_ProbeOffsetTexture) {
        m_probeOffsetBindlessIndex = m_ProbeOffsetTexture->getBindlessIndex();
    }
}

uint32_t DynamicDiffuseGI::getSunLightDataIndex(std::shared_ptr<Scene> scene)
{

    auto &reg = scene->getRegistry();
    auto lightView = reg.view<LightComponent>();

    for (auto ent : lightView) {
        auto &lightComp = lightView.get<LightComponent>(ent);
        if (lightComp.type == LightType::Directional) {
            return lightComp.lightDataBuffer->getDescriptorIndex();
        }
    }

    return 0;
}

void DynamicDiffuseGI::clearTextures()
{

    if (m_CommandBuffers[0]->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to begin command buffer");
        return;
    }

    VkImageSubresourceRange subresourceRange;
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = m_ProbeVolume.gridDimensions.y;

    // Transition images from UNDEFINED to GENERAL layout before clearing
    std::vector<VkImageMemoryBarrier> layoutTransitions;

    VkImageMemoryBarrier radianceTransition = m_RadianceTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    layoutTransitions.push_back(radianceTransition);

    VkImageMemoryBarrier visibilityTransition = m_VisibilityTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    layoutTransitions.push_back(visibilityTransition);

    vkCmdPipelineBarrier(m_CommandBuffers[0]->getCommandBufferVk(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(layoutTransitions.size()),
                         layoutTransitions.data());

    VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 0.0f}};
    vkCmdClearColorImage(m_CommandBuffers[0]->getCommandBufferVk(), m_RadianceTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL,
                         &clearColor, 1, &subresourceRange);

    vkCmdClearColorImage(m_CommandBuffers[0]->getCommandBufferVk(), m_VisibilityTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL,
                         &clearColor, 1, &subresourceRange);

    if (m_CommandBuffers[0]->end() != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to end command buffer");
        return;
    }

    m_computeQueue->submitQueue(m_CommandBuffers[0]);
}

void DynamicDiffuseGI::populateProbesCompute(std::shared_ptr<Scene> scene, uint32_t frameIndex)
{
    RAPTURE_PROFILE_FUNCTION();

    {
        // Compute a completely new random rotation each frame using James Arvo's method
        // from Graphics Gems 3 (pg 117-120). This prevents flickering by generating
        // a fresh uniform random rotation rather than accumulating incremental rotations.
        static std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        const float RTXGI_2PI = 2.0f * glm::pi<float>();

        // Setup a random rotation matrix using 3 uniform random variables
        float u1 = RTXGI_2PI * dist(rng);
        float cos1 = std::cos(u1);
        float sin1 = std::sin(u1);

        float u2 = RTXGI_2PI * dist(rng);
        float cos2 = std::cos(u2);
        float sin2 = std::sin(u2);

        float u3 = dist(rng);
        float sq3 = 2.0f * std::sqrt(u3 * (1.0f - u3));

        float s2 = 2.0f * u3 * sin2 * sin2 - 1.0f;
        float c2 = 2.0f * u3 * cos2 * cos2 - 1.0f;
        float sc = 2.0f * u3 * sin2 * cos2;

        // Create the random rotation matrix (GLM is column-major)
        glm::mat3 transform;
        transform[0][0] = cos1 * c2 - sin1 * sc;
        transform[1][0] = sin1 * c2 + cos1 * sc;
        transform[2][0] = sq3 * cos2;

        transform[0][1] = cos1 * sc - sin1 * s2;
        transform[1][1] = sin1 * sc + cos1 * s2;
        transform[2][1] = sq3 * sin2;

        transform[0][2] = cos1 * (sq3 * cos2) - sin1 * (sq3 * sin2);
        transform[1][2] = sin1 * (sq3 * cos2) + cos1 * (sq3 * sin2);
        transform[2][2] = 1.0f - 2.0f * u3;

        glm::quat rotationQuat = glm::quat_cast(transform);
        m_ProbeVolume.probeRayRotation = glm::vec4(rotationQuat.x, rotationQuat.y, rotationQuat.z, rotationQuat.w);
        m_isVolumeDirty = true;
    }

    if (m_isVolumeDirty) {
        // Update the probe volume
        updateProbeVolume();
    }
    updateSkybox(scene);

    auto tlas = scene->getTLAS();
    if (!tlas || !tlas->isBuilt() || tlas->getInstanceCount() == 0) {
        RP_CORE_WARN("Scene TLAS is not built");
        return;
    }

    auto currentCommandBuffer = m_CommandBuffers[frameIndex];

    if (currentCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to begin command buffer");
        return;
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(currentCommandBuffer->getCommandBufferVk(), "DynamicDiffuseGI::populateProbesCompute");
        castRays(scene, frameIndex);
    }

    if (m_RayDataTextureFlattened) {
        m_RayDataTextureFlattened->update(currentCommandBuffer);
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(currentCommandBuffer->getCommandBufferVk(), "DynamicDiffuseGI::relocateProbes");
        relocateProbes(frameIndex);
    }

    if (m_ProbeOffsetTextureFlattened) {
        m_ProbeOffsetTextureFlattened->update(currentCommandBuffer);
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(currentCommandBuffer->getCommandBufferVk(), "DynamicDiffuseGI::classifyProbes");
        classifyProbes(frameIndex);
    }

    if (m_ProbeClassificationTextureFlattened) {
        m_ProbeClassificationTextureFlattened->update(currentCommandBuffer);
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(currentCommandBuffer->getCommandBufferVk(), "DynamicDiffuseGI::blendTextures");
        blendTextures(frameIndex);
    }

    if (m_IrradianceTextureFlattened) {
        m_IrradianceTextureFlattened->update(currentCommandBuffer);
    }
    if (m_DistanceTextureFlattened) {
        m_DistanceTextureFlattened->update(currentCommandBuffer);
    }

    RAPTURE_PROFILE_GPU_COLLECT(currentCommandBuffer->getCommandBufferVk());

    if (currentCommandBuffer->end() != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to end command buffer");
        return;
    }

    m_computeQueue->submitQueue(currentCommandBuffer);

    m_isFirstFrame = false;
}

void DynamicDiffuseGI::classifyProbes(uint32_t frameIndex)
{
    RAPTURE_PROFILE_FUNCTION();

    auto currentCommandBuffer = m_CommandBuffers[frameIndex];

    // Barrier to ensure ray data is ready for reading and classification texture is ready for writing
    std::vector<VkImageMemoryBarrier> preClassifyBarriers;

    VkImageMemoryBarrier classificationWriteBarrier = m_ProbeClassificationTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
    preClassifyBarriers.push_back(classificationWriteBarrier);

    vkCmdPipelineBarrier(currentCommandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(preClassifyBarriers.size()), preClassifyBarriers.data());

    // Bind pipeline and descriptor sets
    m_DDGI_ProbeClassificationPipeline->bind(currentCommandBuffer->getCommandBufferVk());
    DescriptorManager::bindSet(0, currentCommandBuffer, m_DDGI_ProbeClassificationPipeline);
    DescriptorManager::bindSet(3, currentCommandBuffer, m_DDGI_ProbeClassificationPipeline);
    m_probeClassificationDescriptorSet->bind(currentCommandBuffer->getCommandBufferVk(), m_DDGI_ProbeClassificationPipeline);

    // Push constants
    DDGIClassifyPushConstants pushConstants = {};
    pushConstants.rayDataIndex = m_RayDataTexture->getBindlessIndex();
    pushConstants.probeOffsetHandle = m_probeOffsetBindlessIndex;
    vkCmdPushConstants(currentCommandBuffer->getCommandBufferVk(), m_DDGI_ProbeClassificationPipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DDGIClassifyPushConstants), &pushConstants);

    // Dispatch
    uint32_t totalProbes = m_ProbeVolume.gridDimensions.x * m_ProbeVolume.gridDimensions.y * m_ProbeVolume.gridDimensions.z;
    uint32_t workGroupsX = (totalProbes + 31) / 32; // Match shader local_size_x
    vkCmdDispatch(currentCommandBuffer->getCommandBufferVk(), workGroupsX, 1, 1);

    // Barrier to ensure classification texture is ready for reading by next stages
    VkImageMemoryBarrier classificationReadBarrier = m_ProbeClassificationTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    vkCmdPipelineBarrier(currentCommandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &classificationReadBarrier);
}

void DynamicDiffuseGI::relocateProbes(uint32_t frameIndex)
{
    RAPTURE_PROFILE_FUNCTION();
    auto currentCommandBuffer = m_CommandBuffers[frameIndex];

    // Barrier to ensure offset texture is ready for writing
    std::vector<VkImageMemoryBarrier> preRelocateBarriers;
    VkImageMemoryBarrier offsetWriteBarrier = m_ProbeOffsetTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
    preRelocateBarriers.push_back(offsetWriteBarrier);

    vkCmdPipelineBarrier(currentCommandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(preRelocateBarriers.size()), preRelocateBarriers.data());

    // Bind pipeline and descriptor sets
    m_DDGI_ProbeRelocationPipeline->bind(currentCommandBuffer->getCommandBufferVk());
    DescriptorManager::bindSet(0, currentCommandBuffer, m_DDGI_ProbeRelocationPipeline);
    DescriptorManager::bindSet(3, currentCommandBuffer, m_DDGI_ProbeRelocationPipeline);
    m_probeRelocationDescriptorSet->bind(currentCommandBuffer->getCommandBufferVk(), m_DDGI_ProbeRelocationPipeline);

    // Push constants
    DDGIRelocatePushConstants pushConstants = {};
    pushConstants.rayDataIndex = m_RayDataTexture->getBindlessIndex();
    vkCmdPushConstants(currentCommandBuffer->getCommandBufferVk(), m_DDGI_ProbeRelocationPipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DDGIRelocatePushConstants), &pushConstants);

    // Dispatch
    uint32_t totalProbes = m_ProbeVolume.gridDimensions.x * m_ProbeVolume.gridDimensions.y * m_ProbeVolume.gridDimensions.z;
    uint32_t workGroupsX = (totalProbes + 31) / 32; // Match shader local_size_x
    vkCmdDispatch(currentCommandBuffer->getCommandBufferVk(), workGroupsX, 1, 1);

    // Barrier to ensure offset texture is ready for reading
    VkImageMemoryBarrier offsetReadBarrier = m_ProbeOffsetTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    vkCmdPipelineBarrier(currentCommandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &offsetReadBarrier);
}

void DynamicDiffuseGI::updateFromIndirectLightingComponent(std::shared_ptr<Scene> scene)
{
    // Query for IndirectLightingComponent
    auto view = scene->getRegistry().view<IndirectLightingComponent>();
    if (view.empty()) {
        return; // No indirect lighting component - use current settings
    }

    auto &ilComp = view.get<IndirectLightingComponent>(*view.begin());

    // Only update if DDGI technique is selected and enabled
    if (!ilComp.isDDGI() || ilComp.isDisabled()) {
        return;
    }

    auto *ddgiSettings = ilComp.getDDGISettings();
    if (!ddgiSettings) {
        return;
    }

    if (m_ProbeVolume.gridDimensions != ddgiSettings->probeCount) {
        m_ProbeVolume.gridDimensions = ddgiSettings->probeCount;
        m_isVolumeDirty = true;
    }

    if (m_ProbeVolume.spacing != ddgiSettings->probeSpacing) {
        m_ProbeVolume.spacing = ddgiSettings->probeSpacing;
        m_isVolumeDirty = true;
    }

    if (m_ProbeVolume.origin != ddgiSettings->gridOrigin) {
        m_ProbeVolume.origin = ddgiSettings->gridOrigin;
        m_isVolumeDirty = true;
    }

    if (m_ProbeVolume.probeNumRays != static_cast<int>(ddgiSettings->raysPerProbe)) {
        m_ProbeVolume.probeNumRays = static_cast<int>(ddgiSettings->raysPerProbe);
        m_isVolumeDirty = true;
    }

    // Note: intensity and visualizeProbes can be used by renderer, not stored in ProbeVolume
}

void DynamicDiffuseGI::updateSkybox(std::shared_ptr<Scene> scene)
{
    // Query for SkyboxComponent from registry
    SkyboxComponent *skyboxComp = nullptr;
    auto view = scene->getRegistry().view<SkyboxComponent>();
    if (!view.empty()) {
        skyboxComp = &view.get<SkyboxComponent>(*view.begin());
    }

    std::shared_ptr<Texture> newTexture =
        (skyboxComp && skyboxComp->skyboxTexture && skyboxComp->skyboxTexture->isReadyForSampling()) ? skyboxComp->skyboxTexture
                                                                                                     : s_defaultSkyboxTexture;

    RAPTURE_PROFILE_FUNCTION();

    if (m_skyboxTexture != newTexture) {
        m_skyboxTexture = newTexture;
        // Skybox texture doesn't need special handling in the new system
        // since it's accessed via bindless
    }
}

void DynamicDiffuseGI::castRays(std::shared_ptr<Scene> scene, uint32_t frameIndex)
{

    RAPTURE_PROFILE_FUNCTION();

    auto currentCommandBuffer = m_CommandBuffers[frameIndex];

    // Get TLAS from scene
    auto tlas = scene->getTLAS();
    if (!tlas || !tlas->isBuilt()) {
        // RP_CORE_WARN("DynamicDiffuseGI::castRays - Scene TLAS is not built");
        return;
    }

    // NOTE: Ray rotation/jitter has been disabled in the shader because any per-frame
    // variation causes severe flickering. The probeRayRotation quaternion is no longer
    // updated here. Investigation needed to understand why hysteresis isn't stabilizing
    // the results when rotation is enabled.

    // === BARRIER PHASE 1: Prepare for trace shader (3 dependencies) ===
    std::vector<VkImageMemoryBarrier> preTraceBarriers;

    VkImageMemoryBarrier rayDataWriteBarrier = m_RayDataTexture->getImageMemoryBarrier(
        m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

    preTraceBarriers.push_back(rayDataWriteBarrier);

    // Transition probe textures for reading (they contain data from previous frame)
    VkImageMemoryBarrier radianceReadBarrier = m_RadianceTexture->getImageMemoryBarrier(
        m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);

    preTraceBarriers.push_back(radianceReadBarrier);

    VkImageMemoryBarrier visibilityReadBarrier = m_VisibilityTexture->getImageMemoryBarrier(
        m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);

    preTraceBarriers.push_back(visibilityReadBarrier);

    VkImageMemoryBarrier probeClassificationReadBarrier = m_ProbeClassificationTexture->getImageMemoryBarrier(
        m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);

    preTraceBarriers.push_back(probeClassificationReadBarrier);

    vkCmdPipelineBarrier(currentCommandBuffer->getCommandBufferVk(),
                         m_isFirstFrame ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(preTraceBarriers.size()), preTraceBarriers.data());

    // Bind the compute pipeline
    m_DDGI_ProbeTracePipeline->bind(currentCommandBuffer->getCommandBufferVk());

    // Use the new descriptor manager system
    // Set 0: Common resources (camera, lights, shadows, probe volume)
    DescriptorManager::bindSet(0, currentCommandBuffer, m_DDGI_ProbeTracePipeline);

    // Set 1: Material resources (not used in DDGI)
    // Set 2: Object/Mesh resources (mesh data SSBO)
    // DescriptorManager::bindSet(2, currentCommandBuffer, m_DDGI_ProbeTracePipeline);

    // Set 3: Bindless arrays
    DescriptorManager::bindSet(3, currentCommandBuffer, m_DDGI_ProbeTracePipeline);

    // Set 4: DDGI specific storage images
    m_probeTraceDescriptorSet->bind(currentCommandBuffer->getCommandBufferVk(), m_DDGI_ProbeTracePipeline);

    auto &reg = scene->getRegistry();
    auto lightView = reg.view<LightComponent>();

    // Set push constants with texture and buffer indices
    DDGITracePushConstants pushConstants = {};
    pushConstants.lightCount = static_cast<uint32_t>(lightView.size());
    pushConstants.sunLightDataIndex = getSunLightDataIndex(scene);
    pushConstants.skyboxTextureIndex = m_skyboxTexture ? m_skyboxTexture->getBindlessIndex() : 0;
    pushConstants.tlasIndex = tlas->getBindlessIndex();
    pushConstants.prevRadianceIndex = m_probeIrradianceBindlessIndex;
    pushConstants.prevVisibilityIndex = m_probeVisibilityBindlessIndex;
    pushConstants.probeOffsetHandle = m_probeOffsetBindlessIndex;

    vkCmdPushConstants(currentCommandBuffer->getCommandBufferVk(), m_DDGI_ProbeTracePipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DDGITracePushConstants), &pushConstants);

    // Dispatch the compute shader
    // Workgroup size is 16x16x1 based on shader, dispatch based on probe grid dimensions
    uint32_t workGroupsX = m_ProbeVolume.gridDimensions.x;
    uint32_t workGroupsY = m_ProbeVolume.gridDimensions.z;
    uint32_t workGroupsZ = m_ProbeVolume.gridDimensions.y;

    vkCmdDispatch(currentCommandBuffer->getCommandBufferVk(), workGroupsX, workGroupsY, workGroupsZ);

    // === BARRIER PHASE 2: After trace shader - transition ray data for reading ===
    VkImageMemoryBarrier rayDataReadBarrier = m_RayDataTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    vkCmdPipelineBarrier(currentCommandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &rayDataReadBarrier);
}

void DynamicDiffuseGI::blendTextures(uint32_t frameIndex)
{

    RAPTURE_PROFILE_FUNCTION();

    auto currentCommandBuffer = m_CommandBuffers[frameIndex];

    // === BARRIER PHASE 5: Prepare for blending shaders ===
    // Transition probe textures to GENERAL layout for read-modify-write operations
    // The blend shader will read from the same texture it writes to (for hysteresis blending)
    std::vector<VkImageMemoryBarrier> preBlendingBarriers;

    VkImageMemoryBarrier radianceReadWriteBarrier =
        m_RadianceTexture->getImageMemoryBarrier(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                                                 VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    preBlendingBarriers.push_back(radianceReadWriteBarrier);

    VkImageMemoryBarrier visibilityReadWriteBarrier = m_VisibilityTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    preBlendingBarriers.push_back(visibilityReadWriteBarrier);

    vkCmdPipelineBarrier(currentCommandBuffer->getCommandBufferVk(),
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // Wait for flatten to finish
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // Prepare for blending operations
                         0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(preBlendingBarriers.size()), preBlendingBarriers.data());

    // Irradiance blending shader
    m_DDGI_ProbeIrradianceBlendingPipeline->bind(currentCommandBuffer->getCommandBufferVk());

    // Use the new descriptor manager system for blending shaders
    DescriptorManager::bindSet(0, currentCommandBuffer, m_DDGI_ProbeIrradianceBlendingPipeline); // probe volume
    DescriptorManager::bindSet(3, currentCommandBuffer, m_DDGI_ProbeIrradianceBlendingPipeline); // bindless
    m_probeIrradianceBlendingDescriptorSet->bind(currentCommandBuffer->getCommandBufferVk(),
                                                 m_DDGI_ProbeIrradianceBlendingPipeline);

    // Set push constants for radiance blending
    DDGIBlendPushConstants radianceBlendConstants = {};
    radianceBlendConstants.prevTextureIndex = m_probeIrradianceBindlessIndex;
    radianceBlendConstants.rayDataIndex = m_RayDataTexture->getBindlessIndex();

    vkCmdPushConstants(currentCommandBuffer->getCommandBufferVk(), m_DDGI_ProbeIrradianceBlendingPipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DDGIBlendPushConstants), &radianceBlendConstants);

    vkCmdDispatch(currentCommandBuffer->getCommandBufferVk(), m_ProbeVolume.gridDimensions.x, m_ProbeVolume.gridDimensions.z,
                  m_ProbeVolume.gridDimensions.y);

    // Distance blending shader
    m_DDGI_ProbeDistanceBlendingPipeline->bind(currentCommandBuffer->getCommandBufferVk());

    DescriptorManager::bindSet(0, currentCommandBuffer, m_DDGI_ProbeDistanceBlendingPipeline); // probe volume
    DescriptorManager::bindSet(3, currentCommandBuffer, m_DDGI_ProbeDistanceBlendingPipeline); // bindless
    m_probeDistanceBlendingDescriptorSet->bind(currentCommandBuffer->getCommandBufferVk(), m_DDGI_ProbeDistanceBlendingPipeline);

    // Set push constants for visibility blending
    DDGIBlendPushConstants visibilityBlendConstants = {};
    visibilityBlendConstants.prevTextureIndex = m_probeVisibilityBindlessIndex;
    visibilityBlendConstants.rayDataIndex = m_RayDataTexture->getBindlessIndex();

    vkCmdPushConstants(currentCommandBuffer->getCommandBufferVk(), m_DDGI_ProbeDistanceBlendingPipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DDGIBlendPushConstants), &visibilityBlendConstants);

    vkCmdDispatch(currentCommandBuffer->getCommandBufferVk(), m_ProbeVolume.gridDimensions.x, m_ProbeVolume.gridDimensions.z,
                  m_ProbeVolume.gridDimensions.y);

    // === BARRIER PHASE 6: After blending shaders - transition to shader read ===
    // Transition probe textures back to shader read mode for next frame and final lighting
    std::vector<VkImageMemoryBarrier> postBlendingBarriers;

    VkImageMemoryBarrier radianceReadBarrier = m_RadianceTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    postBlendingBarriers.push_back(radianceReadBarrier);

    VkImageMemoryBarrier visibilityReadBarrier = m_VisibilityTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    postBlendingBarriers.push_back(visibilityReadBarrier);

    vkCmdPipelineBarrier(
        currentCommandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,                                         // After blending operations
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // For next frame and final lighting
        0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(postBlendingBarriers.size()), postBlendingBarriers.data());
}

void DynamicDiffuseGI::initTextures()
{

    TextureSpecification irradianceSpec;
    irradianceSpec.width = m_ProbeVolume.gridDimensions.x * m_ProbeVolume.probeNumIrradianceTexels;
    irradianceSpec.height = m_ProbeVolume.gridDimensions.z * m_ProbeVolume.probeNumIrradianceTexels;
    irradianceSpec.depth = m_ProbeVolume.gridDimensions.y;
    irradianceSpec.type = TextureType::TEXTURE2D_ARRAY;
    irradianceSpec.format = TextureFormat::RGBA16F; // Changed from R11G11B10F to eliminate blue channel precision loss
    irradianceSpec.filter = TextureFilter::Linear;
    irradianceSpec.storageImage = true;
    irradianceSpec.wrap = TextureWrap::ClampToEdge;
    irradianceSpec.srgb = false;

    TextureSpecification distanceSpec;
    distanceSpec.width = m_ProbeVolume.gridDimensions.x * m_ProbeVolume.probeNumDistanceTexels;
    distanceSpec.height = m_ProbeVolume.gridDimensions.z * m_ProbeVolume.probeNumDistanceTexels;
    distanceSpec.depth = m_ProbeVolume.gridDimensions.y;
    distanceSpec.type = TextureType::TEXTURE2D_ARRAY;
    distanceSpec.format = TextureFormat::RG16F;
    distanceSpec.filter = TextureFilter::Linear;
    distanceSpec.storageImage = true;
    distanceSpec.srgb = false;
    distanceSpec.wrap = TextureWrap::ClampToEdge;

    TextureSpecification rayDataSpec;
    rayDataSpec.width = m_ProbeVolume.probeNumRays;
    rayDataSpec.height = m_ProbeVolume.gridDimensions.x * m_ProbeVolume.gridDimensions.z;
    rayDataSpec.depth = m_ProbeVolume.gridDimensions.y;
    rayDataSpec.type = TextureType::TEXTURE2D_ARRAY;
    rayDataSpec.format = TextureFormat::RGBA32F;
    rayDataSpec.filter = TextureFilter::Nearest;
    rayDataSpec.storageImage = true;
    rayDataSpec.srgb = false;
    rayDataSpec.wrap = TextureWrap::ClampToEdge;

    TextureSpecification probeClassificationSpec = rayDataSpec;
    probeClassificationSpec.width = m_ProbeVolume.gridDimensions.x;
    probeClassificationSpec.height = m_ProbeVolume.gridDimensions.z;
    probeClassificationSpec.depth = m_ProbeVolume.gridDimensions.y;
    probeClassificationSpec.format = TextureFormat::R8UI;

    TextureSpecification probeOffsetSpec = probeClassificationSpec;
    probeOffsetSpec.format = TextureFormat::RGBA32F;

    // Create the textures
    m_RayDataTexture = std::make_shared<Texture>(rayDataSpec);

    m_RadianceTexture = std::make_shared<Texture>(irradianceSpec);
    m_VisibilityTexture = std::make_shared<Texture>(distanceSpec);

    m_ProbeClassificationTexture = std::make_shared<Texture>(probeClassificationSpec);
    m_ProbeOffsetTexture = std::make_shared<Texture>(probeOffsetSpec);

    // Create flattened textures using TextureFlattener
    m_RayDataTextureFlattened = TextureFlattener::createFlattenTexture(m_RayDataTexture, "[DDGI] Flattened Ray Data");
    m_IrradianceTextureFlattened = TextureFlattener::createFlattenTexture(m_RadianceTexture, "[DDGI] Irradiance Flattened");
    m_DistanceTextureFlattened = TextureFlattener::createFlattenTexture(m_VisibilityTexture, "[DDGI] Distance Flattened");
    m_ProbeClassificationTextureFlattened = TextureFlattener::createFlattenTexture(
        m_ProbeClassificationTexture, "[DDGI] Probe Classification Flattened", FlattenerDataType::UINT);
    m_ProbeOffsetTextureFlattened = TextureFlattener::createFlattenTexture(m_ProbeOffsetTexture, "[DDGI] Probe Offset Flattened");

    clearTextures();

    // --- Create custom descriptor sets for each compute shader ---

    // For Probe Irradiance Blending
    {
        DescriptorSetBindings bindings;
        bindings.setNumber = 4;
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, true,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_IRRADIANCE_ATLAS});
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, TextureViewType::DEFAULT, false,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_CLASSIFICATION});
        m_probeIrradianceBlendingDescriptorSet = std::make_shared<DescriptorSet>(bindings);
        m_probeIrradianceBlendingDescriptorSet
            ->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_IRRADIANCE_ATLAS)
            ->add(m_RadianceTexture);
        m_probeIrradianceBlendingDescriptorSet
            ->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_CLASSIFICATION)
            ->add(m_ProbeClassificationTexture);
    }

    // For Probe Distance Blending
    {
        DescriptorSetBindings bindings;
        bindings.setNumber = 4;
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, true,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_DISTANCE_ATLAS});
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, TextureViewType::DEFAULT, false,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_CLASSIFICATION});
        m_probeDistanceBlendingDescriptorSet = std::make_shared<DescriptorSet>(bindings);
        m_probeDistanceBlendingDescriptorSet
            ->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_DISTANCE_ATLAS)
            ->add(m_VisibilityTexture);
        m_probeDistanceBlendingDescriptorSet
            ->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_CLASSIFICATION)
            ->add(m_ProbeClassificationTexture);
    }

    // For Probe Tracing
    {
        DescriptorSetBindings bindings;
        bindings.setNumber = 4;
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, true,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::RAY_DATA});
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, TextureViewType::DEFAULT, false,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_CLASSIFICATION});
        m_probeTraceDescriptorSet = std::make_shared<DescriptorSet>(bindings);
        m_probeTraceDescriptorSet->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::RAY_DATA)
            ->add(m_RayDataTexture);
        m_probeTraceDescriptorSet
            ->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_CLASSIFICATION)
            ->add(m_ProbeClassificationTexture);
    }

    // For Probe Classification
    {
        DescriptorSetBindings bindings;
        bindings.setNumber = 4;
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, true,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_CLASSIFICATION});
        m_probeClassificationDescriptorSet = std::make_shared<DescriptorSet>(bindings);
        m_probeClassificationDescriptorSet
            ->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_CLASSIFICATION)
            ->add(m_ProbeClassificationTexture);
    }

    // For Probe Relocation
    {
        DescriptorSetBindings bindings;
        bindings.setNumber = 4;
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, true,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_RELOCATION});
        m_probeRelocationDescriptorSet = std::make_shared<DescriptorSet>(bindings);
        m_probeRelocationDescriptorSet
            ->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_RELOCATION)
            ->add(m_ProbeOffsetTexture);
    }

    RP_CORE_INFO("DDGI: Created custom descriptor sets for compute pipelines.");
}

void DynamicDiffuseGI::updateProbeVolume()
{

    RAPTURE_PROFILE_FUNCTION();

    if (!m_ProbeInfoBuffer) {
        RP_CORE_ERROR("Probe info buffer not initialized");
        return;
    }

    if (!m_isVolumeDirty) {
        return;
    }

    m_ProbeInfoBuffer->addDataGPU(&m_ProbeVolume, sizeof(ProbeVolume), 0);
    m_isVolumeDirty = false;
}

void DynamicDiffuseGI::initProbeInfoBuffer()
{

    ProbeVolume probeVolume;

    probeVolume.probeRayRotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    probeVolume.origin = glm::vec3(-0.4f, 5.4f, -0.25f);

    probeVolume.rotation = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

    probeVolume.spacing = glm::vec3(1.02f, 0.5f, 0.45f);
    probeVolume.gridDimensions = glm::uvec3(22, 22, 22);

    probeVolume.probeNumRays = 256;
    probeVolume.probeStaticRayCount = 32;
    probeVolume.probeNumIrradianceTexels = 8;
    probeVolume.probeNumDistanceTexels = 16;
    probeVolume.probeNumIrradianceInteriorTexels = probeVolume.probeNumIrradianceTexels - 2;
    probeVolume.probeNumDistanceInteriorTexels = probeVolume.probeNumDistanceTexels - 2;

    probeVolume.probeHysteresis = 0.97f;
    probeVolume.probeMaxRayDistance = 10000.0f;
    // Self-shadow bias scale (B). The view-bias term is no longer used with the new unified formula.
    probeVolume.probeNormalBias = 0.1f; // B parameter from the paper (works well for most scenes)
    probeVolume.probeViewBias = 0.3f;   // Unused
    probeVolume.probeDistanceExponent = 50.0f;
    probeVolume.probeIrradianceEncodingGamma = 5.0f;

    probeVolume.probeBrightnessThreshold = 1.0f;

    probeVolume.probeMinFrontfaceDistance = 0.1f;

    probeVolume.probeRandomRayBackfaceThreshold = 0.1f;
    probeVolume.probeFixedRayBackfaceThreshold = 0.25f;

    probeVolume.probeRelocationEnabled = 1.0f;
    probeVolume.probeClassificationEnabled = 1.0f;
    probeVolume.probeChangeThreshold = 0.2f;
    probeVolume.probeMinValidSamples = 16.0f;

    m_ProbeVolume = probeVolume;

    m_ProbeInfoBuffer = std::make_shared<UniformBuffer>(sizeof(ProbeVolume), BufferUsage::STATIC, m_allocator);
    m_ProbeInfoBuffer->addDataGPU(&probeVolume, sizeof(ProbeVolume), 0);

    // Add probe volume UBO to the descriptor manager immediately
    auto probeInfoSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::DDGI_PROBE_INFO);
    if (probeInfoSet) {
        auto binding = probeInfoSet->getUniformBufferBinding(DescriptorSetBindingLocation::DDGI_PROBE_INFO);
        if (binding) {
            binding->add(m_ProbeInfoBuffer);
            RP_CORE_INFO("Added probe volume UBO to descriptor set 0, binding 5");
        } else {
            RP_CORE_ERROR("Failed to get uniform buffer binding for probe info");
        }
    } else {
        RP_CORE_ERROR("Failed to get descriptor set for probe info");
    }
}

void DynamicDiffuseGI::onResize(uint32_t framesInFlight)
{
    if (m_framesInFlight == framesInFlight) {
        return; // No change needed if frame count is the same
    }

    m_framesInFlight = framesInFlight;

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    CommandPoolConfig poolConfig;
    poolConfig.name = "DDGI Command Pool";
    poolConfig.queueFamilyIndex = vc.getComputeQueueIndex();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    auto pool = CommandPoolManager::createCommandPool(poolConfig);
    m_CommandBuffers = pool->getCommandBuffers(m_framesInFlight, "DDGI");

    RP_CORE_INFO("DDGI system resized for {} frames in flight.", m_framesInFlight);
}

} // namespace Rapture
