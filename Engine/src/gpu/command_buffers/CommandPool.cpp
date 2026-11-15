#include <rapture/gpu/command_buffers/CommandPool.h>

#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/TracyProfiler.h>

#include <algorithm>

namespace Rapture {

// NVIDIA driver has internal state accumulation issues with vkResetCommandPool causing growing latency.
// When enabled, uses VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT and skips vkResetCommandPool entirely.
// Command buffers reset implicitly on vkBeginCommandBuffer instead.
#define RAPTURE_SKIP_COMMAND_POOL_RESET 0

CommandPool::CommandPool(VkDevice device, uint32_t queueFamilyIndex, CmdBufferLevel level)
    : m_device(device), m_queueFamilyIndex(queueFamilyIndex), m_level(level)
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
#if RAPTURE_SKIP_COMMAND_POOL_RESET
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
#else
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
#endif // RAPTURE_SKIP_COMMAND_POOL_RESET
    poolInfo.queueFamilyIndex = queueFamilyIndex;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to create command pool!");
        m_commandPool = VK_NULL_HANDLE;
        return;
    }

    m_commandBuffer = std::make_unique<CommandBuffer>(this, level, "cmd");
}

CommandPool::~CommandPool()
{
    m_commandBuffer.reset();
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    m_commandPool = VK_NULL_HANDLE;
}

void CommandPool::markPendingSignal(VkSemaphore timelineSemaphore, uint64_t signalValue)
{
    for (PendingSignal &pending : m_pendingSignals) {
        if (pending.timelineSemaphore == timelineSemaphore) {
            pending.signalValue = std::max(pending.signalValue, signalValue);
            return;
        }
    }

    m_pendingSignals.push_back(PendingSignal{.timelineSemaphore = timelineSemaphore, .signalValue = signalValue});
}

bool CommandPool::isIdleOnGpu() const
{
    for (const PendingSignal &pending : m_pendingSignals) {
        uint64_t reachedValue = 0;
        if (vkGetSemaphoreCounterValue(m_device, pending.timelineSemaphore, &reachedValue) != VK_SUCCESS) {
            return false;
        }
        if (reachedValue < pending.signalValue) {
            return false;
        }
    }

    return true;
}

void CommandPool::reset()
{
    RAPTURE_PROFILE_FUNCTION();

    m_pendingSignals.clear();
    m_commandBuffer->clearSecondaries();
#if !RAPTURE_SKIP_COMMAND_POOL_RESET
    vkResetCommandPool(m_device, m_commandPool, 0);
#endif // !RAPTURE_SKIP_COMMAND_POOL_RESET
}

CommandBufferLease::CommandBufferLease() = default;

CommandBufferLease::CommandBufferLease(CommandPoolManager *manager, std::unique_ptr<CommandPool> pool)
    : m_manager(manager), m_pool(std::move(pool))
{
}

CommandBufferLease::~CommandBufferLease()
{
    release();
}

CommandBufferLease::CommandBufferLease(CommandBufferLease &&other) noexcept
    : m_manager(other.m_manager), m_pool(std::move(other.m_pool))
{
}

CommandBufferLease &CommandBufferLease::operator=(CommandBufferLease &&other) noexcept
{
    if (this != &other) {
        release();
        m_manager = other.m_manager;
        m_pool = std::move(other.m_pool);
    }
    return *this;
}

CommandBuffer *CommandBufferLease::get() const
{
    if (m_pool == nullptr) {
        return nullptr;
    }
    return m_pool->getCommandBuffer();
}

void CommandBufferLease::release()
{
    if (m_pool == nullptr) {
        return;
    }

    // a buffer dropped without being submitted still holds the secondaries recorded into it
    m_pool->getCommandBuffer()->clearSecondaries();
    m_manager->returnPool(std::move(m_pool));
}

CommandPoolManager::CommandPoolManager(VkDevice device) : m_device(device) {}

CommandPoolManager::~CommandPoolManager()
{
    closeAllPools();
}

CommandBufferLease CommandPoolManager::leasePrimary(uint32_t queueFamilyIndex)
{
    return lease(queueFamilyIndex, CmdBufferLevel::PRIMARY);
}

CommandBufferLease CommandPoolManager::leaseSecondary(uint32_t queueFamilyIndex)
{
    return lease(queueFamilyIndex, CmdBufferLevel::SECONDARY);
}

CommandBufferLease CommandPoolManager::lease(uint32_t queueFamilyIndex, CmdBufferLevel level)
{
    RAPTURE_PROFILE_FUNCTION();

    std::unique_ptr<CommandPool> pool;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto reusable =
            std::find_if(m_returnedPools.begin(), m_returnedPools.end(), [&](const std::unique_ptr<CommandPool> &candidate) {
                return candidate->getQueueFamilyIndex() == queueFamilyIndex && candidate->getLevel() == level &&
                       candidate->isIdleOnGpu();
            });

        if (reusable != m_returnedPools.end()) {
            pool = std::move(*reusable);
            m_returnedPools.erase(reusable);
        }
    }

    if (pool != nullptr) {
        pool->reset();
        return CommandBufferLease(this, std::move(pool));
    }

    pool = std::make_unique<CommandPool>(m_device, queueFamilyIndex, level);
    if (pool->getCommandPoolVk() == VK_NULL_HANDLE) {
        return {};
    }

    return CommandBufferLease(this, std::move(pool));
}

void CommandPoolManager::returnPool(std::unique_ptr<CommandPool> pool)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_returnedPools.push_back(std::move(pool));
}

void CommandPoolManager::closeAllPools()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_returnedPools.clear();
}

} // namespace Rapture
