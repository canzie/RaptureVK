#pragma once

#include <vma/vk_mem_alloc.h>
#include "TextureCommon.h"
#include <string>
#include <memory>

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




class Texture {
public:
    // Constructor for loading from file path
    // when isLoadingAsync is true, it is expected to use the loadImageFromFile manually with the given threadId
    Texture(const std::string& path, TextureFilter filter=TextureFilter::Linear, TextureWrap wrap=TextureWrap::Repeat, bool isLoadingAsync=false);
    
    // Constructor for creating texture from specification (no file loading)
    Texture(const TextureSpecification& spec);
    
    ~Texture();

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
    
    // Static method to create a default white texture
    static std::shared_ptr<Texture> createDefaultWhiteTexture();

    void loadImageFromFile(size_t threadId=0);

    void setReadyForSampling(bool ready) {
        m_readyForSampling = ready;
    }

    bool isReadyForSampling() const {
        return m_readyForSampling;
    }

    void copyFromImage(VkImage image, VkImageLayout otherLayout, VkImageLayout newLayout, VkSemaphore waitSemaphore=VK_NULL_HANDLE, VkSemaphore signalSemaphore=VK_NULL_HANDLE);

    VkImageMemoryBarrier getImageMemoryBarrier(VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask);

private:
    void createImage();
    void createImageView();
    void transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout, size_t threadId=0);
    void copyBufferToImage(VkBuffer buffer, uint32_t width, uint32_t height, size_t threadId=0);
    
    // Helper function to validate spec against loaded image data
    bool validateSpecificationAgainstImageData(int width, int height, int channels);
    
    // Helper function to create spec from image file info
    void createSpecificationFromImageFile(const std::string& path, TextureFilter filter, TextureWrap wrap);

private:
    std::string m_path;
    bool m_loadFromFile;
    
    std::unique_ptr<Sampler> m_sampler;
    VkImage m_image;
    VkImageView m_imageView{VK_NULL_HANDLE};
    VkImageView m_imageViewStencilOnly{VK_NULL_HANDLE};
    VkImageView m_imageViewDepthOnly{VK_NULL_HANDLE};

    VmaAllocation m_allocation;


    TextureSpecification m_spec;

    bool m_readyForSampling = false;
};

}

