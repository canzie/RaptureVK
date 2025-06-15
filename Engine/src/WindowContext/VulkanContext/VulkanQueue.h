#pragma once

#include <vulkan/vulkan.h>

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include <mutex>

namespace Rapture
{
    class VulkanQueue
    {
    public:
        VulkanQueue(VkDevice device, uint32_t queueFamilyIndex);
        ~VulkanQueue();

        // execute saved command buffers
        void submitCommandBuffers(VkFence fence = nullptr);
        void submitCommandBuffers(VkSubmitInfo& submitInfo, VkFence fence = nullptr);

        // execute immediately
        void submitQueue(std::shared_ptr<CommandBuffer> commandBuffer, VkSubmitInfo& submitInfo, VkFence fence = nullptr);
        void submitQueue(std::shared_ptr<CommandBuffer> commandBuffer, VkFence fence = nullptr);
        
        // add command buffer to queue
        void addCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer);
        void waitIdle(); 

        // should probably check if queue is present but well see
        VkResult presentQueue(VkPresentInfoKHR& presentInfo);

        VkQueue getQueueVk() const { return m_queue; }

        void clear();

    private:
        std::vector<std::shared_ptr<CommandBuffer>> m_commandBuffers;
        VkDevice m_device;
        VkQueue m_queue;
        uint32_t m_queueFamilyIndex;
        std::mutex m_commandBufferMutex;
        std::mutex m_queueMutex;
    };
}
