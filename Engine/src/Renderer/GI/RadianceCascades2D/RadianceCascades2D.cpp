#include "RadianceCascades2D.h"

#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "AssetManager/AssetManager.h"
#include "WindowContext/Application.h"

#include "Components/Components.h"

#include "Buffers/Descriptors/DescriptorManager.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <format>


namespace Rapture {



struct RCMergeCascadePushConstants {
    uint32_t prevCascadeIndex;
    uint32_t currentCascadeIndex;
};

RadianceCascades2D::RadianceCascades2D(uint32_t framesInFlight) {
    // load shaders and compute pipelines
    buildPipelines();
    buildCommandBuffers(framesInFlight);
}

RadianceCascades2D::~RadianceCascades2D() {

    auto cascadeLevelInfoSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::RC_CASCADE_LEVEL_INFO);
    if (!cascadeLevelInfoSet) {
        RP_CORE_ERROR("Failed to get cascade level info set");
        throw std::runtime_error("Failed to get cascade level info set");
    }

    auto cascadeLevelInfoBinding = cascadeLevelInfoSet->getUniformBufferBinding((DescriptorSetBindingLocation::RC_CASCADE_LEVEL_INFO));
    if (!cascadeLevelInfoBinding) {
        RP_CORE_ERROR("Failed to get cascade level info binding");
        throw std::runtime_error("Failed to get cascade level info binding");
    }

    for (size_t i = 0; i < m_cascadeUniformBufferIndices.size(); i++) {
        cascadeLevelInfoBinding->free(m_cascadeUniformBufferIndices[i]);
    }

    
}

void RadianceCascades2D::build(const BuildParams2D &buildParams) {


    m_buildParams = buildParams;

    float prevMinRange = 0.0f;

    for (uint32_t i = 0; i < MAX_CASCADES; i++) {
        RadianceCascadeLevel2D cascade = {};

        cascade.cascadeLevel = i;

        float maxRange = buildParams.baseRange * std::pow(buildParams.rangeExp, static_cast<float>(i));

        cascade.minProbeDistance = prevMinRange;
        cascade.maxProbeDistance = maxRange;


        prevMinRange = maxRange;

        // Grid dimensions: baseGridDimensions / 2^i
        glm::vec2 scaledDimsF = glm::vec2(buildParams.baseGridDimensions) / std::pow(buildParams.gridDimensionsExp, static_cast<float>(i));
        cascade.probeGridDimensions = glm::max(glm::ivec2(glm::round(scaledDimsF)), glm::ivec2(1));

        // Calculate angular resolution: Q_i = baseAngularResolution * 2^i
        cascade.angularResolution = static_cast<uint32_t>(
            buildParams.baseAngularResolution * std::pow(buildParams.angularResolutionExp, static_cast<float>(i))
        );
        cascade.angularResolution = glm::max(cascade.angularResolution, 2u);

        // Calculate probe spacing: ∆p_i = baseSpacing * 2^i
        cascade.probeSpacing = glm::vec2(buildParams.baseSpacing) * std::pow(buildParams.gridDimensionsExp, static_cast<float>(i));

        // this is the center of the grid, the shader should shift the grid by half of the extent
        cascade.probeOrigin = glm::vec2(0.0f);

        cascade.cascadeTextureIndex = 0xFFFFFFFF;


        float overlap = glm::length(glm::vec2(buildParams.baseSpacing) * std::pow(buildParams.gridDimensionsExp, static_cast<float>(i+1)));
        cascade.maxProbeDistance = cascade.maxProbeDistance + overlap;

        m_radianceCascades[i] = cascade;

        
    }

    try {
        buildTextures();
        buildDescriptorSet();
        buildUniformBuffers();
    } catch (const std::runtime_error& e) {
        RP_CORE_ERROR("Failed to build cascade textures: {}", e.what());
        throw;
    }




}

void RadianceCascades2D::buildTextures() {

    TextureSpecification defaultSpec = {};
    defaultSpec.filter = TextureFilter::Linear;
    defaultSpec.srgb = false;
    defaultSpec.storageImage = true;
    defaultSpec.format = TextureFormat::RGBA32F;
    defaultSpec.type = TextureType::TEXTURE2D;

    for (uint32_t i = 0; i < MAX_CASCADES; i++) {

        RadianceCascadeLevel2D& cascade = m_radianceCascades[i];

        if (cascade.cascadeLevel == UINT32_MAX) {
            RP_CORE_ERROR("Cascade not initializes, call build() first");
            throw std::runtime_error("Cascade not initialized, call build() first");
        }
        defaultSpec.width = cascade.probeGridDimensions.x * cascade.angularResolution;
        defaultSpec.height = cascade.probeGridDimensions.y * cascade.angularResolution;

        m_cascadeTextures[i] = std::make_shared<Texture>(defaultSpec);

        uint32_t bindlessIndex = m_cascadeTextures[i]->getBindlessIndex();

        if (bindlessIndex == UINT32_MAX) {
            RP_CORE_ERROR("Failed to get bindless index for cascade({}) texture", cascade.cascadeLevel);
            throw std::runtime_error(std::format("Failed to get bindless index for cascade({}) texture", cascade.cascadeLevel).c_str());
        }

        AssetVariant textureVariant = m_cascadeTextures[i];
        std::shared_ptr<AssetVariant> textureVariantPtr = std::make_shared<AssetVariant>(textureVariant);
        AssetManager::registerVirtualAsset(textureVariantPtr, "[RC] Cascade Texture: " + std::to_string(cascade.cascadeLevel), AssetType::Texture);

        m_cascadeTextures[i]->setReadyForSampling(true);

        // make a copy of cascade 0 for the irradiance step
        if (i == 0) {
            m_irradianceCascadeTexture = std::make_shared<Texture>(defaultSpec);
            AssetVariant irradianceVariant = m_irradianceCascadeTexture;
            std::shared_ptr<AssetVariant> irradianceVariantPtr = std::make_shared<AssetVariant>(irradianceVariant);
            AssetManager::registerVirtualAsset(irradianceVariantPtr, "[RC] Irradiance Cascade Texture: " + std::to_string(cascade.cascadeLevel), AssetType::Texture);
    
            m_irradianceCascadeTexture->setReadyForSampling(true);
            cascade.irradianceTextureIndex = m_irradianceCascadeTexture->getBindlessIndex();
        }
        

        cascade.cascadeTextureIndex = bindlessIndex;

    }



}

void RadianceCascades2D::buildPipelines() {

    auto& app = Application::getInstance();
    auto& proj = app.getProject();
    auto shaderDir = proj.getProjectShaderDirectory();

    ShaderImportConfig shaderBaseProbeConfig; 
    shaderBaseProbeConfig.compileInfo.includePath = shaderDir / "glsl/RadianceCascades2D/";



    auto [probeTraceShader, probeTraceShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/RadianceCascades2D/RCProbeTrace.cs.glsl", shaderBaseProbeConfig);
    auto [mergeCascadeShader, mergeCascadeShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/RadianceCascades2D/RCPCascadeMerge.cs.glsl", shaderBaseProbeConfig);
    auto [integrateIrradianceShader, integrateIrradianceShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/RadianceCascades2D/RCIntegrateIrradiance.cs.glsl", shaderBaseProbeConfig);

    ComputePipelineConfiguration probeTraceConfig;
    probeTraceConfig.shader = probeTraceShader;

    ComputePipelineConfiguration mergeCascadeConfig;
    mergeCascadeConfig.shader = mergeCascadeShader;

    ComputePipelineConfiguration integrateIrradianceConfig;
    integrateIrradianceConfig.shader = integrateIrradianceShader;


    m_probeTracePipeline = std::make_shared<ComputePipeline>(probeTraceConfig);
    m_mergeCascadePipeline = std::make_shared<ComputePipeline>(mergeCascadeConfig);

    m_integrateIrradiancePipeline = std::make_shared<ComputePipeline>(integrateIrradianceConfig);

}

void RadianceCascades2D::buildCommandBuffers(uint32_t framesInFlight) {
    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();

    CommandPoolConfig poolConfig;
    poolConfig.name = "RC Command Pool";
    poolConfig.queueFamilyIndex = vc.getQueueFamilyIndices().computeFamily.value();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    auto pool = CommandPoolManager::createCommandPool(poolConfig);
    m_commandBuffers = pool->getCommandBuffers(framesInFlight);


}

void RadianceCascades2D::buildUniformBuffers() {

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();

    auto cascadeLevelInfoSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::RC_CASCADE_LEVEL_INFO);
    if (!cascadeLevelInfoSet) {
        RP_CORE_ERROR("Failed to get cascade level info set");
        throw std::runtime_error("Failed to get cascade level info set");
    }

    auto cascadeLevelInfoBinding = cascadeLevelInfoSet->getUniformBufferBinding((DescriptorSetBindingLocation::RC_CASCADE_LEVEL_INFO));
    if (!cascadeLevelInfoBinding) {
        RP_CORE_ERROR("Failed to get cascade level info binding");
        throw std::runtime_error("Failed to get cascade level info binding");
    }

    for (uint32_t i = 0; i < MAX_CASCADES; i++) {
        m_cascadeUniformBuffers.push_back(std::make_shared<UniformBuffer>(sizeof(RadianceCascadeLevel2D), BufferUsage::STATIC, vc.getVmaAllocator(), &m_radianceCascades[i]));
        m_cascadeUniformBuffers[i]->addDataGPU(&m_radianceCascades[i], sizeof(RadianceCascadeLevel2D), 0);

        m_cascadeUniformBufferIndices.push_back(cascadeLevelInfoBinding->add(m_cascadeUniformBuffers[i]));
    }
}

void RadianceCascades2D::mergeCascades(std::shared_ptr<CommandBuffer> commandBuffer) {
    RAPTURE_PROFILE_FUNCTION();

    // Transition all cascade textures to general layout for read/write access


    // Bind the merge cascade pipeline
    m_mergeCascadePipeline->bind(commandBuffer->getCommandBufferVk());

    // Bind global descriptor sets (cascade level info and bindless resources)
    DescriptorManager::bindSet(0, commandBuffer, m_mergeCascadePipeline);
    DescriptorManager::bindSet(3, commandBuffer, m_mergeCascadePipeline);

    // Merge cascades from highest to lowest (n-1 merges total)
    // We merge cascade n+1 into cascade n, starting from MAX_CASCADES-1 down to 1
    for (int32_t currentCascade = MAX_CASCADES - 2; currentCascade >= 0; currentCascade--) {
        uint32_t prevCascade = currentCascade + 1;
        
        // Skip if we're beyond valid cascades
        if (m_radianceCascades[currentCascade].cascadeLevel == UINT32_MAX || 
            m_radianceCascades[prevCascade].cascadeLevel == UINT32_MAX) {
            continue;
        }

        VkImageMemoryBarrier cascadeBarrier = m_cascadeTextures[currentCascade]->getImageMemoryBarrier(
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
            VK_IMAGE_LAYOUT_GENERAL, 
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

        vkCmdPipelineBarrier(
                commandBuffer->getCommandBufferVk(),
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &cascadeBarrier
            );
        // Bind the output descriptor set (current cascade texture)
        m_probeTraceDescriptorSets[currentCascade]->bind(commandBuffer->getCommandBufferVk(), m_mergeCascadePipeline);

        // Set up push constants
        RCMergeCascadePushConstants pushConstants = {};
        pushConstants.prevCascadeIndex = prevCascade;
        pushConstants.currentCascadeIndex = currentCascade;

        vkCmdPushConstants(
            commandBuffer->getCommandBufferVk(),
            m_mergeCascadePipeline->getPipelineLayoutVk(),
            VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(RCMergeCascadePushConstants), &pushConstants
        );

        // Calculate dispatch size based on current cascade dimensions
        const auto& currentCascadeInfo = m_radianceCascades[currentCascade];
        uint32_t imageWidth = currentCascadeInfo.probeGridDimensions.x * currentCascadeInfo.angularResolution;
        uint32_t imageHeight = currentCascadeInfo.probeGridDimensions.y * currentCascadeInfo.angularResolution;

        uint32_t workGroupsX = (imageWidth + 7) / 8;
        uint32_t workGroupsY = (imageHeight + 7) / 8;

        // Dispatch the merge compute shader
        vkCmdDispatch(commandBuffer->getCommandBufferVk(), workGroupsX, workGroupsY, 1);

        // Add a memory barrier after each dispatch to prevent race conditions.
        // This ensures that the writes to the current cascade's texture are complete
        // before it is read in the next iteration of the loop.
        VkImageMemoryBarrier intermediateMergeBarrier = m_cascadeTextures[currentCascade]->getImageMemoryBarrier(
            VK_IMAGE_LAYOUT_GENERAL, 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
            VK_ACCESS_SHADER_WRITE_BIT, 
            VK_ACCESS_SHADER_READ_BIT);

        vkCmdPipelineBarrier(
            commandBuffer->getCommandBufferVk(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &intermediateMergeBarrier
        );
    }



}

void RadianceCascades2D::integrateCascade(std::shared_ptr<CommandBuffer> commandBuffer) {
    RAPTURE_PROFILE_FUNCTION();


    VkImageMemoryBarrier integrationBarrier = m_irradianceCascadeTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_GENERAL, 
        0,
        VK_ACCESS_SHADER_WRITE_BIT);

    VkImageMemoryBarrier cascadeBarrier = m_cascadeTextures[0]->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT);

    vkCmdPipelineBarrier(
            commandBuffer->getCommandBufferVk(),
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &integrationBarrier
        );

    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &cascadeBarrier
    );

    m_integrateIrradiancePipeline->bind(commandBuffer->getCommandBufferVk());


    DescriptorManager::bindSet(0, commandBuffer, m_integrateIrradiancePipeline);
    DescriptorManager::bindSet(3, commandBuffer, m_integrateIrradiancePipeline);
    m_integrateIrradianceDescriptorSet->bind(commandBuffer->getCommandBufferVk(), m_integrateIrradiancePipeline);

    // Calculate dispatch size based on current cascade dimensions
    const auto& cascade0Info = m_radianceCascades[0];
    uint32_t imageWidth = cascade0Info.probeGridDimensions.x * cascade0Info.angularResolution;
    uint32_t imageHeight = cascade0Info.probeGridDimensions.y * cascade0Info.angularResolution;

    uint32_t workGroupsX = (imageWidth + 7) / 8;
    uint32_t workGroupsY = (imageHeight + 7) / 8;

    // Dispatch the merge compute shader
    vkCmdDispatch(commandBuffer->getCommandBufferVk(), workGroupsX, workGroupsY, 1);



    VkImageMemoryBarrier postIntegrationBarrier = m_irradianceCascadeTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_GENERAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);

    vkCmdPipelineBarrier(
            commandBuffer->getCommandBufferVk(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &postIntegrationBarrier
        );


}

void RadianceCascades2D::updateBaseRange(float baseRange) {
    float prevMinRange = 0.0f;

    if (baseRange < m_buildParams.baseSpacing) {
        return;
    }

    m_buildParams.baseRange = baseRange;

    for (uint32_t i = 0; i < MAX_CASCADES; i++) {
        RadianceCascadeLevel2D& cascade = m_radianceCascades[i];

        float maxRange = baseRange * std::pow(m_buildParams.rangeExp, static_cast<float>(i));

        cascade.minProbeDistance = prevMinRange;
        cascade.maxProbeDistance = maxRange;

        prevMinRange = maxRange;

        float overlap = glm::length(glm::length(glm::vec2(m_buildParams.baseSpacing) * std::pow(m_buildParams.gridDimensionsExp, static_cast<float>(i+1))));
        cascade.maxProbeDistance = cascade.maxProbeDistance + overlap;

        m_cascadeUniformBuffers[i]->addDataGPU(&cascade, sizeof(RadianceCascadeLevel2D), 0);
    }

    
}

void RadianceCascades2D::updateBaseSpacing(float baseSpacing) {
    if (baseSpacing > m_buildParams.baseRange) {
        return;
    }

    m_buildParams.baseSpacing = baseSpacing;

    for (uint32_t i = 0; i < MAX_CASCADES; i++) {
        RadianceCascadeLevel2D& cascade = m_radianceCascades[i];
        cascade.probeSpacing = glm::vec2(baseSpacing) * std::pow(m_buildParams.gridDimensionsExp, static_cast<float>(i));
        m_cascadeUniformBuffers[i]->addDataGPU(&cascade, sizeof(RadianceCascadeLevel2D), 0);
    }
}

void RadianceCascades2D::castRays(std::shared_ptr<Scene> scene, uint32_t frameIndex) {
    RAPTURE_PROFILE_FUNCTION();

    auto tlas = scene->getTLAS();
    if (!tlas || !tlas->isBuilt() || tlas->getInstanceCount() == 0) {
        return;
    }




    auto currentCommandBuffer = m_commandBuffers[frameIndex];




    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        
    if (vkBeginCommandBuffer(currentCommandBuffer->getCommandBufferVk(), &beginInfo) != VK_SUCCESS) {
        RP_CORE_ERROR("RadianceCascades::castRays - Failed to begin command buffer");
        return;
    }
    //-------------------------------------------------------------------------------------------------
    {    
        RAPTURE_PROFILE_GPU_SCOPE(currentCommandBuffer->getCommandBufferVk(), "RadianceCascades2D::castRays");
    
    std::vector<VkImageMemoryBarrier> preTraceBarriers;


    for (uint32_t i = 0; i < MAX_CASCADES; i++) {
        VkImageMemoryBarrier cascadeWriteBarrier = m_cascadeTextures[i]->getImageMemoryBarrier(
            VK_IMAGE_LAYOUT_UNDEFINED, 
            VK_IMAGE_LAYOUT_GENERAL, 
            0, 
            VK_ACCESS_SHADER_WRITE_BIT);

        preTraceBarriers.push_back(cascadeWriteBarrier);
    }
    
    vkCmdPipelineBarrier(
        currentCommandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(preTraceBarriers.size()), preTraceBarriers.data()
    );


    m_probeTracePipeline->bind(currentCommandBuffer->getCommandBufferVk());

    DescriptorManager::bindSet(0, currentCommandBuffer, m_probeTracePipeline);
    DescriptorManager::bindSet(3, currentCommandBuffer, m_probeTracePipeline);

    auto& reg = scene->getRegistry();
    auto lightsView = reg.view<LightComponent>();

    for (uint32_t i = 0; i < MAX_CASCADES; i++) {

        m_probeTraceDescriptorSets[i]->bind(currentCommandBuffer->getCommandBufferVk(), m_probeTracePipeline);


        RCProbeTracePushConstants2D pushConstants = {};
        pushConstants.cascadeIndex = i;
        pushConstants.cascadeLevels = MAX_CASCADES;
        pushConstants.tlasIndex = tlas->getBindlessIndex();
        pushConstants.lightCount = static_cast<uint32_t>(lightsView.size());

        SkyboxComponent* skyboxComp = scene->getSkyboxComponent();
        if (skyboxComp && skyboxComp->skyboxTexture && skyboxComp->skyboxTexture->isReadyForSampling()) {
            pushConstants.skyboxTextureIndex = skyboxComp->skyboxTexture->getBindlessIndex();
        } else {
            pushConstants.skyboxTextureIndex = UINT32_MAX;
        }

        vkCmdPushConstants(
            currentCommandBuffer->getCommandBufferVk(),
            m_probeTracePipeline->getPipelineLayoutVk(),
            VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(RCProbeTracePushConstants2D), &pushConstants
        );

        const auto& cascade = m_radianceCascades[i];
        
        uint32_t imageWidth = m_cascadeTextures[i]->getSpecification().width;
        uint32_t imageHeight = m_cascadeTextures[i]->getSpecification().height;


        uint32_t workGroupsX = (imageWidth + 7) / 8;
        uint32_t workGroupsY = (imageHeight + 7) / 8;
    
        vkCmdDispatch(currentCommandBuffer->getCommandBufferVk(), workGroupsX, workGroupsY, 1);
    }


    std::vector<VkImageMemoryBarrier> postTraceBarriers;
    for (uint32_t i = 0; i < MAX_CASCADES; i++) {
        VkImageMemoryBarrier cascadeWriteBarrier = m_cascadeTextures[i]->getImageMemoryBarrier(
            VK_IMAGE_LAYOUT_GENERAL, 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
            VK_ACCESS_SHADER_WRITE_BIT, 
            VK_ACCESS_SHADER_READ_BIT);

        postTraceBarriers.push_back(cascadeWriteBarrier);
    }
    
    vkCmdPipelineBarrier(
        currentCommandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(postTraceBarriers.size()), postTraceBarriers.data()
    );

    }   

    {    
        RAPTURE_PROFILE_GPU_SCOPE(currentCommandBuffer->getCommandBufferVk(), "RadianceCascades2D::mergeCascades");

        mergeCascades(currentCommandBuffer);
    }

    {    
        RAPTURE_PROFILE_GPU_SCOPE(currentCommandBuffer->getCommandBufferVk(), "RadianceCascades2D::integrateCascade");

        integrateCascade(currentCommandBuffer);
    }



    //-------------------------------------------------------------------------------------------------
    RAPTURE_PROFILE_GPU_COLLECT(currentCommandBuffer->getCommandBufferVk());


    if (vkEndCommandBuffer(currentCommandBuffer->getCommandBufferVk()) != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::populateProbesCompute - Failed to end command buffer");
        return;
    }

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();

    vc.getComputeQueue()->submitQueue(currentCommandBuffer);



}

void RadianceCascades2D::buildDescriptorSet() {
    
    DescriptorSetBindings bindings;
    bindings.setNumber = 4;

    DescriptorSetBinding bindingLocation;
    bindingLocation.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindingLocation.count = 1;
    bindingLocation.viewType = TextureViewType::DEFAULT;
    bindingLocation.useStorageImageInfo = true;
    bindingLocation.location = DescriptorSetBindingLocation::CUSTOM_0;

    bindings.bindings.push_back(bindingLocation);
    m_probeTraceDescriptorSets.resize(MAX_CASCADES);
    for (uint32_t i = 0; i < MAX_CASCADES; i++) {
        m_probeTraceDescriptorSets[i] = std::make_shared<DescriptorSet>(bindings);
        auto textureBinding = m_probeTraceDescriptorSets[i]->getTextureBinding((DescriptorSetBindingLocation::CUSTOM_0));
        textureBinding->add(m_cascadeTextures[i]);
    }

    // #irradiancedescriptor
    DescriptorSetBindings irradianceBindings;
    irradianceBindings.setNumber = 4;
    irradianceBindings.bindings.push_back(bindingLocation);
    m_integrateIrradianceDescriptorSet = std::make_shared<DescriptorSet>(irradianceBindings);
    auto irradianceTextureBinding = m_integrateIrradianceDescriptorSet->getTextureBinding((DescriptorSetBindingLocation::CUSTOM_0));
    irradianceTextureBinding->add(m_irradianceCascadeTexture);

}

std::vector<glm::vec3> RadianceCascades2D::getCascadeProbePositions(uint32_t cascadeIndex) const {
    if (cascadeIndex >= MAX_CASCADES) {
        RP_CORE_ERROR("Invalid cascade index: {}", cascadeIndex);
        return {};
    }

    const auto& cascade = m_radianceCascades[cascadeIndex];
    
    // Skip invalid cascades
    if (cascade.cascadeLevel == UINT32_MAX) {
        return {};
    }

    std::vector<glm::vec3> positions;
    positions.reserve(cascade.probeGridDimensions.x * cascade.probeGridDimensions.y);

    // Calculate probe positions using the same logic as the shader GetProbeWorldPosition
    for (int x = 0; x < cascade.probeGridDimensions.x; x++) {
        for (int y = 0; y < cascade.probeGridDimensions.y; y++) {
            glm::ivec2 probeCoords(x, y);
            
            // Multiply the grid coordinates by the probe spacing
            glm::vec2 probeGridWorldPosition = glm::vec2(probeCoords) * cascade.probeSpacing;

            // Shift the grid of probes by half of each axis extent to center the volume about its origin
            glm::vec2 probeGridShift = (cascade.probeSpacing * glm::vec2(cascade.probeGridDimensions - 1)) * 0.5f;

            // Center the probe grid about the origin
            glm::vec2 probeWorldPosition = probeGridWorldPosition;// - probeGridShift;

            positions.push_back(glm::vec3(probeWorldPosition.x, 0.0f, probeWorldPosition.y));
        
        }
    }

    return positions;
}



}
