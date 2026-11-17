#include <rapture/core/utils/Rgba8Image.h>

// static so the writer cannot collide with another target that also builds stb_image_write
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif // __GNUC__
#include <stb_image_write.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // __GNUC__

#include <algorithm>
#include <cstring>

namespace Rapture {

static constexpr uint32_t RGBA8_BYTES_PER_PIXEL = 4;

Rgba8Image Rgba8Image_crop(const Rgba8Image &image, int64_t x, int64_t y, int64_t width, int64_t height)
{
    const int64_t left = std::clamp<int64_t>(x, 0, image.width);
    const int64_t top = std::clamp<int64_t>(y, 0, image.height);
    const int64_t right = std::clamp<int64_t>(x + width, 0, image.width);
    const int64_t bottom = std::clamp<int64_t>(y + height, 0, image.height);
    if (right <= left || bottom <= top) {
        return {};
    }

    Rgba8Image cropped;
    cropped.width = static_cast<uint32_t>(right - left);
    cropped.height = static_cast<uint32_t>(bottom - top);
    cropped.pixels.resize(static_cast<size_t>(cropped.width) * cropped.height * RGBA8_BYTES_PER_PIXEL);

    const size_t sourceStride = static_cast<size_t>(image.width) * RGBA8_BYTES_PER_PIXEL;
    const size_t rowBytes = static_cast<size_t>(cropped.width) * RGBA8_BYTES_PER_PIXEL;
    for (uint32_t row = 0; row < cropped.height; row++) {
        const uint8_t *source = image.pixels.data() + (static_cast<size_t>(top) + row) * sourceStride +
                                static_cast<size_t>(left) * RGBA8_BYTES_PER_PIXEL;
        std::memcpy(cropped.pixels.data() + row * rowBytes, source, rowBytes);
    }

    return cropped;
}

bool Rgba8Image_writePng(const Rgba8Image &image, const std::filesystem::path &path)
{
    if (image.width == 0 || image.height == 0) {
        return false;
    }

    const int stride = static_cast<int>(image.width * RGBA8_BYTES_PER_PIXEL);
    return stbi_write_png(path.string().c_str(), static_cast<int>(image.width), static_cast<int>(image.height),
                          static_cast<int>(RGBA8_BYTES_PER_PIXEL), image.pixels.data(), stride) != 0;
}

} // namespace Rapture
