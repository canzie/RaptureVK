#include <rapture/gpu/command_buffers/CommandBuffer.h>

#include <rapture/app/Application.h>
#include <rapture/core/utils/Log.h>

#include <rapture/gpu/command_buffers/CommandPool.h>

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

void CommandBuffer::executeSecondary(CommandBufferLease secondary)
{
    if (m_level != CmdBufferLevel::PRIMARY) {
        RP_CORE_ERROR("CommandBuffer[{}]: executeSecondary called on non-primary buffer", m_name);
        return;
    }

    if (!secondary.isValid() || !secondary->isSecondary()) {
        RP_CORE_ERROR("CommandBuffer[{}]: executeSecondary called with invalid secondary buffer", m_name);
        return;
    }

    VkCommandBuffer secondaryVk = secondary->getCommandBufferVk();
    vkCmdExecuteCommands(m_commandBuffer, 1, &secondaryVk);
    m_secondaries.push_back(std::move(secondary));
}

} // namespace Rapture
