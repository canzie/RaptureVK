#ifndef RAPTURE__TEXTURE_VIEW_H
#define RAPTURE__TEXTURE_VIEW_H

#include <cstdint>
#include <vulkan/vulkan.h>

namespace Rapture {

class Texture;

enum TextureChannel {
    TEXTURE_CHANNEL_R,
    TEXTURE_CHANNEL_G,
    TEXTURE_CHANNEL_B,
    TEXTURE_CHANNEL_A,
    TEXTURE_CHANNEL_ZERO,
    TEXTURE_CHANNEL_ONE,
    TEXTURE_CHANNEL_COUNT
};

/**
 * @brief Which source channel each output channel of a view reads
 */
struct TextureSwizzle {
    TextureChannel r = TEXTURE_CHANNEL_R;
    TextureChannel g = TEXTURE_CHANNEL_G;
    TextureChannel b = TEXTURE_CHANNEL_B;
    TextureChannel a = TEXTURE_CHANNEL_A;
};

struct TextureViewConfig {
    uint32_t baseMip;
    uint32_t mipCount;
    TextureSwizzle swizzle;
};

/**
 * @brief A view onto a range of a texture's mips, with its channels remapped
 *
 * The view does not keep the texture alive, so it must not outlive it.
 */
class TextureView {
  public:
    /**
     * @brief Creates a view onto a texture
     * @param texture The texture viewed
     * @param config Which mips the view covers and how its channels map
     */
    TextureView(const Texture &texture, const TextureViewConfig &config);
    ~TextureView();

    TextureView(const TextureView &) = delete;
    TextureView &operator=(const TextureView &) = delete;

    VkImageView getImageView() const { return m_imageView; }

  private:
    VkImageView m_imageView = VK_NULL_HANDLE;
};

} // namespace Rapture

#endif // RAPTURE__TEXTURE_VIEW_H
