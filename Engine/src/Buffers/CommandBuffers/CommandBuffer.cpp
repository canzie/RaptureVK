#include "CommandBuffer.h"

#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include "CommandPool.h"

namespace Rapture {

CommandBuffer::CommandBuffer(std::shared_ptr<CommandPool> commandPool)
{

    auto &app = Application::getInstance();
    m_device = app.getVulkanContext().getLogicalDevice();
    m_commandPool = commandPool;

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool->getCommandPoolVk();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to allocate command buffers!");
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

CommandBuffer::CommandBuffer(std::shared_ptr<CommandPool> commandPool, VkCommandBuffer commandBuffer)
    : m_commandBuffer(commandBuffer), m_commandPool(commandPool)
{
    auto &app = Application::getInstance();
    m_device = app.getVulkanContext().getLogicalDevice();
}

CommandBuffer::~CommandBuffer()
{
    vkFreeCommandBuffers(m_device, m_commandPool->getCommandPoolVk(), 1, &m_commandBuffer);
}

void CommandBuffer::reset(VkCommandBufferResetFlags flags)
{
    VkResult result = vkResetCommandBuffer(m_commandBuffer, flags);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("CommandBuffer::reset - failed to reset command buffer (VkResult code: {})", static_cast<int>(result));
    }
}

VkResult CommandBuffer::end()
{
    return vkEndCommandBuffer(m_commandBuffer);
}

std::vector<std::shared_ptr<CommandBuffer>> CommandBuffer::createCommandBuffers(std::shared_ptr<CommandPool> commandPool,
                                                                                uint32_t count)
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
        commandBuffers.push_back(std::make_shared<CommandBuffer>(commandPool, commandBuffersVk[i]));
    }

    return commandBuffers;
}
} // namespace Rapture