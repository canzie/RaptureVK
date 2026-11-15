#ifndef RAPTURE__VULKAN_QUEUE_H
#define RAPTURE__VULKAN_QUEUE_H

#include <vulkan/vulkan.h>

#include <atomic>
#include <mutex>
#include <rapture/gpu/command_buffers/CommandBuffer.h>
#include <span>
#include <string>
#include <vector>

namespace Rapture {

/**
 * @brief A semaphore a submission waits on before the given stages run, a binary semaphore takes value 0
 */
struct SemaphoreWait {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    uint64_t value = 0;
    VkPipelineStageFlags2 stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
};

/**
 * @brief A semaphore a submission signals once it finishes, a binary semaphore takes value 0
 */
struct SemaphoreSignal {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    uint64_t value = 0;
};

/**
 * @brief What a submission waits on and signals besides the queue's own timeline
 */
struct SubmitSynchronization {
    std::span<const SemaphoreWait> waits = {};
    std::span<const SemaphoreSignal> signals = {};
    VkFence fence = VK_NULL_HANDLE;
};

/**
 * @brief A device queue with one timeline semaphore that every submission to it signals, in submission order
 */
class VulkanQueue {
  public:
    VulkanQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex = 0, const std::string &name = "unnamed");
    ~VulkanQueue();

    /**
     * @brief Submits the batched command buffers
     * @return False if the submission failed
     */
    bool flush();

    /**
     * @brief Adds a command buffer to the batch the next flush or submission sends
     * @param commandBuffer The recorded command buffer
     * @return The timeline value the queue reaches once the batch is done
     */
    uint64_t addToBatch(CommandBuffer *commandBuffer);

    /**
     * @brief Submits a command buffer now, after the batched ones
     * @param commandBuffer The recorded command buffer
     * @param synchronization The semaphores to wait on and signal, and the fence to signal
     * @return The timeline value the queue reaches once the command buffer is done, 0 if the submission failed
     */
    uint64_t submitQueue(CommandBuffer *commandBuffer, const SubmitSynchronization &synchronization = {});

    void waitIdle();

    /**
     * @brief The timeline value of the most recent submission sent to the device
     * @return The value, 0 before the first submission
     */
    uint64_t getLastSubmittedValue() const { return m_lastSubmittedValue.load(std::memory_order_acquire); }

    /**
     * @brief Blocks the calling thread until the device has finished every submission up to a timeline value
     * @param value The timeline value to wait for
     */
    void waitForTimelineValue(uint64_t value) const;

    // should probably check if queue is present but well see
    VkResult presentQueue(VkPresentInfoKHR &presentInfo);

    VkQueue getQueueVk() const { return m_queue; }
    VkSemaphore getTimelineSemaphore() const { return m_timelineSemaphore; }

    [[nodiscard]] std::unique_lock<std::mutex> acquireQueueLock() { return std::unique_lock<std::mutex>(m_queueMutex); }

    void setDeviceFaultFn(PFN_vkGetDeviceFaultInfoEXT fn) { m_vkGetDeviceFaultInfoEXT = fn; }

  private:
    void createTimelineSemaphore();

    /**
     * @brief Submits the batch followed by another submission, the caller holds both mutexes
     * @param submitAfterBatch The submission to send after the batch, nullptr to send only the batch
     * @param fence The fence to signal once everything submitted is done
     * @return False if the submission failed
     */
    bool submitBatchThen(const VkSubmitInfo2 *submitAfterBatch, VkFence fence);

  private:
    std::vector<CommandBuffer *> m_cmdBufferBatch;
    uint64_t m_batchSignalValue = 0;
    uint64_t m_nextTimelineValue = 1;
    std::atomic<uint64_t> m_lastSubmittedValue{0};

    VkDevice m_device;
    VkQueue m_queue;
    uint32_t m_queueFamilyIndex;
    std::string m_name;
    std::mutex m_queueMutex;
    std::mutex m_cmdBatchMutex;

    VkSemaphore m_timelineSemaphore = VK_NULL_HANDLE;
    PFN_vkGetDeviceFaultInfoEXT m_vkGetDeviceFaultInfoEXT = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__VULKAN_QUEUE_H
