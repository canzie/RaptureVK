#ifndef RAPTURE__TEXTURE_H
#define RAPTURE__TEXTURE_H

#include "Buffers/Descriptors/DescriptorBinding.h"
#include "TextureCommon.h"

#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <vk_mem_alloc.h>

namespace Rapture {

struct Counter;

class Sampler {
  public:
    Sampler(const TextureSpecification &spec);
    Sampler(VkFilter filter, VkSamplerAddressMode wrap);
    ~Sampler();

    VkSampler getSamplerVk() const { return m_sampler; }

  private:
    VkSampler m_sampler;
};

class Texture {
  public:
    /**
     * @brief Create texture from specification only (no file loading)
     */
    explicit Texture(TextureSpecification spec);

    /**
     * @brief Create texture from file path (synchronous)
     */
    explicit Texture(const std::string &path, TextureSpecification spec = TextureSpecification());

    /**
     * @brief Create cubemap from 6 file paths (synchronous)
     */
    explicit Texture(const std::vector<std::string> &paths, TextureSpecification spec = TextureSpecification());

    ~Texture();

    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

    /**
     * @brief Load texture asynchronously via job system, optionally decrements counter when done
     */
    static std::unique_ptr<Texture> loadAsync(const std::string &path,
                                              TextureSpecification spec = TextureSpecification(),
                                              Counter *completionCounter = nullptr);

    /**
     * @brief Load cubemap asynchronously via job system, optionally decrements counter when done
     */
    static std::unique_ptr<Texture> loadAsync(const std::vector<std::string> &paths,
                                              TextureSpecification spec = TextureSpecification(),
                                              Counter *completionCounter = nullptr);

    TextureStatus getStatus() const { return m_status.load(std::memory_order_acquire); }
    bool isReady() const { return getStatus() == TextureStatus::READY; }

    VkImage getImage() const { return m_image; }
    VkImageView getImageView() const { return m_imageView; }
    VkImageView getDepthOnlyImageView() const { return m_imageViewDepthOnly; }
    VkImageView getStencilOnlyImageView() const { return m_imageViewStencilOnly; }

    const Sampler &getSampler() const { return *m_sampler; }
    const TextureSpecification &getSpecification() const { return m_spec; }
    VkFormat getFormat() const { return toVkFormat(m_spec.format); }

    VkDescriptorImageInfo getDescriptorImageInfo(TextureViewType viewType = TextureViewType::DEFAULT) const;
    VkDescriptorImageInfo getStorageImageDescriptorInfo() const;
    uint32_t getBindlessIndex();

    VkImageMemoryBarrier getImageMemoryBarrier(VkImageLayout oldLayout, VkImageLayout newLayout,
                                               VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask);

    void uploadData(std::span<const uint8_t> data);

    /**
     * @brief Set a single pixel value (fire-and-forget, no GPU wait)
     */
    void setPixel(uint32_t x, uint32_t y, uint32_t rgba);

    /**
     * @brief Set a single pixel value for 3D textures (fire-and-forget, no GPU wait)
     */
    void setPixel(uint32_t x, uint32_t y, uint32_t z, uint32_t rgba);

    /**
     * @brief Set multiple pixels from raw data (fire-and-forget, no GPU wait)
     */
    void setPixels(std::span<const uint8_t> data);

    void copyFromImage(VkImage image, VkImageLayout otherLayout, VkImageLayout newLayout,
                       VkSemaphore waitSemaphore = VK_NULL_HANDLE, VkSemaphore signalSemaphore = VK_NULL_HANDLE,
                       VkCommandBuffer extCommandBuffer = VK_NULL_HANDLE, bool useInternalFence = true);

    static std::unique_ptr<Texture> createDefaultWhiteTexture();
    static std::unique_ptr<Texture> createDefaultWhiteCubemapTexture();

  private:
    Texture(const std::vector<std::string> &paths, TextureSpecification spec, bool deferLoading);

    void createImage();
    void createImageView();
    void createSpecificationFromImageFile(const std::vector<std::string> &paths);
    bool validateSpecificationAgainstImageData(int width, int height, int channels);

    void recordTransitionImageLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);
    void recordCopyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, uint32_t width, uint32_t height);
    void recordGenerateMipmaps(VkCommandBuffer cmd);

    void transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, uint32_t width, uint32_t height);
    void generateMipmaps();
    void loadImageFromFileSync();

    void startAsyncLoad(Counter *completionCounter);

    std::vector<std::string> m_paths;

    std::unique_ptr<Sampler> m_sampler;
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkImageView m_imageViewStencilOnly = VK_NULL_HANDLE;
    VkImageView m_imageViewDepthOnly = VK_NULL_HANDLE;

    VmaAllocation m_allocation = VK_NULL_HANDLE;

    TextureSpecification m_spec;

    std::atomic<TextureStatus> m_status{TextureStatus::NOT_LOADED};

    uint32_t m_bindlessIndex = UINT32_MAX;

    static std::shared_ptr<DescriptorBindingTexture> s_bindlessTextures;
};

} // namespace Rapture

#endif // RAPTURE__TEXTURE_H
