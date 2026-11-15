#ifndef RAPTURE__COMMAND_POOL_H
#define RAPTURE__COMMAND_POOL_H

#include <rapture/gpu/command_buffers/CommandBuffer.h>
#include <rapture/gpu/command_buffers/CommandBufferLease.h>

#include <vulkan/vulkan.h>

#include <memory>
#include <mutex>
#include <vector>

namespace Rapture {

/**
 * @brief A Vulkan command pool holding a single command buffer
 */
class CommandPool {
  public:
    CommandPool(VkDevice device, uint32_t queueFamilyIndex, CmdBufferLevel level);
    ~CommandPool();

    CommandPool(const CommandPool &) = delete;
    CommandPool &operator=(const CommandPool &) = delete;

    VkCommandPool getCommandPoolVk() const { return m_commandPool; }
    CommandBuffer *getCommandBuffer() const { return m_commandBuffer.get(); }
    uint32_t getQueueFamilyIndex() const { return m_queueFamilyIndex; }
    CmdBufferLevel getLevel() const { return m_level; }

    /**
     * @brief Records a submission the pool's command buffer is part of
     * @param timelineSemaphore The timeline semaphore the submission signals
     * @param signalValue The value it signals once the GPU is done with the submission
     */
    void markPendingSignal(VkSemaphore timelineSemaphore, uint64_t signalValue);

    /**
     * @brief Whether the GPU has finished every submission of the pool's command buffer
     */
    bool isIdleOnGpu() const;

    /**
     * @brief Returns the command buffer to the initial state, ready to record again
     */
    void reset();

  private:
    struct PendingSignal {
        VkSemaphore timelineSemaphore;
        uint64_t signalValue;
    };

  private:
    VkDevice m_device;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex;
    CmdBufferLevel m_level;

    std::unique_ptr<CommandBuffer> m_commandBuffer;
    std::vector<PendingSignal> m_pendingSignals;
};

/**
 * @brief Hands out command buffer leases, reusing pools the GPU is done with
 */
class CommandPoolManager {
  public:
    explicit CommandPoolManager(VkDevice device);
    ~CommandPoolManager();

    CommandPoolManager(const CommandPoolManager &) = delete;
    CommandPoolManager &operator=(const CommandPoolManager &) = delete;

    /**
     * @brief Leases a primary command buffer, safe to record from any thread or job
     * @param queueFamilyIndex The queue family the buffer is submitted to
     * @return The lease, empty if no pool could be created
     */
    CommandBufferLease leasePrimary(uint32_t queueFamilyIndex);

    /**
     * @brief Leases a secondary command buffer, safe to record from any thread or job
     * @param queueFamilyIndex The queue family of the primary it is recorded into
     * @return The lease, empty if no pool could be created
     */
    CommandBufferLease leaseSecondary(uint32_t queueFamilyIndex);

    /**
     * @brief Destroys every pool that is not leased out, the GPU must be idle
     */
    void closeAllPools();

  private:
    friend class CommandBufferLease;

    /**
     * @brief Leases a pool of the given family and level, reusing one the GPU is done with
     * @param queueFamilyIndex The queue family of the pool
     * @param level The level of the pool's command buffer
     * @return The lease, empty if no pool could be created
     */
    CommandBufferLease lease(uint32_t queueFamilyIndex, CmdBufferLevel level);

    /**
     * @brief Takes back a pool whose lease was dropped
     * @param pool The pool
     */
    void returnPool(std::unique_ptr<CommandPool> pool);

  private:
    VkDevice m_device;
    std::mutex m_mutex;
    std::vector<std::unique_ptr<CommandPool>> m_returnedPools;
};

} // namespace Rapture

#endif // RAPTURE__COMMAND_POOL_H
