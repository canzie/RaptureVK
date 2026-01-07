#include "SceneRenderTarget.h"
#include "Logging/Log.h"
#include "WindowContext/Application.h"

namespace Rapture {

SceneRenderTarget::SceneRenderTarget(uint32_t width, uint32_t height, uint32_t imageCount, TextureFormat format)
    : m_type(TargetType::OFFSCREEN), m_width(width), m_height(height), m_format(format), m_swapChain(nullptr)
{

    createOffscreenTextures(width, height, imageCount, format);
    RP_CORE_INFO("Created offscreen SceneRenderTarget: {}x{} with {} images", width, height, imageCount);
}

SceneRenderTarget::SceneRenderTarget(std::shared_ptr<SwapChain> swapChain) : m_type(TargetType::SWAPCHAIN), m_swapChain(swapChain)
{

    if (!m_swapChain) {
        RP_CORE_ERROR("Cannot create swapchain target with null swapchain");
        return;
    }

    m_width = swapChain->getExtent().width;
    m_height = swapChain->getExtent().height;

    RP_CORE_INFO("Created swapchain-backed SceneRenderTarget: {}x{}", m_width, m_height);
}

SceneRenderTarget::~SceneRenderTarget()
{
    m_offscreenTextures.clear();
    m_swapChain.reset();
}

void SceneRenderTarget::createOffscreenTextures(uint32_t width, uint32_t height, uint32_t imageCount, TextureFormat format)
{
    m_offscreenTextures.clear();
    m_offscreenTextures.reserve(imageCount);

    TextureSpecification spec;
    spec.width = width;
    spec.height = height;
    spec.depth = 1;
    spec.type = TextureType::TEXTURE2D;
    spec.format = format;
    spec.srgb = false; // rgba32f/16f does not have srgb i think
    spec.mipLevels = 1;
    spec.wrap = TextureWrap::ClampToEdge;
    spec.filter = TextureFilter::Linear;

    for (uint32_t i = 0; i < imageCount; i++) {
        auto texture = std::make_shared<Texture>(spec);
        m_offscreenTextures.push_back(texture);
    }
}

void SceneRenderTarget::resize(uint32_t width, uint32_t height)
{
    if (m_type == TargetType::SWAPCHAIN) {
        RP_CORE_WARN("Cannot manually resize swapchain target. "
                     "Swapchain resize is handled separately.");
        return;
    }

    if (width == 0 || height == 0) {
        RP_CORE_WARN("Invalid dimensions: {}x{}", width, height);
        return;
    }

    if (width == m_width && height == m_height) {
        return; // No change needed
    }

    RP_CORE_INFO("Resizing SceneRenderTarget: {}x{} -> {}x{}", m_width, m_height, width, height);

    // Wait for GPU to finish using the old textures
    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    vulkanContext.waitIdle();

    m_width = width;
    m_height = height;

    uint32_t imageCount = static_cast<uint32_t>(m_offscreenTextures.size());
    createOffscreenTextures(width, height, imageCount, m_format);
}

void SceneRenderTarget::onSwapChainRecreated()
{
    if (m_type == TargetType::SWAPCHAIN && m_swapChain) {
        m_width = m_swapChain->getExtent().width;
        m_height = m_swapChain->getExtent().height;
        RP_CORE_INFO("SceneRenderTarget updated after swapchain recreation: {}x{}", m_width, m_height);
    }
    // For OFFSCREEN type, this is a no-op - viewport size is independent
}

VkImage SceneRenderTarget::getImage(uint32_t index) const
{
    if (m_type == TargetType::SWAPCHAIN) {
        if (m_swapChain && index < m_swapChain->getImageCount()) {
            return m_swapChain->getImages()[index];
        }
        RP_CORE_ERROR("Invalid index {} for swapchain", index);
        return VK_NULL_HANDLE;
    }

    if (index < m_offscreenTextures.size()) {
        return m_offscreenTextures[index]->getImage();
    }
    RP_CORE_ERROR("Invalid index {} for offscreen target", index);
    return VK_NULL_HANDLE;
}

VkImageView SceneRenderTarget::getImageView(uint32_t index) const
{
    if (m_type == TargetType::SWAPCHAIN) {
        if (m_swapChain && index < m_swapChain->getImageCount()) {
            return m_swapChain->getImageViews()[index];
        }
        RP_CORE_ERROR("Invalid index {} for swapchain", index);
        return VK_NULL_HANDLE;
    }

    if (index < m_offscreenTextures.size()) {
        return m_offscreenTextures[index]->getImageView();
    }
    RP_CORE_ERROR("Invalid index {} for offscreen target", index);
    return VK_NULL_HANDLE;
}

VkFormat SceneRenderTarget::getFormat() const
{
    if (m_type == TargetType::SWAPCHAIN) {
        return m_swapChain ? m_swapChain->getImageFormat() : VK_FORMAT_UNDEFINED;
    }
    return toVkFormat(m_format, true); // SRGB format
}

VkExtent2D SceneRenderTarget::getExtent() const
{
    return {m_width, m_height};
}

uint32_t SceneRenderTarget::getImageCount() const
{
    if (m_type == TargetType::SWAPCHAIN) {
        return m_swapChain ? m_swapChain->getImageCount() : 0;
    }
    return static_cast<uint32_t>(m_offscreenTextures.size());
}

std::shared_ptr<Texture> SceneRenderTarget::getTexture(uint32_t index) const
{
    if (m_type == TargetType::SWAPCHAIN) {
        RP_CORE_WARN("Swapchain targets don't have Texture objects");
        return nullptr;
    }

    if (index < m_offscreenTextures.size()) {
        return m_offscreenTextures[index];
    }
    RP_CORE_ERROR("Invalid index {}", index);
    return nullptr;
}

void SceneRenderTarget::transitionToShaderReadLayout(CommandBuffer *commandBuffer, uint32_t imageIndex)
{
    if (m_type != TargetType::OFFSCREEN) {
        return; // Only offscreen targets need this transition
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = getImage(imageIndex);
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace Rapture
