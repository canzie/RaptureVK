#include <rapture/platform/RenderWindow.h>

#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/rp_assert.h>
#include <rapture/gpu/command_buffers/CommandPool.h>
#include <rapture/gpu/swap_chains/SwapChain.h>
#include <rapture/gpu/vulkan_context/VulkanContext.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstring>
#include <span>
#include <utility>

namespace Rapture {

static constexpr uint32_t RGBA8_BYTES_PER_PIXEL = 4;

/**
 * @brief A presented frame being copied into CPU visible memory
 */
struct FrameReadback {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void *mapped = nullptr;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
};

static bool s_formatHasBlueFirst(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

static bool s_formatCanBeCaptured(VkFormat format)
{
    return s_formatHasBlueFirst(format) || format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB;
}

static void s_recordPresentImageTransition(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout from, VkImageLayout to,
                                           VkAccessFlags srcAccess, VkAccessFlags dstAccess)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}

/**
 * @brief Records a copy of the image about to be presented into a buffer the CPU can read
 * @param commandBuffer The frame's command buffer, with the image already in the present layout
 * @param swapChain The swapchain the image belongs to
 * @param imageIndex The image being presented
 * @param allocator Where the buffer is allocated
 * @return The readback, with no buffer if the image cannot be copied
 */
static FrameReadback s_recordFrameReadback(VkCommandBuffer commandBuffer, const SwapChain &swapChain, uint32_t imageIndex,
                                           VmaAllocator allocator)
{
    FrameReadback readback;
    readback.extent = swapChain.getExtent();
    readback.format = swapChain.getImageFormat();

    if ((swapChain.getImageUsage() & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0 || !s_formatCanBeCaptured(readback.format)) {
        RP_CORE_ERROR("this window's images cannot be read back");
        return readback;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = static_cast<VkDeviceSize>(readback.extent.width) * readback.extent.height * RGBA8_BYTES_PER_PIXEL;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocated{};
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &readback.buffer, &readback.allocation, &allocated) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create the frame readback buffer");
        readback.buffer = VK_NULL_HANDLE;
        return readback;
    }
    readback.mapped = allocated.pMappedData;

    VkImage image = swapChain.getImages()[imageIndex];
    s_recordPresentImageTransition(commandBuffer, image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {readback.extent.width, readback.extent.height, 1};
    vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);

    s_recordPresentImageTransition(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                   VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT);
    return readback;
}

/**
 * @brief Turns a finished readback into an image, releasing its buffer
 * @param readback The readback, whose copy the GPU has finished
 * @param allocator Where its buffer was allocated
 * @return The frame, empty if nothing was copied
 */
static Rgba8Image s_takeFrameReadback(FrameReadback &readback, VmaAllocator allocator)
{
    Rgba8Image image;
    if (readback.buffer == VK_NULL_HANDLE) {
        return image;
    }

    image.width = readback.extent.width;
    image.height = readback.extent.height;
    image.pixels.resize(static_cast<size_t>(image.width) * image.height * RGBA8_BYTES_PER_PIXEL);

    vmaInvalidateAllocation(allocator, readback.allocation, 0, VK_WHOLE_SIZE);
    std::memcpy(image.pixels.data(), readback.mapped, image.pixels.size());
    vmaDestroyBuffer(allocator, readback.buffer, readback.allocation);
    readback.buffer = VK_NULL_HANDLE;

    const bool blueFirst = s_formatHasBlueFirst(readback.format);
    for (size_t i = 0; i < image.pixels.size(); i += RGBA8_BYTES_PER_PIXEL) {
        if (blueFirst) {
            std::swap(image.pixels[i], image.pixels[i + 2]);
        }
        // what the window shows is opaque, whatever the compositor was handed
        image.pixels[i + 3] = 255;
    }

    return image;
}

RenderWindow::RenderWindow(std::unique_ptr<WindowContext> windowContext, VulkanContext &context)
    : m_windowContext(std::move(windowContext)), m_context(&context), m_instance(context.getInstance()), m_surface(VK_NULL_HANDLE),
      m_swapChain(nullptr)
{
    createSurface();
}

RenderWindow::~RenderWindow()
{
    m_swapChain.reset();

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
}

void RenderWindow::createSurface()
{
    VkResult result = glfwCreateWindowSurface(m_instance, static_cast<GLFWwindow *>(m_windowContext->getNativeWindowContext()),
                                              nullptr, &m_surface);
    RP_ASSERT(result == VK_SUCCESS, "Failed to create window surface!");
}

void RenderWindow::createSwapChain(VulkanContext &context)
{
    m_swapChain = std::make_shared<SwapChain>(context.getLogicalDevice(), m_surface, context.getPhysicalDevice(),
                                              context.getQueueFamilyIndices(), m_windowContext.get());

    m_swapchainRecreateConn = m_swapChain->onRecreationRequested.connect([this]() {
        int width = 0, height = 0;
        m_windowContext->getFramebufferSize(&width, &height);
        if (width == 0 || height == 0) {
            return;
        }

        m_context->waitIdle();

        m_swapChain->recreate();
        m_swapChain->onRecreated.fire();
    });

    m_windowResizeConn = m_windowContext->onResize.connect([this](uint32_t width, uint32_t height) {
        (void)width;
        (void)height;
        m_framebufferResized = true;
    });
}

AcquiredFrame RenderWindow::beginFrame()
{
    if (m_windowContext->isMinimized()) {
        return {};
    }

    int imageIndex = m_swapChain->acquireImage(m_currentFrame);
    if (imageIndex == -1) {
        m_currentFrame = 0;
        return {};
    }

    m_currentImageIndex = static_cast<uint32_t>(imageIndex);

    m_currentCommandBuffer = m_context->getRenderContext().commandPoolManager->leasePrimary(m_context->getGraphicsQueueIndex());

    if (m_currentCommandBuffer->begin(0) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to begin present command buffer!");
        return {};
    }

    AcquiredFrame frame;
    frame.acquired = true;
    frame.imageIndex = m_currentImageIndex;
    frame.imageView = m_swapChain->getImageViews()[m_currentImageIndex];
    frame.commandBuffer = m_currentCommandBuffer.get();
    return frame;
}

void RenderWindow::captureNextFrame(CaptureCallback onCaptured)
{
    m_pendingCaptures.push_back(std::move(onCaptured));
}

void RenderWindow::endFrame()
{
    FrameReadback readback;
    if (!m_pendingCaptures.empty()) {
        readback = s_recordFrameReadback(m_currentCommandBuffer->getCommandBufferVk(), *m_swapChain, m_currentImageIndex,
                                         m_context->getVmaAllocator());
    }

    if (m_currentCommandBuffer->end() != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to record present command buffer!");
        return;
    }

    VkSemaphore renderFinished = m_swapChain->getRenderFinishedSemaphore(m_currentImageIndex);

    SemaphoreWait imageAvailableWait{.semaphore = m_swapChain->getImageAvailableSemaphore(m_currentFrame),
                                     .value = 0,
                                     .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    SemaphoreSignal renderFinishedSignal{.semaphore = renderFinished, .value = 0};

    const uint64_t submittedValue = m_context->getGraphicsQueue()->submitQueue(
        m_currentCommandBuffer.get(), SubmitSynchronization{.waits = std::span<const SemaphoreWait>(&imageAvailableWait, 1),
                                                            .signals = std::span<const SemaphoreSignal>(&renderFinishedSignal, 1),
                                                            .fence = m_swapChain->getInFlightFence(m_currentFrame)});

    m_currentCommandBuffer.release();

    if (!m_pendingCaptures.empty()) {
        m_context->getGraphicsQueue()->waitForTimelineValue(submittedValue);
        const Rgba8Image image = s_takeFrameReadback(readback, m_context->getVmaAllocator());

        std::vector<CaptureCallback> captures = std::move(m_pendingCaptures);
        m_pendingCaptures.clear();
        for (const CaptureCallback &capture : captures) {
            capture(image);
        }
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;

    VkSwapchainKHR swapChains[] = {m_swapChain->getSwapChainVk()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_currentImageIndex;
    presentInfo.pResults = nullptr;

    VkResult result = m_context->getPresentQueue()->presentQueue(presentInfo);
    m_swapChain->signalImageAvailability(m_currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        m_currentFrame = 0;
        m_swapChain->onRecreationRequested.fire();
        return;
    } else if (result != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to present swap chain image!");
        return;
    }

    m_currentFrame = (m_currentFrame + 1) % m_swapChain->getImageCount();
}

void RenderWindow::onUpdate()
{
    m_windowContext->onUpdate();
}

} // namespace Rapture
