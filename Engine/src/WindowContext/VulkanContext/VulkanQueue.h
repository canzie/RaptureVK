#pragma once

#include <vulkan/vulkan.h>

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include <mutex>
#include <string>

namespace Rapture {
class VulkanQueue {
  public:
    VulkanQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex = 0, const std::string &name = "unnamed");
    ~VulkanQueue();

    // execute saved command buffers (returns false if any command buffer cannot be submitted)
    bool submitCommandBuffers(VkFence fence = nullptr);
    bool submitCommandBuffers(VkSubmitInfo &submitInfo, VkFence fence = nullptr);

    // execute immediately (returns false if command buffer cannot be submitted)
    bool submitQueue(std::shared_ptr<CommandBuffer> commandBuffer, VkSubmitInfo &submitInfo, VkFence fence = nullptr);
    bool submitQueue(std::shared_ptr<CommandBuffer> commandBuffer, VkFence fence = nullptr);

    // add command buffer to queue
    void addCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer);
    void waitIdle();

    // should probably check if queue is present but well see
    VkResult presentQueue(VkPresentInfoKHR &presentInfo);

    VkQueue getQueueVk() const { return m_queue; }
    VkSemaphore getTimelineSemaphore() const { return m_timelineSemaphore; }
    uint64_t getCurrentTimelineValue() const { return m_timelineValue; }

    [[nodiscard]] std::unique_lock<std::mutex> acquireQueueLock() { return std::unique_lock<std::mutex>(m_queueMutex); }

    void clear();

  private:
    void createTimelineSemaphore();

    std::vector<std::shared_ptr<CommandBuffer>> m_commandBuffers;
    VkDevice m_device;
    VkQueue m_queue;
    uint32_t m_queueFamilyIndex;
    std::string m_name;
    std::mutex m_commandBufferMutex;
    std::mutex m_queueMutex;

    // Timeline semaphore for tracking command buffer completion
    VkSemaphore m_timelineSemaphore = VK_NULL_HANDLE;
    uint64_t m_timelineValue = 0;
};
} // namespace Rapture
