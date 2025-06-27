#pragma once

#include <vk_mem_alloc.h>
#include "TextureCommon.h"
#include "Buffers/Descriptors/DescriptorBinding.h"
#include <string>
#include <memory>
#include <vector>

namespace Rapture {

class Sampler {
public:
    Sampler(const TextureSpecification& spec);
    Sampler(VkFilter filter, VkSamplerAddressMode wrap);
    ~Sampler();

    VkSampler getSamplerVk() const { return m_sampler; }

private:
    VkSampler m_sampler;
};

class Texture : public std::enable_shared_from_this<Texture> {
public:
    // Constructor for loading from file path
    // when isLoadingAsync is true, it is expected to use the loadImageFromFile manually with the given threadId
    Texture(const std::string& path, TextureSpecification spec = TextureSpecification(), bool isLoadingAsync=false);
    
    // Constructor for loading cubemap from multiple file paths
    Texture(const std::vector<std::string>& paths, TextureSpecification spec = TextureSpecification(), bool isLoadingAsync=false);

    // Constructor for creating texture from specification (no file loading)
    Texture(const TextureSpecification& spec);
    
    ~Texture();


    void loadImageFromFile(size_t threadId=0);

    void copyFromImage(VkImage image, VkImageLayout otherLayout, VkImageLayout newLayout, VkSemaphore waitSemaphore=VK_NULL_HANDLE, VkSemaphore signalSemaphore=VK_NULL_HANDLE, VkCommandBuffer extCommandBuffer=VK_NULL_HANDLE, bool useInternalFence=true);

    // Getters
    VkImage getImage() const { return m_image; }
    VkImageView getImageView() const { return m_imageView; }
    VkImageView getDepthOnlyImageView() const { return m_imageViewDepthOnly; }
    VkImageView getStencilOnlyImageView() const { return m_imageViewStencilOnly; }
    const Sampler& getSampler() const { return *m_sampler; }
    const TextureSpecification& getSpecification() const { return m_spec; }
    VkFormat getFormat() const { return toVkFormat(m_spec.format); }

    // Get descriptor image info for use in descriptor sets
    VkDescriptorImageInfo getDescriptorImageInfo(TextureViewType viewType=TextureViewType::DEFAULT) const;
    // Get descriptor image info for storage images (used in compute shaders)
    VkDescriptorImageInfo getStorageImageDescriptorInfo() const;
    VkImageMemoryBarrier getImageMemoryBarrier(VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask);
    uint32_t getBindlessIndex();

    void setReadyForSampling(bool ready) {
        m_readyForSampling = ready;
    }

    bool isReadyForSampling() const {
        return m_readyForSampling;
    }

    // Static method to create a default white texture
    static std::shared_ptr<Texture> createDefaultWhiteTexture();
    static std::shared_ptr<Texture> createDefaultWhiteCubemapTexture();

private:
    // creates a vulkan image using the specification
    void createImage();
    // creates a vulkan image view using the specification and the image from createImage
    void createImageView();

    void transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout, size_t threadId=0);
    void copyBufferToImage(VkBuffer buffer, uint32_t width, uint32_t height, size_t threadId=0);
    
    // Generate mipmaps for the texture using vkCmdBlitImage
    void generateMipmaps(size_t threadId=0);
    
    // Helper function to validate spec against loaded image data
    bool validateSpecificationAgainstImageData(int width, int height, int channels);
    
    // Helper function to create spec from image file info
    void createSpecificationFromImageFile(const std::vector<std::string>& paths);

private:
    
    std::vector<std::string> m_paths;
    
    std::unique_ptr<Sampler> m_sampler;
    VkImage m_image;
    VkImageView m_imageView{VK_NULL_HANDLE};
    VkImageView m_imageViewStencilOnly{VK_NULL_HANDLE};
    VkImageView m_imageViewDepthOnly{VK_NULL_HANDLE};

    VmaAllocation m_allocation;

    TextureSpecification m_spec;

    bool m_readyForSampling = false;

    uint32_t m_bindlessIndex = UINT32_MAX;

    static std::shared_ptr<DescriptorBindingTexture> s_bindlessTextures;
};

}

