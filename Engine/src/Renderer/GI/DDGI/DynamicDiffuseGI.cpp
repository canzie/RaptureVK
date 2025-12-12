#include "DynamicDiffuseGI.h"

#include "AssetManager/Asset.h"
#include "AssetManager/AssetManager.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorBinding.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Components/Components.h"
#include "Events/AssetEvents.h"
#include "Materials/MaterialParameters.h"
#include "Renderer/GI/DDGI/DDGICommon.h"
#include "Scenes/Entities/Entity.h"
#include "Scenes/Scene.h"
#include "Textures/Texture.h"
#include "Textures/TextureCommon.h"
#include "WindowContext/Application.h"

#include "Logging/TracyProfiler.h"

#include <cmath>
#include <glm/gtc/constants.hpp>
#include <random>

namespace Rapture {

// Push constants for DDGI compute shaders
struct DDGITracePushConstants {
    uint32_t skyboxTextureIndex;
    uint32_t sunLightDataIndex;
    uint32_t lightCount;
    uint32_t prevRadianceIndex;
    uint32_t prevVisibilityIndex;
    alignas(16) glm::vec3 cameraPosition;
    uint32_t tlasIndex;
};

struct DDGIBlendPushConstants {
    uint32_t prevTextureIndex;
    uint32_t rayDataIndex;
    uint32_t writeToAlternateTexture; // 0 = write to primary, 1 = write to alternate
};

struct DDGIClassifyPushConstants {
    uint32_t rayDataIndex;
};

struct DDGIRelocatePushConstants {
    uint32_t rayDataIndex;
};

std::shared_ptr<Texture> DynamicDiffuseGI::s_defaultSkyboxTexture = nullptr;

DynamicDiffuseGI::DynamicDiffuseGI(uint32_t framesInFlight)
    : m_DDGI_ProbeTraceShader(nullptr), m_DDGI_ProbeIrradianceBlendingShader(nullptr), m_DDGI_ProbeDistanceBlendingShader(nullptr),
      m_DDGI_ProbeRelocationShader(nullptr), m_DDGI_ProbeClassificationShader(nullptr), m_DDGI_ProbeTracePipeline(nullptr),
      m_DDGI_ProbeIrradianceBlendingPipeline(nullptr), m_DDGI_ProbeDistanceBlendingPipeline(nullptr),
      m_DDGI_ProbeRelocationPipeline(nullptr), m_DDGI_ProbeClassificationPipeline(nullptr), m_MeshInfoBuffer(nullptr),
      m_ProbeInfoBuffer(nullptr), m_framesInFlight(framesInFlight), m_isEvenFrame(true), m_isPopulated(false), m_isFirstFrame(true),
      m_meshCount(0), m_probeIrradianceBindlessIndex(UINT32_MAX), m_probeVisibilityBindlessIndex(UINT32_MAX),
      m_prevProbeIrradianceBindlessIndex(UINT32_MAX), m_prevProbeVisibilityBindlessIndex(UINT32_MAX), m_skyboxTexture(nullptr),
      m_probeTraceDescriptorSet(nullptr), m_probeIrradianceBlendingDescriptorSet(nullptr),
      m_probeDistanceBlendingDescriptorSet(nullptr), m_probeClassificationDescriptorSet(nullptr),
      m_probeRelocationDescriptorSet(nullptr)
{

    // Listen for material instance changes so we can patch the MeshInfo SSBO on-demand.
    AssetEvents::onMaterialInstanceChanged().addListener([this](MaterialInstance *mat) {
        if (mat) {
            m_dirtyMaterials.insert(mat);
        }
    });

    // Listen for mesh transform changes so we can patch the MeshInfo SSBO on-demand.
    AssetEvents::onMeshTransformChanged().addListener([this](EntityID ent) {
        if (ent) {
            m_dirtyMeshes.insert(ent);
        }
    });

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
    poolConfig.queueFamilyIndex = vc.getQueueFamilyIndices().computeFamily.value();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    auto pool = CommandPoolManager::createCommandPool(poolConfig);

    m_CommandBuffers = pool->getCommandBuffers(m_framesInFlight);

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
        m_prevProbeIrradianceBindlessIndex = m_PrevRadianceTexture->getBindlessIndex();
    }
    if (m_VisibilityTexture) {
        m_probeVisibilityBindlessIndex = m_VisibilityTexture->getBindlessIndex();
        m_prevProbeVisibilityBindlessIndex = m_PrevVisibilityTexture->getBindlessIndex();
    }
}

void DynamicDiffuseGI::updateMeshInfoBuffer(std::shared_ptr<Scene> scene)
{
    if ((m_dirtyMaterials.empty() && m_dirtyMeshes.empty()) || !m_MeshInfoBuffer) {
        return;
    }

    // Offsets inside MeshInfo struct that correspond to material parameters
    constexpr size_t MATERIAL_START_OFFSET = offsetof(MeshInfo, AlbedoTextureIndex);
    constexpr size_t MATERIAL_END_OFFSET = offsetof(MeshInfo, iboIndex); // first field after material params
    constexpr size_t MATERIAL_PARAM_SIZE = MATERIAL_END_OFFSET - MATERIAL_START_OFFSET;
    constexpr size_t TRANSFORM_OFFSET = offsetof(MeshInfo, modelMatrix);

    struct PackedParams {
        uint32_t AlbedoTextureIndex;
        uint32_t NormalTextureIndex;
        alignas(16) glm::vec3 albedo;
        alignas(16) glm::vec3 emissiveColor;
        uint32_t EmissiveFactorTextureIndex;
    };

    for (auto *mat : m_dirtyMaterials) {
        if (!mat) continue;

        // Build a small struct containing just the material-related parameters in the same layout as MeshInfo
        PackedParams params = {};

        params.AlbedoTextureIndex = mat->getParameter(ParameterID::ALBEDO_MAP).asUInt();
        params.NormalTextureIndex = mat->getParameter(ParameterID::NORMAL_MAP).asUInt();
        params.albedo = mat->getParameter(ParameterID::ALBEDO).asVec3();
        params.emissiveColor = mat->getParameter(ParameterID::EMISSIVE).asVec3();
        params.EmissiveFactorTextureIndex = UINT32_MAX;

        auto offsetsIt = m_MaterialToOffsets.find(mat);
        if (offsetsIt == m_MaterialToOffsets.end()) continue;

        for (uint32_t baseOffset : offsetsIt->second) {
            uint32_t dstOffset = baseOffset + static_cast<uint32_t>(MATERIAL_START_OFFSET);
            m_MeshInfoBuffer->addData(&params, static_cast<uint32_t>(MATERIAL_PARAM_SIZE), dstOffset);
        }
    }

    for (auto entityID : m_dirtyMeshes) {
        Entity ent = Entity(entityID, scene.get());
        if (!ent) continue;

        auto offsetIt = m_MeshToOffsets.find(entityID);
        if (offsetIt == m_MeshToOffsets.end()) continue;

        uint32_t dstOffset = offsetIt->second + static_cast<uint32_t>(TRANSFORM_OFFSET);
        glm::mat4 modelMatrix = ent.getComponent<TransformComponent>().transformMatrix();
        RP_CORE_INFO("Updating mesh info buffer for entity: {}", ent.getID());
        m_MeshInfoBuffer->addData((void *)&modelMatrix, sizeof(glm::mat4), dstOffset);
    }

    m_dirtyMeshes.clear();
    m_dirtyMaterials.clear();
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

    VkImageMemoryBarrier radianceTransition = m_RadianceTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    layoutTransitions.push_back(radianceTransition);

    VkImageMemoryBarrier prevRadianceTransition = m_PrevRadianceTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    layoutTransitions.push_back(prevRadianceTransition);

    VkImageMemoryBarrier visibilityTransition = m_VisibilityTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    layoutTransitions.push_back(visibilityTransition);

    VkImageMemoryBarrier prevVisibilityTransition = m_PrevVisibilityTexture->getImageMemoryBarrier(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    layoutTransitions.push_back(prevVisibilityTransition);

    vkCmdPipelineBarrier(m_CommandBuffers[0]->getCommandBufferVk(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(layoutTransitions.size()),
                         layoutTransitions.data());

    VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 0.0f}};
    vkCmdClearColorImage(m_CommandBuffers[0]->getCommandBufferVk(), m_RadianceTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL,
                         &clearColor, 1, &subresourceRange);

    vkCmdClearColorImage(m_CommandBuffers[0]->getCommandBufferVk(), m_PrevRadianceTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL,
                         &clearColor, 1, &subresourceRange);

    vkCmdClearColorImage(m_CommandBuffers[0]->getCommandBufferVk(), m_VisibilityTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL,
                         &clearColor, 1, &subresourceRange);

    vkCmdClearColorImage(m_CommandBuffers[0]->getCommandBufferVk(), m_PrevVisibilityTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL,
                         &clearColor, 1, &subresourceRange);

    if (m_CommandBuffers[0]->end() != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::clearTextures - Failed to end command buffer");
        return;
    }

    m_computeQueue->submitQueue(m_CommandBuffers[0]);
}

void DynamicDiffuseGI::populateProbes(std::shared_ptr<Scene> scene)
{

    RAPTURE_PROFILE_FUNCTION();

    auto tlas = scene->getTLAS();
    if (!tlas || !tlas->isBuilt() || tlas->getInstanceCount() == 0) {
        // RP_CORE_WARN("DynamicDiffuseGI::populateProbes - Scene TLAS is not built");
        return;
    }

    auto &tlasInstances = tlas->getInstances();

    if (m_meshCount == tlas->getInstanceCount()) {
        return;
    }

    auto &reg = scene->getRegistry();
    auto MMview = reg.view<MaterialComponent, MeshComponent, TransformComponent>(entt::exclude<LightComponent>);

    std::vector<MeshInfo> meshInfos(tlas->getInstanceCount());

    int i = 0;

    for (auto inst : tlasInstances) {

        Entity ent = Entity(inst.entityID, scene.get());

        MeshInfo &meshinfo = meshInfos[i];
        meshinfo = {};
        meshinfo.AlbedoTextureIndex = 0;
        meshinfo.NormalTextureIndex = 0;
        meshinfo.vboIndex = 0;
        meshinfo.iboIndex = 0;
        meshinfo.positionAttributeOffsetBytes = 0;
        meshinfo.texCoordAttributeOffsetBytes = 0;
        meshinfo.normalAttributeOffsetBytes = 0;
        meshinfo.tangentAttributeOffsetBytes = 0;
        meshinfo.vertexStrideBytes = 0;
        meshinfo.indexType = 0;
        meshinfo.albedo = glm::vec3(1.0f);
        meshinfo.EmissiveFactorTextureIndex = UINT32_MAX;
        meshinfo.emissiveColor = glm::vec3(0.0f);

        // Get the mesh component and retrieve buffer indices
        if (MMview.contains(ent)) {
            auto [meshComp, materialComp, transformComp] = MMview.get<MeshComponent, MaterialComponent, TransformComponent>(ent);

            meshinfo.modelMatrix = transformComp.transformMatrix();

            if (materialComp.material) {
                auto &material = materialComp.material;
                meshinfo.AlbedoTextureIndex = material->getParameter(ParameterID::ALBEDO_MAP).asUInt();
                meshinfo.NormalTextureIndex = material->getParameter(ParameterID::NORMAL_MAP).asUInt();

                meshinfo.albedo = material->getParameter(ParameterID::ALBEDO).asVec3();
                meshinfo.emissiveColor = material->getParameter(ParameterID::EMISSIVE).asVec3();
            }

            if (meshComp.mesh) {
                auto vertexBuffer = meshComp.mesh->getVertexBuffer();
                auto indexBuffer = meshComp.mesh->getIndexBuffer();

                if (vertexBuffer) {

                    meshinfo.vboIndex = vertexBuffer->getBindlessIndex();

                    auto &bfl = vertexBuffer->getBufferLayout();

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
        m_MeshInfoBuffer = std::make_shared<StorageBuffer>(sizeof(MeshInfo) * meshInfos.size(), BufferUsage::DYNAMIC, m_allocator,
                                                           meshInfos.data());
    } else {
        m_MeshInfoBuffer->addData(meshInfos.data(), sizeof(MeshInfo) * meshInfos.size(), 0);
    }

    // Rebuild material-offset lookup table
    m_MaterialToOffsets.clear();
    m_MeshToOffsets.clear();

    for (uint32_t idx = 0; idx < meshInfos.size(); ++idx) {
        Entity ent = Entity(tlasInstances[idx].entityID, scene.get());
        if (MMview.contains(ent)) {
            auto [meshComp, materialComp, transformComp] = MMview.get<MeshComponent, MaterialComponent, TransformComponent>(ent);
            if (materialComp.material) {
                m_MeshToOffsets[ent.getID()] = idx * static_cast<uint32_t>(sizeof(MeshInfo));
                m_MaterialToOffsets[materialComp.material.get()].push_back(idx * static_cast<uint32_t>(sizeof(MeshInfo)));
            }
        }
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

    m_isPopulated = true;
    RP_CORE_INFO("Populated {} mesh infos for DDGI", meshInfos.size());
}

void DynamicDiffuseGI::populateProbesCompute(std::shared_ptr<Scene> scene, uint32_t frameIndex)
{

    RAPTURE_PROFILE_FUNCTION();

    // Apply any material changes collected since last frame
    updateMeshInfoBuffer(scene);

    if (!m_isPopulated) {
        // First populate the probe data
        populateProbes(scene);
    }
    if (m_isVolumeDirty) {
        // Update the probe volume
        updateProbeVolume();
    }
    updateSkybox(scene);

    auto tlas = scene->getTLAS();
    if (!tlas || !tlas->isBuilt() || tlas->getInstanceCount() == 0) {
        // RP_CORE_WARN("DynamicDiffuseGI::populateProbesCompute - Scene TLAS is not built");
        return;
    }

    auto currentCommandBuffer = m_CommandBuffers[frameIndex];

    // Begin command buffer
    if (currentCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::castRays - Failed to begin command buffer");
        return;
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(currentCommandBuffer->getCommandBufferVk(), "DynamicDiffuseGI::populateProbesCompute");
        // Cast rays using compute shader
        castRays(scene, frameIndex);
    }
    // Flatten ray data texture (within the same command buffer)
    if (m_RayDataTextureFlattened) {
        RAPTURE_PROFILE_SCOPE("Flattening ray data texture");
        m_RayDataTextureFlattened->update(currentCommandBuffer);
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(currentCommandBuffer->getCommandBufferVk(), "DynamicDiffuseGI::relocateProbes");
        relocateProbes(frameIndex);
    }

    if (m_ProbeOffsetTextureFlattened) {
        RAPTURE_PROFILE_SCOPE("Flattening probe offset texture");
        m_ProbeOffsetTextureFlattened->update(currentCommandBuffer);
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(currentCommandBuffer->getCommandBufferVk(), "DynamicDiffuseGI::classifyProbes");
        classifyProbes(frameIndex);
    }

    if (m_ProbeClassificationTextureFlattened) {
        RAPTURE_PROFILE_SCOPE("Flattening probe classification texture");
        m_ProbeClassificationTextureFlattened->update(currentCommandBuffer);
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(currentCommandBuffer->getCommandBufferVk(), "DynamicDiffuseGI::blendTextures");
        // Blend textures
        blendTextures(frameIndex);
    }

    // Flatten final textures (within the same command buffer)
    if (m_IrradianceTextureFlattened) {
        RAPTURE_PROFILE_SCOPE("Flattening irradiance texture");
        m_IrradianceTextureFlattened->update(currentCommandBuffer);
    }
    if (m_DistanceTextureFlattened) {
        RAPTURE_PROFILE_SCOPE("Flattening distance texture");
        m_DistanceTextureFlattened->update(currentCommandBuffer);
    }

    RAPTURE_PROFILE_GPU_COLLECT(currentCommandBuffer->getCommandBufferVk());

    // End command buffer
    if (currentCommandBuffer->end() != VK_SUCCESS) {
        RP_CORE_ERROR("DynamicDiffuseGI::populateProbesCompute - Failed to end command buffer");
        return;
    }

    // Submit command buffer (single submit for all operations)
    m_computeQueue->submitQueue(currentCommandBuffer);

    // Toggle frame flag for double buffering
    m_isEvenFrame = !m_isEvenFrame;

    // Mark that first frame is complete
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

void DynamicDiffuseGI::updateSkybox(std::shared_ptr<Scene> scene)
{
    SkyboxComponent *skyboxComp = scene->getSkyboxComponent();
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

std::shared_ptr<Texture> DynamicDiffuseGI::getRadianceTexture()
{

    if (m_isEvenFrame) {
        return m_RadianceTexture;
    } else {
        return m_PrevRadianceTexture;
    }
}
std::shared_ptr<Texture> DynamicDiffuseGI::getPrevRadianceTexture()
{

    if (m_isEvenFrame) {
        return m_PrevRadianceTexture;
    } else {
        return m_RadianceTexture;
    }
}

std::shared_ptr<Texture> DynamicDiffuseGI::getVisibilityTexture()
{
    if (m_isEvenFrame) {
        return m_VisibilityTexture;
    } else {
        return m_PrevVisibilityTexture;
    }
}

std::shared_ptr<Texture> DynamicDiffuseGI::getPrevVisibilityTexture()
{
    if (m_isEvenFrame) {
        return m_PrevVisibilityTexture;
    } else {
        return m_VisibilityTexture;
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

    // === BARRIER PHASE 1: Prepare for trace shader (3 dependencies) ===
    std::vector<VkImageMemoryBarrier> preTraceBarriers;

    // 1. Transition ray data texture to general layout for storage image access
    VkImageMemoryBarrier rayDataWriteBarrier = m_RayDataTexture->getImageMemoryBarrier(
        m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

    preTraceBarriers.push_back(rayDataWriteBarrier);

    // 2. Ensure previous radiance texture is ready for reading
    VkImageMemoryBarrier prevRadianceReadBarrier =
        (m_isEvenFrame ? m_PrevRadianceTexture : m_RadianceTexture)
            ->getImageMemoryBarrier(m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT,
                                    VK_ACCESS_SHADER_READ_BIT);

    preTraceBarriers.push_back(prevRadianceReadBarrier);

    // 3. Ensure previous visibility texture is ready for reading
    VkImageMemoryBarrier prevVisibilityReadBarrier =
        (m_isEvenFrame ? m_PrevVisibilityTexture : m_VisibilityTexture)
            ->getImageMemoryBarrier(m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT,
                                    VK_ACCESS_SHADER_READ_BIT);

    preTraceBarriers.push_back(prevVisibilityReadBarrier);

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
    pushConstants.prevRadianceIndex = m_isEvenFrame ? m_prevProbeIrradianceBindlessIndex : m_probeIrradianceBindlessIndex;
    pushConstants.prevVisibilityIndex = m_isEvenFrame ? m_prevProbeVisibilityBindlessIndex : m_probeVisibilityBindlessIndex;

    auto camEnt = scene->getMainCamera();
    if (auto camTransform = camEnt.lock()) {
        auto &transform = camTransform->getComponent<TransformComponent>();
        pushConstants.cameraPosition = transform.translation();
    } else {
        pushConstants.cameraPosition = glm::vec3(0.0f);
    }

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
    // Even though blending shaders are not implemented, prepare the barriers for when they are
    std::vector<VkImageMemoryBarrier> preBlendingBarriers;

    // Ensure ray data is in shader read mode (should already be done)
    // Ensure previous textures are in shader read mode (should already be done)
    // Transition current textures to storage image mode for blending output
    VkImageMemoryBarrier currentRadianceWriteBarrier =
        (m_isEvenFrame ? m_RadianceTexture : m_PrevRadianceTexture)
            ->getImageMemoryBarrier(m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_IMAGE_LAYOUT_GENERAL, m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT,
                                    VK_ACCESS_SHADER_WRITE_BIT);

    preBlendingBarriers.push_back(currentRadianceWriteBarrier);

    VkImageMemoryBarrier currentVisibilityWriteBarrier =
        (m_isEvenFrame ? m_VisibilityTexture : m_PrevVisibilityTexture)
            ->getImageMemoryBarrier(m_isFirstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_IMAGE_LAYOUT_GENERAL, m_isFirstFrame ? 0 : VK_ACCESS_SHADER_READ_BIT,
                                    VK_ACCESS_SHADER_WRITE_BIT);

    preBlendingBarriers.push_back(currentVisibilityWriteBarrier);

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
    radianceBlendConstants.prevTextureIndex = m_isEvenFrame ? m_prevProbeIrradianceBindlessIndex : m_probeIrradianceBindlessIndex;
    radianceBlendConstants.rayDataIndex = m_RayDataTexture->getBindlessIndex();
    radianceBlendConstants.writeToAlternateTexture =
        m_isEvenFrame ? 0 : 1; // 0 = write to primary (RadianceTexture), 1 = write to alternate (PrevRadianceTexture)

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
    visibilityBlendConstants.prevTextureIndex = m_isEvenFrame ? m_prevProbeVisibilityBindlessIndex : m_probeVisibilityBindlessIndex;
    visibilityBlendConstants.rayDataIndex = m_RayDataTexture->getBindlessIndex();
    visibilityBlendConstants.writeToAlternateTexture =
        m_isEvenFrame ? 0 : 1; // 0 = write to primary (VisibilityTexture), 1 = write to alternate (PrevVisibilityTexture)

    vkCmdPushConstants(currentCommandBuffer->getCommandBufferVk(), m_DDGI_ProbeDistanceBlendingPipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DDGIBlendPushConstants), &visibilityBlendConstants);

    vkCmdDispatch(currentCommandBuffer->getCommandBufferVk(), m_ProbeVolume.gridDimensions.x, m_ProbeVolume.gridDimensions.z,
                  m_ProbeVolume.gridDimensions.y);

    // === BARRIER PHASE 6: After blending shaders - transition to shader read ===
    // Transition current textures to shader read mode for next frame and final lighting
    std::vector<VkImageMemoryBarrier> postBlendingBarriers;

    VkImageMemoryBarrier currentRadianceReadBarrier =
        (m_isEvenFrame ? m_RadianceTexture : m_PrevRadianceTexture)
            ->getImageMemoryBarrier(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                                    VK_ACCESS_SHADER_READ_BIT);

    postBlendingBarriers.push_back(currentRadianceReadBarrier);

    VkImageMemoryBarrier currentVisibilityReadBarrier =
        (m_isEvenFrame ? m_VisibilityTexture : m_PrevVisibilityTexture)
            ->getImageMemoryBarrier(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                                    VK_ACCESS_SHADER_READ_BIT);

    postBlendingBarriers.push_back(currentVisibilityReadBarrier);

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
    irradianceSpec.format = TextureFormat::R11G11B10F;
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

    // Previous frame textures for temporal blending
    m_PrevRadianceTexture = std::make_shared<Texture>(irradianceSpec);
    m_PrevVisibilityTexture = std::make_shared<Texture>(distanceSpec);

    m_ProbeClassificationTexture = std::make_shared<Texture>(probeClassificationSpec);
    m_ProbeOffsetTexture = std::make_shared<Texture>(probeOffsetSpec);

    // Create flattened textures using TextureFlattener
    m_RayDataTextureFlattened = TextureFlattener::createFlattenTexture(m_RayDataTexture, "[DDGI] Flattened Ray Data Texture");
    m_IrradianceTextureFlattened = TextureFlattener::createFlattenTexture(m_RadianceTexture, "[DDGI] Irradiance Flattened Texture");
    m_DistanceTextureFlattened = TextureFlattener::createFlattenTexture(m_VisibilityTexture, "[DDGI] Distance Flattened Texture");
    m_ProbeClassificationTextureFlattened = TextureFlattener::createFlattenTexture(
        m_ProbeClassificationTexture, "[DDGI] Probe Classification Flattened Texture", FlattenerDataType::UINT);
    m_ProbeOffsetTextureFlattened =
        TextureFlattener::createFlattenTexture(m_ProbeOffsetTexture, "[DDGI] Probe Offset Flattened Texture");

    clearTextures();

    // --- Create custom descriptor sets for each compute shader ---

    // For Probe Irradiance Blending
    {
        DescriptorSetBindings bindings;
        bindings.setNumber = 4;
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, true,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_IRRADIANCE_ATLAS});
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, true,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_IRRADIANCE_ATLAS_ALT});
        m_probeIrradianceBlendingDescriptorSet = std::make_shared<DescriptorSet>(bindings);
        m_probeIrradianceBlendingDescriptorSet
            ->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_IRRADIANCE_ATLAS)
            ->add(m_RadianceTexture);
        m_probeIrradianceBlendingDescriptorSet
            ->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_IRRADIANCE_ATLAS_ALT)
            ->add(m_PrevRadianceTexture);
    }

    // For Probe Distance Blending
    {
        DescriptorSetBindings bindings;
        bindings.setNumber = 4;
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, true,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_DISTANCE_ATLAS});
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, true,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_DISTANCE_ATLAS_ALT});
        m_probeDistanceBlendingDescriptorSet = std::make_shared<DescriptorSet>(bindings);
        m_probeDistanceBlendingDescriptorSet
            ->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_DISTANCE_ATLAS)
            ->add(m_VisibilityTexture);
        m_probeDistanceBlendingDescriptorSet
            ->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::PROBE_DISTANCE_ATLAS_ALT)
            ->add(m_PrevVisibilityTexture);
    }

    // For Probe Tracing (assuming it writes to RayDataTexture)
    {
        DescriptorSetBindings bindings;
        bindings.setNumber = 4;
        bindings.bindings.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, TextureViewType::DEFAULT, true,
                                     (DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::RAY_DATA});
        m_probeTraceDescriptorSet = std::make_shared<DescriptorSet>(bindings);
        m_probeTraceDescriptorSet->getTextureBinding((DescriptorSetBindingLocation)DDGIDescriptorSetBindingLocation::RAY_DATA)
            ->add(m_RayDataTexture);
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
        RP_CORE_ERROR("DynamicDiffuseGI::updateProbeVolume - Probe info buffer not initialized");
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

    probeVolume.probeRayRotation = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    // Randomize ray directions
    {
        static std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        // Generate three independent uniform random variables
        float u1 = dist(rng);
        float u2 = dist(rng);
        float u3 = dist(rng);

        // Convert to a uniform random unit quaternion (James Arvo, Graphics Gems III)
        float sqrt1_u1 = std::sqrt(1.0f - u1);
        float sqrt_u1 = std::sqrt(u1);

        float theta1 = 2.0f * glm::pi<float>() * u2;
        float theta2 = 2.0f * glm::pi<float>() * u3;

        glm::vec4 quat;
        quat.x = sqrt1_u1 * std::sin(theta1);
        quat.y = sqrt1_u1 * std::cos(theta1);
        quat.z = sqrt_u1 * std::sin(theta2);
        quat.w = sqrt_u1 * std::cos(theta2);

        // Store the new rotation and flag the buffer for GPU update
        probeVolume.probeRayRotation = quat;
    }

    probeVolume.origin = glm::vec3(-0.4f, 5.4f, -0.25f);

    probeVolume.rotation = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

    probeVolume.spacing = glm::vec3(1.02f, 1.5f, 1.02f);
    probeVolume.gridDimensions = glm::uvec3(24, 12, 24);

    probeVolume.probeNumRays = 256;
    probeVolume.probeStaticRayCount = 32;
    probeVolume.probeNumIrradianceTexels = 8;
    probeVolume.probeNumDistanceTexels = 16;
    probeVolume.probeNumIrradianceInteriorTexels = probeVolume.probeNumIrradianceTexels - 2;
    probeVolume.probeNumDistanceInteriorTexels = probeVolume.probeNumDistanceTexels - 2;

    probeVolume.probeHysteresis = 0.93f;
    probeVolume.probeMaxRayDistance = 100000.0f;
    // Self-shadow bias scale (B). The view-bias term is no longer used with the new unified formula.
    probeVolume.probeNormalBias = 0.3f; // B parameter from the paper (works well for most scenes)
    probeVolume.probeViewBias = 0.0f;   // Unused
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

void DynamicDiffuseGI::onResize(uint32_t framesInFlight)
{
    if (m_framesInFlight == framesInFlight) {
        return; // No change needed if frame count is the same
    }

    m_framesInFlight = framesInFlight;

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();

    // Recreate command buffers with the new number of frames in flight
    CommandPoolConfig poolConfig;
    poolConfig.name = "DDGI Command Pool";
    poolConfig.queueFamilyIndex = vc.getQueueFamilyIndices().computeFamily.value();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    auto pool = CommandPoolManager::createCommandPool(poolConfig);
    m_CommandBuffers = pool->getCommandBuffers(m_framesInFlight);

    RP_CORE_INFO("DDGI system resized for {} frames in flight.", m_framesInFlight);
}

} // namespace Rapture
