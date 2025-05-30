#include "VulkanQueue.h"

#include "Logging/Log.h"
#include <stdexcept>

namespace Rapture
{
    VulkanQueue::VulkanQueue(VkDevice device, uint32_t queueFamilyIndex)
    {
        m_device = device;
        m_queueFamilyIndex = queueFamilyIndex;

        if (m_device == VK_NULL_HANDLE)
        {
            RP_CORE_ERROR("VulkanQueue: Device is NULL");
            throw std::runtime_error("VulkanQueue: Device is NULL");
        }

        vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &m_queue);
    }

    VulkanQueue::~VulkanQueue()
    {
    }

    void VulkanQueue::submitCommandBuffers(VkFence fence)
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        std::lock_guard<std::mutex> lock2(m_commandBufferMutex);


        if (m_commandBuffers.empty())
        {
            return;
        }

        std::vector<VkCommandBuffer> commandBuffers;
        for (auto& commandBuffer : m_commandBuffers)
        {
            commandBuffers.push_back(commandBuffer->getCommandBufferVk());
        }

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = commandBuffers.size();
        submitInfo.pCommandBuffers = commandBuffers.data();


        
        if (vkQueueSubmit(m_queue, 1, &submitInfo, fence) != VK_SUCCESS) {
            RP_CORE_ERROR("VulkanQueue::submitCommandBuffers - failed to submit draw command buffer!");
            throw std::runtime_error("VulkanQueue::submitCommandBuffers - failed to submit draw command buffer!");
        }
        m_commandBuffers.clear();
    }

    void VulkanQueue::submitCommandBuffers(VkSubmitInfo& submitInfo, VkFence fence)
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        std::lock_guard<std::mutex> lock2(m_commandBufferMutex);

        if (m_commandBuffers.empty())
        {
            return;
        }

        std::vector<VkCommandBuffer> commandBuffers;
        for (auto& commandBuffer : m_commandBuffers)
        {
            commandBuffers.push_back(commandBuffer->getCommandBufferVk());
        }

        submitInfo.commandBufferCount = commandBuffers.size();
        submitInfo.pCommandBuffers = commandBuffers.data();


        
        if (vkQueueSubmit(m_queue, 1, &submitInfo, fence) != VK_SUCCESS) {
            RP_CORE_ERROR("VulkanQueue::submitCommandBuffers - failed to submit draw command buffer!");
            throw std::runtime_error("VulkanQueue::submitCommandBuffers - failed to submit draw command buffer!");
        }
        m_commandBuffers.clear();
    }

    void VulkanQueue::addCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer)
    {
        std::lock_guard<std::mutex> lock(m_commandBufferMutex);
        m_commandBuffers.push_back(commandBuffer);
    }

    void VulkanQueue::waitIdle()
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        vkQueueWaitIdle(m_queue);
    }

    VkResult VulkanQueue::presentQueue(VkPresentInfoKHR& presentInfo)
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        return vkQueuePresentKHR(m_queue, &presentInfo);
    }
}