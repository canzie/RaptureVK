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

std::shared_ptr<Texture> DynamicDiffuseGI::s_defaultSkyboxTexture = nullptr;

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
    m_isEvenFrame(true),
    m_isFirstFrame(true),
    m_DDGI_ProbeTraceShader(nullptr),
    m_DDGI_ProbeIrradianceBlendingShader(nullptr),
    m_DDGI_ProbeDistanceBlendingShader(nullptr),
    m_Flatten2dArrayShader(nullptr),
    m_DDGI_ProbeTracePipeline(nullptr),
    m_DDGI_ProbeIrradianceBlendingPipeline(nullptr),
    m_DDGI_ProbeDistanceBlendingPipeline(nullptr),
    m_Flatten2dArrayPipeline(nullptr),
    m_MeshInfoBuffer(nullptr),
    m_SunLightBuffer(nullptr),
    m_skyboxTexture(nullptr) {

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
    initializeSunProperties();


    
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

    DescriptorSetBindings uboBindings;
    // just take any shader where the probe volume is the only one in a set
    uboBindings.layout = m_DDGI_ProbeIrradianceBlendingShader->getDescriptorSetLayouts()[1]; 
    
    // Binding 0: ProbeInfo UBO  
    DescriptorSetBinding probeInfoBinding;
    probeInfoBinding.binding = 0;
    probeInfoBinding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    probeInfoBinding.count = 1;
    probeInfoBinding.resource = m_ProbeInfoBuffer;
    uboBindings.bindings.push_back(probeInfoBinding);

    m_probeVolumeDescriptorSet = std::make_shared<DescriptorSet>(uboBindings);
        

    createProbeTraceDescriptorSets(scene);
    createProbeBlendingDescriptorSets(scene, true);
    createProbeBlendingDescriptorSets(scene, false);

}

void DynamicDiffuseGI::createProbeTraceDescriptorSets(std::shared_ptr<Scene> scene) {

    m_rayTraceDescriptorSets.clear();

    // Set 0: Textures, TLAS, and SSBO descriptors (without previous textures)
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
        
        DescriptorSetBinding skyboxBinding;
        skyboxBinding.binding = 3;
        skyboxBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        skyboxBinding.count = 1;
        skyboxBinding.viewType = TextureViewType::DEFAULT;
        skyboxBinding.resource = m_skyboxTexture;
        resourceBindings.bindings.push_back(skyboxBinding);
        

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

    // Set 2: Previous textures - create two versions for frame alternation
    if (m_DDGI_ProbeTraceShader && m_DDGI_ProbeTraceShader->getDescriptorSetLayouts().size() > 2) {
        // Set 2 for when m_isEvenFrame = true (previous = PrevRadianceTexture)
        DescriptorSetBindings evenFrameBindings;
        evenFrameBindings.layout = m_DDGI_ProbeTraceShader->getDescriptorSetLayouts()[2];
        
        // Binding 0: Previous probe irradiance texture
        DescriptorSetBinding prevRadianceBinding;
        prevRadianceBinding.binding = 0;
        prevRadianceBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        prevRadianceBinding.count = 1;
        prevRadianceBinding.viewType = TextureViewType::DEFAULT;
        prevRadianceBinding.useStorageImageInfo = false;
        prevRadianceBinding.resource = m_PrevRadianceTexture;
        evenFrameBindings.bindings.push_back(prevRadianceBinding);
        
        // Binding 1: Previous probe distance texture
        DescriptorSetBinding prevVisibilityBinding;
        prevVisibilityBinding.binding = 1;
        prevVisibilityBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        prevVisibilityBinding.count = 1;
        prevVisibilityBinding.viewType = TextureViewType::DEFAULT;
        prevVisibilityBinding.useStorageImageInfo = false;
        prevVisibilityBinding.resource = m_PrevVisibilityTexture;
        evenFrameBindings.bindings.push_back(prevVisibilityBinding);
        
        m_rayTracePrevTextureDescriptorSet[0] = std::make_shared<DescriptorSet>(evenFrameBindings);

        // Set 2 for when m_isEvenFrame = false (previous = RadianceTexture)
        DescriptorSetBindings oddFrameBindings;
        oddFrameBindings.layout = m_DDGI_ProbeTraceShader->getDescriptorSetLayouts()[2];
        
        // Binding 0: Previous probe irradiance texture
        prevRadianceBinding.resource = m_RadianceTexture;
        oddFrameBindings.bindings.push_back(prevRadianceBinding);
        
        // Binding 1: Previous probe distance texture  
        prevVisibilityBinding.resource = m_VisibilityTexture;
        oddFrameBindings.bindings.push_back(prevVisibilityBinding);
        
        m_rayTracePrevTextureDescriptorSet[1] = std::make_shared<DescriptorSet>(oddFrameBindings);
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

        // Binding 0: Input texture array (sampler2DArray) - this is the ray data texture array
        DescriptorSetBinding flattenInputBinding;
        flattenInputBinding.binding = 0;
        flattenInputBinding.viewType = TextureViewType::DEFAULT;
        flattenInputBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        flattenInputBinding.resource = m_RayDataTexture;
        flattenInputBinding.useStorageImageInfo = false;
        flattenResourceBindings.bindings.push_back(flattenInputBinding);

        // Binding 1: Output flattened texture (storage image2D) - this is the flattened output
        DescriptorSetBinding flattenOutputBinding;
        flattenOutputBinding.binding = 1;
        flattenOutputBinding.viewType = TextureViewType::DEFAULT;
        flattenOutputBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        flattenOutputBinding.resource = m_RayDataTextureFlattened;
        flattenOutputBinding.useStorageImageInfo = true;
        flattenResourceBindings.bindings.push_back(flattenOutputBinding);

        m_flattenDescriptorSet[0] = std::make_shared<DescriptorSet>(flattenResourceBindings);

        // Irradiance Flattening
        flattenResourceBindings.bindings.clear();
        flattenInputBinding.binding = 0;
        flattenInputBinding.viewType = TextureViewType::DEFAULT;
        flattenInputBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        flattenInputBinding.resource = m_RadianceTexture;
        flattenInputBinding.useStorageImageInfo = false;
        flattenResourceBindings.bindings.push_back(flattenInputBinding);

        // Binding 1: Output flattened texture (storage image2D) - this is the flattened output
        flattenOutputBinding.binding = 1;
        flattenOutputBinding.viewType = TextureViewType::DEFAULT;
        flattenOutputBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        flattenOutputBinding.resource = m_IrradianceTextureFlattened;
        flattenOutputBinding.useStorageImageInfo = true;
        flattenResourceBindings.bindings.push_back(flattenOutputBinding);

        m_flattenDescriptorSet[1] = std::make_shared<DescriptorSet>(flattenResourceBindings);


        // Distance Flattening
        flattenResourceBindings.bindings.clear();
        flattenInputBinding.binding = 0;
        flattenInputBinding.viewType = TextureViewType::DEFAULT;
        flattenInputBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        flattenInputBinding.resource = m_VisibilityTexture;
        flattenInputBinding.useStorageImageInfo = false;
        flattenResourceBindings.bindings.push_back(flattenInputBinding);

        // Binding 1: Output flattened texture (storage image2D) - this is the flattened output
        flattenOutputBinding.binding = 1;
        flattenOutputBinding.viewType = TextureViewType::DEFAULT;
        flattenOutputBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        flattenOutputBinding.resource = m_DistanceTextureFlattened;
        flattenOutputBinding.useStorageImageInfo = true;
        flattenResourceBindings.bindings.push_back(flattenOutputBinding);

        m_flattenDescriptorSet[2] = std::make_shared<DescriptorSet>(flattenResourceBindings);

    }

}

void DynamicDiffuseGI::createProbeBlendingDescriptorSets(std::shared_ptr<Scene> scene, bool isEvenFrame) {

    int descriptorSetIndex = isEvenFrame ? 0 : 1;

    // Set 0: Textures, TLAS, and SSBO descriptors
    if (m_DDGI_ProbeIrradianceBlendingShader && m_DDGI_ProbeIrradianceBlendingShader->getDescriptorSetLayouts().size() > 0) {
        DescriptorSetBindings resourceBindings;
        resourceBindings.layout = m_DDGI_ProbeIrradianceBlendingShader->getDescriptorSetLayouts()[0];
        
        // Binding 0: RayData output texture (storage image)
        DescriptorSetBinding rayDataBinding;
        rayDataBinding.binding = 0;
        rayDataBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        rayDataBinding.count = 1;
        rayDataBinding.viewType = TextureViewType::DEFAULT;
        rayDataBinding.resource = m_RayDataTexture;
        rayDataBinding.useStorageImageInfo = false;
        resourceBindings.bindings.push_back(rayDataBinding);
        
        // Binding 1: Previous probe irradiance texture
        DescriptorSetBinding irradianceBinding;
        irradianceBinding.binding = 1;
        irradianceBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        irradianceBinding.count = 1;
        irradianceBinding.viewType = TextureViewType::DEFAULT;
        irradianceBinding.useStorageImageInfo = true;
        if (isEvenFrame) {
            irradianceBinding.resource = m_RadianceTexture;
        } else {
            irradianceBinding.resource = m_PrevRadianceTexture;
        }
        
        resourceBindings.bindings.push_back(irradianceBinding);
        
        // Binding 2: Previous probe irradiance texture (for hysteresis)
        DescriptorSetBinding prevIrradianceBinding;
        prevIrradianceBinding.binding = 2;
        prevIrradianceBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        prevIrradianceBinding.count = 1;
        prevIrradianceBinding.viewType = TextureViewType::DEFAULT;
        prevIrradianceBinding.useStorageImageInfo = false;
        if (isEvenFrame) {
            prevIrradianceBinding.resource = m_PrevRadianceTexture; 
        } else {
            prevIrradianceBinding.resource = m_RadianceTexture;      
        }
        resourceBindings.bindings.push_back(prevIrradianceBinding);

        m_IrradianceBlendingDescriptorSet[descriptorSetIndex] = std::make_shared<DescriptorSet>(resourceBindings);
    
    }

    // Set 0: Textures, TLAS, and SSBO descriptors
    if (m_DDGI_ProbeDistanceBlendingShader && m_DDGI_ProbeDistanceBlendingShader->getDescriptorSetLayouts().size() > 0) {
        DescriptorSetBindings resourceBindings;
        resourceBindings.layout = m_DDGI_ProbeDistanceBlendingShader->getDescriptorSetLayouts()[0];
        
        // Binding 0: RayData output texture (storage image)
        DescriptorSetBinding rayDataBinding;
        rayDataBinding.binding = 0;
        rayDataBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        rayDataBinding.count = 1;
        rayDataBinding.viewType = TextureViewType::DEFAULT;
        rayDataBinding.resource = m_RayDataTexture;
        rayDataBinding.useStorageImageInfo = false;
        resourceBindings.bindings.push_back(rayDataBinding);
        
        // Binding 1: Previous probe irradiance texture
        DescriptorSetBinding distanceBinding;
        distanceBinding.binding = 1;
        distanceBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        distanceBinding.count = 1;
        distanceBinding.viewType = TextureViewType::DEFAULT;
        distanceBinding.useStorageImageInfo = true;
        if (isEvenFrame) {
            distanceBinding.resource = m_VisibilityTexture;
        } else {
            distanceBinding.resource = m_PrevVisibilityTexture;
        }
        
        resourceBindings.bindings.push_back(distanceBinding);
        
        // Binding 2: Previous probe distance texture
        DescriptorSetBinding prevDistanceBinding;
        prevDistanceBinding.binding = 2;
        prevDistanceBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        prevDistanceBinding.count = 1;
        prevDistanceBinding.viewType = TextureViewType::DEFAULT;
        prevDistanceBinding.useStorageImageInfo = false;
        if (isEvenFrame) {
            prevDistanceBinding.resource = m_PrevVisibilityTexture;
        } else {
            prevDistanceBinding.resource = m_VisibilityTexture;
        }
        resourceBindings.bindings.push_back(prevDistanceBinding);

        m_DistanceBlendingDescriptorSet[descriptorSetIndex] = std::make_shared<DescriptorSet>(resourceBindings);
    }


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
    
    VkImageMemoryBarrier radianceTransition{};
    radianceTransition.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    radianceTransition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    radianceTransition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    radianceTransition.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    radianceTransition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    radianceTransition.image = m_RadianceTexture->getImage();
    radianceTransition.subresourceRange = subresourceRange;
    radianceTransition.srcAccessMask = 0;
    radianceTransition.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    layoutTransitions.push_back(radianceTransition);
    
    VkImageMemoryBarrier prevRadianceTransition{};
    prevRadianceTransition.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    prevRadianceTransition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    prevRadianceTransition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    prevRadianceTransition.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    prevRadianceTransition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    prevRadianceTransition.image = m_PrevRadianceTexture->getImage();
    prevRadianceTransition.subresourceRange = subresourceRange;
    prevRadianceTransition.srcAccessMask = 0;
    prevRadianceTransition.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    layoutTransitions.push_back(prevRadianceTransition);
    
    VkImageMemoryBarrier visibilityTransition{};
    visibilityTransition.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    visibilityTransition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    visibilityTransition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    visibilityTransition.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    visibilityTransition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    visibilityTransition.image = m_VisibilityTexture->getImage();
    visibilityTransition.subresourceRange = subresourceRange;
    visibilityTransition.srcAccessMask = 0;
    visibilityTransition.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    layoutTransitions.push_back(visibilityTransition);
    
    VkImageMemoryBarrier prevVisibilityTransition{};
    prevVisibilityTransition.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    prevVisibilityTransition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    prevVisibilityTransition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    prevVisibilityTransition.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    prevVisibilityTransition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    prevVisibilityTransition.image = m_PrevVisibilityTexture->getImage();
    prevVisibilityTransition.subresourceRange = subresourceRange;
    prevVisibilityTransition.srcAccessMask = 0;
    prevVisibilityTransition.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
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
                auto albedoTexture = material->getParameter(ParameterID::ALBEDO_MAP).asTexture();
                auto normalTexture = material->getParameter(ParameterID::NORMAL_MAP).asTexture();
                auto metallicRoughnessTexture = material->getParameter(ParameterID::METALLIC_ROUGHNESS_MAP).asTexture();

                if (albedoTexture) {
                    meshinfo.AlbedoTextureIndex = albedoTexture->getBindlessIndex();
                }
                if (normalTexture) {
                    meshinfo.NormalTextureIndex = normalTexture->getBindlessIndex();
                }
                if (metallicRoughnessTexture) {
                    meshinfo.MetallicRoughnessTextureIndex = metallicRoughnessTexture->getBindlessIndex();
                }
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
    }
    
    updateSunProperties(scene);
    updateSkybox(scene);

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    if (vkBeginCommandBuffer(m_CommandBuffer->getCommandBufferVk(), &beginInfo) != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::castRays - Failed to begin command buffer");
        return;
    }

    // Cast rays using compute shader
    castRays(scene);
    
    
    flattenTextures(m_RayDataTextureFlattened, m_RayDataTexture, 0);
    blendTextures();

    flattenTextures(m_IrradianceTextureFlattened, m_RadianceTexture, 1);
    flattenTextures(m_DistanceTextureFlattened, m_VisibilityTexture, 2);

    // End command buffer
    if (vkEndCommandBuffer(m_CommandBuffer->getCommandBufferVk()) != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::castRays - Failed to end command buffer");
        return;
    }
    

    // Submit command buffer
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

    if (m_skyboxTexture != newTexture) {
        m_skyboxTexture = newTexture;
        if (!m_rayTraceDescriptorSets.empty()) {
            createProbeTraceDescriptorSets(scene);
        }
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


    // Get TLAS from scene
    const auto& tlas = scene->getTLAS();
    if (!tlas.isBuilt()) {
        RP_CORE_WARN("DynamicDiffuseGI::castRays - Scene TLAS is not built");
        return;
    }


    // === BARRIER PHASE 1: Prepare for trace shader (3 dependencies) ===
    std::vector<VkImageMemoryBarrier> preTraceBarriers;
    
    // 1. Transition ray data texture to general layout for storage image access
    VkImageMemoryBarrier rayDataWriteBarrier{};
    rayDataWriteBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    rayDataWriteBarrier.oldLayout = m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    rayDataWriteBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    rayDataWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    rayDataWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    rayDataWriteBarrier.image = m_RayDataTexture->getImage();
    rayDataWriteBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rayDataWriteBarrier.subresourceRange.baseMipLevel = 0;
    rayDataWriteBarrier.subresourceRange.levelCount = 1;
    rayDataWriteBarrier.subresourceRange.baseArrayLayer = 0;
    rayDataWriteBarrier.subresourceRange.layerCount = m_ProbeVolume.gridDimensions.y;
    rayDataWriteBarrier.srcAccessMask = m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT;
    rayDataWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    preTraceBarriers.push_back(rayDataWriteBarrier);

    // 2. Ensure previous radiance texture is ready for reading
    VkImageMemoryBarrier prevRadianceReadBarrier{};
    prevRadianceReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    prevRadianceReadBarrier.oldLayout = m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    prevRadianceReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    prevRadianceReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    prevRadianceReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    prevRadianceReadBarrier.image = (m_isEvenFrame ? m_PrevRadianceTexture : m_RadianceTexture)->getImage();
    prevRadianceReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    prevRadianceReadBarrier.subresourceRange.baseMipLevel = 0;
    prevRadianceReadBarrier.subresourceRange.levelCount = 1;
    prevRadianceReadBarrier.subresourceRange.baseArrayLayer = 0;
    prevRadianceReadBarrier.subresourceRange.layerCount = m_ProbeVolume.gridDimensions.y;
    prevRadianceReadBarrier.srcAccessMask = m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT;
    prevRadianceReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    preTraceBarriers.push_back(prevRadianceReadBarrier);

    // 3. Ensure previous visibility texture is ready for reading
    VkImageMemoryBarrier prevVisibilityReadBarrier{};
    prevVisibilityReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    prevVisibilityReadBarrier.oldLayout = m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    prevVisibilityReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    prevVisibilityReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    prevVisibilityReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    prevVisibilityReadBarrier.image = (m_isEvenFrame ? m_PrevVisibilityTexture : m_VisibilityTexture)->getImage();
    prevVisibilityReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    prevVisibilityReadBarrier.subresourceRange.baseMipLevel = 0;
    prevVisibilityReadBarrier.subresourceRange.levelCount = 1;
    prevVisibilityReadBarrier.subresourceRange.baseArrayLayer = 0;
    prevVisibilityReadBarrier.subresourceRange.layerCount = m_ProbeVolume.gridDimensions.y;
    prevVisibilityReadBarrier.srcAccessMask = m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT;
    prevVisibilityReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
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

    // Create descriptor sets for the compute shader
    // Based on shader analysis:
    // Set 0: Textures, TLAS, and SSBO descriptors (RayData at 0, skybox at 3, TLAS at 4, MeshInfo at 5)
    // Set 1: UBO bindings (ProbeInfo at binding 0, SunProperties at binding 1)
    // Set 2: Previous textures (previous radiance at 0, previous visibility at 1)
    // Set 3: Bindless arrays (textures at binding 0, buffers at binding 1)

    

    std::vector<VkDescriptorSet> resourceDescriptorSetVk(m_rayTraceDescriptorSets.size());
    for (int i = 0; i < m_rayTraceDescriptorSets.size(); i++) {
        resourceDescriptorSetVk[i] = m_rayTraceDescriptorSets[i]->getDescriptorSet();
    }

    vkCmdBindDescriptorSets(m_CommandBuffer->getCommandBufferVk(),
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_DDGI_ProbeTracePipeline->getPipelineLayoutVk(),
                            0, static_cast<uint32_t>(m_rayTraceDescriptorSets.size()), resourceDescriptorSetVk.data(), 0, nullptr);

    // Set 2: Previous textures - alternate between descriptor sets based on frame
    int prevTextureSetIndex = m_isEvenFrame ? 0 : 1;
    VkDescriptorSet prevTextureDescriptorSet = m_rayTracePrevTextureDescriptorSet[prevTextureSetIndex]->getDescriptorSet();
    vkCmdBindDescriptorSets(m_CommandBuffer->getCommandBufferVk(),
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_DDGI_ProbeTracePipeline->getPipelineLayoutVk(),
                            2, 1, &prevTextureDescriptorSet, 0, nullptr);

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

    // === BARRIER PHASE 2: After trace shader - transition ray data for reading ===
    VkImageMemoryBarrier rayDataReadBarrier{};
    rayDataReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    rayDataReadBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    rayDataReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    rayDataReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    rayDataReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    rayDataReadBarrier.image = m_RayDataTexture->getImage();
    rayDataReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rayDataReadBarrier.subresourceRange.baseMipLevel = 0;
    rayDataReadBarrier.subresourceRange.levelCount = 1;
    rayDataReadBarrier.subresourceRange.baseArrayLayer = 0;
    rayDataReadBarrier.subresourceRange.layerCount = m_ProbeVolume.gridDimensions.y;
    rayDataReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    rayDataReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
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



    // === BARRIER PHASE 5: Prepare for blending shaders ===
    // Even though blending shaders are not implemented, prepare the barriers for when they are
    std::vector<VkImageMemoryBarrier> preBlendingBarriers;
    
    // Ensure ray data is in shader read mode (should already be done)
    // Ensure previous textures are in shader read mode (should already be done)
    // Transition current textures to storage image mode for blending output
    VkImageMemoryBarrier currentRadianceWriteBarrier{};
    currentRadianceWriteBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    currentRadianceWriteBarrier.oldLayout = m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    currentRadianceWriteBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    currentRadianceWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    currentRadianceWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    currentRadianceWriteBarrier.image = (m_isEvenFrame ? m_RadianceTexture : m_PrevRadianceTexture)->getImage();
    currentRadianceWriteBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    currentRadianceWriteBarrier.subresourceRange.baseMipLevel = 0;
    currentRadianceWriteBarrier.subresourceRange.levelCount = 1;
    currentRadianceWriteBarrier.subresourceRange.baseArrayLayer = 0;
    currentRadianceWriteBarrier.subresourceRange.layerCount = m_ProbeVolume.gridDimensions.y;
    currentRadianceWriteBarrier.srcAccessMask = m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT;
    currentRadianceWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    preBlendingBarriers.push_back(currentRadianceWriteBarrier);
    
    VkImageMemoryBarrier currentVisibilityWriteBarrier{};
    currentVisibilityWriteBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    currentVisibilityWriteBarrier.oldLayout = m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    currentVisibilityWriteBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    currentVisibilityWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    currentVisibilityWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    currentVisibilityWriteBarrier.image = (m_isEvenFrame ? m_VisibilityTexture : m_PrevVisibilityTexture)->getImage();
    currentVisibilityWriteBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    currentVisibilityWriteBarrier.subresourceRange.baseMipLevel = 0;
    currentVisibilityWriteBarrier.subresourceRange.levelCount = 1;
    currentVisibilityWriteBarrier.subresourceRange.baseArrayLayer = 0;
    currentVisibilityWriteBarrier.subresourceRange.layerCount = m_ProbeVolume.gridDimensions.y;
    currentVisibilityWriteBarrier.srcAccessMask = m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT;
    currentVisibilityWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
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

    int descriptorSetIndex = m_isEvenFrame ? 0 : 1;


    // Irradiance blending shader
    m_DDGI_ProbeIrradianceBlendingPipeline->bind(m_CommandBuffer->getCommandBufferVk());


    VkDescriptorSet irradianceBlendingDescriptorSet[2] = {
        m_IrradianceBlendingDescriptorSet[descriptorSetIndex]->getDescriptorSet(), 
        m_probeVolumeDescriptorSet->getDescriptorSet()};

    vkCmdBindDescriptorSets(m_CommandBuffer->getCommandBufferVk(),
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_DDGI_ProbeIrradianceBlendingPipeline->getPipelineLayoutVk(),
                            0, 2, irradianceBlendingDescriptorSet, 0, nullptr);

    vkCmdDispatch(m_CommandBuffer->getCommandBufferVk(), 
                 m_ProbeVolume.gridDimensions.x, 
                 m_ProbeVolume.gridDimensions.z, 
                 m_ProbeVolume.gridDimensions.y);

    // Distance blending shader

    m_DDGI_ProbeDistanceBlendingPipeline->bind(m_CommandBuffer->getCommandBufferVk());

    VkDescriptorSet distanceBlendingDescriptorSet[2] = {
        m_DistanceBlendingDescriptorSet[descriptorSetIndex]->getDescriptorSet(), 
        m_probeVolumeDescriptorSet->getDescriptorSet()};

    vkCmdBindDescriptorSets(m_CommandBuffer->getCommandBufferVk(),
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_DDGI_ProbeDistanceBlendingPipeline->getPipelineLayoutVk(),
                            0, 2, distanceBlendingDescriptorSet, 0, nullptr);

    vkCmdDispatch(m_CommandBuffer->getCommandBufferVk(), 
                 m_ProbeVolume.gridDimensions.x, 
                 m_ProbeVolume.gridDimensions.z, 
                 m_ProbeVolume.gridDimensions.y);

    // === BARRIER PHASE 6: After blending shaders - transition to shader read ===
    // Transition current textures to shader read mode for next frame and final lighting
    std::vector<VkImageMemoryBarrier> postBlendingBarriers;
    
    VkImageMemoryBarrier currentRadianceReadBarrier{};
    currentRadianceReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    currentRadianceReadBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    currentRadianceReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    currentRadianceReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    currentRadianceReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    currentRadianceReadBarrier.image = (m_isEvenFrame ? m_RadianceTexture : m_PrevRadianceTexture)->getImage();
    currentRadianceReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    currentRadianceReadBarrier.subresourceRange.baseMipLevel = 0;
    currentRadianceReadBarrier.subresourceRange.levelCount = 1;
    currentRadianceReadBarrier.subresourceRange.baseArrayLayer = 0;
    currentRadianceReadBarrier.subresourceRange.layerCount = m_ProbeVolume.gridDimensions.y;
    currentRadianceReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    currentRadianceReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    postBlendingBarriers.push_back(currentRadianceReadBarrier);
    
    VkImageMemoryBarrier currentVisibilityReadBarrier{};
    currentVisibilityReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    currentVisibilityReadBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    currentVisibilityReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    currentVisibilityReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    currentVisibilityReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    currentVisibilityReadBarrier.image = (m_isEvenFrame ? m_VisibilityTexture : m_PrevVisibilityTexture)->getImage();
    currentVisibilityReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    currentVisibilityReadBarrier.subresourceRange.baseMipLevel = 0;
    currentVisibilityReadBarrier.subresourceRange.levelCount = 1;
    currentVisibilityReadBarrier.subresourceRange.baseArrayLayer = 0;
    currentVisibilityReadBarrier.subresourceRange.layerCount = m_ProbeVolume.gridDimensions.y;
    currentVisibilityReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    currentVisibilityReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
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

void DynamicDiffuseGI::flattenTextures(std::shared_ptr<Texture> flatTexture, std::shared_ptr<Texture> texture, int descriptorSetIndex) {
    if (!m_Flatten2dArrayShader || !m_RayDataTexture) {
        RP_CORE_WARN("Cannot flatten textures: missing shader or ray data texture");
        return;
    }



    // === BARRIER PHASE 3: Before flatten shader - prepare flattened output texture ===
    VkImageMemoryBarrier flattenedOutputBarrier{};
    flattenedOutputBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    flattenedOutputBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    flattenedOutputBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    flattenedOutputBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    flattenedOutputBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    flattenedOutputBarrier.image = flatTexture->getImage();
    flattenedOutputBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    flattenedOutputBarrier.subresourceRange.baseMipLevel = 0;
    flattenedOutputBarrier.subresourceRange.levelCount = 1;
    flattenedOutputBarrier.subresourceRange.baseArrayLayer = 0;
    flattenedOutputBarrier.subresourceRange.layerCount = 1;
    flattenedOutputBarrier.srcAccessMask = 0;
    flattenedOutputBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(
        m_CommandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // Wait for trace shader to finish
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // This flatten operation
        0,
        0, nullptr,
        0, nullptr,
        1, &flattenedOutputBarrier
    );

    FlattenPushConstants pushConstants;
    pushConstants.layerCount = texture->getSpecification().depth;
    pushConstants.layerWidth = texture->getSpecification().width;
    pushConstants.layerHeight = texture->getSpecification().height;
    pushConstants.tilesPerRow =  static_cast<uint32_t>(ceil(sqrt(texture->getSpecification().depth)));

    m_Flatten2dArrayPipeline->bind(m_CommandBuffer->getCommandBufferVk());

    vkCmdPushConstants(m_CommandBuffer->getCommandBufferVk(),
                       m_Flatten2dArrayPipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(FlattenPushConstants), &pushConstants);
    

    // bind the m_RayDataTexture at 0 (input) and the m_RayDataTextureFlattened at 1 (output)
    VkDescriptorSet flattenDescriptorSet = m_flattenDescriptorSet[descriptorSetIndex]->getDescriptorSet();
    vkCmdBindDescriptorSets(m_CommandBuffer->getCommandBufferVk(),
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_Flatten2dArrayPipeline->getPipelineLayoutVk(),
                            0, 1, &flattenDescriptorSet, 0, nullptr);

    vkCmdDispatch(m_CommandBuffer->getCommandBufferVk(), 
                 (flatTexture->getSpecification().width + 15) / 16, 
                 (flatTexture->getSpecification().height + 15) / 16, 
                 1);

    // === BARRIER PHASE 4: After flatten shader - transition flattened texture for reading ===
    VkImageMemoryBarrier flattenedReadBarrier{};
    flattenedReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    flattenedReadBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    flattenedReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    flattenedReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    flattenedReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    flattenedReadBarrier.image = flatTexture->getImage();
    flattenedReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    flattenedReadBarrier.subresourceRange.baseMipLevel = 0;
    flattenedReadBarrier.subresourceRange.levelCount = 1;
    flattenedReadBarrier.subresourceRange.baseArrayLayer = 0;
    flattenedReadBarrier.subresourceRange.layerCount = 1;
    flattenedReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    flattenedReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(m_CommandBuffer->getCommandBufferVk(),
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &flattenedReadBarrier);

    
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
  
  // Create flattened 2D textures for visualization
  uint32_t tilesPerRow = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(m_ProbeVolume.gridDimensions.y))));
  
  TextureSpecification flattenedSpec;
  flattenedSpec.width = rayDataSpec.width * tilesPerRow;
  flattenedSpec.height = rayDataSpec.height * static_cast<uint32_t>(ceil(float(rayDataSpec.depth) / tilesPerRow));
  flattenedSpec.type = TextureType::TEXTURE2D;
  flattenedSpec.format = TextureFormat::RGBA32F;
  flattenedSpec.filter = TextureFilter::Nearest;
  flattenedSpec.storageImage = true;
  flattenedSpec.srgb = false;
  flattenedSpec.wrap = TextureWrap::ClampToEdge;
  
  m_RayDataTextureFlattened = std::make_shared<Texture>(flattenedSpec);

  TextureSpecification flattenedSpecIrradiance;
  flattenedSpecIrradiance.width = irradianceSpec.width * tilesPerRow;
  flattenedSpecIrradiance.height = irradianceSpec.height * static_cast<uint32_t>(ceil(float(irradianceSpec.depth) / tilesPerRow));
  flattenedSpecIrradiance.type = TextureType::TEXTURE2D;
  flattenedSpecIrradiance.format = TextureFormat::RGBA32F;
  flattenedSpecIrradiance.filter = TextureFilter::Nearest;
  flattenedSpecIrradiance.storageImage = true;
  flattenedSpecIrradiance.srgb = false;
  flattenedSpecIrradiance.wrap = TextureWrap::ClampToEdge;
  
  m_IrradianceTextureFlattened = std::make_shared<Texture>(flattenedSpecIrradiance);

  TextureSpecification flattenedSpecDistance;
  flattenedSpecDistance.width = distanceSpec.width * tilesPerRow;
  flattenedSpecDistance.height = distanceSpec.height * static_cast<uint32_t>(ceil(float(distanceSpec.depth) / tilesPerRow));
  flattenedSpecDistance.type = TextureType::TEXTURE2D;
  flattenedSpecDistance.format = TextureFormat::RGBA32F;
  flattenedSpecDistance.filter = TextureFilter::Nearest;
  flattenedSpecDistance.storageImage = true;
  flattenedSpecDistance.srgb = false;
  flattenedSpecDistance.wrap = TextureWrap::ClampToEdge;
  
  m_DistanceTextureFlattened = std::make_shared<Texture>(flattenedSpecDistance);

    // Register textures with asset manager for debugging
    AssetVariant distFlatVariant = m_DistanceTextureFlattened;
    std::shared_ptr<AssetVariant> distFlatVariantPtr = std::make_shared<AssetVariant>(distFlatVariant);
    AssetManager::registerVirtualAsset(distFlatVariantPtr, "[DDGI] Distance Flattened Texture", AssetType::Texture);
    
    AssetVariant irradianceFlatVariant = m_IrradianceTextureFlattened;
    std::shared_ptr<AssetVariant> irradianceFlatVariantPtr = std::make_shared<AssetVariant>(irradianceFlatVariant);
    AssetManager::registerVirtualAsset(irradianceFlatVariantPtr, "[DDGI] Irradiance Flattened Texture", AssetType::Texture);

    AssetVariant flattenedVariant = m_RayDataTextureFlattened;
    std::shared_ptr<AssetVariant> flattenedVariantPtr = std::make_shared<AssetVariant>(flattenedVariant);
    AssetManager::registerVirtualAsset(flattenedVariantPtr, "[DDGI] Flattened Ray Data Texture", AssetType::Texture);

    clearTextures();

    m_RayDataTextureFlattened->setReadyForSampling(true);
    m_DistanceTextureFlattened->setReadyForSampling(true);
    m_IrradianceTextureFlattened->setReadyForSampling(true);

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

        m_ProbeVolume = probeVolume;
        

        m_ProbeInfoBuffer = std::make_shared<UniformBuffer>(sizeof(ProbeVolume), BufferUsage::STATIC, m_allocator);
        m_ProbeInfoBuffer->addDataGPU(&probeVolume, sizeof(ProbeVolume), 0);

}

void DynamicDiffuseGI::updateSunProperties(std::shared_ptr<Scene> scene) {

    auto& reg = scene->getRegistry();
    auto MMview = reg.view<TransformComponent, LightComponent, ShadowComponent>();

    // Use default fake values for now as requested
    m_SunShadowProps.sunLightSpaceMatrix = glm::mat4(1.0f);
    m_SunShadowProps.sunDirectionWorld = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));
    m_SunShadowProps.sunColor = glm::vec3(1.0f, 1.0f, 1.0f);
    m_SunShadowProps.sunIntensity = 0.0f;
    m_SunShadowProps.sunShadowTextureArrayIndex = 0;

    for (auto ent : MMview) {
        auto [transformComp, lightComp, shadowMapComp] = MMview.get<TransformComponent, LightComponent, ShadowComponent>(ent);

        if (lightComp.type == LightType::Directional) {
            m_SunShadowProps.sunLightSpaceMatrix = shadowMapComp.shadowMap->getLightViewProjection();
            glm::quat rotationQuat = transformComp.transforms.getRotationQuat();
            m_SunShadowProps.sunDirectionWorld = glm::normalize(rotationQuat * glm::vec3(0, 0, -1));
            m_SunShadowProps.sunColor = lightComp.color;
            m_SunShadowProps.sunIntensity = lightComp.intensity;
            m_SunShadowProps.sunShadowTextureArrayIndex = shadowMapComp.shadowMap->getTextureHandle();
            break;
        }

    }


    // Update the sun light buffer
    if (!m_SunLightBuffer) {
        m_SunLightBuffer = std::make_shared<UniformBuffer>(
            sizeof(SunProperties),
            BufferUsage::STREAM,
            m_allocator,
            &m_SunShadowProps
        );
    } else {
        m_SunLightBuffer->addData(&m_SunShadowProps, sizeof(SunProperties), 0);
    }
}

void DynamicDiffuseGI::initializeSunProperties() {
    // Use default fake values for now as requested
    m_SunShadowProps = {};
    m_SunShadowProps.sunLightSpaceMatrix = glm::mat4(1.0f);
    m_SunShadowProps.sunDirectionWorld = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));
    m_SunShadowProps.sunColor = glm::vec3(1.0f, 1.0f, 1.0f);
    m_SunShadowProps.sunIntensity = 0.0f;
    m_SunShadowProps.sunShadowTextureArrayIndex = 0;


    // Update the sun light buffer
    if (!m_SunLightBuffer) {
        m_SunLightBuffer = std::make_shared<UniformBuffer>(
            sizeof(SunProperties),
            BufferUsage::STREAM,
            m_allocator,
            &m_SunShadowProps
        );
    } else {
        m_SunLightBuffer->addData(&m_SunShadowProps, sizeof(SunProperties), 0);
    }
}

}

