#include "CommandPool.h"

#include "CommandBuffer.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "WindowContext/Application.h"

namespace Rapture {

// NVIDIA driver has internal state accumulation issues with vkResetCommandPool causing growing latency.
// When enabled, uses VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT and skips vkResetCommandPool entirely.
// Command buffers reset implicitly on vkBeginCommandBuffer instead.
#define RAPTURE_SKIP_COMMAND_POOL_RESET 0

std::unordered_map<CommandPoolHash, std::vector<std::unique_ptr<CommandPool>>> CommandPoolManager::s_commandPools;
std::mutex CommandPoolManager::s_mutex;
uint32_t CommandPoolManager::s_framesInFlight = 0;
uint32_t CommandPoolManager::s_currentFrameIndex = 0;

CommandPool::CommandPool(const CommandPoolConfig &config)
    : m_createInfo{}, m_commandPool(VK_NULL_HANDLE), m_hash(config.hash()), m_device(VK_NULL_HANDLE),
      m_resetFlags(config.resetFlags)
{

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    m_device = device;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
#if RAPTURE_SKIP_COMMAND_POOL_RESET
    poolInfo.flags = config.flags | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
#else
    poolInfo.flags = config.flags | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
#endif
    poolInfo.queueFamilyIndex = config.queueFamilyIndex;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to create command pool!");
        m_commandPool = VK_NULL_HANDLE;
    }
}

CommandPool::~CommandPool()
{

    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    m_commandPool = VK_NULL_HANDLE;
}

CommandBuffer *CommandPool::getPrimaryCommandBuffer()
{
    if (m_primaryCommandBufferIndex >= m_primaryCommandBuffers.size()) {
        allocateCommandBuffer(CmdBufferLevel::PRIMARY);
    }
    m_needsReset = true;
    return m_primaryCommandBuffers[m_primaryCommandBufferIndex++].get();
}

CommandBuffer *CommandPool::getSecondaryCommandBuffer()
{
    if (m_secondaryCommandBufferIndex >= m_secondaryCommandBuffers.size()) {
        allocateCommandBuffer(CmdBufferLevel::SECONDARY);
    }
    m_needsReset = true;
    return m_secondaryCommandBuffers[m_secondaryCommandBufferIndex++].get();
}

void CommandPool::markPendingSignal(VkSemaphore timelineSemaphore, uint64_t signalValue)
{
    auto it = m_pendingSignals.find(timelineSemaphore);
    if (it != m_pendingSignals.end()) {
        it->second = std::max(it->second, signalValue);
    } else {
        m_pendingSignals[timelineSemaphore] = signalValue;
    }
    m_needsReset = true;
}

void CommandPool::resetIfNeeded()
{
    RAPTURE_PROFILE_FUNCTION();

    if (!m_needsReset) {
        return;
    }

    if (!m_pendingSignals.empty()) {
        std::vector<VkSemaphore> semaphores;
        std::vector<uint64_t> values;
        semaphores.reserve(m_pendingSignals.size());
        values.reserve(m_pendingSignals.size());

        for (const auto &[sem, val] : m_pendingSignals) {
            semaphores.push_back(sem);
            values.push_back(val);
        }

        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = static_cast<uint32_t>(semaphores.size());
        waitInfo.pSemaphores = semaphores.data();
        waitInfo.pValues = values.data();

        vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX);
        m_pendingSignals.clear();
    }

    if (m_resetFlags & VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT) {
        m_primaryCommandBuffers.clear();
        m_secondaryCommandBuffers.clear();
    }

    m_primaryCommandBufferIndex = 0;
    m_secondaryCommandBufferIndex = 0;
#if !RAPTURE_SKIP_COMMAND_POOL_RESET
    vkResetCommandPool(m_device, m_commandPool, m_resetFlags);
#endif
    m_needsReset = false;
}

void CommandPool::allocateCommandBuffer(CmdBufferLevel level)
{
    auto commandBuffer = std::make_unique<CommandBuffer>(this, level, "cmd");
    if (level == CmdBufferLevel::PRIMARY) {
        m_primaryCommandBuffers.push_back(std::move(commandBuffer));
    } else {
        m_secondaryCommandBuffers.push_back(std::move(commandBuffer));
    }
}

void CommandPoolManager::init(uint32_t framesInFlight)
{
    s_framesInFlight = framesInFlight;

    RP_CORE_INFO("Initialized CommandPoolManager with {} frames in flight!", framesInFlight);
}

void CommandPoolManager::shutdown()
{
    closeAllPools();
}

CommandPoolHash CommandPoolManager::createCommandPool(const CommandPoolConfig &config)
{
    RAPTURE_PROFILE_FUNCTION();

    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_commandPools.find(config.hash()) != s_commandPools.end()) {
        return config.hash();
    }

    std::vector<std::unique_ptr<CommandPool>> commandPools(s_framesInFlight);
    for (uint32_t i = 0; i < s_framesInFlight; i++) {
        commandPools[i] = std::make_unique<CommandPool>(config);
        if (commandPools[i]->getCommandPoolVk() == VK_NULL_HANDLE) {
            RP_CORE_ERROR("Failed to create command pool({})!", i);
            return 0;
        }
    }
    s_commandPools.insert(std::make_pair(config.hash(), std::move(commandPools)));
    return config.hash();
}

CommandPool *CommandPoolManager::getCommandPool(CommandPoolHash cpHash, uint32_t frameIndex)
{

    (void)frameIndex;

    auto it = s_commandPools.find(cpHash);
    if (it == s_commandPools.end()) {
        RP_CORE_ERROR("Command pool not found for hash {}!", cpHash);
        return nullptr;
    }

    if (s_currentFrameIndex >= it->second.size()) {
        RP_CORE_ERROR("Frame index {} out of bounds (pool size: {}, framesInFlight: {})!", s_currentFrameIndex, it->second.size(),
                      s_framesInFlight);
        return nullptr;
    }

    return it->second[s_currentFrameIndex].get();
}

CommandPool *CommandPoolManager::getCommandPool(CommandPoolHash cpHash)
{
    return getCommandPool(cpHash, s_currentFrameIndex);
}

void CommandPoolManager::beginFrame()
{
    RAPTURE_PROFILE_FUNCTION();

    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto &[hash, pools] : s_commandPools) {
        if (s_currentFrameIndex < pools.size()) {
            pools[s_currentFrameIndex]->resetIfNeeded();
        }
    }
}

void CommandPoolManager::endFrame()
{
    s_currentFrameIndex = (s_currentFrameIndex + 1) % s_framesInFlight;
}

void CommandPoolManager::closeAllPools()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_commandPools.clear();
}

} // namespace Rapture