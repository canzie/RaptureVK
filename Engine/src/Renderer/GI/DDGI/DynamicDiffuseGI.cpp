#include "DynamicDiffuseGI.h"

#include "AssetManager/Asset.h"
#include "Components/Components.h"
#include "Materials/MaterialParameters.h"
#include "Renderer/GI/DDGI/DDGICommon.h"
#include "Scenes/Entities/Entity.h"
#include "Scenes/Scene.h"
#include "Textures/Texture.h"
#include "Textures/TextureCommon.h"
#include "WindowContext/Application.h"
#include "AssetManager/AssetManager.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Buffers/Descriptors/DescriptorBinding.h"

#include "Logging/TracyProfiler.h"


namespace Rapture {

// Push constants for DDGI compute shaders
struct DDGITracePushConstants {
    uint32_t skyboxTextureIndex;
    uint32_t sunLightDataIndex;
    uint32_t prevRadianceIndex;
    uint32_t prevVisibilityIndex;
    uint32_t tlasIndex;
};

struct DDGIBlendPushConstants {
    uint32_t prevTextureIndex;
    uint32_t rayDataIndex;
    uint32_t writeToAlternateTexture; // 0 = write to primary, 1 = write to alternate
};

std::shared_ptr<Texture> DynamicDiffuseGI::s_defaultSkyboxTexture = nullptr;

DynamicDiffuseGI::DynamicDiffuseGI() 
  : m_ProbeInfoBuffer(nullptr),
    m_Hysteresis(0.96f),
    m_isPopulated(false),
    m_meshCount(0),
    m_isEvenFrame(true),
    m_isFirstFrame(true),
    m_DDGI_ProbeTraceShader(nullptr),
    m_DDGI_ProbeIrradianceBlendingShader(nullptr),
    m_DDGI_ProbeDistanceBlendingShader(nullptr),
    m_DDGI_ProbeTracePipeline(nullptr),
    m_DDGI_ProbeIrradianceBlendingPipeline(nullptr),
    m_DDGI_ProbeDistanceBlendingPipeline(nullptr),
    m_MeshInfoBuffer(nullptr),
    m_skyboxTexture(nullptr),
    m_probeIrradianceBindlessIndex(UINT32_MAX),
    m_prevProbeIrradianceBindlessIndex(UINT32_MAX),
    m_probeVisibilityBindlessIndex(UINT32_MAX),
    m_prevProbeVisibilityBindlessIndex(UINT32_MAX) {

    if (!s_defaultSkyboxTexture) {
        s_defaultSkyboxTexture = Texture::createDefaultWhiteCubemapTexture();
    }
    m_skyboxTexture = s_defaultSkyboxTexture;



    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();
    m_allocator = vc.getVmaAllocator();
    m_computeQueue = vc.getComputeQueue();

    createPipelines();

    CommandPoolConfig poolConfig;
    poolConfig.name = "DDGI Command Pool";
    poolConfig.queueFamilyIndex = vc.getQueueFamilyIndices().computeFamily.value();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    auto pool = CommandPoolManager::createCommandPool(poolConfig);

    m_CommandBuffer = pool->getCommandBuffer();

    initProbeInfoBuffer();
    initTextures();


    
}

DynamicDiffuseGI::~DynamicDiffuseGI() {

}

void DynamicDiffuseGI::createPipelines() {

    auto& app = Application::getInstance();
    auto& proj = app.getProject();
    auto shaderDir = proj.getProjectShaderDirectory();

    ShaderImportConfig shaderIrradianceBlendConfig;
    shaderIrradianceBlendConfig.compileInfo.macros.push_back("DDGI_BLEND_RADIANCE");
    shaderIrradianceBlendConfig.compileInfo.includePath = shaderDir / "glsl/ddgi/";
    ShaderImportConfig shaderDistanceBlendConfig;
    shaderDistanceBlendConfig.compileInfo.macros.push_back("DDGI_BLEND_DISTANCE");
    shaderDistanceBlendConfig.compileInfo.includePath = shaderDir / "glsl/ddgi/";
    ShaderImportConfig shaderProbeTraceConfig;
    shaderProbeTraceConfig.compileInfo.includePath = shaderDir / "glsl/ddgi/";

    auto [probeTraceShader, probeTraceShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/ddgi/ProbeTrace.cs.glsl", shaderProbeTraceConfig);
    auto [probeIrradianceBlendShader, probeIrradianceBlendShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/ddgi/ProbeBlending.cs.glsl", shaderIrradianceBlendConfig);
    auto [probeDistanceBlendShader, probeDistanceBlendShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/ddgi/ProbeBlending.cs.glsl", shaderDistanceBlendConfig);

    m_DDGI_ProbeTraceShader = probeTraceShader;
    m_DDGI_ProbeIrradianceBlendingShader = probeIrradianceBlendShader;
    m_DDGI_ProbeDistanceBlendingShader = probeDistanceBlendShader;

    ComputePipelineConfiguration probeTraceConfig;
    probeTraceConfig.shader = m_DDGI_ProbeTraceShader;
    ComputePipelineConfiguration probeIrradianceBlendingConfig;
    probeIrradianceBlendingConfig.shader = m_DDGI_ProbeIrradianceBlendingShader;
    ComputePipelineConfiguration probeDistanceBlendingConfig;
    probeDistanceBlendingConfig.shader = m_DDGI_ProbeDistanceBlendingShader;

    m_DDGI_ProbeTracePipeline = std::make_shared<ComputePipeline>(probeTraceConfig);
    m_DDGI_ProbeIrradianceBlendingPipeline = std::make_shared<ComputePipeline>(probeIrradianceBlendingConfig);
    m_DDGI_ProbeDistanceBlendingPipeline = std::make_shared<ComputePipeline>(probeDistanceBlendingConfig);

}

void DynamicDiffuseGI::setupProbeTextures() {
    RP_CORE_TRACE("Setting up probe textures for bindless access");

    // Get bindless indices for probe textures (these will be used in lighting pass)
    if (m_RadianceTexture) {
        m_probeIrradianceBindlessIndex = m_RadianceTexture->getBindlessIndex();
        m_prevProbeIrradianceBindlessIndex = m_PrevRadianceTexture->getBindlessIndex();
    }
    if (m_VisibilityTexture) {
        m_probeVisibilityBindlessIndex = m_VisibilityTexture->getBindlessIndex();
        m_prevProbeVisibilityBindlessIndex = m_PrevVisibilityTexture->getBindlessIndex();
    }

    // Add probe textures to their specific storage image bindings in set 3
    auto bindlessSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::BINDLESS_TEXTURES);
    if (bindlessSet) {
        // Add ray data texture to binding 2
        auto rayDataBinding = bindlessSet->getTextureBinding(DescriptorSetBindingLocation::DDGI_RAY_DATA_STORAGE);
        if (rayDataBinding) {
            rayDataBinding->add(m_RayDataTexture);
        }
        
        // Add irradiance textures to bindings 3 and 4
        auto irradianceBinding = bindlessSet->getTextureBinding(DescriptorSetBindingLocation::DDGI_IRRADIANCE_STORAGE);
        if (irradianceBinding) {
            irradianceBinding->add(m_RadianceTexture);
        }
        
        auto prevIrradianceBinding = bindlessSet->getTextureBinding(DescriptorSetBindingLocation::DDGI_PREV_IRRADIANCE_STORAGE);
        if (prevIrradianceBinding) {
            prevIrradianceBinding->add(m_PrevRadianceTexture);
        }
        
        // Add visibility textures to bindings 5 and 6
        auto visibilityBinding = bindlessSet->getTextureBinding(DescriptorSetBindingLocation::DDGI_VISIBILITY_STORAGE);
        if (visibilityBinding) {
            visibilityBinding->add(m_VisibilityTexture);
        }
        
        auto prevVisibilityBinding = bindlessSet->getTextureBinding(DescriptorSetBindingLocation::DDGI_PREV_VISIBILITY_STORAGE);
        if (prevVisibilityBinding) {
            prevVisibilityBinding->add(m_PrevVisibilityTexture);
        }
        
        RP_CORE_INFO("DDGI: Added storage textures to fixed bindings in set 3");
    }
}

uint32_t DynamicDiffuseGI::getSunLightDataIndex(std::shared_ptr<Scene> scene) {

    auto& reg = scene->getRegistry();
    auto lightView = reg.view<LightComponent>();

    for (auto ent : lightView) {
        auto& lightComp = lightView.get<LightComponent>(ent);
        if (lightComp.type == LightType::Directional) {
            return lightComp.lightDataBuffer->getDescriptorIndex();
        }
    }

    return 0;
}

void DynamicDiffuseGI::clearTextures() {

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(m_CommandBuffer->getCommandBufferVk(), &beginInfo) != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::clearTextures - Failed to begin command buffer");
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
    

    VkImageMemoryBarrier radianceTransition = m_RadianceTexture->getImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    layoutTransitions.push_back(radianceTransition);
    
    VkImageMemoryBarrier prevRadianceTransition = m_PrevRadianceTexture->getImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    layoutTransitions.push_back(prevRadianceTransition);
    
    VkImageMemoryBarrier visibilityTransition = m_VisibilityTexture->getImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    layoutTransitions.push_back(visibilityTransition);
    
    VkImageMemoryBarrier prevVisibilityTransition = m_PrevVisibilityTexture->getImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    layoutTransitions.push_back(prevVisibilityTransition);

    vkCmdPipelineBarrier(
        m_CommandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(layoutTransitions.size()), layoutTransitions.data()
    );

    VkClearColorValue clearColor = { {0.0f, 0.0f, 0.0f, 0.0f} };
    vkCmdClearColorImage(
        m_CommandBuffer->getCommandBufferVk(),
        m_RadianceTexture->getImage(),
        VK_IMAGE_LAYOUT_GENERAL,
        &clearColor,
        1, &subresourceRange
    );

    vkCmdClearColorImage(
        m_CommandBuffer->getCommandBufferVk(),
        m_PrevRadianceTexture->getImage(),
        VK_IMAGE_LAYOUT_GENERAL,
        &clearColor,
        1, &subresourceRange
    );

    vkCmdClearColorImage(
        m_CommandBuffer->getCommandBufferVk(),
        m_VisibilityTexture->getImage(),
        VK_IMAGE_LAYOUT_GENERAL,
        &clearColor,
        1, &subresourceRange
    );

    vkCmdClearColorImage(
        m_CommandBuffer->getCommandBufferVk(),
        m_PrevVisibilityTexture->getImage(),
        VK_IMAGE_LAYOUT_GENERAL,
        &clearColor,
        1, &subresourceRange
    );

    if (vkEndCommandBuffer(m_CommandBuffer->getCommandBufferVk()) != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::clearTextures - Failed to end command buffer");
        return;
    }

    m_computeQueue->submitQueue(m_CommandBuffer);

}

void DynamicDiffuseGI::populateProbes(std::shared_ptr<Scene> scene){

    RAPTURE_PROFILE_FUNCTION();

    auto& tlas = scene->getTLAS();
    auto& tlasInstances = tlas.getInstances();

    if (m_meshCount == tlas.getInstanceCount()) {
        return;
    }

    auto& reg = scene->getRegistry();
    auto MMview = reg.view<MaterialComponent, MeshComponent, TransformComponent>();


    std::vector<MeshInfo> meshInfos(tlas.getInstanceCount());

    int i = 0;

    for (auto inst : tlasInstances) {

        Entity ent = Entity(inst.entityID, scene.get());

        MeshInfo& meshinfo = meshInfos[i];
        meshinfo = {};
        meshinfo.AlbedoTextureIndex = 0;
        meshinfo.NormalTextureIndex = 0; 
        meshinfo.MetallicRoughnessTextureIndex = 0;
        meshinfo.vboIndex = 0;
        meshinfo.iboIndex = 0;
        meshinfo.positionAttributeOffsetBytes = 0;
        meshinfo.texCoordAttributeOffsetBytes = 0;
        meshinfo.normalAttributeOffsetBytes = 0;
        meshinfo.tangentAttributeOffsetBytes = 0;
        meshinfo.vertexStrideBytes = 0;
        meshinfo.indexType = 0;


        // Get the mesh component and retrieve buffer indices
        if (MMview.contains(ent)) {
            auto [meshComp, materialComp, transformComp] = MMview.get<MeshComponent, MaterialComponent, TransformComponent>(ent);

            meshinfo.modelMatrix = transformComp.transformMatrix();

            if (materialComp.material) {
                auto& material = materialComp.material;
                meshinfo.AlbedoTextureIndex = material->getParameter(ParameterID::ALBEDO_MAP).asUInt();
                meshinfo.NormalTextureIndex = material->getParameter(ParameterID::NORMAL_MAP).asUInt();
                meshinfo.MetallicRoughnessTextureIndex = material->getParameter(ParameterID::METALLIC_ROUGHNESS_MAP).asUInt();

            }

            if (meshComp.mesh) {
                auto vertexBuffer = meshComp.mesh->getVertexBuffer();
                auto indexBuffer = meshComp.mesh->getIndexBuffer();

                if (vertexBuffer) {

                    meshinfo.vboIndex = vertexBuffer->getBindlessIndex();

                    auto& bfl = vertexBuffer->getBufferLayout();

                    meshinfo.positionAttributeOffsetBytes = bfl.getAttributeOffset(BufferAttributeID::POSITION);
                    meshinfo.texCoordAttributeOffsetBytes = bfl.getAttributeOffset(BufferAttributeID::TEXCOORD_0);
                    meshinfo.normalAttributeOffsetBytes = bfl.getAttributeOffset(BufferAttributeID::NORMAL);
                    meshinfo.tangentAttributeOffsetBytes = bfl.getAttributeOffset(BufferAttributeID::TANGENT);
                    meshinfo.vertexStrideBytes = bfl.calculateVertexSize();

                }
                
                if (indexBuffer) {
                    meshinfo.iboIndex = indexBuffer->getBindlessIndex();
                    meshinfo.indexType = indexBuffer->getIndexType();
                }
            }
        }

        // Set the mesh index to match the TLAS instance custom index
        meshinfo.meshIndex = inst.instanceCustomIndex;

        i++;
    }

    // Update the mesh count
    m_meshCount = static_cast<uint32_t>(meshInfos.size());

    // Create or update the mesh info buffer
    if (!m_MeshInfoBuffer || m_MeshInfoBuffer->getSize() < sizeof(MeshInfo) * meshInfos.size()) {
        m_MeshInfoBuffer = std::make_shared<StorageBuffer>(
        sizeof(MeshInfo) * meshInfos.size(),
        BufferUsage::STATIC,
        m_allocator,
        meshInfos.data()
        );
    } else {
        m_MeshInfoBuffer->addDataGPU(meshInfos.data(), sizeof(MeshInfo) * meshInfos.size(), 0);
    }

    // Add mesh info buffer to the mesh data descriptor set
    auto meshDataSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::DDGI_SCENE_INFO_SSBOS);
    if (meshDataSet) {
        auto binding = meshDataSet->getSSBOBinding(DescriptorSetBindingLocation::DDGI_SCENE_INFO_SSBOS);
        if (binding) {
            m_meshDataSSBOIndex = binding->add(m_MeshInfoBuffer);
            RP_CORE_INFO("DDGI Mesh data SSBO index: {}", m_meshDataSSBOIndex);
        }
    }

    // Setup probe textures for bindless access if not already done
    if (m_probeIrradianceBindlessIndex == UINT32_MAX) {
        setupProbeTextures();
    }

    m_isPopulated = true;
    RP_CORE_INFO("Populated {} mesh infos for DDGI", meshInfos.size());
}

void DynamicDiffuseGI::populateProbesCompute(std::shared_ptr<Scene> scene) {

    RAPTURE_PROFILE_FUNCTION();

    if (!m_isPopulated) {
        // First populate the probe data
        populateProbes(scene);
    }
    if (m_isVolumeDirty) {
        // Update the probe volume
        updateProbeVolume();
    }
    updateSkybox(scene);

    auto& tlas = scene->getTLAS();
    if (!tlas.isBuilt() || tlas.getInstanceCount() == 0) {
        //RP_CORE_WARN("DynamicDiffuseGI::populateProbesCompute - Scene TLAS is not built");
        return;
    }
    

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    if (vkBeginCommandBuffer(m_CommandBuffer->getCommandBufferVk(), &beginInfo) != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::castRays - Failed to begin command buffer");
        return;
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(m_CommandBuffer->getCommandBufferVk(), "DynamicDiffuseGI::populateProbesCompute");
        // Cast rays using compute shader
        castRays(scene);
    }   
    // Flatten ray data texture (within the same command buffer)
    if (m_RayDataTextureFlattened) {
        RAPTURE_PROFILE_SCOPE("Flattening ray data texture");
        m_RayDataTextureFlattened->update(m_CommandBuffer);
    }
    
    {
        RAPTURE_PROFILE_GPU_SCOPE(m_CommandBuffer->getCommandBufferVk(), "DynamicDiffuseGI::blendTextures");
        // Blend textures
        blendTextures();
    }

    // Flatten final textures (within the same command buffer)
    if (m_IrradianceTextureFlattened) {
        RAPTURE_PROFILE_SCOPE("Flattening irradiance texture");
        m_IrradianceTextureFlattened->update(m_CommandBuffer);
    }
    if (m_DistanceTextureFlattened) {
        RAPTURE_PROFILE_SCOPE("Flattening distance texture");
        m_DistanceTextureFlattened->update(m_CommandBuffer);
    }

    RAPTURE_PROFILE_GPU_COLLECT(m_CommandBuffer->getCommandBufferVk());


    // End command buffer
    if (vkEndCommandBuffer(m_CommandBuffer->getCommandBufferVk()) != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::populateProbesCompute - Failed to end command buffer");
        return;
    }
    
    // Submit command buffer (single submit for all operations)
    m_computeQueue->submitQueue(m_CommandBuffer);

    // Toggle frame flag for double buffering
    m_isEvenFrame = !m_isEvenFrame;
    
    // Mark that first frame is complete
    m_isFirstFrame = false;

}

void DynamicDiffuseGI::updateSkybox(std::shared_ptr<Scene> scene) {
    SkyboxComponent* skyboxComp = scene->getSkyboxComponent();
    std::shared_ptr<Texture> newTexture = (skyboxComp && skyboxComp->skyboxTexture && skyboxComp->skyboxTexture->isReadyForSampling()) 
                                            ? skyboxComp->skyboxTexture 
                                            : s_defaultSkyboxTexture;

    RAPTURE_PROFILE_FUNCTION();

    if (m_skyboxTexture != newTexture) {
        m_skyboxTexture = newTexture;
        // Skybox texture doesn't need special handling in the new system
        // since it's accessed via bindless
    }
}



std::shared_ptr<Texture> DynamicDiffuseGI::getRadianceTexture() {

    if (m_isEvenFrame) {
        return m_RadianceTexture;
    } else {
        return m_PrevRadianceTexture;
    }

}
std::shared_ptr<Texture> DynamicDiffuseGI::getPrevRadianceTexture() {

    if (m_isEvenFrame) {
        return m_PrevRadianceTexture;
    } else {
        return m_RadianceTexture;
    }
}

std::shared_ptr<Texture> DynamicDiffuseGI::getVisibilityTexture() {
    if (m_isEvenFrame) {
        return m_VisibilityTexture;
    } else {
        return m_PrevVisibilityTexture;
    }
}

std::shared_ptr<Texture> DynamicDiffuseGI::getPrevVisibilityTexture() {
    if (m_isEvenFrame) {
        return m_PrevVisibilityTexture;
    } else {
        return m_VisibilityTexture;
    }
}



void DynamicDiffuseGI::castRays(std::shared_ptr<Scene> scene) {

    RAPTURE_PROFILE_FUNCTION();


    // Get TLAS from scene
    const auto& tlas = scene->getTLAS();
    if (!tlas.isBuilt()) {
        RP_CORE_WARN("DynamicDiffuseGI::castRays - Scene TLAS is not built");
        return;
    }


    // === BARRIER PHASE 1: Prepare for trace shader (3 dependencies) ===
    std::vector<VkImageMemoryBarrier> preTraceBarriers;
    
    // 1. Transition ray data texture to general layout for storage image access
    VkImageMemoryBarrier rayDataWriteBarrier = m_RayDataTexture->getImageMemoryBarrier(
        m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_IMAGE_LAYOUT_GENERAL, 
        m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT, 
        VK_ACCESS_SHADER_WRITE_BIT);

    preTraceBarriers.push_back(rayDataWriteBarrier);

    // 2. Ensure previous radiance texture is ready for reading
    VkImageMemoryBarrier prevRadianceReadBarrier = (m_isEvenFrame ? m_PrevRadianceTexture : m_RadianceTexture)->getImageMemoryBarrier(
        m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT, 
        VK_ACCESS_SHADER_READ_BIT);

    preTraceBarriers.push_back(prevRadianceReadBarrier);

    // 3. Ensure previous visibility texture is ready for reading
    VkImageMemoryBarrier prevVisibilityReadBarrier = (m_isEvenFrame ? m_PrevVisibilityTexture : m_VisibilityTexture)->getImageMemoryBarrier(
        m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT, 
        VK_ACCESS_SHADER_READ_BIT);

    preTraceBarriers.push_back(prevVisibilityReadBarrier);
    
    vkCmdPipelineBarrier(
        m_CommandBuffer->getCommandBufferVk(),
        m_isFirstFrame ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(preTraceBarriers.size()), preTraceBarriers.data()
    );

    // Bind the compute pipeline
    m_DDGI_ProbeTracePipeline->bind(m_CommandBuffer->getCommandBufferVk());

    // Use the new descriptor manager system
    // Set 0: Common resources (camera, lights, shadows, probe volume)
    DescriptorManager::bindSet(0, m_CommandBuffer, m_DDGI_ProbeTracePipeline);
    
    // Set 1: Material resources (not used in DDGI)
    // Set 2: Object/Mesh resources (mesh data SSBO)
    DescriptorManager::bindSet(2, m_CommandBuffer, m_DDGI_ProbeTracePipeline);
    
    // Set 3: Bindless arrays
    DescriptorManager::bindSet(3, m_CommandBuffer, m_DDGI_ProbeTracePipeline);

    // Set push constants with texture and buffer indices
    DDGITracePushConstants pushConstants = {};
    pushConstants.sunLightDataIndex = getSunLightDataIndex(scene);
    pushConstants.skyboxTextureIndex = m_skyboxTexture ? m_skyboxTexture->getBindlessIndex() : 0;
    pushConstants.tlasIndex = tlas.getBindlessIndex();
    pushConstants.prevRadianceIndex = m_isEvenFrame ? m_prevProbeIrradianceBindlessIndex : m_probeIrradianceBindlessIndex;
    pushConstants.prevVisibilityIndex = m_isEvenFrame ? m_prevProbeVisibilityBindlessIndex : m_probeVisibilityBindlessIndex;
    
    vkCmdPushConstants(m_CommandBuffer->getCommandBufferVk(),
                       m_DDGI_ProbeTracePipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(DDGITracePushConstants), &pushConstants);

    // Dispatch the compute shader
    // Workgroup size is 16x16x1 based on shader, dispatch based on probe grid dimensions
    uint32_t workGroupsX = m_ProbeVolume.gridDimensions.x;
    uint32_t workGroupsY = m_ProbeVolume.gridDimensions.z; 
    uint32_t workGroupsZ = m_ProbeVolume.gridDimensions.y;
    
    vkCmdDispatch(m_CommandBuffer->getCommandBufferVk(), workGroupsX, workGroupsY, workGroupsZ);

    // === BARRIER PHASE 2: After trace shader - transition ray data for reading ===
    VkImageMemoryBarrier rayDataReadBarrier = m_RayDataTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_GENERAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_SHADER_WRITE_BIT, 
        VK_ACCESS_SHADER_READ_BIT);

    
    vkCmdPipelineBarrier(
        m_CommandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &rayDataReadBarrier
    );

}

void DynamicDiffuseGI::blendTextures() {

    RAPTURE_PROFILE_FUNCTION();

    // === BARRIER PHASE 5: Prepare for blending shaders ===
    // Even though blending shaders are not implemented, prepare the barriers for when they are
    std::vector<VkImageMemoryBarrier> preBlendingBarriers;
    
    // Ensure ray data is in shader read mode (should already be done)
    // Ensure previous textures are in shader read mode (should already be done)
    // Transition current textures to storage image mode for blending output
    VkImageMemoryBarrier currentRadianceWriteBarrier = (m_isEvenFrame ? m_RadianceTexture : m_PrevRadianceTexture)->getImageMemoryBarrier(
        m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_IMAGE_LAYOUT_GENERAL, 
        m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT, 
        VK_ACCESS_SHADER_WRITE_BIT);

    preBlendingBarriers.push_back(currentRadianceWriteBarrier);
    
    VkImageMemoryBarrier currentVisibilityWriteBarrier = (m_isEvenFrame ? m_VisibilityTexture : m_PrevVisibilityTexture)->getImageMemoryBarrier(
        m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_IMAGE_LAYOUT_GENERAL, 
        m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT, 
        VK_ACCESS_SHADER_WRITE_BIT);

    preBlendingBarriers.push_back(currentVisibilityWriteBarrier);
    
    vkCmdPipelineBarrier(
        m_CommandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // Wait for flatten to finish
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // Prepare for blending operations
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(preBlendingBarriers.size()), preBlendingBarriers.data()
    );

    // Irradiance blending shader
    m_DDGI_ProbeIrradianceBlendingPipeline->bind(m_CommandBuffer->getCommandBufferVk());

    // Use the new descriptor manager system for blending shaders
    DescriptorManager::bindSet(0, m_CommandBuffer, m_DDGI_ProbeIrradianceBlendingPipeline); // probe volume
    DescriptorManager::bindSet(3, m_CommandBuffer, m_DDGI_ProbeIrradianceBlendingPipeline); // bindless

    // Set push constants for radiance blending
    DDGIBlendPushConstants radianceBlendConstants = {};
    radianceBlendConstants.prevTextureIndex = m_isEvenFrame ? m_prevProbeIrradianceBindlessIndex : m_probeIrradianceBindlessIndex;
    radianceBlendConstants.rayDataIndex = m_RayDataTexture->getBindlessIndex();
    radianceBlendConstants.writeToAlternateTexture = m_isEvenFrame ? 0 : 1; // 0 = write to primary (RadianceTexture), 1 = write to alternate (PrevRadianceTexture)
    
    vkCmdPushConstants(m_CommandBuffer->getCommandBufferVk(),
                       m_DDGI_ProbeIrradianceBlendingPipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(DDGIBlendPushConstants), &radianceBlendConstants);

    vkCmdDispatch(m_CommandBuffer->getCommandBufferVk(), 
                 m_ProbeVolume.gridDimensions.x, 
                 m_ProbeVolume.gridDimensions.z, 
                 m_ProbeVolume.gridDimensions.y);

    // Distance blending shader
    m_DDGI_ProbeDistanceBlendingPipeline->bind(m_CommandBuffer->getCommandBufferVk());

    DescriptorManager::bindSet(0, m_CommandBuffer, m_DDGI_ProbeDistanceBlendingPipeline); // probe volume
    DescriptorManager::bindSet(3, m_CommandBuffer, m_DDGI_ProbeDistanceBlendingPipeline); // bindless

    // Set push constants for visibility blending
    DDGIBlendPushConstants visibilityBlendConstants = {};
    visibilityBlendConstants.prevTextureIndex = m_isEvenFrame ? m_prevProbeVisibilityBindlessIndex : m_probeVisibilityBindlessIndex;
    visibilityBlendConstants.rayDataIndex = m_RayDataTexture->getBindlessIndex();
    visibilityBlendConstants.writeToAlternateTexture = m_isEvenFrame ? 0 : 1; // 0 = write to primary (VisibilityTexture), 1 = write to alternate (PrevVisibilityTexture)
    
    vkCmdPushConstants(m_CommandBuffer->getCommandBufferVk(),
                       m_DDGI_ProbeDistanceBlendingPipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(DDGIBlendPushConstants), &visibilityBlendConstants);

    vkCmdDispatch(m_CommandBuffer->getCommandBufferVk(), 
                 m_ProbeVolume.gridDimensions.x, 
                 m_ProbeVolume.gridDimensions.z, 
                 m_ProbeVolume.gridDimensions.y);

    // === BARRIER PHASE 6: After blending shaders - transition to shader read ===
    // Transition current textures to shader read mode for next frame and final lighting
    std::vector<VkImageMemoryBarrier> postBlendingBarriers;
    
    VkImageMemoryBarrier currentRadianceReadBarrier = (m_isEvenFrame ? m_RadianceTexture : m_PrevRadianceTexture)->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_GENERAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_SHADER_WRITE_BIT, 
        VK_ACCESS_SHADER_READ_BIT);


    postBlendingBarriers.push_back(currentRadianceReadBarrier);
    
    VkImageMemoryBarrier currentVisibilityReadBarrier = (m_isEvenFrame ? m_VisibilityTexture : m_PrevVisibilityTexture)->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_GENERAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_ACCESS_SHADER_WRITE_BIT, 
        VK_ACCESS_SHADER_READ_BIT);

    postBlendingBarriers.push_back(currentVisibilityReadBarrier);
    
    vkCmdPipelineBarrier(
        m_CommandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // After blending operations
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // For next frame and final lighting
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(postBlendingBarriers.size()), postBlendingBarriers.data()
    );

}



void DynamicDiffuseGI::initTextures(){

  TextureSpecification irradianceSpec;
  irradianceSpec.width = m_ProbeVolume.gridDimensions.x * m_ProbeVolume.probeNumIrradianceInteriorTexels;
  irradianceSpec.height = m_ProbeVolume.gridDimensions.z * m_ProbeVolume.probeNumIrradianceInteriorTexels;
  irradianceSpec.depth = m_ProbeVolume.gridDimensions.y;
  irradianceSpec.type = TextureType::TEXTURE2D_ARRAY;
  irradianceSpec.format = TextureFormat::R11G11B10F;
  irradianceSpec.filter = TextureFilter::Linear;
  irradianceSpec.storageImage = true;
  irradianceSpec.wrap = TextureWrap::ClampToEdge;
  irradianceSpec.srgb = false;

  TextureSpecification distanceSpec;
  distanceSpec.width = m_ProbeVolume.gridDimensions.x * m_ProbeVolume.probeNumDistanceInteriorTexels;
  distanceSpec.height = m_ProbeVolume.gridDimensions.z * m_ProbeVolume.probeNumDistanceInteriorTexels;
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

  // Create the textures
  m_RayDataTexture = std::make_shared<Texture>(rayDataSpec);

  m_RadianceTexture = std::make_shared<Texture>(irradianceSpec);
  m_VisibilityTexture = std::make_shared<Texture>(distanceSpec);
  
  // Previous frame textures for temporal blending
  m_PrevRadianceTexture = std::make_shared<Texture>(irradianceSpec);
  m_PrevVisibilityTexture = std::make_shared<Texture>(distanceSpec);
  
  // Create flattened textures using TextureFlattener
  m_RayDataTextureFlattened = TextureFlattener::createFlattenTexture(m_RayDataTexture, "[DDGI] Flattened Ray Data Texture");
  m_IrradianceTextureFlattened = TextureFlattener::createFlattenTexture(m_RadianceTexture, "[DDGI] Irradiance Flattened Texture");
  m_DistanceTextureFlattened = TextureFlattener::createFlattenTexture(m_VisibilityTexture, "[DDGI] Distance Flattened Texture");

  clearTextures();

}

void DynamicDiffuseGI::updateProbeVolume() {

    RAPTURE_PROFILE_FUNCTION();

    if (!m_ProbeInfoBuffer) {
        RP_CORE_ERROR("DynamicDiffuseGI::updateProbeVolume - Probe info buffer not initialized");
        return;
    }

    if (!m_isVolumeDirty) {
        return;
    }

    m_ProbeInfoBuffer->addDataGPU(&m_ProbeVolume, sizeof(ProbeVolume), 0);
    m_isVolumeDirty = false;

}

void DynamicDiffuseGI::initProbeInfoBuffer() {

        ProbeVolume probeVolume;

        probeVolume.origin = glm::vec3(-0.4f, 5.4f, -0.25f);
        
        probeVolume.rotation = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        probeVolume.probeRayRotation = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        
        probeVolume.spacing = glm::vec3(1.02f, 1.5f, 1.02f);
        probeVolume.gridDimensions = glm::uvec3(24, 12, 24);
        
        probeVolume.probeNumRays = 256;
        probeVolume.probeNumIrradianceInteriorTexels = 8;
        probeVolume.probeNumDistanceInteriorTexels = 16;

        probeVolume.probeHysteresis = 0.93f;
        probeVolume.probeMaxRayDistance = 100000.0f;
        probeVolume.probeNormalBias = 0.1f;
        probeVolume.probeViewBias = 0.1f;
        probeVolume.probeDistanceExponent = 10.0f;
        probeVolume.probeIrradianceEncodingGamma = 2.2f;

        probeVolume.probeBrightnessThreshold = 0.1f;

        probeVolume.probeMinFrontfaceDistance = 0.1f;
    
        probeVolume.probeRandomRayBackfaceThreshold = 0.1f;
        probeVolume.probeFixedRayBackfaceThreshold = 0.25f;

        probeVolume.probeRelocationEnabled = 1.0f;
        probeVolume.probeClassificationEnabled = 1.0f;
        probeVolume.probeChangeThreshold = 0.1f;
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
                RP_CORE_INFO("DDGI: Added probe volume UBO to descriptor set 0, binding 5");
            } else {
                RP_CORE_ERROR("DDGI: Failed to get uniform buffer binding for probe info");
            }
        } else {
            RP_CORE_ERROR("DDGI: Failed to get descriptor set for probe info");
        }

}

}

