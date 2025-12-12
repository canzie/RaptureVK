#include "CommandBuffer.h"

#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include "CommandPool.h"

namespace Rapture {

const char *cmdBufferStateToString(CmdBufferState state)
{
    switch (state) {
    case CmdBufferState::INITIAL:
        return "INITIAL";
    case CmdBufferState::RECORDING:
        return "RECORDING";
    case CmdBufferState::EXECUTABLE:
        return "EXECUTABLE";
    case CmdBufferState::PENDING:
        return "PENDING";
    case CmdBufferState::INVALID:
        return "INVALID";
    default:
        return "UNKNOWN";
    }
}

CommandBuffer::CommandBuffer(std::shared_ptr<CommandPool> commandPool, const std::string &name)
    : m_commandPool(commandPool), m_state(CmdBufferState::INITIAL), m_name(name)
{
    auto &app = Application::getInstance();
    m_device = app.getVulkanContext().getLogicalDevice();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool->getCommandPoolVk();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer) != VK_SUCCESS) {
        RP_CORE_ERROR("CommandBuffer[{}]: failed to allocate command buffer!", m_name);
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

CommandBuffer::CommandBuffer(std::shared_ptr<CommandPool> commandPool, VkCommandBuffer commandBuffer, const std::string &name)
    : m_commandBuffer(commandBuffer), m_commandPool(commandPool), m_state(CmdBufferState::INITIAL), m_name(name)
{
    auto &app = Application::getInstance();
    m_device = app.getVulkanContext().getLogicalDevice();
}

CommandBuffer::~CommandBuffer()
{
    std::lock_guard<std::mutex> lock(m_stateMutex);

    // Check if still pending - this is an error
    if (m_state == CmdBufferState::PENDING) {
        updatePendingState();
        if (m_state == CmdBufferState::PENDING) {
            RP_CORE_ERROR("CommandBuffer[{}]: destroying command buffer while still PENDING!", m_name);
        }
    }

    vkFreeCommandBuffers(m_device, m_commandPool->getCommandPoolVk(), 1, &m_commandBuffer);
}

CmdBufferState CommandBuffer::getState()
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    updatePendingState();
    return m_state;
}

void CommandBuffer::updatePendingState()
{
    if (m_state != CmdBufferState::PENDING || m_pendingSemaphore == VK_NULL_HANDLE) {
        return;
    }

    uint64_t currentValue = 0;
    VkResult result = vkGetSemaphoreCounterValue(m_device, m_pendingSemaphore, &currentValue);
    if (result != VK_SUCCESS) {
        RP_CORE_WARN("CommandBuffer[{}]: failed to query timeline semaphore value", m_name);
        return;
    }

    if (currentValue >= m_pendingSignalValue) {
        if (m_oneTimeSubmit) {
            m_state = CmdBufferState::INVALID;
        } else {
            m_state = CmdBufferState::EXECUTABLE;
        }
        m_pendingSemaphore = VK_NULL_HANDLE;
        m_pendingSignalValue = 0;
    }
}

void CommandBuffer::reset(VkCommandBufferResetFlags flags)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    updatePendingState();

    if (m_state == CmdBufferState::PENDING) {
        RP_CORE_ERROR("CommandBuffer[{}]: cannot reset while in PENDING state", m_name);
        return;
    }

    if (m_state == CmdBufferState::RECORDING) {
        RP_CORE_WARN("CommandBuffer[{}]: resetting while in RECORDING state", m_name);
    }

    VkResult result = vkResetCommandBuffer(m_commandBuffer, flags);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("CommandBuffer[{}]: failed to reset (VkResult: {})", m_name, static_cast<int>(result));
        m_state = CmdBufferState::INVALID;
        return;
    }

    m_state = CmdBufferState::INITIAL;
    m_oneTimeSubmit = false;
    m_pendingSemaphore = VK_NULL_HANDLE;
    m_pendingSignalValue = 0;
}

VkResult CommandBuffer::begin(VkCommandBufferUsageFlags flags)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    updatePendingState();

    if (m_state == CmdBufferState::PENDING) {
        RP_CORE_ERROR("CommandBuffer[{}]: cannot begin while in PENDING state", m_name);
        return VK_ERROR_UNKNOWN;
    }

    if (m_state == CmdBufferState::RECORDING) {
        RP_CORE_ERROR("CommandBuffer[{}]: cannot begin while already in RECORDING state", m_name);
        return VK_ERROR_UNKNOWN;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;
    beginInfo.pInheritanceInfo = nullptr;

    VkResult result = vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("CommandBuffer[{}]: vkBeginCommandBuffer failed (VkResult: {})", m_name, static_cast<int>(result));
        m_state = CmdBufferState::INVALID;
        return result;
    }

    m_state = CmdBufferState::RECORDING;
    m_oneTimeSubmit = (flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != 0;
    return VK_SUCCESS;
}

VkResult CommandBuffer::end()
{
    std::lock_guard<std::mutex> lock(m_stateMutex);

    if (m_state != CmdBufferState::RECORDING) {
        RP_CORE_ERROR("CommandBuffer[{}]: cannot end, not in RECORDING state (current: {})", m_name,
                      cmdBufferStateToString(m_state));
        return VK_ERROR_UNKNOWN;
    }

    VkResult result = vkEndCommandBuffer(m_commandBuffer);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("CommandBuffer[{}]: vkEndCommandBuffer failed (VkResult: {})", m_name, static_cast<int>(result));
        m_state = CmdBufferState::INVALID;
        return result;
    }

    m_state = CmdBufferState::EXECUTABLE;
    return VK_SUCCESS;
}

bool CommandBuffer::onSubmit(VkSemaphore timelineSemaphore, uint64_t signalValue)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    updatePendingState();

    if (m_state != CmdBufferState::EXECUTABLE) {
        RP_CORE_ERROR("CommandBuffer[{}]: cannot submit, not in EXECUTABLE state (current: {})", m_name,
                      cmdBufferStateToString(m_state));
        return false;
    }

    m_state = CmdBufferState::PENDING;
    m_pendingSemaphore = timelineSemaphore;
    m_pendingSignalValue = signalValue;
    return true;
}

void CommandBuffer::onSubmitFailed()
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    // On submit failure, the command buffer remains in EXECUTABLE state
    // (the GPU never started processing it)
    if (m_state == CmdBufferState::PENDING) {
        m_state = CmdBufferState::EXECUTABLE;
        m_pendingSemaphore = VK_NULL_HANDLE;
        m_pendingSignalValue = 0;
    }
}

bool CommandBuffer::canSubmit()
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    updatePendingState();
    return m_state == CmdBufferState::EXECUTABLE;
}

bool CommandBuffer::canReset()
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    updatePendingState();
    return m_state != CmdBufferState::PENDING;
}

bool CommandBuffer::canBegin()
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    updatePendingState();
    // Can begin from INITIAL, or from EXECUTABLE/INVALID if pool has RESET_COMMAND_BUFFER_BIT
    return m_state == CmdBufferState::INITIAL || m_state == CmdBufferState::EXECUTABLE || m_state == CmdBufferState::INVALID;
}

std::vector<std::shared_ptr<CommandBuffer>> CommandBuffer::createCommandBuffers(std::shared_ptr<CommandPool> commandPool,
                                                                                uint32_t count, const std::string &namePrefix)
{
    std::vector<VkCommandBuffer> commandBuffersVk(count);

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool->getCommandPoolVk();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = count;

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffersVk.data()) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to allocate command buffers!");
        throw std::runtime_error("failed to allocate command buffers!");
    }

    std::vector<std::shared_ptr<CommandBuffer>> commandBuffers;
    for (uint32_t i = 0; i < count; i++) {
        std::string name = namePrefix + "_" + std::to_string(i);
        commandBuffers.push_back(std::make_shared<CommandBuffer>(commandPool, commandBuffersVk[i], name));
    }

    return commandBuffers;
}
} // namespace Rapture