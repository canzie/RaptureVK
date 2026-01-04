#pragma once

#include <cmath>
#include <cstdint>
#include <vulkan/vulkan.h>

#include "Logging/Log.h"

namespace Rapture {

// Texture filtering modes
enum class TextureFilter {
    Nearest,              // VK_FILTER_NEAREST
    Linear,               // VK_FILTER_LINEAR
    NearestMipmapNearest, // Min/Mag: NEAREST, MipmapMode: NEAREST
    LinearMipmapNearest,  // Min/Mag: LINEAR,  MipmapMode: NEAREST
    NearestMipmapLinear,  // Min/Mag: NEAREST, MipmapMode: LINEAR
    LinearMipmapLinear    // Min/Mag: LINEAR,  MipmapMode: LINEAR
};

// Texture wrapping modes
enum class TextureWrap {
    ClampToEdge,    // VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    MirroredRepeat, // VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
    Repeat,         // VK_SAMPLER_ADDRESS_MODE_REPEAT
    ClampToBorder   // VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
};

enum class TextureFormat : uint16_t {
    RGBA8, // VK_FORMAT_R8G8B8A8_UNORM or VK_FORMAT_R8G8B8A8_SRGB
    RGB8,  // VK_FORMAT_R8G8B8_UNORM or VK_FORMAT_R8G8B8_SRGB (requires swizzle or different view format if HW doesn't support well)
    BGRA8, // VK_FORMAT_B8G8R8A8_UNORM or VK_FORMAT_B8G8R8A8_SRGB
    RGBA16F,    // VK_FORMAT_R16G16B16A16_SFLOAT
    RGB16F,     // VK_FORMAT_R16G16B16_SFLOAT
    RGBA32F,    // VK_FORMAT_R32G32B32A32_SFLOAT
    RGB32F,     // VK_FORMAT_R32G32B32_SFLOAT
    R11G11B10F, // VK_FORMAT_B10G11R11_UFLOAT_PACK32 (Note: BGR order in Vulkan common format)
    RG16F,      // VK_FORMAT_R16G16_SFLOAT
    R16F,       // VK_FORMAT_R16_SFLOAT
    R8UI,       // VK_FORMAT_R8_UINT
    R8U,        // VK_FORMAT_R8_SINT
    D32F,       // VK_FORMAT_D32_SFLOAT
    D24S8       // VK_FORMAT_D24_UNORM_S8_UINT
};

enum class TextureType : uint8_t {
    TEXTURE1D,
    TEXTURE2D,
    TEXTURE3D,
    TEXTURE2D_ARRAY,
    TEXTURECUBE
};

enum class TextureViewType : uint8_t {
    DEFAULT,
    STENCIL,
    DEPTH,
    COLOR
};

enum class TextureStatus {
    NOT_LOADED,
    LOADING,
    UPLOADING,
    READY,
    FAILED
};

struct TextureSpecification {
    TextureType type = TextureType::TEXTURE2D;
    TextureFormat format = TextureFormat::RGB8;
    TextureWrap wrap = TextureWrap::Repeat;
    TextureFilter filter = TextureFilter::Linear;
    bool srgb = true;              // to distinguish between UNORM and SRGB for relevant formats
    bool shadowComparison = false; // Enable shadow comparison sampling for depth textures
    bool storageImage = false;     // Enable storage image usage for compute shaders
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1; // For 3D textures

    uint32_t mipLevels = 1; // 1 = no mipmaps, 0 = auto-calculate maximum possible mip levels
};

inline VkImageType toVkImageType(TextureType type)
{
    switch (type) {
    case TextureType::TEXTURE1D:
        return VK_IMAGE_TYPE_1D;
    case TextureType::TEXTURE2D:
    case TextureType::TEXTURE2D_ARRAY:
    case TextureType::TEXTURECUBE:
        return VK_IMAGE_TYPE_2D;
    case TextureType::TEXTURE3D:
        return VK_IMAGE_TYPE_3D;
    default:
        RP_CORE_ERROR("Unsupported type!");
        return VK_IMAGE_TYPE_MAX_ENUM;
    }
}

inline VkImageViewType toVkImageViewType(TextureType type)
{
    switch (type) {
    case TextureType::TEXTURE1D:
        return VK_IMAGE_VIEW_TYPE_1D;
    case TextureType::TEXTURE2D:
        return VK_IMAGE_VIEW_TYPE_2D;
    case TextureType::TEXTURE3D:
        return VK_IMAGE_VIEW_TYPE_3D;
    case TextureType::TEXTURE2D_ARRAY:
        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureType::TEXTURECUBE:
        return VK_IMAGE_VIEW_TYPE_CUBE;
    default:
        RP_CORE_ERROR("Unsupported type!");
        return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    }
}

inline VkFormat toVkFormat(TextureFormat format, bool srgb = true)
{
    switch (format) {
    case TextureFormat::RGBA8:
        return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::RGB8:
        return srgb ? VK_FORMAT_R8G8B8_SRGB
                    : VK_FORMAT_R8G8B8_UNORM; // Check hardware support or use VK_FORMAT_R8G8B8A8 and swizzle for RGB.
    case TextureFormat::BGRA8:
        return srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::RGBA16F:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::RGB16F:
        return VK_FORMAT_R16G16B16_SFLOAT; // Requires VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT for this format
    case TextureFormat::RGBA32F:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case TextureFormat::RGB32F:
        return VK_FORMAT_R32G32B32_SFLOAT; // Requires VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT for this format
    case TextureFormat::R11G11B10F:
        return VK_FORMAT_B10G11R11_UFLOAT_PACK32; // Note BGR order and ufloat
    case TextureFormat::RG16F:
        return VK_FORMAT_R16G16_SFLOAT;
    case TextureFormat::R16F:
        return VK_FORMAT_R16_SFLOAT;
    case TextureFormat::R8UI:
        return VK_FORMAT_R8_UINT;
    case TextureFormat::R8U:
        return VK_FORMAT_R8_SINT;

    case TextureFormat::D32F:
        return VK_FORMAT_D32_SFLOAT;
    case TextureFormat::D24S8:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    default:
        RP_CORE_ERROR("Unsupported format!");
        return VK_FORMAT_UNDEFINED;
    }
}

inline VkSamplerAddressMode toVkSamplerAddressMode(TextureWrap wrapMode)
{
    switch (wrapMode) {
    case TextureWrap::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case TextureWrap::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case TextureWrap::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case TextureWrap::ClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    default:
        RP_CORE_ERROR("Unsupported wrap mode!");
        return VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
    }
}

inline VkFilter toVkFilter(TextureFilter filter)
{
    switch (filter) {
    case TextureFilter::Nearest:
    case TextureFilter::NearestMipmapNearest:
    case TextureFilter::NearestMipmapLinear:
        return VK_FILTER_NEAREST;
    case TextureFilter::Linear:
    case TextureFilter::LinearMipmapNearest:
    case TextureFilter::LinearMipmapLinear:
        return VK_FILTER_LINEAR;
    default:
        RP_CORE_ERROR("Unsupported filter!");
        return VK_FILTER_MAX_ENUM;
    }
}

inline VkSamplerMipmapMode toVkSamplerMipmapMode(TextureFilter filter)
{
    switch (filter) {
    case TextureFilter::NearestMipmapNearest:
    case TextureFilter::LinearMipmapNearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case TextureFilter::NearestMipmapLinear:
    case TextureFilter::LinearMipmapLinear:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    case TextureFilter::Nearest:               // No mipmapping involved
    case TextureFilter::Linear:                // No mipmapping involved
        return VK_SAMPLER_MIPMAP_MODE_NEAREST; // Or LINEAR, effectively ignored if mipLevels = 1
    default:
        RP_CORE_ERROR("Unsupported filter!");
        return VK_SAMPLER_MIPMAP_MODE_MAX_ENUM;
    }
}

inline bool isArrayType(TextureType type)
{
    return type == TextureType::TEXTURE2D_ARRAY;
}

inline bool isCubeType(TextureType type)
{
    return type == TextureType::TEXTURECUBE;
}

inline bool isDepthFormat(TextureFormat format)
{
    switch (format) {
    case TextureFormat::D32F:
    case TextureFormat::D24S8:
        return true;
    default:
        return false;
    }
}

inline bool hasStencilComponent(TextureFormat format)
{
    switch (format) {
    case TextureFormat::D24S8:
        return true;
    default:
        return false;
    }
}

inline VkImageAspectFlags getImageAspectFlags(TextureFormat format)
{
    if (isDepthFormat(format)) {
        VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilComponent(format)) {
            aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        return aspectFlags;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

inline uint32_t getBytesPerPixel(TextureFormat format)
{
    switch (format) {
    case TextureFormat::RGBA8:
        return 4; // 4 channels × 1 byte
    case TextureFormat::RGB8:
        return 3; // 3 channels × 1 byte
    case TextureFormat::BGRA8:
        return 4; // 4 channels × 1 byte
    case TextureFormat::RGBA16F:
        return 8; // 4 channels × 2 bytes
    case TextureFormat::RGB16F:
        return 6; // 3 channels × 2 bytes
    case TextureFormat::RGBA32F:
        return 16; // 4 channels × 4 bytes
    case TextureFormat::RGB32F:
        return 12; // 3 channels × 4 bytes
    case TextureFormat::R11G11B10F:
        return 4; // Packed format
    case TextureFormat::RG16F:
        return 4; // 2 channels × 2 bytes
    case TextureFormat::R16F:
        return 2; // 1 channel × 2 bytes
    case TextureFormat::R8UI:
        return 1; // 1 channel × 1 byte
    case TextureFormat::R8U:
        return 1; // 1 channel × 1 byte
    case TextureFormat::D32F:
        return 4; // 1 channel × 4 bytes
    case TextureFormat::D24S8:
        return 4; // Packed format
    default:
        RP_CORE_ERROR("Unsupported format!");
        return 4; // Default fallback
    }
}

inline uint32_t calculateMaxMipLevels(uint32_t width, uint32_t height)
{
    return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}

} // namespace Rapture
