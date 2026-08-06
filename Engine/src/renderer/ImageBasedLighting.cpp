#include "ImageBasedLighting.h"

#include "utils/EnginePaths.h"

#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/command_buffers/CommandPool.h"
#include "buffers/descriptors/DescriptorSet.h"
#include "jobs/Counter.h"
#include "jobs/Job.h"
#include "jobs/JobSystem.h"
#include "logging/Log.h"
#include "pipelines/ComputePipeline.h"
#include "scenes/Project.h"
#include "shaders/Shader.h"
#include "textures/Texture.h"
#include "window_context/Application.h"
#include "window_context/vulkan_context/TimelineSemaphore.h"

#include <algorithm>

namespace Rapture {

static constexpr uint32_t IRRADIANCE_SIZE = 32;
static constexpr uint32_t PREFILTER_SIZE = 128;
static constexpr uint32_t PREFILTER_MIPS = 5;
static constexpr uint32_t BRDF_LUT_SIZE = 512;
static constexpr uint32_t WORKGROUP_SIZE = 8;

struct PrefilterPushConstants {
    float roughness;
    float srcResolution;
};

static std::unique_ptr<Shader> s_loadShader(const char *relPath)
{
    auto shaderDir = EnginePaths::shaderDirectory();
    auto shader = std::make_unique<Shader>(shaderDir / relPath);
    if (!shader->isReady()) {
        RP_CORE_ERROR("Failed to load IBL bake shader {}", relPath);
        return nullptr;
    }
    return shader;
}

static std::unique_ptr<Texture> s_createCube(uint32_t size, uint32_t mipLevels)
{
    TextureSpecification spec{};
    spec.type = TextureType::TEXTURECUBE;
    spec.format = TextureFormat::RGBA16F;
    spec.srgb = false;
    spec.width = size;
    spec.height = size;
    spec.depth = 1;
    spec.mipLevels = mipLevels;
    spec.storageImage = true;
    spec.filter = mipLevels > 1 ? TextureFilter::LinearMipmapLinear : TextureFilter::Linear;
    spec.wrap = TextureWrap::ClampToEdge;
    return std::make_unique<Texture>(spec);
}

static void s_barrierCube(VkCommandBuffer cmd, Texture &cube, VkImageLayout oldLayout, VkImageLayout newLayout,
                          VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                          VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = cube.getImage();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = cube.getSpecification().mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

static std::shared_ptr<DescriptorSet> s_makeSampleWriteSet()
{
    DescriptorSetBindings bindings;
    bindings.setNumber = 4;

    DescriptorSetBinding source{};
    source.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    source.location = DescriptorSetBindingLocation::CUSTOM_0;
    bindings.bindings.push_back(source);

    DescriptorSetBinding output{};
    output.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    output.location = DescriptorSetBindingLocation::CUSTOM_1;
    output.useStorageImageInfo = true;
    bindings.bindings.push_back(output);

    return std::make_shared<DescriptorSet>(bindings);
}

ImageBasedLighting::ImageBasedLighting() = default;
ImageBasedLighting::~ImageBasedLighting() = default;

void ImageBasedLighting::bakeFromCube(Texture *sourceCube)
{
    if (sourceCube == nullptr || !sourceCube->isReady()) {
        return;
    }

    m_requestedSource.store(sourceCube, std::memory_order_release);
    m_requestGen.fetch_add(1, std::memory_order_acq_rel);
    kickBakeIfIdle();
}

void ImageBasedLighting::kickBakeIfIdle()
{
    bool expected = false;
    if (!m_baking.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    uint64_t startedGen = m_requestGen.load(std::memory_order_acquire);
    Texture *sourceCube = m_requestedSource.load(std::memory_order_acquire);
    m_ready.store(false, std::memory_order_release);

    jobs().run(JobDeclaration(
        [this, sourceCube, startedGen](JobContext &jctx) {
            if (m_irradianceShader == nullptr) {
                m_irradianceShader = s_loadShader("SPIRV/Generators/IrradianceConvolution.cs.spv");
            }
            if (m_prefilterShader == nullptr) {
                m_prefilterShader = s_loadShader("SPIRV/Generators/SpecularPrefilter.cs.spv");
            }
            if (!m_brdfBaked && m_brdfShader == nullptr) {
                m_brdfShader = s_loadShader("SPIRV/Generators/BRDFIntegration.cs.spv");
            }
            if (m_irradianceShader == nullptr || m_prefilterShader == nullptr) {
                m_baking.store(false, std::memory_order_release);
                return;
            }

            auto &vc = Application::getInstance().getVulkanContext();
            auto &rc = vc.getRenderContext();

            if (m_irradianceCube == nullptr) {
                m_irradianceCube = s_createCube(IRRADIANCE_SIZE, 1);
            }
            if (m_prefilteredCube == nullptr) {
                m_prefilteredCube = s_createCube(PREFILTER_SIZE, PREFILTER_MIPS);
            }
            m_prefilteredMipCount = PREFILTER_MIPS;
            if (!m_brdfBaked) {
                TextureSpecification lutSpec{};
                lutSpec.type = TextureType::TEXTURE2D;
                lutSpec.format = TextureFormat::RG16F;
                lutSpec.srgb = false;
                lutSpec.width = BRDF_LUT_SIZE;
                lutSpec.height = BRDF_LUT_SIZE;
                lutSpec.mipLevels = 1;
                lutSpec.storageImage = true;
                lutSpec.filter = TextureFilter::Linear;
                lutSpec.wrap = TextureWrap::ClampToEdge;
                m_brdfLut = std::make_unique<Texture>(lutSpec);
            }

            auto irradiancePipeline = std::make_shared<ComputePipeline>(ComputePipelineConfiguration{m_irradianceShader.get()});
            auto prefilterPipeline = std::make_shared<ComputePipeline>(ComputePipelineConfiguration{m_prefilterShader.get()});
            std::shared_ptr<ComputePipeline> brdfPipeline;

            auto irradianceSet = s_makeSampleWriteSet();
            irradianceSet->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*sourceCube);
            irradianceSet->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_1)->add(*m_irradianceCube);

            std::vector<std::shared_ptr<DescriptorSet>> prefilterSets(PREFILTER_MIPS);
            for (uint32_t mip = 0; mip < PREFILTER_MIPS; ++mip) {
                prefilterSets[mip] = s_makeSampleWriteSet();
                prefilterSets[mip]->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*sourceCube);
                prefilterSets[mip]
                    ->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_1)
                    ->addStorageMip(*m_prefilteredCube, mip);
            }

            std::shared_ptr<DescriptorSet> brdfSet;

            size_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
            CommandPoolConfig poolConfig{};
            poolConfig.queueFamilyIndex = vc.getGraphicsQueueIndex();
            poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            poolConfig.resetFlags = VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT;
            poolConfig.threadId = threadId;
            auto poolHash = rc.commandPoolManager->createCommandPool(poolConfig);
            auto commandPool = rc.commandPoolManager->getCommandPool(poolHash);
            auto commandBuffer = commandPool->getPrimaryCommandBuffer();

            commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            VkCommandBuffer cmd = commandBuffer->getCommandBufferVk();

            s_barrierCube(cmd, *m_irradianceCube, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT,
                          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            irradiancePipeline->bind(cmd);
            irradianceSet->bind(cmd, irradiancePipeline);
            uint32_t irrGroups = (IRRADIANCE_SIZE + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
            vkCmdDispatch(cmd, irrGroups, irrGroups, 6);
            s_barrierCube(cmd, *m_irradianceCube, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            s_barrierCube(cmd, *m_prefilteredCube, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                          VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            prefilterPipeline->bind(cmd);
            for (uint32_t mip = 0; mip < PREFILTER_MIPS; ++mip) {
                uint32_t mipSize = std::max(1u, PREFILTER_SIZE >> mip);
                PrefilterPushConstants pc{};
                pc.roughness = PREFILTER_MIPS > 1 ? static_cast<float>(mip) / static_cast<float>(PREFILTER_MIPS - 1) : 0.0f;
                pc.srcResolution = static_cast<float>(sourceCube->getSpecification().width);

                prefilterSets[mip]->bind(cmd, prefilterPipeline);
                vkCmdPushConstants(cmd, prefilterPipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                uint32_t groups = (mipSize + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
                vkCmdDispatch(cmd, groups, groups, 6);
            }
            s_barrierCube(cmd, *m_prefilteredCube, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            if (!m_brdfBaked && m_brdfShader != nullptr) {
                brdfPipeline = std::make_shared<ComputePipeline>(ComputePipelineConfiguration{m_brdfShader.get()});
                brdfSet = std::make_shared<DescriptorSet>([] {
                    DescriptorSetBindings b;
                    b.setNumber = 4;
                    DescriptorSetBinding out{};
                    out.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    out.location = DescriptorSetBindingLocation::CUSTOM_0;
                    out.useStorageImageInfo = true;
                    b.bindings.push_back(out);
                    return b;
                }());
                brdfSet->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*m_brdfLut);

                VkImageMemoryBarrier toGeneral{};
                toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toGeneral.image = m_brdfLut->getImage();
                toGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                toGeneral.subresourceRange.levelCount = 1;
                toGeneral.subresourceRange.layerCount = 1;
                toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                                     nullptr, 1, &toGeneral);

                brdfPipeline->bind(cmd);
                brdfSet->bind(cmd, brdfPipeline);
                uint32_t groups = (BRDF_LUT_SIZE + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
                vkCmdDispatch(cmd, groups, groups, 1);

                VkImageMemoryBarrier toRead = toGeneral;
                toRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                toRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                     nullptr, 0, nullptr, 1, &toRead);
            }

            commandBuffer->end();

            auto graphicsQueue = vc.getGraphicsQueue();
            uint64_t signalValue = graphicsQueue->addToBatch(commandBuffer);

            TimelineSemaphore semaphoreWrapper(graphicsQueue->getTimelineSemaphore());
            Counter gpuCounter;
            gpuCounter.increment();
            jobs().submitGpuWait(&semaphoreWrapper, signalValue, gpuCounter);
            jctx.waitFor(gpuCounter, 0);

            m_irradianceCube->markReady();
            m_prefilteredCube->markReady();
            if (!m_brdfBaked && m_brdfLut) {
                m_brdfLut->markReady();
                m_brdfBaked = true;
            }

            m_ready.store(true, std::memory_order_release);
            m_baking.store(false, std::memory_order_release);

            if (m_requestGen.load(std::memory_order_acquire) != startedGen) {
                kickBakeIfIdle();
            }
        },
        JobPriority::NORMAL, QueueAffinity::ANY, nullptr, "IBL bake"));
}

uint32_t ImageBasedLighting::getIrradianceBindlessIndex()
{
    return isReady() && m_irradianceCube ? m_irradianceCube->getBindlessIndex() : 0;
}

uint32_t ImageBasedLighting::getPrefilteredBindlessIndex()
{
    return isReady() && m_prefilteredCube ? m_prefilteredCube->getBindlessIndex() : 0;
}

uint32_t ImageBasedLighting::getBrdfLutBindlessIndex()
{
    return isReady() && m_brdfLut ? m_brdfLut->getBindlessIndex() : 0;
}

} // namespace Rapture
