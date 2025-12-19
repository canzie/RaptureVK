#include "radiance_cascades.h"

#include "AssetManager/AssetManager.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorBinding.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Components/Components.h"
#include "Logging/Log.h"
#include "Textures/TextureCommon.h"
#include "WindowContext/Application.h"

#include "Logging/TracyProfiler.h"

namespace Rapture {

enum class RCDescriptorSetBindingLocation : uint32_t {
    RADIANCE_TEXTURE = 400,
    VOLUME_UBO = 401,
};

struct RCTracePushConstants {
    uint32_t tlasIndex;
    uint32_t skyboxTextureIndex;
    uint32_t cascadeIndex;
    uint32_t lightCount;
};

RadianceCascades::RadianceCascades(uint32_t framesInFlight)
    : m_isVolumeDirty(true), m_framesInFlight(framesInFlight), m_isFirstFrame(true)
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    m_allocator = vc.getVmaAllocator();
    m_computeQueue = vc.getComputeQueue();

    m_config.origin = glm::vec3(0.0f, 5.0f, 0.0f);
    m_config.baseProbeSpacing = glm::vec3(0.5f);
    m_config.baseGridSize = glm::uvec3(64);
    m_config.numCascades = 4;
    m_config.baseRange = 1.0f;
    m_config.baseAngularResolution = 4;

    createPipelines();

    CommandPoolConfig poolConfig;
    poolConfig.name = "RadianceCascades Command Pool";
    poolConfig.queueFamilyIndex = vc.getComputeQueueIndex();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    auto pool = CommandPoolManager::createCommandPool(poolConfig);
    m_commandBuffers = pool->getCommandBuffers(m_framesInFlight, "RadianceCascades");

    initVolumeUBO();
    initTextures();
}

RadianceCascades::~RadianceCascades() {}

void RadianceCascades::createPipelines()
{
    auto &app = Application::getInstance();
    auto &proj = app.getProject();
    auto shaderDir = proj.getProjectShaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderDir / "glsl/radiance_cascades/";

    auto [traceShader, traceShaderHandle] =
        AssetManager::importAsset<Shader>(shaderDir / "glsl/radiance_cascades/Trace.cs.glsl", shaderConfig);

    m_traceShader = traceShader;

    ComputePipelineConfiguration traceConfig;
    traceConfig.shader = m_traceShader;

    m_tracePipeline = std::make_shared<ComputePipeline>(traceConfig);
}

void RadianceCascades::initVolumeUBO()
{
    m_volumeGPU = buildRCVolumeGPU(m_config);
    PrintRcVolumeGPU(m_volumeGPU);

    m_volumeUBO = std::make_shared<UniformBuffer>(sizeof(RCVolumeGPU), BufferUsage::STATIC, m_allocator);
    m_volumeUBO->addDataGPU(&m_volumeGPU, sizeof(RCVolumeGPU), 0);

    m_isVolumeDirty = false;
}

void RadianceCascades::updateVolumeUBO()
{
    if (!m_isVolumeDirty) return;

    m_volumeGPU = buildRCVolumeGPU(m_config);
    m_volumeUBO->addDataGPU(&m_volumeGPU, sizeof(RCVolumeGPU), 0);
    m_isVolumeDirty = false;
}

void RadianceCascades::initTextures()
{
    m_radianceTextures.clear();
    m_radianceTexturesFlattened.clear();
    m_traceDescriptorSet = nullptr;

    DescriptorSetBindings bindings;
    bindings.setNumber = 4;
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RC_MAX_CASCADES, TextureViewType::DEFAULT, true,
                                 static_cast<DescriptorSetBindingLocation>(RCDescriptorSetBindingLocation::RADIANCE_TEXTURE)});
    bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, TextureViewType::DEFAULT, false,
                                 static_cast<DescriptorSetBindingLocation>(RCDescriptorSetBindingLocation::VOLUME_UBO)});

    m_traceDescriptorSet = std::make_shared<DescriptorSet>(bindings);
    m_traceDescriptorSet
        ->getUniformBufferBinding(static_cast<DescriptorSetBindingLocation>(RCDescriptorSetBindingLocation::VOLUME_UBO))
        ->add(m_volumeUBO);

    for (uint32_t i = 0; i < m_config.numCascades; ++i) {
        const auto &cascade = m_volumeGPU.cascades[i];

        TextureSpecification radianceSpec;
        radianceSpec.width = cascade.gridSize.x * cascade.angularResolution;
        radianceSpec.height = cascade.gridSize.z * cascade.angularResolution;
        radianceSpec.depth = cascade.gridSize.y * cascade.angularResolution;
        radianceSpec.type = TextureType::TEXTURE2D_ARRAY;
        radianceSpec.format = TextureFormat::RGBA32F;
        radianceSpec.filter = TextureFilter::Nearest;
        radianceSpec.storageImage = true;
        radianceSpec.wrap = TextureWrap::ClampToEdge;
        radianceSpec.srgb = false;

        auto radianceTexture = std::make_shared<Texture>(radianceSpec);
        m_radianceTextures.push_back(radianceTexture);

        m_traceDescriptorSet
            ->getTextureBinding(static_cast<DescriptorSetBindingLocation>(RCDescriptorSetBindingLocation::RADIANCE_TEXTURE))
            ->add(radianceTexture);

        auto flattened = TextureFlattener::createFlattenTexture(radianceTexture, fmt::format("[RC] C{} Radiance Flattened", i));
        m_radianceTexturesFlattened.push_back(flattened);

        RP_CORE_INFO("RadianceCascades: Created C{} radiance texture {}x{}x{}", i, radianceSpec.width, radianceSpec.height,
                     radianceSpec.depth);
    }
}

void RadianceCascades::setConfig(const RadianceCascadeConfig &config)
{
    m_config = config;
    m_isVolumeDirty = true;
}

void RadianceCascades::update(std::shared_ptr<Scene> scene, uint32_t frameIndex)
{
    RAPTURE_PROFILE_FUNCTION();

    auto tlas = scene->getTLAS();
    if (!tlas || !tlas->isBuilt() || tlas->getInstanceCount() == 0) {
        return;
    }

    if (m_isVolumeDirty) {
        updateVolumeUBO();
    }

    auto currentCommandBuffer = m_commandBuffers[frameIndex];

    if (currentCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != VK_SUCCESS) {
        RP_CORE_ERROR("RadianceCascades: Failed to begin command buffer");
        return;
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(currentCommandBuffer->getCommandBufferVk(), "RadianceCascades::traceCascades");
        traceCascades(scene, frameIndex);
    }

    for (const auto &flattenedTexture : m_radianceTexturesFlattened) {
        if (flattenedTexture) {
            flattenedTexture->update(currentCommandBuffer);
        }
    }

    RAPTURE_PROFILE_GPU_COLLECT(currentCommandBuffer->getCommandBufferVk());

    if (currentCommandBuffer->end() != VK_SUCCESS) {
        RP_CORE_ERROR("RadianceCascades: Failed to end command buffer");
        return;
    }

    m_computeQueue->submitQueue(currentCommandBuffer);
    m_isFirstFrame = false;
}

void RadianceCascades::traceCascades(std::shared_ptr<Scene> scene, uint32_t frameIndex)
{
    RAPTURE_PROFILE_FUNCTION();

    auto currentCommandBuffer = m_commandBuffers[frameIndex];
    auto tlas = scene->getTLAS();

    std::vector<VkImageMemoryBarrier> writeBarriers;

    for (const auto &texture : m_radianceTextures) {
        writeBarriers.push_back(texture->getImageMemoryBarrier(
            m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT));
    }

    if (!writeBarriers.empty()) {
        vkCmdPipelineBarrier(currentCommandBuffer->getCommandBufferVk(),
                             m_isFirstFrame ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                             static_cast<uint32_t>(writeBarriers.size()), writeBarriers.data());
    }

    m_tracePipeline->bind(currentCommandBuffer->getCommandBufferVk());

    DescriptorManager::bindSet(0, currentCommandBuffer, m_tracePipeline);
    DescriptorManager::bindSet(3, currentCommandBuffer, m_tracePipeline);
    m_traceDescriptorSet->bind(currentCommandBuffer->getCommandBufferVk(), m_tracePipeline);

    auto &reg = scene->getRegistry();
    auto skyboxView = reg.view<SkyboxComponent>();
    auto lightView = reg.view<LightComponent>();

    uint32_t skyboxTextureIndex = 0;

    if (!skyboxView.empty()) {
        auto &skyboxComp = skyboxView.get<SkyboxComponent>(*skyboxView.begin());
        if (skyboxComp.skyboxTexture && skyboxComp.skyboxTexture->isReadyForSampling()) {
            skyboxTextureIndex = skyboxComp.skyboxTexture->getBindlessIndex();
        }
    }

    for (uint32_t i = 0; i < m_config.numCascades; ++i) {
        RCTracePushConstants pushConstants = {};
        pushConstants.tlasIndex = tlas->getBindlessIndex();
        pushConstants.cascadeIndex = i;
        pushConstants.skyboxTextureIndex = skyboxTextureIndex;
        pushConstants.lightCount = static_cast<uint32_t>(lightView.size());
        vkCmdPushConstants(currentCommandBuffer->getCommandBufferVk(), m_tracePipeline->getPipelineLayoutVk(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RCTracePushConstants), &pushConstants);

        const auto &cascade = m_volumeGPU.cascades[i];
        uint32_t tilesPerDim = (cascade.angularResolution + 7) / 8;
        vkCmdDispatch(currentCommandBuffer->getCommandBufferVk(), cascade.gridSize.x * tilesPerDim,
                      cascade.gridSize.z * tilesPerDim, cascade.gridSize.y * cascade.angularResolution);
    }

    std::vector<VkImageMemoryBarrier> readBarriers;

    for (const auto &texture : m_radianceTextures) {
        readBarriers.push_back(texture->getImageMemoryBarrier(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                              VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT));
    }

    if (!readBarriers.empty()) {
        vkCmdPipelineBarrier(currentCommandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                             nullptr, static_cast<uint32_t>(readBarriers.size()), readBarriers.data());
    }
}

void RadianceCascades::onResize(uint32_t framesInFlight)
{
    if (m_framesInFlight == framesInFlight) {
        return;
    }

    m_framesInFlight = framesInFlight;

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    CommandPoolConfig poolConfig;
    poolConfig.name = "RadianceCascades Command Pool";
    poolConfig.queueFamilyIndex = vc.getComputeQueueIndex();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    auto pool = CommandPoolManager::createCommandPool(poolConfig);
    m_commandBuffers = pool->getCommandBuffers(m_framesInFlight, "RadianceCascades");

    RP_CORE_INFO("RadianceCascades: Resized for {} frames in flight", m_framesInFlight);
}

} // namespace Rapture
