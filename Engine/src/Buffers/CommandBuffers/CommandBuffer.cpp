#include "CommandBuffer.h"

#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include "CommandPool.h"

#include <set>

namespace Rapture {

CommandBuffer::CommandBuffer(CommandPool *commandPool, CmdBufferLevel level, const std::string &name)
    : m_commandPool(commandPool), m_device(VK_NULL_HANDLE), m_level(level), m_name(name)
{
    auto &app = Application::getInstance();
    m_device = app.getVulkanContext().getLogicalDevice();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool->getCommandPoolVk();
    allocInfo.level = (level == CmdBufferLevel::SECONDARY) ? VK_COMMAND_BUFFER_LEVEL_SECONDARY : VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer) != VK_SUCCESS) {
        RP_CORE_ERROR("CommandBuffer[{}]: failed to allocate command buffer!", m_name);
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

CommandBuffer::CommandBuffer(CommandPool *commandPool, VkCommandBuffer commandBuffer, CmdBufferLevel level, const std::string &name)
    : m_commandBuffer(commandBuffer), m_commandPool(commandPool), m_level(level), m_name(name)
{
    auto &app = Application::getInstance();
    m_device = app.getVulkanContext().getLogicalDevice();
}

CommandBuffer::~CommandBuffer()
{
    // we do not manually freem the the command pool will manage the frees by calling a reset or a destruction of the pool
}

VkResult CommandBuffer::begin(VkCommandBufferUsageFlags flags)
{
    if (m_level == CmdBufferLevel::SECONDARY) {
        RP_CORE_ERROR("CommandBuffer[{}]: begin called on secondary buffer", m_name);
        return VK_ERROR_UNKNOWN;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;
    beginInfo.pInheritanceInfo = nullptr;

    VkResult result = vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("CommandBuffer[{}]: vkBeginCommandBuffer failed (VkResult: {})", m_name, static_cast<int>(result));
    }

    return result;
}

VkResult CommandBuffer::beginSecondary(const SecondaryBufferInheritance &inheritance, VkCommandBufferUsageFlags flags)
{

    if (m_level != CmdBufferLevel::SECONDARY) {
        RP_CORE_ERROR("CommandBuffer[{}]: beginSecondary called on primary buffer", m_name);
        return VK_ERROR_UNKNOWN;
    }

    VkCommandBufferInheritanceRenderingInfo inheritanceRendering{};
    inheritanceRendering.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
    inheritanceRendering.flags = inheritance.renderingFlags;
    inheritanceRendering.viewMask = inheritance.viewMask;
    inheritanceRendering.colorAttachmentCount = static_cast<uint32_t>(inheritance.colorFormats.size());
    inheritanceRendering.pColorAttachmentFormats = inheritance.colorFormats.data();
    inheritanceRendering.depthAttachmentFormat = inheritance.depthFormat;
    inheritanceRendering.stencilAttachmentFormat = inheritance.stencilFormat;
    inheritanceRendering.rasterizationSamples = inheritance.samples;

    VkCommandBufferInheritanceInfo inheritanceInfo{};
    inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritanceInfo.pNext = &inheritanceRendering;
    inheritanceInfo.renderPass = VK_NULL_HANDLE;
    inheritanceInfo.subpass = 0;
    inheritanceInfo.framebuffer = VK_NULL_HANDLE;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    beginInfo.pInheritanceInfo = &inheritanceInfo;

    VkResult result = vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("CommandBuffer[{}]: vkBeginCommandBuffer failed (VkResult: {})", m_name, static_cast<int>(result));
    }

    return result;
}

VkResult CommandBuffer::end()
{
    VkResult result = vkEndCommandBuffer(m_commandBuffer);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("CommandBuffer[{}]: vkEndCommandBuffer failed (VkResult: {})", m_name, static_cast<int>(result));
    }

    return result;
}

void CommandBuffer::executeSecondary(CommandBuffer &secondary)
{
    if (m_level != CmdBufferLevel::PRIMARY) {
        RP_CORE_ERROR("CommandBuffer[{}]: executeSecondary called on non-primary buffer", m_name);
        return;
    }

    if (!secondary.isSecondary()) {
        RP_CORE_ERROR("CommandBuffer[{}]: executeSecondary called with invalid secondary buffer", m_name);
        return;
    }

    secondaries.push_back(&secondary);
    VkCommandBuffer secondaryVk = secondary.getCommandBufferVk();
    vkCmdExecuteCommands(m_commandBuffer, 1, &secondaryVk);
}

void CommandBuffer::executeSecondaries(const std::vector<const CommandBuffer *> &secondaries)
{
    if (m_level != CmdBufferLevel::PRIMARY) {
        RP_CORE_ERROR("CommandBuffer[{}]: executeSecondaries called on non-primary buffer", m_name);
        return;
    }

    if (secondaries.empty()) {
        return;
    }

    std::vector<VkCommandBuffer> secondaryBuffers;
    secondaryBuffers.reserve(secondaries.size());
    for (const auto *secondary : secondaries) {
        if (!secondary || !secondary->isSecondary()) {
            RP_CORE_ERROR("CommandBuffer[{}]: executeSecondaries contains invalid secondary buffer", m_name);
            return;
        }
        secondaryBuffers.push_back(secondary->getCommandBufferVk());
    }
    vkCmdExecuteCommands(m_commandBuffer, static_cast<uint32_t>(secondaryBuffers.size()), secondaryBuffers.data());
}

std::vector<std::unique_ptr<CommandBuffer>> CommandBuffer::createCommandBuffers(CommandPool *commandPool, uint32_t count,
                                                                                const std::string &namePrefix)
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

    std::vector<std::unique_ptr<CommandBuffer>> commandBuffers;
    for (uint32_t i = 0; i < count; i++) {
        std::string name = namePrefix + "_" + std::to_string(i);
        commandBuffers.push_back(std::make_unique<CommandBuffer>(commandPool, commandBuffersVk[i], CmdBufferLevel::PRIMARY, name));
    }

    return commandBuffers;
}

std::vector<std::unique_ptr<CommandBuffer>> CommandBuffer::createSecondaryCommandBuffers(CommandPool *commandPool, uint32_t count,
                                                                                         const std::string &namePrefix)
{
    std::vector<VkCommandBuffer> commandBuffersVk(count);

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool->getCommandPoolVk();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    allocInfo.commandBufferCount = count;

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffersVk.data()) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to allocate secondary command buffers!");
        return {};
    }

    std::vector<std::unique_ptr<CommandBuffer>> commandBuffers;
    commandBuffers.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        std::string name = namePrefix + "_" + std::to_string(i);
        commandBuffers.push_back(
            std::make_unique<CommandBuffer>(commandPool, commandBuffersVk[i], CmdBufferLevel::SECONDARY, name));
    }

    return commandBuffers;
}

} // namespace Rapture
