#ifndef RAPTURE__TEXTURE_H
#define RAPTURE__TEXTURE_H

#include "TextureCommon.h"
#include "gpu/descriptors/DescriptorBinding.h"

#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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

    /**
     * @brief The view to render into, which covers mip 0 alone
     *
     * A rendering attachment must reference a single mip, while the sampled view spans the whole
     * chain, so the two differ once a texture is mipped.
     * @return The attachment view
     */
    VkImageView getAttachmentImageView() const
    {
        return m_imageViewAttachment != VK_NULL_HANDLE ? m_imageViewAttachment : m_imageView;
    }

    VkImageView getDepthOnlyImageView() const { return m_imageViewDepthOnly; }
    VkImageView getStencilOnlyImageView() const { return m_imageViewStencilOnly; }

    const Sampler &getSampler() const { return *m_sampler; }
    const TextureSpecification &getSpecification() const { return m_spec; }
    VkFormat getFormat() const { return toVkFormat(m_spec.format, m_spec.srgb); }

    /**
     * @brief Approximate GPU footprint of this texture in bytes across all mips and layers
     * @return The estimated image size derived from the specification
     */
    uint64_t getSizeBytes() const;

    /**
     * @brief Copy the image contents back to CPU memory, tightly packed mip-major across all layers
     *
     * The spec must have allowReadback set. The returned buffer is laid out mip by mip, each mip
     * holding its full layer set contiguously, sized to match getSizeBytes().
     * @return The image bytes, or empty on failure
     */
    std::vector<uint8_t> readbackData();

    /**
     * @brief Copy one rectangle of mip zero back to CPU memory, tightly packed
     *
     * The spec must have allowReadback set. Blocking: the copy is submitted and waited on before
     * the bytes are returned.
     * @param x Region origin x in pixels
     * @param y Region origin y in pixels
     * @param width Region width in pixels
     * @param height Region height in pixels
     * @return The region's bytes, or empty on failure
     */
    std::vector<uint8_t> readbackRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

    /**
     * @brief Serialize this texture into a self-contained blob
     * @param sourcePath The texture's source file
     * @return The serialized bytes, or empty on failure
     */
    std::vector<uint8_t> serialize(std::string_view sourcePath);

    /**
     * @brief Rebuild a texture from a serialized blob
     * @param blob The serialized bytes
     * @return The texture, or nullptr on failure
     */
    static std::unique_ptr<Texture> deserialize(std::span<const uint8_t> blob);

    /**
     * @brief The source path referenced by a serialized texture blob
     * @param blob The serialized bytes
     * @return The path, or empty if absent
     */
    static std::string readBlobSourcePath(std::span<const uint8_t> blob);

    VkDescriptorImageInfo getDescriptorImageInfo(TextureViewType viewType = TextureViewType::DEFAULT) const;

    /**
     * @brief Get a storage-image descriptor bound to a single mip of a cubemap
     *
     * Storage-image views must be single-mip, so prefilter-style passes that write one
     * roughness mip at a time bind the matching 2D-array (6-layer) view here.
     * @param mip Mip level to bind
     * @return Descriptor image info in GENERAL layout with no sampler
     */
    VkDescriptorImageInfo getStorageMipDescriptorInfo(uint32_t mip) const;

    uint32_t getBindlessIndex();

    /**
     * @brief The layout the texture was last transitioned into
     *
     * Tracked as commands are recorded, so it is only sound while submission stays linear and
     * single-queue, and while a texture is not transitioned from two threads at once.
     * @return The tracked layout, UNDEFINED before the first transition
     */
    VkImageLayout getCurrentLayout() const { return m_currentLayout; }

    /**
     * @brief Records a layout the image was put into by something that did not go through Texture
     * @param layout The layout the image is now in
     */
    void setCurrentLayout(VkImageLayout layout) { m_currentLayout = layout; }

    /**
     * @brief Builds a barrier out of the tracked layout, and records the new one
     * @param newLayout Layout to transition into
     * @param srcAccessMask Access to make available
     * @param dstAccessMask Access to make visible
     * @return The barrier, for the caller to submit
     */
    VkImageMemoryBarrier getImageMemoryBarrier(VkImageLayout newLayout, VkAccessFlags srcAccessMask,
                                               VkAccessFlags dstAccessMask);

    /**
     * @brief Builds a barrier out of an explicit old layout, and records the new one
     * @param oldLayout Layout the image is assumed to be in
     * @param newLayout Layout to transition into
     * @param srcAccessMask Access to make available
     * @param dstAccessMask Access to make visible
     * @return The barrier, for the caller to submit
     */
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
    static std::unique_ptr<Texture> createDefaultFlatNormalTexture();
    static std::unique_ptr<Texture> createDefaultWhiteCubemapTexture();

  private:
    Texture(TextureSpecification spec, bool deferLoading);

    void createImage();
    void createImageView();
    void uploadInitialData(const std::vector<std::span<const uint8_t>> &layerData);

    /**
     * @brief Upload packed bytes into every mip and layer of the image, then mark it ready
     * @param bytes The tightly packed bytes matching this texture's layout
     */
    void uploadCompressedBlob(std::span<const uint8_t> bytes);

    void recordTransitionImageLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);
    void recordCopyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, uint32_t width, uint32_t height);
    void recordGenerateMipmaps(VkCommandBuffer cmd);

    void transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, uint32_t width, uint32_t height);
    void generateMipmaps();

    std::unique_ptr<Sampler> m_sampler;
    VkImage m_image = VK_NULL_HANDLE;
    VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkImageView m_imageViewAttachment = VK_NULL_HANDLE;
    VkImageView m_imageViewStencilOnly = VK_NULL_HANDLE;
    VkImageView m_imageViewDepthOnly = VK_NULL_HANDLE;
    VkImageView m_imageViewStorage = VK_NULL_HANDLE;
    std::vector<VkImageView> m_imageViewStorageMips;

    VmaAllocation m_allocation = VK_NULL_HANDLE;

    TextureSpecification m_spec;

    std::atomic<TextureStatus> m_status{TextureStatus::NOT_LOADED};

    uint32_t m_bindlessIndex = UINT32_MAX;

    static std::shared_ptr<DescriptorBindingTexture> s_bindlessTextures;
};

} // namespace Rapture

#endif // RAPTURE__TEXTURE_H
