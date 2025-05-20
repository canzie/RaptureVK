#include "FrameBuffer.h"

#include "WindowContext/Application.h"

#include "Logging/Log.h"

#include <stdexcept>

namespace Rapture {


    FrameBuffer::FrameBuffer(const FramebufferSpecification& specification, VkRenderPass renderPass)
        : m_specification(specification), m_renderPass(renderPass), m_framebuffer(VK_NULL_HANDLE)
    {
        invalidate();
    }

    FrameBuffer::FrameBuffer(const SwapChain &swapChain, uint32_t SCImageViewIndex, VkRenderPass renderPass)
        : m_renderPass(renderPass), m_framebuffer(VK_NULL_HANDLE)
    {
        m_specification.width = swapChain.getExtent().width;
        m_specification.height = swapChain.getExtent().height;
        m_specification.attachments = {swapChain.getImageViews()[SCImageViewIndex]};
        m_specification.swapChainTarget = true;
        invalidate();
    }

    FrameBuffer::~FrameBuffer()
    {
        Application& app = Application::getInstance();
        VkDevice device = app.getVulkanContext().getLogicalDevice();

        if (m_framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, m_framebuffer, nullptr);
        }
    }

    void FrameBuffer::resize(uint32_t width, uint32_t height, std::vector<VkImageView> attachments)
    {
        m_specification.width = width;
        m_specification.height = height;
        m_specification.attachments = attachments;
        invalidate();

    }

    void FrameBuffer::resize(const SwapChain &swapChain, uint32_t SCImageViewIndex)
    {
        m_specification.width = swapChain.getExtent().width;
        m_specification.height = swapChain.getExtent().height;
        m_specification.attachments = {swapChain.getImageViews()[SCImageViewIndex]};
        invalidate();
    }

    void FrameBuffer::invalidate() {
        Application& app = Application::getInstance();
        VkDevice device = app.getVulkanContext().getLogicalDevice();

        if (m_framebuffer != VK_NULL_HANDLE) { // delete the old framebuffer first
            vkDestroyFramebuffer(device, m_framebuffer, nullptr);
        }

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(m_specification.attachments.size());
        framebufferInfo.pAttachments = m_specification.attachments.data();
        framebufferInfo.width = m_specification.width;
        framebufferInfo.height = m_specification.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_framebuffer) != VK_SUCCESS) {
            RP_CORE_ERROR("failed to create framebuffer!");
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
    


}
