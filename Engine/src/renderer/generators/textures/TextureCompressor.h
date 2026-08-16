#ifndef RAPTURE__TEXTURE_COMPRESSOR_H
#define RAPTURE__TEXTURE_COMPRESSOR_H

#include "gpu/textures/TextureCommon.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Rapture {

class Texture;
class JobContext;

/**
 * @brief GPU block-compresses decoded RGBA8 pixels into BC1/BC3/BC4/BC5 textures
 *
 * Construct with the decoded source pixels, which are uploaded once to a temporary GPU texture
 * with a full mip chain. Each compressToBCx() then encodes that source into a destination texture
 * created with the matching BC format, filling every mip level and marking it ready.
 */
class TextureCompressor {
  public:
    TextureCompressor(std::vector<uint8_t> rgba8, uint32_t width, uint32_t height);
    ~TextureCompressor();

    TextureCompressor(const TextureCompressor &) = delete;
    TextureCompressor &operator=(const TextureCompressor &) = delete;

    bool isValid() const { return m_isValid; }

    bool compressToBC1(JobContext &jctx, Texture &dst);
    bool compressToBC3(JobContext &jctx, Texture &dst);
    bool compressToBC4(JobContext &jctx, Texture &dst);
    bool compressToBC5(JobContext &jctx, Texture &dst);

    /**
     * @brief Destroy the cached block-compression encoder shaders
     *
     * Must be called while the Vulkan device is still alive so each shader's
     * shader module and descriptor set layouts are destroyed in time.
     */
    static void shutdown();

  private:
    bool encode(JobContext &jctx, Texture &dst, TextureFormat format);

    std::unique_ptr<Texture> m_source;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_mipLevels = 0;
    bool m_isValid = false;

    static constexpr uint32_t WORKGROUP_SIZE = 8;
};

} // namespace Rapture

#endif // RAPTURE__TEXTURE_COMPRESSOR_H
