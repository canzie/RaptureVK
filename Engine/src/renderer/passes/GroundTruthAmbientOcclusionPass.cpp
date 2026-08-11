#include "GroundTruthAmbientOcclusionPass.h"

#include "utils/EnginePaths.h"

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
    m_occlusionTextures.clear();
    m_pipeline.reset();
}

void GroundTruthAmbientOcclusionPass::loadShaders()
{
    auto shaderPath = EnginePaths::shaderDirectory();

    ShaderImportConfig shaderConfig;
    shaderConfig.compileInfo.includePath = shaderPath / "glsl";

    auto asset = AssetManager::importAsset(shaderPath / "glsl/GroundTruthAmbientOcclusion.cs.glsl", shaderConfig);
    m_shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_shader != nullptr) {
        m_shaderAssets.push_back(std::move(asset));
    }

    RP_ASSERT(m_shader != nullptr, "Ambient occlusion shader failed to load");
    if (m_shader == nullptr) {
        RP_CORE_ERROR("Ambient occlusion shader failed to load");
        return;
    }

    ComputePipelineConfiguration pipelineConfig;
    pipelineConfig.shader = m_shader;
    m_pipeline = std::make_shared<ComputePipeline>(pipelineConfig);
}

void GroundTruthAmbientOcclusionPass::createTextures()
{
    TextureSpecification spec;
    spec.width = m_width;
    spec.height = m_height;
    spec.format = TextureFormat::R16F;
    spec.type = TextureType::TEXTURE2D;
    spec.filter = TextureFilter::Linear;
    spec.wrap = TextureWrap::ClampToEdge;
    spec.srgb = false;
    spec.storageImage = true;

    m_occlusionTextures.reserve(m_framesInFlight);
    for (uint32_t frame = 0; frame < m_framesInFlight; ++frame) {
        m_occlusionTextures.push_back(std::make_unique<Texture>(spec));
        m_occlusionTextures.back()->getBindlessIndex();
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
    for (uint32_t frame = 0; frame < m_framesInFlight; ++frame) {
        auto set = std::make_unique<DescriptorSet>(bindings);
        set->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*m_occlusionTextures[frame]);
        m_occlusionSets.push_back(std::move(set));
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

    ComputeResource occlusion;
    occlusion.texture = getOcclusionTexture(context.frameInFlight);
    occlusion.access = ComputeResourceAccess::WRITE;
    occlusion.discardContents = true;
    occlusion.readableAfter = true;
    m_resources.push_back(occlusion);
}

void GroundTruthAmbientOcclusionPass::record(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_FUNCTION();

    if (!m_pipeline) {
        RP_CORE_ERROR("Pipeline is not initialized!");
        return;
    }

    if (context.targets->depthStencil == nullptr || getOcclusionTexture(context.frameInFlight) == nullptr) {
        return;
    }

    auto &renderData = *(context.scene->getRenderData());
    auto *cameraComp = context.camera.isValid() ? context.camera.tryRead<CameraComponent>() : nullptr;

    m_pipeline->bind(commandBuffer->getCommandBufferVk());
    m_rc->descriptorManager->bindSet(0, commandBuffer, m_pipeline);
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_pipeline);
    m_occlusionSets[context.frameInFlight]->bind(commandBuffer->getCommandBufferVk(), m_pipeline);

    OcclusionPushConstants pushConstants{};
    pushConstants.cameraSSBOIndex = renderData.getCameras().getDescriptorIndex(context.frameInFlight);
    pushConstants.cameraSlotIndex =
        (cameraComp != nullptr && cameraComp->renderDataSlot != UINT32_MAX) ? cameraComp->renderDataSlot : 0;
    pushConstants.depthTextureIndex = context.targets->depthStencil->getBindlessIndex();
    pushConstants.normalTextureIndex = context.targets->gbufferNormalMotion->getBindlessIndex();
    pushConstants.outputSize = glm::ivec2(m_width, m_height);

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_pipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(OcclusionPushConstants), &pushConstants);

    vkCmdDispatch(commandBuffer->getCommandBufferVk(), groupCount(m_width, GTAO_LOCAL_SIZE), groupCount(m_height, GTAO_LOCAL_SIZE),
                  1);
}

} // namespace Rapture
