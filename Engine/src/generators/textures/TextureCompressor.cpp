#include "TextureCompressor.h"

#include "buffers/StorageBuffer.h"
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
#include <mutex>
#include <span>
#include <unordered_map>

namespace Rapture {

struct CompressPushConstants {
    uint32_t mipWidth;
    uint32_t mipHeight;
    uint32_t mipLod;
    uint32_t blockOffset;
};

static const char *s_spvForFormat(TextureFormat format)
{
    switch (format) {
    case TextureFormat::BC1_RGB:
    case TextureFormat::BC1_RGBA:
        return "glsl/Generators/BlockCompress.BC1.cs.spv";
    case TextureFormat::BC3:
        return "glsl/Generators/BlockCompress.BC3.cs.spv";
    case TextureFormat::BC4:
        return "glsl/Generators/BlockCompress.BC4.cs.spv";
    case TextureFormat::BC5:
        return "glsl/Generators/BlockCompress.BC5.cs.spv";
    default:
        return nullptr;
    }
}

// Loads precompiled SPIR-V so glslang never runs on a job worker fiber (its parser overflows the stack).
// TODO move this back to the AssetManager once it is thread-safe
// Encoder shaders are created with new and owned here for the lifetime of the program.
// Released by TextureCompressor::shutdown() while the device is still alive.
static std::mutex s_encoderShaderMutex;
static std::unordered_map<TextureFormat, Shader *> s_encoderShaders;

static Shader *s_getEncoderShader(TextureFormat format)
{
    std::lock_guard<std::mutex> lock(s_encoderShaderMutex);

    auto it = s_encoderShaders.find(format);
    if (it != s_encoderShaders.end()) {
        return it->second;
    }

    const char *spv = s_spvForFormat(format);
    if (spv == nullptr) {
        return nullptr;
    }

    auto shaderDir = Application::getInstance().getProject().getProjectShaderDirectory();

    Shader *shader = new Shader(shaderDir / spv);
    if (!shader->isReady()) {
        RP_CORE_ERROR("Failed to load block compression shader {}", spv);
        delete shader;
        return nullptr;
    }

    s_encoderShaders[format] = shader;
    return shader;
}

void TextureCompressor::shutdown()
{
    std::lock_guard<std::mutex> lock(s_encoderShaderMutex);
    for (auto &[format, shader] : s_encoderShaders) {
        delete shader;
    }
    s_encoderShaders.clear();
}

TextureCompressor::TextureCompressor(std::vector<uint8_t> rgba8, uint32_t width, uint32_t height) : m_width(width), m_height(height)
{
    if (width == 0 || height == 0) {
        RP_CORE_ERROR("Cannot compress texture with zero dimensions");
        return;
    }

    VkDeviceSize expected = static_cast<VkDeviceSize>(width) * height * 4;
    if (rgba8.size() < expected) {
        RP_CORE_ERROR("RGBA8 source is smaller than its dimensions imply");
        return;
    }

    m_mipLevels = calculateMaxMipLevels(width, height);

    TextureSpecification srcSpec{};
    srcSpec.type = TextureType::TEXTURE2D;
    srcSpec.format = TextureFormat::RGBA8;
    srcSpec.srgb = false; // raw UNORM so texelFetch returns the stored bytes, not sRGB-decoded
    srcSpec.width = width;
    srcSpec.height = height;
    srcSpec.depth = 1;
    srcSpec.mipLevels = m_mipLevels;
    srcSpec.filter = TextureFilter::Linear;
    srcSpec.wrap = TextureWrap::ClampToEdge;

    m_source = std::make_unique<Texture>(srcSpec, std::span<const uint8_t>(rgba8.data(), expected));
    if (!m_source->isReady()) {
        RP_CORE_ERROR("Failed to build RGBA8 source texture for compression");
        return;
    }

    m_isValid = true;
}

TextureCompressor::~TextureCompressor() = default;

bool TextureCompressor::compressToBC1(JobContext &jctx, Texture &dst)
{
    return encode(jctx, dst,
                  dst.getSpecification().format == TextureFormat::BC1_RGBA ? TextureFormat::BC1_RGBA : TextureFormat::BC1_RGB);
}

bool TextureCompressor::compressToBC3(JobContext &jctx, Texture &dst)
{
    return encode(jctx, dst, TextureFormat::BC3);
}

bool TextureCompressor::compressToBC4(JobContext &jctx, Texture &dst)
{
    return encode(jctx, dst, TextureFormat::BC4);
}

bool TextureCompressor::compressToBC5(JobContext &jctx, Texture &dst)
{
    return encode(jctx, dst, TextureFormat::BC5);
}

bool TextureCompressor::encode(JobContext &jctx, Texture &dst, TextureFormat format)
{
    if (!m_isValid) {
        RP_CORE_ERROR("TextureCompressor is not valid");
        dst.markFailed();
        return false;
    }

    if (dst.getSpecification().format != format) {
        RP_CORE_ERROR("Destination texture format does not match the requested compression format");
        dst.markFailed();
        return false;
    }

    if (dst.getSpecification().width != m_width || dst.getSpecification().height != m_height ||
        dst.getSpecification().mipLevels != m_mipLevels) {
        RP_CORE_ERROR("Destination texture dimensions do not match the source");
        dst.markFailed();
        return false;
    }

    Shader *shader = s_getEncoderShader(format);
    if (shader == nullptr || !shader->isReady()) {
        RP_CORE_ERROR("Failed to load block compression shader");
        dst.markFailed();
        return false;
    }

    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    VmaAllocator allocator = vc.getVmaAllocator();
    auto &rc = vc.getRenderContext();

    const uint32_t blockUints = getBytesPerBlock(format) / sizeof(uint32_t);

    std::vector<uint32_t> blockOffsets(m_mipLevels);
    std::vector<uint32_t> blocksX(m_mipLevels);
    std::vector<uint32_t> blocksY(m_mipLevels);
    uint32_t totalUints = 0;
    for (uint32_t mip = 0; mip < m_mipLevels; ++mip) {
        uint32_t mipWidth = std::max(1u, m_width >> mip);
        uint32_t mipHeight = std::max(1u, m_height >> mip);
        blocksX[mip] = (mipWidth + 3u) / 4u;
        blocksY[mip] = (mipHeight + 3u) / 4u;
        blockOffsets[mip] = totalUints;
        totalUints += blocksX[mip] * blocksY[mip] * blockUints;
    }

    VkDeviceSize scratchSize = static_cast<VkDeviceSize>(totalUints) * sizeof(uint32_t);
    auto scratch = std::make_shared<StorageBuffer>(scratchSize, BufferUsage::STATIC, allocator, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    ComputePipelineConfiguration pipelineConfig;
    pipelineConfig.shader = shader;
    auto pipeline = std::make_shared<ComputePipeline>(pipelineConfig);

    DescriptorSetBindings bindings;
    bindings.setNumber = 4;

    DescriptorSetBinding sourceBinding{};
    sourceBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sourceBinding.location = DescriptorSetBindingLocation::CUSTOM_0;
    bindings.bindings.push_back(sourceBinding);

    DescriptorSetBinding outputBinding{};
    outputBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    outputBinding.location = DescriptorSetBindingLocation::CUSTOM_1;
    bindings.bindings.push_back(outputBinding);

    auto descriptorSet = std::make_shared<DescriptorSet>(bindings);
    descriptorSet->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*m_source);
    descriptorSet->getSSBOBinding(DescriptorSetBindingLocation::CUSTOM_1)->add(*scratch);

    size_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());

    CommandPoolConfig poolConfig{};
    poolConfig.queueFamilyIndex = vc.getGraphicsQueueIndex();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolConfig.resetFlags = VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT;
    poolConfig.threadId = threadId;

    auto commandPoolHash = rc.commandPoolManager->createCommandPool(poolConfig);
    auto commandPool = rc.commandPoolManager->getCommandPool(commandPoolHash);
    auto commandBuffer = commandPool->getPrimaryCommandBuffer();

    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VkCommandBuffer cmd = commandBuffer->getCommandBufferVk();

    VkImageMemoryBarrier toTransferDst{};
    toTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.image = dst.getImage();
    toTransferDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransferDst.subresourceRange.baseMipLevel = 0;
    toTransferDst.subresourceRange.levelCount = m_mipLevels;
    toTransferDst.subresourceRange.baseArrayLayer = 0;
    toTransferDst.subresourceRange.layerCount = 1;
    toTransferDst.srcAccessMask = 0;
    toTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toTransferDst);

    pipeline->bind(cmd);
    descriptorSet->bind(cmd, pipeline);

    for (uint32_t mip = 0; mip < m_mipLevels; ++mip) {
        CompressPushConstants pc{};
        pc.mipWidth = std::max(1u, m_width >> mip);
        pc.mipHeight = std::max(1u, m_height >> mip);
        pc.mipLod = mip;
        pc.blockOffset = blockOffsets[mip];
        vkCmdPushConstants(cmd, pipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t groupsX = (blocksX[mip] + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        uint32_t groupsY = (blocksY[mip] + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    }

    VkBufferMemoryBarrier scratchBarrier{};
    scratchBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    scratchBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    scratchBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    scratchBarrier.buffer = scratch->getBufferVk();
    scratchBarrier.offset = 0;
    scratchBarrier.size = VK_WHOLE_SIZE;
    scratchBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    scratchBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                         &scratchBarrier, 0, nullptr);

    std::vector<VkBufferImageCopy> copyRegions(m_mipLevels);
    for (uint32_t mip = 0; mip < m_mipLevels; ++mip) {
        VkBufferImageCopy &region = copyRegions[mip];
        region.bufferOffset = static_cast<VkDeviceSize>(blockOffsets[mip]) * sizeof(uint32_t);
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mip;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {std::max(1u, m_width >> mip), std::max(1u, m_height >> mip), 1};
    }
    vkCmdCopyBufferToImage(cmd, scratch->getBufferVk(), dst.getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(copyRegions.size()), copyRegions.data());

    VkImageMemoryBarrier toShaderRead = toTransferDst;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toShaderRead);

    commandBuffer->end();

    auto graphicsQueue = vc.getGraphicsQueue();
    uint64_t signalValue = graphicsQueue->addToBatch(commandBuffer);

    TimelineSemaphore semaphoreWrapper(graphicsQueue->getTimelineSemaphore());
    Counter gpuCounter;
    gpuCounter.increment();
    jobs().submitGpuWait(&semaphoreWrapper, signalValue, gpuCounter);
    jctx.waitFor(gpuCounter, 0);

    dst.markReady();
    return true;
}

} // namespace Rapture
