#include "Texture.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include "Buffers/Descriptors/DescriptorManager.h"
#include "Buffers/Descriptors/DescriptorSet.h"

#include "stb_image.h"

#include <stdexcept>

namespace Rapture {

std::shared_ptr<DescriptorBindingTexture> Texture::s_bindlessTextures = nullptr;

// Sampler implementation
Sampler::Sampler(const TextureSpecification &spec)
{
    auto &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = toVkFilter(spec.filter);
    samplerInfo.minFilter = toVkFilter(spec.filter);
    samplerInfo.addressModeU = toVkSamplerAddressMode(spec.wrap);
    samplerInfo.addressModeV = toVkSamplerAddressMode(spec.wrap);
    samplerInfo.addressModeW = toVkSamplerAddressMode(spec.wrap);
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f; // Could be made configurable
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    // Enable shadow comparison for depth textures when requested
    if (spec.shadowComparison && isDepthFormat(spec.format)) {
        samplerInfo.compareEnable = VK_TRUE;
        samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL; // Standard depth comparison
    } else {
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    }

    samplerInfo.mipmapMode = toVkSamplerMipmapMode(spec.filter);
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(spec.mipLevels);

    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create texture sampler!");
        throw std::runtime_error("Failed to create texture sampler!");
    }
}

Sampler::Sampler(VkFilter filter, VkSamplerAddressMode wrap)
{
    auto &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = wrap;
    samplerInfo.addressModeV = wrap;
    samplerInfo.addressModeW = wrap;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f; // Could be made configurable
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create texture sampler!");
        throw std::runtime_error("Failed to create texture sampler!");
    }
}

Sampler::~Sampler()
{
    auto &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
    }
}

// Texture implementation
Texture::Texture(const std::string &path, TextureSpecification spec, bool isLoadingAsync)
    : m_paths({path}), m_sampler(nullptr), m_image(VK_NULL_HANDLE), m_allocation(VK_NULL_HANDLE), m_spec(spec)
{

    // First, create specification from image file
    createSpecificationFromImageFile(m_paths);

    // Initialize sampler with the spec (will be reconstructed after spec is finalized)
    m_sampler = std::make_unique<Sampler>(m_spec);

    // Create the image first, then load data into it
    createImage();
    createImageView();
    if (!isLoadingAsync) {
        loadImageFromFile();
    }
}

Texture::Texture(const std::vector<std::string> &paths, TextureSpecification spec, bool isLoadingAsync)
    : m_paths(paths), m_sampler(nullptr), m_image(VK_NULL_HANDLE), m_allocation(VK_NULL_HANDLE), m_spec(spec)
{

    // First, create specification from image file
    createSpecificationFromImageFile(m_paths);

    // Initialize sampler with the spec (will be reconstructed after spec is finalized)
    m_sampler = std::make_unique<Sampler>(m_spec);

    // Create the image first, then load data into it
    createImage();
    createImageView();
    if (!isLoadingAsync) {
        loadImageFromFile();
    }
}

Texture::Texture(TextureSpecification spec)
    : m_sampler(nullptr), m_image(VK_NULL_HANDLE), m_allocation(VK_NULL_HANDLE), m_spec(spec)
{

    // Auto-calculate mip levels if mipLevels is 0
    if (m_spec.mipLevels == 0) {
        m_spec.mipLevels = calculateMaxMipLevels(m_spec.width, m_spec.height);
    }

    m_sampler = std::make_unique<Sampler>(m_spec);
    // Create the image and image view
    createImage();
    createImageView();

    // For depth textures, transition to the correct layout
    if (isDepthFormat(m_spec.format)) {
        transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        RP_CORE_INFO("Transitioned depth texture to DEPTH_STENCIL_ATTACHMENT_OPTIMAL layout");
    }
    // m_readyForSampling = true;
}

Texture::~Texture()
{
    auto &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();
    VmaAllocator allocator = app.getVulkanContext().getVmaAllocator();

    // Free the bindless descriptor if allocated
    if (m_bindlessIndex != UINT32_MAX && s_bindlessTextures) {
        s_bindlessTextures->free(m_bindlessIndex);
    }

    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_imageView, nullptr);
    }

    if (m_imageViewDepthOnly != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_imageViewDepthOnly, nullptr);
    }

    if (m_imageViewStencilOnly != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_imageViewStencilOnly, nullptr);
    }

    if (m_image != VK_NULL_HANDLE && m_allocation != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_image, m_allocation);
    }
}

void Texture::createSpecificationFromImageFile(const std::vector<std::string> &paths)
{
    if (paths.empty()) {
        RP_CORE_ERROR("Cannot create texture specification from empty path list.");
        throw std::runtime_error("Cannot create texture specification from empty path list.");
    }

    int width, height, channels;

    // Use stbi_info to get image dimensions without loading the full image, using the first path as representative
    if (!stbi_info(paths[0].c_str(), &width, &height, &channels)) {
        RP_CORE_ERROR("Failed to get image info for: {}", paths[0]);
        throw std::runtime_error("Failed to get image info for: " + paths[0]);
    }

    // Set basic properties
    m_spec.width = static_cast<uint32_t>(width);
    m_spec.height = static_cast<uint32_t>(height);
    m_spec.depth = 1; // For 2D texture or cubemap face depth

    // Always use RGBA8 format since we force 4 channels during loading
    m_spec.format = TextureFormat::RGBA8;

    if (paths.size() == 6) {
        m_spec.type = TextureType::TEXTURECUBE;
        // m_spec.format = TextureFormat::RGBA16F;
    } else if (paths.size() > 1) {
        m_spec.type = TextureType::TEXTURE2D_ARRAY;
        m_spec.depth = paths.size();
    } else {
        m_spec.type = TextureType::TEXTURE2D;
    }

    // Auto-calculate mip levels if mipLevels is 0
    if (m_spec.mipLevels == 0) {
        m_spec.mipLevels = calculateMaxMipLevels(m_spec.width, m_spec.height);
    }
}

bool Texture::validateSpecificationAgainstImageData(int width, int height, int channels)
{
    bool valid = true;

    if (m_spec.width != static_cast<uint32_t>(width)) {
        RP_CORE_ERROR("Width mismatch: spec={}, image={}", m_spec.width, width);
        valid = false;
    }

    if (m_spec.height != static_cast<uint32_t>(height)) {
        RP_CORE_ERROR("Height mismatch: spec={}, image={}", m_spec.height, height);
        valid = false;
    }

    // Check if format matches channels (basic validation)
    uint32_t expectedChannels = 0;
    switch (m_spec.format) {
    case TextureFormat::RGB8:
    case TextureFormat::RGB16F:
    case TextureFormat::RGB32F:
        expectedChannels = 3;
        break;
    case TextureFormat::RGBA8:
    case TextureFormat::RGBA16F:
    case TextureFormat::RGBA32F:
        expectedChannels = 4;
        break;
    default:
        expectedChannels = channels; // Accept whatever for other formats
        break;
    }

    if (expectedChannels != 0 && expectedChannels != static_cast<uint32_t>(channels)) {
        RP_CORE_WARN("Channel count mismatch: expected={}, image={}", expectedChannels, channels);
        // This is a warning, not an error, as we can convert
    }

    return valid;
}

void Texture::loadImageFromFile(size_t threadId)
{
    if (m_paths.empty()) {
        RP_CORE_WARN("No paths provided to load image from file.");
        return;
    }

    auto &app = Application::getInstance();
    VmaAllocator allocator = app.getVulkanContext().getVmaAllocator();

    int width, height, channels;
    int desiredChannels = 4; // Force 4 channels (RGBA) for consistency
    VkDeviceSize layerSize = 0;
    VkDeviceSize imageSize = 0;

    std::vector<stbi_uc *> pixelData;
    pixelData.reserve(m_paths.size());

    for (const auto &path : m_paths) {
        stbi_uc *pixels = stbi_load(path.c_str(), &width, &height, &channels, desiredChannels);
        if (!pixels) {
            RP_CORE_ERROR("Failed to load texture image: {}", path);
            // Cleanup previously loaded images
            for (stbi_uc *p : pixelData) {
                stbi_image_free(p);
            }
            throw std::runtime_error("Failed to load texture image: " + path);
        }

        if (pixelData.empty()) { // First image
            validateSpecificationAgainstImageData(width, height, desiredChannels);
            layerSize = static_cast<VkDeviceSize>(width) * height * desiredChannels;
        }
        pixelData.push_back(pixels);
    }

    imageSize = layerSize * m_paths.size();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        for (stbi_uc *p : pixelData) stbi_image_free(p);
        RP_CORE_ERROR("Failed to create staging buffer for texture!");
        throw std::runtime_error("Failed to create staging buffer for texture!");
    }

    // Copy pixel data to staging buffer
    void *data;
    vmaMapMemory(allocator, stagingAllocation, &data);
    for (size_t i = 0; i < pixelData.size(); ++i) {
        memcpy(static_cast<char *>(data) + (i * layerSize), pixelData[i], static_cast<size_t>(layerSize));
    }
    vmaUnmapMemory(allocator, stagingAllocation);

    for (stbi_uc *p : pixelData) stbi_image_free(p);

    // Transition image layout and copy data
    transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, threadId);
    copyBufferToImage(stagingBuffer, static_cast<uint32_t>(width), static_cast<uint32_t>(height), threadId);

    // Generate mipmaps if we have multiple mip levels
    if (m_spec.mipLevels > 1) {
        generateMipmaps(threadId);
    } else {
        // Single mip level, transition to shader read only
        transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, threadId);
    }

    // Clean up staging buffer
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
}

void Texture::copyFromImage(VkImage image, VkImageLayout otherLayout, VkImageLayout newLayout, VkSemaphore waitSemaphore,
                            VkSemaphore signalSemaphore, VkCommandBuffer extCommandBuffer, bool useInternalFence)
{

    if (m_image == VK_NULL_HANDLE || image == VK_NULL_HANDLE) {
        RP_CORE_ERROR("Cannot copy image: One or both VkImages are VK_NULL_HANDLE");
        throw std::runtime_error("Cannot copy image: One or both VkImages are VK_NULL_HANDLE");
    }

    auto &app = Application::getInstance();
    auto graphicsQueue = app.getVulkanContext().getGraphicsQueue();

    std::shared_ptr<CommandBuffer> internalCommandBuffer = nullptr;
    VkCommandBuffer commandBufferVk;

    bool useExternalCommandBuffer = (extCommandBuffer != VK_NULL_HANDLE);

    if (!useExternalCommandBuffer) {
        // Get or create a command pool for graphics operations
        CommandPoolConfig poolConfig{};
        poolConfig.queueFamilyIndex = app.getVulkanContext().getQueueFamilyIndices().graphicsFamily.value();
        poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        auto commandPool = CommandPoolManager::createCommandPool(poolConfig);
        internalCommandBuffer = commandPool->getCommandBuffer();
        commandBufferVk = internalCommandBuffer->getCommandBufferVk();

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBufferVk, &beginInfo);
    } else {
        commandBufferVk = extCommandBuffer;
    }

    // Transition source image to transfer source optimal
    VkImageMemoryBarrier sourceBarrier{};
    sourceBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    sourceBarrier.oldLayout = otherLayout;
    sourceBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sourceBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sourceBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    sourceBarrier.image = image;
    sourceBarrier.subresourceRange.aspectMask = getImageAspectFlags(m_spec.format);
    sourceBarrier.subresourceRange.baseMipLevel = 0;
    sourceBarrier.subresourceRange.levelCount = m_spec.mipLevels;
    sourceBarrier.subresourceRange.baseArrayLayer = 0;
    sourceBarrier.subresourceRange.layerCount = isCubeType(m_spec.type) ? 6 : (isArrayType(m_spec.type) ? m_spec.depth : 1);
    sourceBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    sourceBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    // Transition destination image to transfer destination optimal
    VkImageMemoryBarrier destBarrier{};
    destBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    destBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    destBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    destBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    destBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    destBarrier.image = m_image;
    destBarrier.subresourceRange.aspectMask = getImageAspectFlags(m_spec.format);
    destBarrier.subresourceRange.baseMipLevel = 0;
    destBarrier.subresourceRange.levelCount = m_spec.mipLevels;
    destBarrier.subresourceRange.baseArrayLayer = 0;
    destBarrier.subresourceRange.layerCount = isCubeType(m_spec.type) ? 6 : (isArrayType(m_spec.type) ? m_spec.depth : 1);
    destBarrier.srcAccessMask = 0;
    destBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    VkImageMemoryBarrier imageMemoryBarriers[] = {sourceBarrier, destBarrier};

    // Execute the layout transitions
    vkCmdPipelineBarrier(commandBufferVk, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, imageMemoryBarriers);

    // Copy the image with component mapping to handle color channel ordering
    VkImageCopy copyRegion{};
    copyRegion.srcSubresource.aspectMask = getImageAspectFlags(m_spec.format);
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = isCubeType(m_spec.type) ? 6 : (isArrayType(m_spec.type) ? m_spec.depth : 1);
    copyRegion.srcOffset = {0, 0, 0};
    copyRegion.dstSubresource.aspectMask = getImageAspectFlags(m_spec.format);
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = isCubeType(m_spec.type) ? 6 : (isArrayType(m_spec.type) ? m_spec.depth : 1);
    copyRegion.dstOffset = {0, 0, 0};
    copyRegion.extent = {m_spec.width, m_spec.height, m_spec.depth};

    // Create a blit command to handle the color channel swap
    VkImageBlit blitRegion{};
    blitRegion.srcSubresource.aspectMask = getImageAspectFlags(m_spec.format);
    blitRegion.srcSubresource.mipLevel = 0;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = isCubeType(m_spec.type) ? 6 : (isArrayType(m_spec.type) ? m_spec.depth : 1);
    blitRegion.srcOffsets[0] = {0, 0, 0};
    blitRegion.srcOffsets[1] = {(int32_t)m_spec.width, (int32_t)m_spec.height, 1};
    blitRegion.dstSubresource.aspectMask = getImageAspectFlags(m_spec.format);
    blitRegion.dstSubresource.mipLevel = 0;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = isCubeType(m_spec.type) ? 6 : (isArrayType(m_spec.type) ? m_spec.depth : 1);
    blitRegion.dstOffsets[0] = {0, 0, 0};
    blitRegion.dstOffsets[1] = {(int32_t)m_spec.width, (int32_t)m_spec.height, 1};

    // Use blit instead of copy to handle color channel ordering
    vkCmdBlitImage(commandBufferVk, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &blitRegion, VK_FILTER_NEAREST);

    // Transition source image back to original layout
    sourceBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sourceBarrier.newLayout = otherLayout;
    sourceBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sourceBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    // Transition destination image to final layout
    destBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    destBarrier.newLayout = newLayout;
    destBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    destBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkImageMemoryBarrier finalBarriers[] = {sourceBarrier, destBarrier};

    // Execute the final layout transitions
    vkCmdPipelineBarrier(commandBufferVk, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 2, finalBarriers);

    if (!useExternalCommandBuffer) {
        internalCommandBuffer->end();

        // Submit the command buffer
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
        if (waitSemaphore != VK_NULL_HANDLE) {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &waitSemaphore;
            submitInfo.pWaitDstStageMask = waitStages;
        }
        if (signalSemaphore != VK_NULL_HANDLE) {
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &signalSemaphore;
        }

        if (useInternalFence) {
            // Traditional blocking approach
            VkFence fence;
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            vkCreateFence(app.getVulkanContext().getLogicalDevice(), &fenceInfo, nullptr, &fence);

            graphicsQueue->submitQueue(internalCommandBuffer, submitInfo, fence);

            vkWaitForFences(app.getVulkanContext().getLogicalDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(app.getVulkanContext().getLogicalDevice(), fence, nullptr);
        } else {
            // Non-blocking approach - caller handles synchronization
            graphicsQueue->submitQueue(internalCommandBuffer, submitInfo, VK_NULL_HANDLE);
        }
    }
}

VkImageMemoryBarrier Texture::getImageMemoryBarrier(VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccessMask,
                                                    VkAccessFlags dstAccessMask)
{
    VkImageMemoryBarrier barrier{};
    // Image layout transitions for dynamic rendering
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange.aspectMask = isDepthFormat(m_spec.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    if (m_spec.format == TextureFormat::D24S8) {
        barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = isCubeType(m_spec.type) ? 6 : (isArrayType(m_spec.type) ? m_spec.depth : 1);
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    return barrier;
}

uint32_t Texture::getBindlessIndex()
{
    if (m_bindlessIndex != UINT32_MAX) {
        return m_bindlessIndex;
    }

    // Initialize the bindless buffer pool if not already done
    if (s_bindlessTextures == nullptr) {
        auto set = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::BINDLESS_TEXTURES);
        if (set) {
            s_bindlessTextures = set->getTextureBinding(DescriptorSetBindingLocation::BINDLESS_TEXTURES);
        }
    }

    if (s_bindlessTextures) {
        m_bindlessIndex = s_bindlessTextures->add(shared_from_this());
    }

    return m_bindlessIndex;
}

void Texture::createImage()
{
    auto &app = Application::getInstance();
    VmaAllocator allocator = app.getVulkanContext().getVmaAllocator();

    if (m_spec.width == 0 || m_spec.height == 0 || m_spec.depth == 0) {
        RP_CORE_ERROR("Texture::createImage() - Invalid texture specification --- dimesnions should be greater than 0! width: {}, "
                      "height: {}, depth: {}",
                      m_spec.width, m_spec.height, m_spec.depth);
        throw std::runtime_error("Texture::createImage() - Invalid texture specification!");
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = toVkImageType(m_spec.type);
    imageInfo.extent.width = m_spec.width;
    imageInfo.extent.height = m_spec.height;
    imageInfo.extent.depth = (isArrayType(m_spec.type) || isCubeType(m_spec.type)) ? 1 : m_spec.depth;
    imageInfo.mipLevels = m_spec.mipLevels;
    imageInfo.arrayLayers = isArrayType(m_spec.type) ? m_spec.depth : (isCubeType(m_spec.type) ? 6 : 1);
    imageInfo.format = toVkFormat(m_spec.format, m_spec.srgb);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Set usage flags based on format type
    if (isDepthFormat(m_spec.format)) {
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    } else {
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        // Add transfer source bit if we have multiple mip levels (needed for mipmap generation)
        if (m_spec.mipLevels > 1) {
            imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        // Add storage image usage for compute shaders if requested
        if (m_spec.storageImage) {
            imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
    }
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = isCubeType(m_spec.type) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_image, &m_allocation, nullptr) != VK_SUCCESS) {
        RP_CORE_ERROR("Texture::createImage() - Failed to create image!");
        throw std::runtime_error("Texture::createImage() - Failed to create image!");
    }
}

void Texture::createImageView()
{
    auto &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = toVkImageViewType(m_spec.type);
    viewInfo.format = toVkFormat(m_spec.format, m_spec.srgb);

    // For depth-stencil formats, only use depth aspect for the main view to comply with Vulkan spec
    // The spec requires that descriptor set image views have either depth OR stencil aspect, not both
    if (isDepthFormat(m_spec.format)) {
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        viewInfo.subresourceRange.aspectMask = getImageAspectFlags(m_spec.format);
    }

    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_spec.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = isArrayType(m_spec.type) ? m_spec.depth : (isCubeType(m_spec.type) ? 6 : 1);

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create texture image view!");
        throw std::runtime_error("Failed to create texture image view!");
    }

    // Create additional views for depth-stencil formats
    if (isDepthFormat(m_spec.format)) {

        // Create depth-only view
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_ONE;
        if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageViewDepthOnly) != VK_SUCCESS) {
            RP_CORE_ERROR("Failed to create depth-only image view!");
            throw std::runtime_error("Failed to create depth-only image view!");
        }
    }
    if (hasStencilComponent(m_spec.format)) {
        // Create stencil-only view
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        // Force R,G,B to stencil value (usually read into R), and Alpha to ONE for stencil view.
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageViewStencilOnly) != VK_SUCCESS) {
            RP_CORE_ERROR("Failed to create stencil-only image view!");
            throw std::runtime_error("Failed to create stencil-only image view!");
        }
    }
}

VkDescriptorImageInfo Texture::getDescriptorImageInfo(TextureViewType viewType) const
{
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    switch (viewType) {
    case TextureViewType::DEFAULT:
    case TextureViewType::COLOR:
        imageInfo.imageView = m_imageView;
        break;
    case TextureViewType::STENCIL:
        imageInfo.imageView = m_imageViewStencilOnly;
        break;
    case TextureViewType::DEPTH:
        imageInfo.imageView = m_imageViewDepthOnly;
        break;
    default:
        RP_CORE_WARN("Texture::getDescriptorImageInfo - Invalid texture view type! Using default view.");
        imageInfo.imageView = m_imageView;
        break;
    }
    if (imageInfo.imageView == VK_NULL_HANDLE) {
        RP_CORE_WARN("Texture::getDescriptorImageInfo - Invalid texture view type! Using default view.");
        imageInfo.imageView = m_imageView;
    }

    imageInfo.sampler = m_sampler->getSamplerVk();
    return imageInfo;
}

VkDescriptorImageInfo Texture::getStorageImageDescriptorInfo() const
{
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // Storage images use GENERAL layout
    imageInfo.imageView = m_imageView;
    imageInfo.sampler = VK_NULL_HANDLE; // Storage images don't use samplers
    return imageInfo;
}

std::shared_ptr<Texture> Texture::createDefaultWhiteTexture()
{

    // Create a 1x1 white texture
    TextureSpecification spec{};
    spec.width = 1;
    spec.height = 1;
    spec.depth = 1;
    spec.type = TextureType::TEXTURE2D;
    spec.format = TextureFormat::RGBA8;
    spec.filter = TextureFilter::Linear;
    spec.wrap = TextureWrap::Repeat;
    spec.srgb = false; // Use linear for default texture
    spec.mipLevels = 1;

    auto defaultWhiteTexture = std::make_shared<Texture>(spec);

    // Fill with white pixel data
    auto &app = Application::getInstance();
    VmaAllocator allocator = app.getVulkanContext().getVmaAllocator();

    // Create staging buffer with white pixel data
    uint32_t whitePixel = 0xFFFFFFFF; // RGBA white
    VkDeviceSize imageSize = sizeof(uint32_t);

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create staging buffer for default white texture!");
        throw std::runtime_error("Failed to create staging buffer for default white texture!");
    }

    // Copy white pixel data to staging buffer
    void *data;
    vmaMapMemory(allocator, stagingAllocation, &data);
    memcpy(data, &whitePixel, imageSize);
    vmaUnmapMemory(allocator, stagingAllocation);

    // Transition image layout and copy data
    defaultWhiteTexture->transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    defaultWhiteTexture->copyBufferToImage(stagingBuffer, 1, 1);

    // Generate mipmaps if we have multiple mip levels
    if (defaultWhiteTexture->m_spec.mipLevels > 1) {
        defaultWhiteTexture->generateMipmaps();
    } else {
        // Single mip level, transition to shader read only
        defaultWhiteTexture->transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // Clean up staging buffer
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    RP_CORE_INFO("Created default white texture (1x1 RGBA8)");

    return defaultWhiteTexture;
}

std::shared_ptr<Texture> Texture::createDefaultWhiteCubemapTexture()
{
    // Create a 1x1 white cubemap
    TextureSpecification spec{};
    spec.width = 1;
    spec.height = 1;
    spec.depth = 1;
    spec.type = TextureType::TEXTURECUBE;
    spec.format = TextureFormat::RGBA8;
    spec.filter = TextureFilter::Linear;
    spec.wrap = TextureWrap::Repeat;
    spec.srgb = false;
    spec.mipLevels = 1;

    auto defaultCubemap = std::make_shared<Texture>(spec);

    auto &app = Application::getInstance();
    VmaAllocator allocator = app.getVulkanContext().getVmaAllocator();

    // Staging buffer for 6 faces
    uint32_t whitePixel = 0xFFFFFFFF;
    VkDeviceSize faceSize = sizeof(uint32_t);
    VkDeviceSize imageSize = faceSize * 6;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create staging buffer for default white cubemap!");
        throw std::runtime_error("Failed to create staging buffer for default white cubemap!");
    }

    void *data;
    vmaMapMemory(allocator, stagingAllocation, &data);
    for (int i = 0; i < 6; ++i) {
        memcpy(static_cast<char *>(data) + (i * faceSize), &whitePixel, faceSize);
    }
    vmaUnmapMemory(allocator, stagingAllocation);

    defaultCubemap->transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    defaultCubemap->copyBufferToImage(stagingBuffer, 1, 1);

    // Generate mipmaps if we have multiple mip levels
    if (defaultCubemap->m_spec.mipLevels > 1) {
        defaultCubemap->generateMipmaps();
    } else {
        // Single mip level, transition to shader read only
        defaultCubemap->transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    RP_CORE_INFO("Created default white cubemap texture (1x1x6 RGBA8)");

    return defaultCubemap;
}

void Texture::transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout, size_t threadId)
{
    if (m_image == VK_NULL_HANDLE) {
        RP_CORE_ERROR("Cannot transition image layout: VkImage is VK_NULL_HANDLE");
        throw std::runtime_error("Cannot transition image layout: VkImage is VK_NULL_HANDLE");
    }

    auto &app = Application::getInstance();
    auto graphicsQueue = app.getVulkanContext().getGraphicsQueue();

    // Get or create a command pool for graphics operations
    CommandPoolConfig poolConfig{};
    poolConfig.queueFamilyIndex = app.getVulkanContext().getQueueFamilyIndices().graphicsFamily.value();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolConfig.threadId = threadId;

    auto commandPool = CommandPoolManager::createCommandPool(poolConfig);
    auto commandBuffer = commandPool->getCommandBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange.aspectMask = getImageAspectFlags(m_spec.format);
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = m_spec.mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = isCubeType(m_spec.type) ? 6 : (isArrayType(m_spec.type) ? m_spec.depth : 1);

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        // Depth texture transition
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        RP_CORE_ERROR("Unsupported layout transition!");
        throw std::invalid_argument("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);

    commandBuffer->end();

    graphicsQueue->submitQueue(commandBuffer, VK_NULL_HANDLE);
    graphicsQueue->waitIdle();

    // CommandBuffer and CommandPool will be automatically cleaned up via RAII
}

void Texture::generateMipmaps(size_t threadId)
{
    if (m_spec.mipLevels <= 1) {
        return; // No mipmaps to generate
    }

    if (m_image == VK_NULL_HANDLE) {
        RP_CORE_ERROR("Cannot generate mipmaps: VkImage is VK_NULL_HANDLE");
        throw std::runtime_error("Cannot generate mipmaps: VkImage is VK_NULL_HANDLE");
    }

    // Check if the image format supports linear blitting
    auto &app = Application::getInstance();
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(app.getVulkanContext().getPhysicalDevice(), toVkFormat(m_spec.format, m_spec.srgb),
                                        &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        RP_CORE_ERROR("Texture image format does not support linear blitting for mipmap generation!");
        throw std::runtime_error("Texture image format does not support linear blitting for mipmap generation!");
    }

    auto graphicsQueue = app.getVulkanContext().getGraphicsQueue();

    // Get or create a command pool for graphics operations
    CommandPoolConfig poolConfig{};
    poolConfig.queueFamilyIndex = app.getVulkanContext().getQueueFamilyIndices().graphicsFamily.value();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolConfig.threadId = threadId;

    auto commandPool = CommandPoolManager::createCommandPool(poolConfig);
    auto commandBuffer = commandPool->getCommandBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = m_image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = isCubeType(m_spec.type) ? 6 : (isArrayType(m_spec.type) ? m_spec.depth : 1);
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = static_cast<int32_t>(m_spec.width);
    int32_t mipHeight = static_cast<int32_t>(m_spec.height);

    for (uint32_t i = 1; i < m_spec.mipLevels; i++) {
        // Transition previous mip level to TRANSFER_SRC_OPTIMAL
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);

        // Set up the blit operation
        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = isCubeType(m_spec.type) ? 6 : (isArrayType(m_spec.type) ? m_spec.depth : 1);

        // Calculate next mip level dimensions
        int32_t nextMipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        int32_t nextMipHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {nextMipWidth, nextMipHeight, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = isCubeType(m_spec.type) ? 6 : (isArrayType(m_spec.type) ? m_spec.depth : 1);

        // Perform the blit operation
        vkCmdBlitImage(commandBuffer->getCommandBufferVk(), m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        // Transition the previous mip level to SHADER_READ_ONLY_OPTIMAL
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Update dimensions for next iteration
        mipWidth = nextMipWidth;
        mipHeight = nextMipHeight;
    }

    // Transition the last mip level to SHADER_READ_ONLY_OPTIMAL
    barrier.subresourceRange.baseMipLevel = m_spec.mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    commandBuffer->end();

    graphicsQueue->submitQueue(commandBuffer, VK_NULL_HANDLE);
    graphicsQueue->waitIdle();

    RP_CORE_INFO("Generated {} mip levels for texture", m_spec.mipLevels);
}

void Texture::copyBufferToImage(VkBuffer buffer, uint32_t width, uint32_t height, size_t threadId)
{
    if (m_image == VK_NULL_HANDLE) {
        RP_CORE_ERROR("Cannot copy buffer to image: VkImage is VK_NULL_HANDLE");
        throw std::runtime_error("Cannot copy buffer to image: VkImage is VK_NULL_HANDLE");
    }

    auto &app = Application::getInstance();
    auto queue = app.getVulkanContext().getGraphicsQueue();

    // Get or create a command pool for graphics operations
    CommandPoolConfig poolConfig{};
    poolConfig.queueFamilyIndex = app.getVulkanContext().getQueueFamilyIndices().graphicsFamily.value();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolConfig.threadId = threadId;

    auto commandPool = CommandPoolManager::createCommandPool(poolConfig);
    auto commandBuffer = commandPool->getCommandBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo);

    std::vector<VkBufferImageCopy> bufferCopyRegions;
    uint32_t layerCount = isCubeType(m_spec.type) ? 6 : (isArrayType(m_spec.type) ? m_spec.depth : 1);
    VkDeviceSize offset = 0;
    VkDeviceSize layerSize = static_cast<VkDeviceSize>(width) * height * 4; // Assuming RGBA8

    for (uint32_t layer = 0; layer < layerCount; ++layer) {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = getImageAspectFlags(m_spec.format);
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = layer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};
        bufferCopyRegions.push_back(region);
        offset += layerSize;
    }

    vkCmdCopyBufferToImage(commandBuffer->getCommandBufferVk(), buffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

    commandBuffer->end();

    queue->submitQueue(commandBuffer, VK_NULL_HANDLE);
    queue->waitIdle();
}

} // namespace Rapture
