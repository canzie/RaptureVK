#ifndef RAPTURE__TEXTURE_H
#define RAPTURE__TEXTURE_H

#include "TextureCommon.h"
#include "buffers/descriptors/DescriptorBinding.h"

#include <atomic>
#include <memory>
#include <span>
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
     * @brief Create texture from specification only (no pixel data)
     */
    explicit Texture(TextureSpecification spec);

    /**
     * @brief Create texture from already-decoded pixel data (synchronous upload)
     */
    explicit Texture(TextureSpecification spec, std::span<const uint8_t> data);

    /**
     * @brief Create array/cubemap texture from already-decoded per-layer pixel data (synchronous upload)
     */
    explicit Texture(TextureSpecification spec, const std::vector<std::span<const uint8_t>> &layerData);

    ~Texture();

    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

    /**
     * @brief Create an image/view immediately, with upload deferred to a later uploadDataAsync() call
     */
    static std::unique_ptr<Texture> createPlaceholder(TextureSpecification spec);

    /**
     * @brief Upload already-decoded pixel data via the job system, optionally decrements counter when done
     */
    void uploadDataAsync(std::vector<uint8_t> data, Counter *completionCounter = nullptr);

    /**
     * @brief Mark the texture load as failed (e.g. source decode failed upstream)
     */
    void markFailed() { m_status.store(TextureStatus::FAILED, std::memory_order_release); }

    /**
     * @brief Mark the texture as ready
     */
    void markReady() { m_status.store(TextureStatus::READY, std::memory_order_release); }

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
    uint32_t getBindlessIndex();

    VkImageMemoryBarrier getImageMemoryBarrier(VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccessMask,
                                               VkAccessFlags dstAccessMask);

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
    Texture(TextureSpecification spec, bool deferLoading);

    void createImage();
    void createImageView();
    void uploadInitialData(const std::vector<std::span<const uint8_t>> &layerData);

    void recordTransitionImageLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);
    void recordCopyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, uint32_t width, uint32_t height);
    void recordGenerateMipmaps(VkCommandBuffer cmd);

    void transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, uint32_t width, uint32_t height);
    void generateMipmaps();

    std::unique_ptr<Sampler> m_sampler;
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkImageView m_imageViewStencilOnly = VK_NULL_HANDLE;
    VkImageView m_imageViewDepthOnly = VK_NULL_HANDLE;
    VkImageView m_imageViewStorage = VK_NULL_HANDLE;

    VmaAllocation m_allocation = VK_NULL_HANDLE;

    TextureSpecification m_spec;

    std::atomic<TextureStatus> m_status{TextureStatus::NOT_LOADED};

    uint32_t m_bindlessIndex = UINT32_MAX;

    static std::shared_ptr<DescriptorBindingTexture> s_bindlessTextures;
};

} // namespace Rapture

#endif // RAPTURE__TEXTURE_H
