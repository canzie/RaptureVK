#include "Texture.h"
#include "WindowContext/Application.h"
#include "Logging/Log.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"

#include "stb_image.h"

#include <stdexcept>

namespace Rapture { 

// Sampler implementation
Sampler::Sampler(const TextureSpecification& spec) {
    auto& app = Application::getInstance();
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
    auto& app = Application::getInstance();
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

Sampler::~Sampler() {
    auto& app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();
    
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
    }
}

// Texture implementation
Texture::Texture(const std::string& path, TextureFilter filter, TextureWrap wrap, bool isLoadingAsync) 
    : m_path(path), m_loadFromFile(true), 
    m_image(VK_NULL_HANDLE), m_allocation(VK_NULL_HANDLE), m_spec({}), m_sampler(nullptr) {
    
    
    // First, create specification from image file
    createSpecificationFromImageFile(path, filter, wrap);
    
    // Initialize sampler with the spec (will be reconstructed after spec is finalized)
    m_sampler = std::make_unique<Sampler>(m_spec);
    
    // Create the image first, then load data into it
    createImage();
    createImageView();
    if (!isLoadingAsync) {
        loadImageFromFile();
    }
}

Texture::Texture(const TextureSpecification& spec) 
    : m_spec(spec), m_loadFromFile(false), 
    m_image(VK_NULL_HANDLE), m_allocation(VK_NULL_HANDLE), m_sampler(nullptr) {
    
    
    m_sampler = std::make_unique<Sampler>(spec);
    // Create the image and image view
    createImage();
    createImageView();
    
    // For depth textures, transition to the correct layout
    if (isDepthFormat(m_spec.format)) {
        transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        RP_CORE_INFO("Transitioned depth texture to DEPTH_STENCIL_ATTACHMENT_OPTIMAL layout");
    }
    //m_readyForSampling = true;
}

Texture::~Texture() {
    auto& app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();
    VmaAllocator allocator = app.getVulkanContext().getVmaAllocator();

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

void Texture::createSpecificationFromImageFile(const std::string& path, TextureFilter filter, TextureWrap wrap) {
    int width, height, channels;
    
    // Use stbi_info to get image dimensions without loading the full image
    if (!stbi_info(path.c_str(), &width, &height, &channels)) {
        RP_CORE_ERROR("Failed to get image info for: {}", path);
        throw std::runtime_error("Failed to get image info for: " + path);
    }
    
    
    // Set basic properties
    m_spec.width = static_cast<uint32_t>(width);
    m_spec.height = static_cast<uint32_t>(height);
    m_spec.depth = 1; // 2D texture
    m_spec.type = TextureType::TEXTURE2D;
    
    // Always use RGBA8 format since we force 4 channels during loading
    m_spec.format = TextureFormat::RGBA8;
    

    
    // Set default values for other properties
    m_spec.filter = TextureFilter::Linear;
    m_spec.wrap = TextureWrap::Repeat;
    m_spec.srgb = false; // Assume sRGB for color textures
    m_spec.mipLevels = 1; // No mipmapping for now
}

bool Texture::validateSpecificationAgainstImageData(int width, int height, int channels) {
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

void Texture::loadImageFromFile(size_t threadId) {
    int width, height, channels;
    
    // Force 4 channels (RGBA) for consistency
    int desiredChannels = 4;
    stbi_uc* pixels = stbi_load(m_path.c_str(), &width, &height, &channels, desiredChannels);
    
    if (!pixels) {
        RP_CORE_ERROR("Failed to load texture image: {}", m_path);
        throw std::runtime_error("Failed to load texture image: " + m_path);
    }
    
    // Spec format is already correctly set to RGBA8
    
    
    // Validate against our specification
    validateSpecificationAgainstImageData(width, height, desiredChannels);
    
    VkDeviceSize imageSize = width * height * desiredChannels;
    
    auto& app = Application::getInstance();
    VmaAllocator allocator = app.getVulkanContext().getVmaAllocator();
    
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
        stbi_image_free(pixels);
        RP_CORE_ERROR("Failed to create staging buffer for texture!");
        throw std::runtime_error("Failed to create staging buffer for texture!");
    }
    
    // Copy pixel data to staging buffer
    void* data;
    vmaMapMemory(allocator, stagingAllocation, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vmaUnmapMemory(allocator, stagingAllocation);
    
    stbi_image_free(pixels);
    
    // Transition image layout and copy data
    transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, threadId);
    copyBufferToImage(stagingBuffer, static_cast<uint32_t>(width), static_cast<uint32_t>(height), threadId);
    transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, threadId);
    
    // Clean up staging buffer
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
}

void Texture::copyFromImage(
    VkImage image, 
    VkImageLayout otherLayout, 
    VkImageLayout newLayout,
    VkSemaphore waitSemaphore,
    VkSemaphore signalSemaphore) {
    if (m_image == VK_NULL_HANDLE || image == VK_NULL_HANDLE) {
        RP_CORE_ERROR("Cannot copy image: One or both VkImages are VK_NULL_HANDLE");
        throw std::runtime_error("Cannot copy image: One or both VkImages are VK_NULL_HANDLE");
    }

    auto& app = Application::getInstance();
    auto graphicsQueue = app.getVulkanContext().getGraphicsQueue();
    
    // Get or create a command pool for graphics operations
    CommandPoolConfig poolConfig{};
    poolConfig.queueFamilyIndex = app.getVulkanContext().getQueueFamilyIndices().graphicsFamily.value();
    poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    
    auto commandPool = CommandPoolManager::createCommandPool(poolConfig);
    auto commandBuffer = commandPool->getCommandBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo);

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
    sourceBarrier.subresourceRange.layerCount = isArrayType(m_spec.type) ? m_spec.depth : 1;
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
    destBarrier.subresourceRange.layerCount = isArrayType(m_spec.type) ? m_spec.depth : 1;
    destBarrier.srcAccessMask = 0;
    destBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    VkImageMemoryBarrier imageMemoryBarriers[] = {sourceBarrier, destBarrier};

    // Execute the layout transitions
    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        2, imageMemoryBarriers
    );

    // Copy the image with component mapping to handle color channel ordering
    VkImageCopy copyRegion{};
    copyRegion.srcSubresource.aspectMask = getImageAspectFlags(m_spec.format);
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = isArrayType(m_spec.type) ? m_spec.depth : 1;
    copyRegion.srcOffset = {0, 0, 0};
    copyRegion.dstSubresource.aspectMask = getImageAspectFlags(m_spec.format);
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = isArrayType(m_spec.type) ? m_spec.depth : 1;
    copyRegion.dstOffset = {0, 0, 0};
    copyRegion.extent = {m_spec.width, m_spec.height, m_spec.depth};

    // Create a blit command to handle the color channel swap
    VkImageBlit blitRegion{};
    blitRegion.srcSubresource.aspectMask = getImageAspectFlags(m_spec.format);
    blitRegion.srcSubresource.mipLevel = 0;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = isArrayType(m_spec.type) ? m_spec.depth : 1;
    blitRegion.srcOffsets[0] = {0, 0, 0};
    blitRegion.srcOffsets[1] = {(int32_t)m_spec.width, (int32_t)m_spec.height, 1};
    blitRegion.dstSubresource.aspectMask = getImageAspectFlags(m_spec.format);
    blitRegion.dstSubresource.mipLevel = 0;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = isArrayType(m_spec.type) ? m_spec.depth : 1;
    blitRegion.dstOffsets[0] = {0, 0, 0};
    blitRegion.dstOffsets[1] = {(int32_t)m_spec.width, (int32_t)m_spec.height, 1};

    // Use blit instead of copy to handle color channel ordering
    vkCmdBlitImage(
        commandBuffer->getCommandBufferVk(),
        image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        m_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &blitRegion,
        VK_FILTER_NEAREST
    );

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
    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        2, finalBarriers
    );

    commandBuffer->end();

    // Submit the command buffer
    VkSubmitInfo submitInfo{};
    if (waitSemaphore != VK_NULL_HANDLE) {
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSemaphore;
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
        submitInfo.pWaitDstStageMask = waitStages;
    }
    if (signalSemaphore != VK_NULL_HANDLE) {
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signalSemaphore;
    }
    //graphicsQueue->addCommandBuffer(commandBuffer);

    graphicsQueue->submitQueue(commandBuffer, submitInfo, VK_NULL_HANDLE);
    graphicsQueue->waitIdle();
}

VkImageMemoryBarrier Texture::getImageMemoryBarrier(VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask)
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
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    return barrier;
}

void Texture::createImage()
{
    auto& app = Application::getInstance();
    VmaAllocator allocator = app.getVulkanContext().getVmaAllocator();

    if (m_spec.width == 0 || m_spec.height == 0 || m_spec.depth == 0) {
        RP_CORE_ERROR("Texture::createImage() - Invalid texture specification --- dimesnions should be greater than 0! width: {}, height: {}, depth: {}", m_spec.width, m_spec.height, m_spec.depth);
        throw std::runtime_error("Texture::createImage() - Invalid texture specification!");
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = toVkImageType(m_spec.type);
    imageInfo.extent.width = m_spec.width;
    imageInfo.extent.height = m_spec.height;
    imageInfo.extent.depth = isArrayType(m_spec.type) ? 1 : m_spec.depth;
    imageInfo.mipLevels = m_spec.mipLevels;
    imageInfo.arrayLayers = isArrayType(m_spec.type) ? m_spec.depth : 1;
    imageInfo.format = toVkFormat(m_spec.format, m_spec.srgb);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Set usage flags based on format type
    if (isDepthFormat(m_spec.format)) {
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    } else {
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        
        // Add storage image usage for compute shaders if requested
        if (m_spec.storageImage) {
            imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
    }
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_image, &m_allocation, nullptr) != VK_SUCCESS) {
        RP_CORE_ERROR("Texture::createImage() - Failed to create image!");
        throw std::runtime_error("Texture::createImage() - Failed to create image!");
    }
}

void Texture::createImageView() {
    auto& app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = toVkImageViewType(m_spec.type);
    viewInfo.format = toVkFormat(m_spec.format, m_spec.srgb);
    viewInfo.subresourceRange.aspectMask = getImageAspectFlags(m_spec.format);
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_spec.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = isArrayType(m_spec.type) ? m_spec.depth : 1;


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

VkDescriptorImageInfo Texture::getDescriptorImageInfo(TextureViewType viewType) const {
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
    if ( imageInfo.imageView == VK_NULL_HANDLE) {
        RP_CORE_WARN("Texture::getDescriptorImageInfo - Invalid texture view type! Using default view.");
        imageInfo.imageView = m_imageView;
    }

    imageInfo.sampler = m_sampler->getSamplerVk();
    return imageInfo;
}

VkDescriptorImageInfo Texture::getStorageImageDescriptorInfo() const {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // Storage images use GENERAL layout
    imageInfo.imageView = m_imageView;
    imageInfo.sampler = VK_NULL_HANDLE; // Storage images don't use samplers
    return imageInfo;
}

std::shared_ptr<Texture> Texture::createDefaultWhiteTexture() {

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
    auto& app = Application::getInstance();
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
    void* data;
    vmaMapMemory(allocator, stagingAllocation, &data);
    memcpy(data, &whitePixel, imageSize);
    vmaUnmapMemory(allocator, stagingAllocation);
    
    // Transition image layout and copy data
    defaultWhiteTexture->transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    defaultWhiteTexture->copyBufferToImage(stagingBuffer, 1, 1);
    defaultWhiteTexture->transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    // Clean up staging buffer
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    
    RP_CORE_INFO("Created default white texture (1x1 RGBA8)");

    
    return defaultWhiteTexture;
}

void Texture::transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout, size_t threadId) {
    if (m_image == VK_NULL_HANDLE) {
        RP_CORE_ERROR("Cannot transition image layout: VkImage is VK_NULL_HANDLE");
        throw std::runtime_error("Cannot transition image layout: VkImage is VK_NULL_HANDLE");
    }
    
    auto& app = Application::getInstance();
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
    barrier.subresourceRange.layerCount = isArrayType(m_spec.type) ? m_spec.depth : 1;

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
    } else {
        RP_CORE_ERROR("Unsupported layout transition!");
        throw std::invalid_argument("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    commandBuffer->end();

    //VkSubmitInfo submitInfo{};
    //submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    //submitInfo.commandBufferCount = 1;
    //VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();
    //submitInfo.pCommandBuffers = &commandBufferVk;
    
    //graphicsQueue->addCommandBuffer(commandBuffer);
    graphicsQueue->submitQueue(commandBuffer, VK_NULL_HANDLE);
    graphicsQueue->waitIdle();
    

    // CommandBuffer and CommandPool will be automatically cleaned up via RAII
}

void Texture::copyBufferToImage(VkBuffer buffer, uint32_t width, uint32_t height, size_t threadId) {
    if (m_image == VK_NULL_HANDLE) {
        RP_CORE_ERROR("Cannot copy buffer to image: VkImage is VK_NULL_HANDLE");
        throw std::runtime_error("Cannot copy buffer to image: VkImage is VK_NULL_HANDLE");
    }
    
    auto& app = Application::getInstance();
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

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = getImageAspectFlags(m_spec.format);
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = isArrayType(m_spec.type) ? m_spec.depth : 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer->getCommandBufferVk(), buffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    commandBuffer->end();

    //VkSubmitInfo submitInfo{};
    //submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    //submitInfo.commandBufferCount = 1;
    //VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();
    //submitInfo.pCommandBuffers = &commandBufferVk;

    
    auto& vulkanContext = app.getVulkanContext();
    //queue->addCommandBuffer(commandBuffer);
    queue->submitQueue(commandBuffer, VK_NULL_HANDLE);
    queue->waitIdle();

}

}
