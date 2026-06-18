#include "AssetHelpers.h"

#include "stb_image.h"

namespace Rapture {

bool getImageDimensions(const std::filesystem::path &path, uint32_t &width, uint32_t &height)
{
    int w, h, channels;
    if (!stbi_info(path.string().c_str(), &w, &h, &channels)) {
        RP_CORE_ERROR("Failed to read image info: {}", path.string());
        return false;
    }
    width = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);
    return true;
}

DecodedImageData decodeImageFile(const std::filesystem::path &path)
{
    DecodedImageData result;
    int width, height, channels;
    stbi_uc *pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        RP_CORE_ERROR("Failed to decode image: {}", path.string());
        return result;
    }

    result.width = static_cast<uint32_t>(width);
    result.height = static_cast<uint32_t>(height);
    result.pixels.assign(pixels, pixels + (static_cast<size_t>(width) * height * 4));
    result.success = true;

    stbi_image_free(pixels);
    return result;
}

DecodedImageData decodeImageMemory(std::span<const uint8_t> data)
{
    DecodedImageData result;
    int width, height, channels;
    stbi_uc *pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels, 4);
    if (!pixels) {
        RP_CORE_ERROR("Failed to decode image from memory");
        return result;
    }

    result.width = static_cast<uint32_t>(width);
    result.height = static_cast<uint32_t>(height);
    result.pixels.assign(pixels, pixels + (static_cast<size_t>(width) * height * 4));
    result.success = true;

    stbi_image_free(pixels);
    return result;
}

} // namespace Rapture
