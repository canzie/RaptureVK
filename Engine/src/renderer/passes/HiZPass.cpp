#include "HiZPass.h"

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

struct LinearizePushConstants {
    alignas(4) uint32_t cameraSSBOIndex;
    alignas(4) uint32_t cameraSlotIndex;
    alignas(4) uint32_t depthTextureIndex;
    alignas(8) glm::ivec2 outputSize;
};

struct ReducePushConstants {
    alignas(4) uint32_t sourceTextureIndex;
    alignas(4) int32_t sourceMip;
    alignas(8) glm::ivec2 sourceSize;
    alignas(8) glm::ivec2 outputSize;
};

static constexpr uint32_t HI_Z_LOCAL_SIZE = 8;

static glm::ivec2 s_mipSize(uint32_t width, uint32_t height, uint32_t mip)
{
    return glm::ivec2(std::max(1u, width >> mip), std::max(1u, height >> mip));
}

HiZPass::HiZPass(uint32_t width, uint32_t height, uint32_t framesInFlight)
    : m_width(width), m_height(height), m_framesInFlight(framesInFlight)
{
    m_rc = &Application::getInstance().getVulkanContext().getRenderContext();

    m_mipLevels = calculateMaxMipLevels(m_width, m_height);

    loadShaders();
    createTextures();
    createDescriptorSets();
}

HiZPass::~HiZPass()
{
    m_mipSets.clear();
    m_hiZTextures.clear();
    m_linearizePipeline.reset();
    m_reducePipeline.reset();
}

void HiZPass::loadShaders()
{
    auto &project = Application::getInstance().getProject();
    auto shaderPath = project.getProjectShaderDirectory();

    ShaderImportConfig linearizeConfig;
    linearizeConfig.compileInfo.includePath = shaderPath / "glsl";

    auto linearizeAsset = AssetManager::importAsset(shaderPath / "glsl/LinearizeDepth.cs.glsl", linearizeConfig);
    m_linearizeShader = linearizeAsset ? linearizeAsset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_linearizeShader != nullptr) {
        m_shaderAssets.push_back(std::move(linearizeAsset));
    }

    ShaderImportConfig reduceConfig;
    reduceConfig.compileInfo.includePath = shaderPath / "glsl";
    reduceConfig.compileInfo.macros.push_back({"DOWNSAMPLE_REDUCTION_MIN"});

    auto reduceAsset = AssetManager::importAsset(shaderPath / "glsl/DownsampleMip.cs.glsl", reduceConfig);
    m_reduceShader = reduceAsset ? reduceAsset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (m_reduceShader != nullptr) {
        m_shaderAssets.push_back(std::move(reduceAsset));
    }

    RP_ASSERT(m_linearizeShader != nullptr && m_reduceShader != nullptr, "Hi-Z shaders failed to load");
    if (m_linearizeShader == nullptr || m_reduceShader == nullptr) {
        RP_CORE_ERROR("Hi-Z shaders failed to load");
        return;
    }

    ComputePipelineConfiguration linearizePipelineConfig;
    linearizePipelineConfig.shader = m_linearizeShader;
    m_linearizePipeline = std::make_shared<ComputePipeline>(linearizePipelineConfig);

    ComputePipelineConfiguration reducePipelineConfig;
    reducePipelineConfig.shader = m_reduceShader;
    m_reducePipeline = std::make_shared<ComputePipeline>(reducePipelineConfig);
}

void HiZPass::createTextures()
{
    TextureSpecification spec;
    spec.width = m_width;
    spec.height = m_height;
    spec.format = TextureFormat::R32F;
    spec.type = TextureType::TEXTURE2D;
    spec.filter = TextureFilter::NearestMipmapNearest;
    spec.wrap = TextureWrap::ClampToEdge;
    spec.srgb = false;
    spec.storageImage = true;
    spec.mipLevels = m_mipLevels;

    m_hiZTextures.reserve(m_framesInFlight);
    for (uint32_t frame = 0; frame < m_framesInFlight; ++frame) {
        m_hiZTextures.push_back(std::make_unique<Texture>(spec));
        m_hiZTextures.back()->getBindlessIndex();
    }
}

void HiZPass::createDescriptorSets()
{
    DescriptorSetBindings bindings;
    bindings.setNumber = 4;

    DescriptorSetBinding outputBinding = {};
    outputBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputBinding.location = DescriptorSetBindingLocation::CUSTOM_0;
    outputBinding.useStorageImageInfo = true;
    bindings.bindings.push_back(outputBinding);

    m_mipSets.resize(m_framesInFlight);
    for (uint32_t frame = 0; frame < m_framesInFlight; ++frame) {
        m_mipSets[frame].reserve(m_mipLevels);
        for (uint32_t mip = 0; mip < m_mipLevels; ++mip) {
            auto set = std::make_unique<DescriptorSet>(bindings);
            set->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->addStorageMip(*m_hiZTextures[frame], mip);
            m_mipSets[frame].push_back(std::move(set));
        }
    }
}

Texture *HiZPass::getHiZTexture(uint32_t frameInFlight) const
{
    if (frameInFlight >= m_hiZTextures.size()) {
        RP_CORE_ERROR("Requested Hi-Z texture for frame {} but only {} exist", frameInFlight, m_hiZTextures.size());
        return nullptr;
    }
    return m_hiZTextures[frameInFlight].get();
}

void HiZPass::onResize(uint32_t width, uint32_t height)
{
    // TODO: the renderer currently destroys and rebuilds every pass on resize, so only the size is
    // updated here. Recreate the textures and descriptor sets in place once passes outlive a resize.
    m_width = width;
    m_height = height;
}

void HiZPass::updateResources(const RenderPassContext &context)
{
    m_resources.clear();

    ComputeResource depth;
    depth.texture = context.targets->depthStencil;
    depth.access = ComputeResourceAccess::READ;
    m_resources.push_back(depth);

    ComputeResource hiZ;
    hiZ.texture = getHiZTexture(context.frameInFlight);
    hiZ.access = ComputeResourceAccess::READ_WRITE;
    hiZ.discardContents = true;
    hiZ.readableAfter = true;
    m_resources.push_back(hiZ);
}

void HiZPass::barrierMip(CommandBuffer *commandBuffer, Texture *texture, uint32_t mip)
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

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void HiZPass::recordLinearize(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    Texture *depth = context.targets->depthStencil;
    auto &renderData = *(context.scene->getRenderData());
    auto *cameraComp = context.camera.isValid() ? context.camera.tryGetComponent<CameraComponent>() : nullptr;

    m_linearizePipeline->bind(commandBuffer->getCommandBufferVk());
    m_rc->descriptorManager->bindSet(0, commandBuffer, m_linearizePipeline);
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_linearizePipeline);
    m_mipSets[context.frameInFlight][0]->bind(commandBuffer->getCommandBufferVk(), m_linearizePipeline);

    LinearizePushConstants pushConstants{};
    pushConstants.cameraSSBOIndex = renderData.getCameras().getDescriptorIndex(context.frameInFlight);
    pushConstants.cameraSlotIndex =
        (cameraComp != nullptr && cameraComp->renderDataSlot != UINT32_MAX) ? cameraComp->renderDataSlot : 0;
    pushConstants.depthTextureIndex = depth->getBindlessIndex();
    pushConstants.outputSize = s_mipSize(m_width, m_height, 0);

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_linearizePipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(LinearizePushConstants), &pushConstants);

    vkCmdDispatch(commandBuffer->getCommandBufferVk(),
                  groupCount(static_cast<uint32_t>(pushConstants.outputSize.x), HI_Z_LOCAL_SIZE),
                  groupCount(static_cast<uint32_t>(pushConstants.outputSize.y), HI_Z_LOCAL_SIZE), 1);
}

void HiZPass::recordReduce(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    Texture *hiZ = getHiZTexture(context.frameInFlight);

    m_reducePipeline->bind(commandBuffer->getCommandBufferVk());
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_reducePipeline);

    for (uint32_t mip = 1; mip < m_mipLevels; ++mip) {
        barrierMip(commandBuffer, hiZ, mip - 1);

        m_mipSets[context.frameInFlight][mip]->bind(commandBuffer->getCommandBufferVk(), m_reducePipeline);

        ReducePushConstants pushConstants{};
        pushConstants.sourceTextureIndex = hiZ->getBindlessIndex();
        pushConstants.sourceMip = static_cast<int32_t>(mip - 1);
        pushConstants.sourceSize = s_mipSize(m_width, m_height, mip - 1);
        pushConstants.outputSize = s_mipSize(m_width, m_height, mip);

        vkCmdPushConstants(commandBuffer->getCommandBufferVk(), m_reducePipeline->getPipelineLayoutVk(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ReducePushConstants), &pushConstants);

        vkCmdDispatch(commandBuffer->getCommandBufferVk(),
                      groupCount(static_cast<uint32_t>(pushConstants.outputSize.x), HI_Z_LOCAL_SIZE),
                      groupCount(static_cast<uint32_t>(pushConstants.outputSize.y), HI_Z_LOCAL_SIZE), 1);
    }
}

void HiZPass::record(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_FUNCTION();

    if (!m_linearizePipeline || !m_reducePipeline) {
        RP_CORE_ERROR("Pipelines are not initialized!");
        return;
    }

    if (context.targets->depthStencil == nullptr || getHiZTexture(context.frameInFlight) == nullptr) {
        return;
    }

    recordLinearize(context, commandBuffer);
    recordReduce(context, commandBuffer);
}

} // namespace Rapture
