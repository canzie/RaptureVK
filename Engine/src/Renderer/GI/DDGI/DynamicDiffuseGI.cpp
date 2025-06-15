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
#include "Buffers/Descriptors/DescriptorArrayManager.h"


namespace Rapture {

struct FlattenPushConstants {
    int layerCount;
    int layerWidth;
    int layerHeight;
    int tilesPerRow;
};

DynamicDiffuseGI::DynamicDiffuseGI() 
  : m_ProbeInfoBuffer(nullptr),
    m_Hysteresis(0.96f),
    m_isPopulated(false),
    m_meshCount(0),
    m_isEvenFrame(false),
    m_DDGI_ProbeTraceShader(nullptr),
    m_DDGI_ProbeIrradianceBlendingShader(nullptr),
    m_DDGI_ProbeDistanceBlendingShader(nullptr),
    m_Flatten2dArrayShader(nullptr),
    m_DDGI_ProbeTracePipeline(nullptr),
    m_DDGI_ProbeIrradianceBlendingPipeline(nullptr),
    m_DDGI_ProbeDistanceBlendingPipeline(nullptr),
    m_Flatten2dArrayPipeline(nullptr),
    m_MeshInfoBuffer(nullptr),
    m_SunLightBuffer(nullptr) {

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();
    m_allocator = vc.getVmaAllocator();

    createPipelines();

    initProbeInfoBuffer();
    initTextures();
    initializeSunProperties();

    CommandPoolConfig poolConfig;
    poolConfig.name = "DDGI Command Pool";
    poolConfig.queueFamilyIndex = vc.getQueueFamilyIndices().computeFamily.value();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    auto pool = CommandPoolManager::createCommandPool(poolConfig);

    m_CommandBuffer = pool->getCommandBuffer();


    
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
    auto [flattenShader, flattenShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/Flatten2dArray.cs.glsl");

    m_DDGI_ProbeTraceShader = probeTraceShader;
    m_DDGI_ProbeIrradianceBlendingShader = probeIrradianceBlendShader;
    m_DDGI_ProbeDistanceBlendingShader = probeDistanceBlendShader;
    m_Flatten2dArrayShader = flattenShader;

    ComputePipelineConfiguration probeTraceConfig;
    probeTraceConfig.shader = m_DDGI_ProbeTraceShader;
    ComputePipelineConfiguration probeIrradianceBlendingConfig;
    probeIrradianceBlendingConfig.shader = m_DDGI_ProbeIrradianceBlendingShader;
    ComputePipelineConfiguration probeDistanceBlendingConfig;
    probeDistanceBlendingConfig.shader = m_DDGI_ProbeDistanceBlendingShader;
    ComputePipelineConfiguration flattenConfig;
    flattenConfig.shader = m_Flatten2dArrayShader;

    m_DDGI_ProbeTracePipeline = std::make_shared<ComputePipeline>(probeTraceConfig);
    m_DDGI_ProbeIrradianceBlendingPipeline = std::make_shared<ComputePipeline>(probeIrradianceBlendingConfig);
    m_DDGI_ProbeDistanceBlendingPipeline = std::make_shared<ComputePipeline>(probeDistanceBlendingConfig);
    m_Flatten2dArrayPipeline = std::make_shared<ComputePipeline>(flattenConfig);

}

void DynamicDiffuseGI::createDescriptorSets(std::shared_ptr<Scene> scene) {

    RP_CORE_TRACE("Creating descriptor sets");

    m_rayTraceDescriptorSets.clear();


    // Set 0: Textures, TLAS, and SSBO descriptors
    if (m_DDGI_ProbeTraceShader && m_DDGI_ProbeTraceShader->getDescriptorSetLayouts().size() > 0) {
        DescriptorSetBindings resourceBindings;
        resourceBindings.layout = m_DDGI_ProbeTraceShader->getDescriptorSetLayouts()[0];
        
        // Binding 0: RayData output texture (storage image)
        DescriptorSetBinding rayDataBinding;
        rayDataBinding.binding = 0;
        rayDataBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        rayDataBinding.count = 1;
        rayDataBinding.viewType = TextureViewType::DEFAULT;
        rayDataBinding.resource = m_RayDataTexture;
        rayDataBinding.useStorageImageInfo = true;
        resourceBindings.bindings.push_back(rayDataBinding);
        
        // Binding 1: Previous probe irradiance texture
        DescriptorSetBinding prevRadianceBinding;
        prevRadianceBinding.binding = 1;
        prevRadianceBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        prevRadianceBinding.count = 1;
        prevRadianceBinding.viewType = TextureViewType::DEFAULT;
        prevRadianceBinding.useStorageImageInfo = false;
        if (m_isEvenFrame) {
            prevRadianceBinding.resource = m_PrevRadianceTexture;
        } else {
            prevRadianceBinding.resource = m_RadianceTexture;
        }
        resourceBindings.bindings.push_back(prevRadianceBinding);
        
        // Binding 2: Previous probe distance texture
        DescriptorSetBinding prevVisibilityBinding;
        prevVisibilityBinding.binding = 2;
        prevVisibilityBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        prevVisibilityBinding.count = 1;
        prevVisibilityBinding.viewType = TextureViewType::DEFAULT;
        prevVisibilityBinding.useStorageImageInfo = false;
        if (m_isEvenFrame) {
            prevVisibilityBinding.resource = m_PrevVisibilityTexture;
        } else {
            prevVisibilityBinding.resource = m_VisibilityTexture;
        }
        resourceBindings.bindings.push_back(prevVisibilityBinding);

        
        // TODO: add cubemap 

        DescriptorSetBinding tlasBinding;
        tlasBinding.binding = 4;
        tlasBinding.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        tlasBinding.resource = std::ref(const_cast<TLAS&>(scene->getTLAS())); // yikes
        resourceBindings.bindings.push_back(tlasBinding);

        DescriptorSetBinding meshInfoBinding;
        meshInfoBinding.binding = 5;
        meshInfoBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        meshInfoBinding.resource = m_MeshInfoBuffer;
        resourceBindings.bindings.push_back(meshInfoBinding);
        
        m_rayTraceDescriptorSets.push_back(std::make_shared<DescriptorSet>(resourceBindings));


    }


    // Set 1: UBO descriptors
    if (m_DDGI_ProbeTraceShader && m_DDGI_ProbeTraceShader->getDescriptorSetLayouts().size() > 1) {
        DescriptorSetBindings uboBindings;
        uboBindings.layout = m_DDGI_ProbeTraceShader->getDescriptorSetLayouts()[1];
        
        // Binding 0: ProbeInfo UBO  
        DescriptorSetBinding probeInfoBinding;
        probeInfoBinding.binding = 0;
        probeInfoBinding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        probeInfoBinding.count = 1;
        probeInfoBinding.resource = m_ProbeInfoBuffer;
        uboBindings.bindings.push_back(probeInfoBinding);
        
        // Binding 1: SunProperties UBO
        DescriptorSetBinding sunPropsBinding;
        sunPropsBinding.binding = 1;
        sunPropsBinding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sunPropsBinding.count = 1;
        sunPropsBinding.resource = m_SunLightBuffer;
        uboBindings.bindings.push_back(sunPropsBinding);
        
        m_rayTraceDescriptorSets.push_back(std::make_shared<DescriptorSet>(uboBindings));

    }


    if (m_Flatten2dArrayShader && m_Flatten2dArrayShader->getDescriptorSetLayouts().size() > 0) {
        DescriptorSetBindings flattenResourceBindings;
        flattenResourceBindings.layout = m_Flatten2dArrayShader->getDescriptorSetLayouts()[0];

        DescriptorSetBinding flattenRayDataBinding;
        flattenRayDataBinding.binding = 0;
        flattenRayDataBinding.viewType = TextureViewType::DEFAULT;
        flattenRayDataBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        flattenRayDataBinding.resource = m_RayDataTextureFlattened;
        flattenRayDataBinding.useStorageImageInfo = false;
        flattenResourceBindings.bindings.push_back(flattenRayDataBinding);

        DescriptorSetBinding flattenInputRayDataBinding;
        flattenInputRayDataBinding.binding = 1;
        flattenInputRayDataBinding.viewType = TextureViewType::DEFAULT;
        flattenInputRayDataBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        flattenInputRayDataBinding.resource = m_RayDataTexture;
        flattenInputRayDataBinding.useStorageImageInfo = true;
        flattenResourceBindings.bindings.push_back(flattenInputRayDataBinding);

        m_flattenDescriptorSet = std::make_shared<DescriptorSet>(flattenResourceBindings);
    }

}

void DynamicDiffuseGI::populateProbes(std::shared_ptr<Scene> scene){

    
    auto& tlas = scene->getTLAS();
    auto& tlasInstances = tlas.getInstances();

    if (m_meshCount == tlas.getInstanceCount()) {
        return;
    }

    auto& reg = scene->getRegistry();
    auto MMview = reg.view<MaterialComponent, MeshComponent>();


    std::vector<MeshInfo> meshInfos(tlas.getInstanceCount());

    int i = 0;

    for (auto inst : tlasInstances) {

        Entity ent = Entity(inst.entityID, scene.get());

        MeshInfo& meshinfo = meshInfos[i];
        meshinfo = {};
        meshinfo.AlbedoTextureIndex = 0; // TODO
        meshinfo.NormalTextureIndex = 0; // 
        meshinfo.MetallicRoughnessTextureIndex = 0;

        // Get the mesh component and retrieve buffer indices
        if (MMview.contains(ent)) {
            auto [meshComp, materialComp] = MMview.get<MeshComponent, MaterialComponent>(ent);

            if (meshComp.mesh) {
                auto vertexBuffer = meshComp.mesh->getVertexBuffer();
                auto indexBuffer = meshComp.mesh->getIndexBuffer();

                if (vertexBuffer) {

                meshinfo.vboIndex = vertexBuffer->getBindlessIndex();

                } else {
                meshinfo.vboIndex = 0;
                }
                
                if (indexBuffer) {
                meshinfo.iboIndex = indexBuffer->getBindlessIndex();
                } else {
                meshinfo.iboIndex = 0;
                }
            } else {
                meshinfo.vboIndex = 0;
                meshinfo.iboIndex = 0;
            }
        } else {
            meshinfo.vboIndex = 0;
            meshinfo.iboIndex = 0;
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

    if (m_rayTraceDescriptorSets.empty()) {
        createDescriptorSets(scene);
    }

    m_isPopulated = true;
    RP_CORE_INFO("Populated {} mesh infos for DDGI", meshInfos.size());
}

void DynamicDiffuseGI::populateProbesCompute(std::shared_ptr<Scene> scene) {
    if (!m_isPopulated) {
        // First populate the probe data
        populateProbes(scene);
        //updateSunProperties(scene);
    }
    
    // Cast rays using compute shader
    castRays(scene);
    
    // Flatten the ray data texture for visualization
    //flattenTextures();
}

void DynamicDiffuseGI::castRays(std::shared_ptr<Scene> scene) {

    if (!m_isPopulated) {
        RP_CORE_WARN("DynamicDiffuseGI::castRays - DynamicDiffuseGI is not populated yet, please call 'populateProbes(Scene)' first");
        return;
    }

    // Get TLAS from scene
    const auto& tlas = scene->getTLAS();
    if (!tlas.isBuilt()) {
        RP_CORE_WARN("DynamicDiffuseGI::castRays - Scene TLAS is not built");
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

    // Transition ray data texture to general layout for storage image access
    VkImageMemoryBarrier rayDataBarrier{};
    rayDataBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    rayDataBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    rayDataBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    rayDataBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    rayDataBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    rayDataBarrier.image = m_RayDataTexture->getImage();
    rayDataBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rayDataBarrier.subresourceRange.baseMipLevel = 0;
    rayDataBarrier.subresourceRange.levelCount = 1;
    rayDataBarrier.subresourceRange.baseArrayLayer = 0;
    rayDataBarrier.subresourceRange.layerCount = m_ProbeVolume.gridDimensions.y; // Number of array layers
    rayDataBarrier.srcAccessMask = 0; // No prior access
    rayDataBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(
        m_CommandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &rayDataBarrier
    );

    // Bind the compute pipeline
    m_DDGI_ProbeTracePipeline->bind(m_CommandBuffer->getCommandBufferVk());

    // Create descriptor sets for the compute shader
    // Based on shader analysis:
    // Set 0: Textures, TLAS, and SSBO descriptors (RayData at 0, prev textures at 1,2, skybox at 3, TLAS at 4, MeshInfo at 5)
    // Set 1: UBO bindings (ProbeInfo at binding 0, SunProperties at binding 1)
    // Set 3: Bindless arrays (textures at binding 0, buffers at binding 1)

    

    std::vector<VkDescriptorSet> resourceDescriptorSetVk(m_rayTraceDescriptorSets.size());
    for (int i = 0; i < m_rayTraceDescriptorSets.size(); i++) {
        resourceDescriptorSetVk[i] = m_rayTraceDescriptorSets[i]->getDescriptorSet();
    }

    vkCmdBindDescriptorSets(m_CommandBuffer->getCommandBufferVk(),
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_DDGI_ProbeTracePipeline->getPipelineLayoutVk(),
                            0, m_rayTraceDescriptorSets.size(), resourceDescriptorSetVk.data(), 0, nullptr);


    // Set 3: Bindless descriptor arrays
    VkDescriptorSet bindlessDescriptorSet = DescriptorArrayManager::getUnifiedSet();
    if (bindlessDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(m_CommandBuffer->getCommandBufferVk(),
                               VK_PIPELINE_BIND_POINT_COMPUTE,
                               m_DDGI_ProbeTracePipeline->getPipelineLayoutVk(),
                               3, 1, &bindlessDescriptorSet, 0, nullptr);
    }

    // Dispatch the compute shader
    // Workgroup size is 16x16x1 based on shader, dispatch based on probe grid dimensions
    uint32_t workGroupsX = m_ProbeVolume.gridDimensions.x;
    uint32_t workGroupsY = m_ProbeVolume.gridDimensions.z; 
    uint32_t workGroupsZ = m_ProbeVolume.gridDimensions.y;
    
    vkCmdDispatch(m_CommandBuffer->getCommandBufferVk(), workGroupsX, workGroupsY, workGroupsZ);

    // Transition ray data texture for reading in subsequent operations
    VkImageMemoryBarrier postDispatchBarrier{};
    postDispatchBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    postDispatchBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    postDispatchBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    postDispatchBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postDispatchBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postDispatchBarrier.image = m_RayDataTexture->getImage();
    postDispatchBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    postDispatchBarrier.subresourceRange.baseMipLevel = 0;
    postDispatchBarrier.subresourceRange.levelCount = 1;
    postDispatchBarrier.subresourceRange.baseArrayLayer = 0;
    postDispatchBarrier.subresourceRange.layerCount = m_ProbeVolume.gridDimensions.y;
    postDispatchBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    postDispatchBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        m_CommandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &postDispatchBarrier
    );

    // End command buffer
    if (vkEndCommandBuffer(m_CommandBuffer->getCommandBufferVk()) != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::castRays - Failed to end command buffer");
        return;
    }

    // Submit command buffer
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto computeQueue = vulkanContext.getComputeQueue();
    
    computeQueue->submitQueue(m_CommandBuffer);

    // Toggle frame flag for double buffering
    m_isEvenFrame = !m_isEvenFrame;
}

void DynamicDiffuseGI::flattenTextures() {
    if (!m_Flatten2dArrayShader || !m_RayDataTexture) {
        RP_CORE_WARN("Cannot flatten textures: missing shader or ray data texture");
        return;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    if (vkBeginCommandBuffer(m_CommandBuffer->getCommandBufferVk(), &beginInfo) != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::flattenTextures - Failed to begin command buffer");
        return;
    }

    FlattenPushConstants pushConstants;
    pushConstants.layerCount = m_RayDataTexture->getSpecification().depth;
    pushConstants.layerWidth = m_RayDataTexture->getSpecification().width;
    pushConstants.layerHeight = m_RayDataTexture->getSpecification().height;
    pushConstants.tilesPerRow =  static_cast<uint32_t>(ceil(sqrt(m_RayDataTexture->getSpecification().depth)));

    m_Flatten2dArrayPipeline->bind(m_CommandBuffer->getCommandBufferVk());

    vkCmdPushConstants(m_CommandBuffer->getCommandBufferVk(),
                       m_Flatten2dArrayPipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(FlattenPushConstants), &pushConstants);
    

    // bind the m_RayDataTexture at 1 and the m_RayDataTextureFlattened at 0
    VkDescriptorSet flattenDescriptorSet = m_flattenDescriptorSet->getDescriptorSet();
    vkCmdBindDescriptorSets(m_CommandBuffer->getCommandBufferVk(),
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_Flatten2dArrayPipeline->getPipelineLayoutVk(),
                            0, 1, &flattenDescriptorSet, 0, nullptr);

    vkCmdDispatch(m_CommandBuffer->getCommandBufferVk(), 
                 m_RayDataTextureFlattened->getSpecification().width / 16, 
                 m_RayDataTextureFlattened->getSpecification().height / 16, 
                 1);


    VkImageMemoryBarrier imageBarrier{};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.image = m_RayDataTextureFlattened->getImage();
    imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.baseMipLevel = 0;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.subresourceRange.baseArrayLayer = 0;
    imageBarrier.subresourceRange.layerCount = 1;
    imageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(m_CommandBuffer->getCommandBufferVk(),
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &imageBarrier);

    if (vkEndCommandBuffer(m_CommandBuffer->getCommandBufferVk()) != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::flattenTextures - Failed to end command buffer");
        return;
    }

    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto computeQueue = vulkanContext.getComputeQueue();
    
    computeQueue->submitQueue(m_CommandBuffer);
    
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

  TextureSpecification distanceSpec;
  distanceSpec.width = m_ProbeVolume.gridDimensions.x * m_ProbeVolume.probeNumDistanceInteriorTexels;
  distanceSpec.height = m_ProbeVolume.gridDimensions.z * m_ProbeVolume.probeNumDistanceInteriorTexels;
  distanceSpec.depth = m_ProbeVolume.gridDimensions.y;
  distanceSpec.type = TextureType::TEXTURE2D_ARRAY;
  distanceSpec.format = TextureFormat::RG16F;
  distanceSpec.filter = TextureFilter::Linear;
  distanceSpec.storageImage = true;
  distanceSpec.wrap = TextureWrap::ClampToEdge;

  TextureSpecification rayDataSpec;
  rayDataSpec.width = m_ProbeVolume.gridDimensions.x * m_ProbeVolume.probeNumIrradianceInteriorTexels;
  rayDataSpec.height = m_ProbeVolume.gridDimensions.z * m_ProbeVolume.probeNumIrradianceInteriorTexels;
  rayDataSpec.depth = m_ProbeVolume.gridDimensions.y;
  rayDataSpec.type = TextureType::TEXTURE2D_ARRAY;
  rayDataSpec.format = TextureFormat::RGBA32F;
  rayDataSpec.filter = TextureFilter::Nearest;
  rayDataSpec.storageImage = true;
  rayDataSpec.wrap = TextureWrap::ClampToEdge;

  // Create the textures
  m_RadianceTexture = std::make_shared<Texture>(irradianceSpec);
  m_VisibilityTexture = std::make_shared<Texture>(distanceSpec);
  m_RayDataTexture = std::make_shared<Texture>(rayDataSpec);
  
  // Previous frame textures for temporal blending
  m_PrevRadianceTexture = std::make_shared<Texture>(irradianceSpec);
  m_PrevVisibilityTexture = std::make_shared<Texture>(distanceSpec);
  
  // Create flattened 2D textures for visualization
  uint32_t tilesPerRow = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(m_ProbeVolume.gridDimensions.y))));
  
  TextureSpecification flattenedSpec;
  flattenedSpec.width = rayDataSpec.width * tilesPerRow;
  flattenedSpec.height = rayDataSpec.height * tilesPerRow;
  flattenedSpec.type = TextureType::TEXTURE2D;
  flattenedSpec.format = TextureFormat::RGBA32F;
  flattenedSpec.filter = TextureFilter::Nearest;
  flattenedSpec.storageImage = true;
  flattenedSpec.wrap = TextureWrap::ClampToEdge;
  
  m_RayDataTextureFlattened = std::make_shared<Texture>(flattenedSpec);

  // Register textures with asset manager for debugging
  AssetVariant rayDataVariant = m_RayDataTexture;
  std::shared_ptr<AssetVariant> rayDataVariantPtr = std::make_shared<AssetVariant>(rayDataVariant);
  AssetManager::registerVirtualAsset(rayDataVariantPtr, "[DDGI] Ray Data Texture", AssetType::Texture);
  
  AssetVariant flattenedVariant = m_RayDataTextureFlattened;
  std::shared_ptr<AssetVariant> flattenedVariantPtr = std::make_shared<AssetVariant>(flattenedVariant);
  AssetManager::registerVirtualAsset(flattenedVariantPtr, "[DDGI] Flattened Ray Data Texture", AssetType::Texture);

}

void DynamicDiffuseGI::initProbeInfoBuffer() {

        ProbeVolume probeVolume;

        probeVolume.origin = glm::vec3(-0.4f, 5.4f, -0.25f);
        
        probeVolume.rotation = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        probeVolume.probeRayRotation = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        
        probeVolume.spacing = glm::vec3(1.02f, 1.5f, 1.02f);
        probeVolume.gridDimensions = glm::uvec3(24, 8, 24);
        
        probeVolume.probeNumRays = 256;
        probeVolume.probeNumIrradianceInteriorTexels = 8;
        probeVolume.probeNumDistanceInteriorTexels = 16;

        probeVolume.probeHysteresis = 0.97f;
        probeVolume.probeMaxRayDistance = 10000.0f;
        probeVolume.probeNormalBias = 0.1f;
        probeVolume.probeViewBias = 0.1f;
        probeVolume.probeDistanceExponent = 10.0f;
        probeVolume.probeIrradianceEncodingGamma = 2.2f;

        probeVolume.probeBrightnessThreshold = 0.1f;

        probeVolume.probeMinFrontfaceDistance = 0.1f;
    
        probeVolume.probeRandomRayBackfaceThreshold = 0.1f;
        probeVolume.probeFixedRayBackfaceThreshold = 0.25f;

        m_ProbeVolume = probeVolume;
        

        m_ProbeInfoBuffer = std::make_shared<UniformBuffer>(sizeof(ProbeVolume), BufferUsage::STATIC, m_allocator);
        m_ProbeInfoBuffer->addDataGPU(&probeVolume, sizeof(ProbeVolume), 0);

}

void DynamicDiffuseGI::updateSunProperties(std::shared_ptr<Scene> scene) {
    // Use default fake values for now as requested
    m_SunShadowProps.sunLightSpaceMatrix = glm::mat4(1.0f);
    m_SunShadowProps.sunDirectionWorld = glm::normalize(glm::vec3(0.5f, -1.0f, 0.3f));
    m_SunShadowProps.sunColor = glm::vec3(1.0f, 0.9f, 0.8f);
    m_SunShadowProps.sunIntensity = 3.0f;
    m_SunShadowProps.sunShadowTextureArrayIndex = 0;

    // Update the sun light buffer
    if (!m_SunLightBuffer) {
        m_SunLightBuffer = std::make_shared<UniformBuffer>(
            sizeof(SunProperties),
            BufferUsage::DYNAMIC,
            m_allocator,
            &m_SunShadowProps
        );
    } else {
        m_SunLightBuffer->addData(&m_SunShadowProps, sizeof(SunProperties), 0);
    }
}

void DynamicDiffuseGI::initializeSunProperties() {
    // Use default fake values for now as requested
    SunProperties sunShadowProps = {};
    sunShadowProps.sunLightSpaceMatrix = glm::mat4(1.0f);
    sunShadowProps.sunDirectionWorld = glm::normalize(glm::vec3(0.5f, -1.0f, 0.3f));
    sunShadowProps.sunColor = glm::vec3(1.0f, 0.9f, 0.8f);
    sunShadowProps.sunIntensity = 3.0f;
    sunShadowProps.sunShadowTextureArrayIndex = 0;

    m_SunShadowProps = sunShadowProps;

    // Update the sun light buffer
    if (!m_SunLightBuffer) {
        m_SunLightBuffer = std::make_shared<UniformBuffer>(
            sizeof(SunProperties),
            BufferUsage::DYNAMIC,
            m_allocator,
            &sunShadowProps
        );
    } else {
        m_SunLightBuffer->addData(&sunShadowProps, sizeof(SunProperties), 0);
    }
}

}

