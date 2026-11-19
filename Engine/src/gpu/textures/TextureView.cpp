#include <rapture/gpu/textures/TextureView.h>

#include <rapture/app/Application.h>
#include <rapture/core/utils/Log.h>
#include <rapture/gpu/textures/Texture.h>

#include <array>

namespace Rapture {

static constexpr std::array<VkComponentSwizzle, TEXTURE_CHANNEL_COUNT> TEXTURE_CHANNEL_SWIZZLES = {
    VK_COMPONENT_SWIZZLE_R,
    VK_COMPONENT_SWIZZLE_G,
    VK_COMPONENT_SWIZZLE_B,
    VK_COMPONENT_SWIZZLE_A,
    VK_COMPONENT_SWIZZLE_ZERO,
    VK_COMPONENT_SWIZZLE_ONE,
};

TextureView::TextureView(const Texture &texture, const TextureViewConfig &config)
{
    const TextureSpecification &spec = texture.getSpecification();
    VkDevice device = Application::getInstance().getVulkanContext().getLogicalDevice();

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.getImage();
    viewInfo.viewType = toVkImageViewType(spec.type);
    viewInfo.format = texture.getFormat();
    viewInfo.components.r = TEXTURE_CHANNEL_SWIZZLES[config.swizzle.r];
    viewInfo.components.g = TEXTURE_CHANNEL_SWIZZLES[config.swizzle.g];
    viewInfo.components.b = TEXTURE_CHANNEL_SWIZZLES[config.swizzle.b];
    viewInfo.components.a = TEXTURE_CHANNEL_SWIZZLES[config.swizzle.a];
    viewInfo.subresourceRange.aspectMask =
        isDepthFormat(spec.format) ? static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT) : getImageAspectFlags(spec.format);
    viewInfo.subresourceRange.baseMipLevel = config.baseMip;
    viewInfo.subresourceRange.levelCount = config.mipCount;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = isArrayType(spec.type) ? spec.depth : (isCubeType(spec.type) ? 6 : 1);

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create texture view over mips {}..{}", config.baseMip, config.baseMip + config.mipCount - 1);
        m_imageView = VK_NULL_HANDLE;
    }
}

TextureView::~TextureView()
{
    if (m_imageView == VK_NULL_HANDLE) {
        return;
    }

    VkDevice device = Application::getInstance().getVulkanContext().getLogicalDevice();
    vkDestroyImageView(device, m_imageView, nullptr);
}

} // namespace Rapture
