#include "CommandPool.h"

#include "CommandBuffer.h"
#include "Logging/Log.h"
#include "WindowContext/Application.h"

namespace Rapture {

#define MAX_DEFERRED_CMD_BUFFER_DESTROY_ATTEMPTS 100

std::unordered_map<CommandPoolHash, std::shared_ptr<CommandPool>> CommandPoolManager::s_commandPools;
std::mutex CommandPoolManager::s_mutex;

void s_updatePendingState(CmdBufferDefferedDestruction &cmdBufferDefferedDestruction, VkDevice device)
{
    if (cmdBufferDefferedDestruction.state != CmdBufferState::PENDING ||
        cmdBufferDefferedDestruction.pendingSemaphore == VK_NULL_HANDLE) {
        return;
    }

    uint64_t currentValue = 0;
    VkResult result = vkGetSemaphoreCounterValue(device, cmdBufferDefferedDestruction.pendingSemaphore, &currentValue);
    if (result != VK_SUCCESS) {
        RP_CORE_WARN("CommandBuffer[{}]: failed to query timeline semaphore value", cmdBufferDefferedDestruction.name);
        return;
    }

    if (currentValue >= cmdBufferDefferedDestruction.pendingSignalValue) {
        cmdBufferDefferedDestruction.state = CmdBufferState::INVALID;
        cmdBufferDefferedDestruction.pendingSemaphore = VK_NULL_HANDLE;
        cmdBufferDefferedDestruction.pendingSignalValue = 0;
    }
}

CommandPool::CommandPool(const CommandPoolConfig &config)
    : m_createInfo{}, m_commandPool(VK_NULL_HANDLE), m_hash(config.hash()), m_device(VK_NULL_HANDLE)
{

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    m_device = device;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = config.flags;
    poolInfo.queueFamilyIndex = config.queueFamilyIndex;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to create command pool!");
        m_commandPool = VK_NULL_HANDLE;
    }
}

CommandPool::~CommandPool()
{
    RP_CORE_TRACE("Command Pool destroying remaining command buffers...");
    for (auto &cmdBufferDefferedDestruction : m_deferredCmdBufferDestructions) {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmdBufferDefferedDestruction.commandBuffer);
    }
    m_deferredCmdBufferDestructions.clear();

    m_savedCommandBuffers.clear();
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    m_commandPool = VK_NULL_HANDLE;
}

void CommandPool::deferCmdBufferDestruction(CmdBufferDefferedDestruction cmdBufferDefferedDestruction)
{
    m_deferredCmdBufferDestructions.push_back(cmdBufferDefferedDestruction);
}

void CommandPool::onUpdate(float dt)
{
    (void)dt;

    for (auto it = m_deferredCmdBufferDestructions.begin(); it != m_deferredCmdBufferDestructions.end();) {
        auto &cmdBufferDefferedDestruction = *it;

        cmdBufferDefferedDestruction.destroyAttempts++;
        if (cmdBufferDefferedDestruction.destroyAttempts > MAX_DEFERRED_CMD_BUFFER_DESTROY_ATTEMPTS) {
            RP_CORE_ERROR("CommandBuffer[{}]: failed to destroy command buffer after {} attempts! forcing removal",
                          cmdBufferDefferedDestruction.name, MAX_DEFERRED_CMD_BUFFER_DESTROY_ATTEMPTS);
            vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmdBufferDefferedDestruction.commandBuffer);
            it = m_deferredCmdBufferDestructions.erase(it);
            continue;
        }

        s_updatePendingState(cmdBufferDefferedDestruction, m_device);
        if (cmdBufferDefferedDestruction.state != CmdBufferState::PENDING) {
            RP_CORE_TRACE("Command Pool cleaned up command buffer: {}", cmdBufferDefferedDestruction.name);
            vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmdBufferDefferedDestruction.commandBuffer);
            it = m_deferredCmdBufferDestructions.erase(it);
            continue;
        }

        it++;
    }
}

std::shared_ptr<CommandBuffer> CommandPool::getCommandBuffer(const std::string &name, bool stayAlive)
{
    auto commandBuffer = std::make_shared<CommandBuffer>(shared_from_this(), name);
    if (stayAlive) {
        m_savedCommandBuffers.push_back(commandBuffer);
    }
    return commandBuffer;
}

std::vector<std::shared_ptr<CommandBuffer>> CommandPool::getCommandBuffers(uint32_t count, const std::string &namePrefix)
{
    return CommandBuffer::createCommandBuffers(shared_from_this(), count, namePrefix);
}

void CommandPoolManager::init()
{
    // s_commandPools.clear();
}

void CommandPoolManager::shutdown()
{
    closeAllPools();
}

void CommandPoolManager::onUpdate(float dt)
{
    for (auto &commandPool : s_commandPools) {
        commandPool.second->onUpdate(dt);
    }
}

std::shared_ptr<CommandPool> CommandPoolManager::createCommandPool(const CommandPoolConfig &config)
{
    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_commandPools.find(config.hash()) != s_commandPools.end()) {
        return s_commandPools[config.hash()];
    }

    auto commandPool = std::make_shared<CommandPool>(config);
    if (commandPool->getCommandPoolVk() == VK_NULL_HANDLE) {
        RP_CORE_ERROR("Failed to create command pool!");
        return nullptr;
    }

    s_commandPools.insert(std::make_pair(config.hash(), commandPool));

    return commandPool;
}

/*
std::shared_ptr<CommandPool> CommandPoolManager::getCommandPool(const CommandPoolConfig& config, bool isStrict)
{
    uint32_t hash = config.hash();

    return getCommandPool(hash);
}
*/

std::shared_ptr<CommandPool> CommandPoolManager::getCommandPool(CommandPoolHash cpHash)
{
    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_commandPools.find(cpHash) == s_commandPools.end()) {
        RP_CORE_ERROR("Command pool not found!");
        return nullptr;
    }

    return s_commandPools[cpHash];
}

/*
void CommandPoolManager::closePool(uint32_t CPHash)
{
    if (s_commandPools.find(CPHash) == s_commandPools.end()) {
        RP_CORE_ERROR("CommandPoolManager::closePool - command pool not found!");
        return;
    }

    s_commandPools.erase(CPHash);
}
*/

void CommandPoolManager::closeAllPools()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_commandPools.clear();
}

} // namespace Rapture