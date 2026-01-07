#pragma once

#include <vulkan/vulkan.h>

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include <atomic>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace Rapture {
class VulkanQueue {
  public:
    VulkanQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex = 0, const std::string &name = "unnamed");
    ~VulkanQueue();

    bool flush();
    uint64_t addToBatch(CommandBuffer *commandBuffer);

    // execute immediately
    bool submitQueue(CommandBuffer *commandBuffer, std::span<VkSemaphore> *signalSemaphores = nullptr,
                     std::span<VkSemaphore> *waitSemaphores = nullptr, VkPipelineStageFlags *waitStage = nullptr,
                     VkFence fence = nullptr);
    bool submitAndFlushQueue(CommandBuffer *commandBuffer, std::span<VkSemaphore> *signalSemaphores = nullptr,
                             std::span<VkSemaphore> *waitSemaphores = nullptr, VkPipelineStageFlags *waitStage = nullptr,
                             VkFence fence = nullptr);

    void waitIdle();

    // should probably check if queue is present but well see
    VkResult presentQueue(VkPresentInfoKHR &presentInfo);

    VkQueue getQueueVk() const { return m_queue; }
    VkSemaphore getTimelineSemaphore() const { return m_timelineSemaphore; }
    uint64_t getCurrentTimelineValue() const { return m_nextTimelineValue.load(); }

    [[nodiscard]] std::unique_lock<std::mutex> acquireQueueLock() { return std::unique_lock<std::mutex>(m_queueMutex); }

    void clear();

  private:
    void createTimelineSemaphore();

    std::vector<CommandBuffer *> m_cmdBufferBatch;

    VkDevice m_device;
    VkQueue m_queue;
    uint32_t m_queueFamilyIndex;
    std::string m_name;
    std::mutex m_queueMutex;
    std::mutex m_cmdBatchMutex;

    VkSemaphore m_immediateTimeSema = VK_NULL_HANDLE;
    VkSemaphore m_timelineSemaphore = VK_NULL_HANDLE;
    std::atomic<uint64_t> m_nextTimelineValue = 1;
    std::atomic<uint64_t> m_nextImmediateTimelineValue = 1;
};
} // namespace Rapture
