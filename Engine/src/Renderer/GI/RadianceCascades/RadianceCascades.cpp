#include "RadianceCascades.h"

#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "AssetManager/AssetManager.h"
#include "WindowContext/Application.h"

#include "Components/Components.h"

#include "Buffers/Descriptors/DescriptorManager.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include <format>

// TODO : Add safety for when the grid dimensions go below 1

namespace Rapture {

struct RCMergeCascadePushConstants {
    uint32_t prevCascadeIndex;
    uint32_t currentCascadeIndex;
};

RadianceCascades::RadianceCascades(uint32_t framesInFlight) {
    // load shaders and compute pipelines
    buildPipelines();
    buildCommandBuffers(framesInFlight);
}

void RadianceCascades::build(const BuildParams &buildParams) {


    float prevMinRange = 0.0f;

    for (uint32_t i = 0; i < MAX_CASCADES; i++) {
        RadianceCascadeLevel cascade = {};

        cascade.cascadeLevel = i;

        float maxRange = buildParams.baseRange * std::pow(buildParams.rangeScaleFactor, static_cast<float>(i));

        cascade.minProbeDistance = prevMinRange;
        cascade.maxProbeDistance = maxRange;

        prevMinRange = maxRange;


        glm::vec3 scaledDimsF = glm::vec3(buildParams.baseGridDimensions) * std::pow(buildParams.gridScaleFactor, static_cast<float>(i));
        cascade.probeGridDimensions = glm::max(glm::ivec3(glm::floor(scaledDimsF)), glm::ivec3(1)); // prevent zero dimensions

        //glm::vec3 extent = glm::vec3(maxRange); // cubic region size
        cascade.probeSpacing = buildParams.baseGridSpacing * std::pow(2.0f, static_cast<float>(i));

        cascade.angularResolution = static_cast<uint32_t>(
            buildParams.baseAngularResolution * std::pow(buildParams.angularScaleFactor, static_cast<float>(i))
        );

        // this is the center of the grid, the shader should shift the grid by half of the extent
        cascade.probeOrigin = glm::vec3(-0.4f, 4.7f, -0.25f);

        cascade.cascadeTextureIndex = 0xFFFFFFFF;

        RP_CORE_INFO("Cascade {}: \n\t Probe Origin: {}, Probe Spacing: {}, \
            Probe Grid Dimensions: {}, \
            Angular Resolution: {}, interval: {} - {}", 
            
            i, glm::to_string(cascade.probeOrigin), glm::to_string(cascade.probeSpacing), glm::to_string(cascade.probeGridDimensions), cascade.angularResolution, cascade.minProbeDistance, cascade.maxProbeDistance);

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

void RadianceCascades::buildTextures() {

    TextureSpecification defaultSpec = {};
    defaultSpec.filter = TextureFilter::Nearest;
    defaultSpec.srgb = false;
    defaultSpec.storageImage = true;
    defaultSpec.format = TextureFormat::RGBA32F;
    defaultSpec.type = TextureType::TEXTURE2D_ARRAY;

    for (uint32_t i = 0; i < MAX_CASCADES; i++) {

        RadianceCascadeLevel& cascade = m_radianceCascades[i];

        if (cascade.cascadeLevel == UINT32_MAX) {
            RP_CORE_ERROR("Cascade not initializes, call build() first");
            throw std::runtime_error("Cascade not initialized, call build() first");
        }
        defaultSpec.width = cascade.probeGridDimensions.x * cascade.angularResolution;
        defaultSpec.height = cascade.probeGridDimensions.z * cascade.angularResolution;
        defaultSpec.depth = cascade.probeGridDimensions.y;

        m_cascadeTextures[i] = std::make_shared<Texture>(defaultSpec);

        uint32_t bindlessIndex = m_cascadeTextures[i]->getBindlessIndex();

        if (bindlessIndex == UINT32_MAX) {
            RP_CORE_ERROR("Failed to get bindless index for cascade({}) texture", cascade.cascadeLevel);
            throw std::runtime_error(std::format("Failed to get bindless index for cascade({}) texture", cascade.cascadeLevel).c_str());
        }

        m_flatCascadeTextures[i] = TextureFlattener::createFlattenTexture(m_cascadeTextures[i], "[RC] Flattened Cascade Texture: " + std::to_string(cascade.cascadeLevel));
        m_flatMergedCascadeTextures[i] = TextureFlattener::createFlattenTexture(m_cascadeTextures[i], "[RC] Flattened Merged Cascade Texture: " + std::to_string(cascade.cascadeLevel));


        cascade.cascadeTextureIndex = bindlessIndex;

    }

}

void RadianceCascades::buildPipelines() {

    auto& app = Application::getInstance();
    auto& proj = app.getProject();
    auto shaderDir = proj.getProjectShaderDirectory();

    ShaderImportConfig shaderBaseProbeConfig; 
    shaderBaseProbeConfig.compileInfo.includePath = shaderDir / "glsl/RadianceCascades/";



    auto [probeTraceShader, probeTraceShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/RadianceCascades/RCProbeTrace.cs.glsl", shaderBaseProbeConfig);
    auto [mergeCascadeShader, mergeCascadeShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/RadianceCascades/RCPCascadeMerge.cs.glsl", shaderBaseProbeConfig);

    ComputePipelineConfiguration probeTraceConfig;
    probeTraceConfig.shader = probeTraceShader;

    ComputePipelineConfiguration mergeCascadeConfig;
    mergeCascadeConfig.shader = mergeCascadeShader;

    m_probeTracePipeline = std::make_shared<ComputePipeline>(probeTraceConfig);
    m_mergeCascadePipeline = std::make_shared<ComputePipeline>(mergeCascadeConfig);

}

void RadianceCascades::buildCommandBuffers(uint32_t framesInFlight) {
    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();

    CommandPoolConfig poolConfig;
    poolConfig.name = "RC Command Pool";
    poolConfig.queueFamilyIndex = vc.getQueueFamilyIndices().computeFamily.value();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    auto pool = CommandPoolManager::createCommandPool(poolConfig);
    m_commandBuffers = pool->getCommandBuffers(framesInFlight);




}

void RadianceCascades::buildUniformBuffers() {

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
        m_cascadeUniformBuffers.push_back(std::make_shared<UniformBuffer>(sizeof(RadianceCascadeLevel), BufferUsage::STATIC, vc.getVmaAllocator(), &m_radianceCascades[i]));
        m_cascadeUniformBuffers[i]->addDataGPU(&m_radianceCascades[i], sizeof(RadianceCascadeLevel), 0);

        cascadeLevelInfoBinding->add(m_cascadeUniformBuffers[i]);
    }
}

void RadianceCascades::mergeCascades(std::shared_ptr<CommandBuffer> commandBuffer) {
    
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
        uint32_t imageHeight = currentCascadeInfo.probeGridDimensions.z * currentCascadeInfo.angularResolution;
        uint32_t imageDepth = currentCascadeInfo.probeGridDimensions.y;

        uint32_t workGroupsX = (imageWidth + 7) / 8;
        uint32_t workGroupsY = (imageHeight + 7) / 8;
        uint32_t workGroupsZ = imageDepth;

        // Dispatch the merge compute shader
        vkCmdDispatch(commandBuffer->getCommandBufferVk(), workGroupsX, workGroupsY, workGroupsZ);

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

void RadianceCascades::castRays(std::shared_ptr<Scene> scene, uint32_t frameIndex) {

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


        RCProbeTracePushConstants pushConstants = {};
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
            0, sizeof(RCProbeTracePushConstants), &pushConstants
        );

        const auto& cascade = m_radianceCascades[i];
        
        uint32_t imageWidth = m_cascadeTextures[i]->getSpecification().width;
        uint32_t imageHeight = m_cascadeTextures[i]->getSpecification().height;
        uint32_t imageDepth = m_cascadeTextures[i]->getSpecification().depth;


        uint32_t workGroupsX = (imageWidth + 7) / 8;
        uint32_t workGroupsY = (imageHeight + 7) / 8;
        uint32_t workGroupsZ = imageDepth;
    
        vkCmdDispatch(currentCommandBuffer->getCommandBufferVk(), workGroupsX, workGroupsY, workGroupsZ);
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


    for (uint32_t i = 0; i < MAX_CASCADES; i++) {
        m_flatCascadeTextures[i]->update(currentCommandBuffer);
    }

    mergeCascades(currentCommandBuffer);

    for (uint32_t i = 0; i < MAX_CASCADES; i++) {
        m_flatMergedCascadeTextures[i]->update(currentCommandBuffer);
    }


    //-------------------------------------------------------------------------------------------------


    if (vkEndCommandBuffer(currentCommandBuffer->getCommandBufferVk()) != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::populateProbesCompute - Failed to end command buffer");
        return;
    }

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();

    vc.getComputeQueue()->submitQueue(currentCommandBuffer);



}

void RadianceCascades::buildDescriptorSet() {
    
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

}

std::vector<glm::vec3> RadianceCascades::getCascadeProbePositions(uint32_t cascadeIndex) const {
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
    positions.reserve(cascade.probeGridDimensions.x * cascade.probeGridDimensions.y * cascade.probeGridDimensions.z);

    // Calculate probe positions using the same logic as the shader GetProbeWorldPosition
    for (int x = 0; x < cascade.probeGridDimensions.x; x++) {
        for (int y = 0; y < cascade.probeGridDimensions.y; y++) {
            for (int z = 0; z < cascade.probeGridDimensions.z; z++) {
                glm::ivec3 probeCoords(x, y, z);
                
                // Multiply the grid coordinates by the probe spacing
                glm::vec3 probeGridWorldPosition = glm::vec3(probeCoords) * cascade.probeSpacing;

                // Shift the grid of probes by half of each axis extent to center the volume about its origin
                glm::vec3 probeGridShift = (cascade.probeSpacing * glm::vec3(cascade.probeGridDimensions - 1)) * 0.5f;

                // Center the probe grid about the origin
                glm::vec3 probeWorldPosition = probeGridWorldPosition - probeGridShift + cascade.probeOrigin;

                positions.push_back(probeWorldPosition);
            }
        }
    }

    return positions;
}


}
