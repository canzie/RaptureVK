#include "VulkanQueue.h"

#include "Logging/Log.h"
#include <stdexcept>

namespace Rapture {
VulkanQueue::VulkanQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, const std::string &name)
    : m_device(device), m_queueFamilyIndex(queueFamilyIndex), m_name(name)
{
    if (m_device == VK_NULL_HANDLE) {
        RP_CORE_ERROR("VulkanQueue[{}]: Device is NULL", m_name);
        throw std::runtime_error("VulkanQueue: Device is NULL");
    }

    vkGetDeviceQueue(m_device, m_queueFamilyIndex, queueIndex, &m_queue);
    createTimelineSemaphore();
    RP_CORE_INFO("VulkanQueue[{}]: Created with timeline semaphore", m_name);
}

VulkanQueue::~VulkanQueue()
{
    if (m_timelineSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_device, m_timelineSemaphore, nullptr);
    }
}

void VulkanQueue::createTimelineSemaphore()
{
    VkSemaphoreTypeCreateInfo timelineCreateInfo{};
    timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = 0;

    VkSemaphoreCreateInfo semaphoreCreateInfo{};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCreateInfo.pNext = &timelineCreateInfo;

    if (vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, &m_timelineSemaphore) != VK_SUCCESS) {
        RP_CORE_ERROR("VulkanQueue[{}]: Failed to create timeline semaphore", m_name);
        throw std::runtime_error("Failed to create timeline semaphore");
    }
}

bool VulkanQueue::submitCommandBuffers(VkFence fence)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    std::lock_guard<std::mutex> lock2(m_commandBufferMutex);

    if (m_commandBuffers.empty()) {
        return true;
    }

    // Atomically prepare all command buffers (check state and transition to PENDING)
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<std::shared_ptr<CommandBuffer>> preparedBuffers;
    commandBuffers.reserve(m_commandBuffers.size());
    preparedBuffers.reserve(m_commandBuffers.size());

    for (auto &cmdBuffer : m_commandBuffers) {
        VkCommandBuffer vkCmdBuffer = cmdBuffer->prepareSubmit();
        if (vkCmdBuffer == VK_NULL_HANDLE) {
            RP_CORE_ERROR("VulkanQueue[{}]: Command buffer '{}' cannot be submitted", m_name, cmdBuffer->getName());
            // Abort all previously prepared buffers
            for (auto &prepared : preparedBuffers) {
                prepared->abortSubmit();
            }
            m_commandBuffers.clear();
            return false;
        }
        commandBuffers.push_back(vkCmdBuffer);
        preparedBuffers.push_back(cmdBuffer);
    }

    // Increment timeline value for this submission
    uint64_t signalValue = ++m_timelineValue;

    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &signalValue;

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
    submitInfo.pCommandBuffers = commandBuffers.data();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_timelineSemaphore;

    VkResult result = vkQueueSubmit(m_queue, 1, &submitInfo, fence);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("VulkanQueue[{}]: submitCommandBuffers(1) failed (VkResult: {})", m_name, static_cast<int>(result));
        for (auto &cmdBuffer : preparedBuffers) {
            cmdBuffer->abortSubmit();
        }
        m_commandBuffers.clear();
        assert(result != VK_ERROR_DEVICE_LOST);
        return false;
    }

    // Complete all command buffers with timeline semaphore info
    for (auto &cmdBuffer : preparedBuffers) {
        cmdBuffer->completeSubmit(m_timelineSemaphore, signalValue);
    }

    m_commandBuffers.clear();
    return true;
}

bool VulkanQueue::submitCommandBuffers(VkSubmitInfo &submitInfo, VkFence fence)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    std::lock_guard<std::mutex> lock2(m_commandBufferMutex);

    if (m_commandBuffers.empty()) {
        RP_CORE_WARN("VulkanQueue[{}]: queue is empty, skipping submission", m_name);
        return true;
    }

    // Atomically prepare all command buffers (check state and transition to PENDING)
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<std::shared_ptr<CommandBuffer>> preparedBuffers;
    commandBuffers.reserve(m_commandBuffers.size());
    preparedBuffers.reserve(m_commandBuffers.size());

    for (auto &cmdBuffer : m_commandBuffers) {
        VkCommandBuffer vkCmdBuffer = cmdBuffer->prepareSubmit();
        if (vkCmdBuffer == VK_NULL_HANDLE) {
            RP_CORE_ERROR("VulkanQueue[{}]: Command buffer '{}' cannot be submitted", m_name, cmdBuffer->getName());
            // Abort all previously prepared buffers
            for (auto &prepared : preparedBuffers) {
                prepared->abortSubmit();
            }
            m_commandBuffers.clear();
            return false;
        }
        commandBuffers.push_back(vkCmdBuffer);
        preparedBuffers.push_back(cmdBuffer);
    }

    // Increment timeline value for this submission
    uint64_t signalValue = ++m_timelineValue;

    // Merge timeline semaphore with existing signal semaphores
    std::vector<VkSemaphore> signalSemaphores(submitInfo.pSignalSemaphores,
                                              submitInfo.pSignalSemaphores + submitInfo.signalSemaphoreCount);
    signalSemaphores.push_back(m_timelineSemaphore);

    std::vector<uint64_t> signalValues(submitInfo.signalSemaphoreCount, 0);
    signalValues.push_back(signalValue);

    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
    timelineInfo.pSignalSemaphoreValues = signalValues.data();
    timelineInfo.waitSemaphoreValueCount = submitInfo.waitSemaphoreCount;
    std::vector<uint64_t> waitValues(submitInfo.waitSemaphoreCount, 0);
    timelineInfo.pWaitSemaphoreValues = waitValues.data();

    submitInfo.pNext = &timelineInfo;
    submitInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
    submitInfo.pCommandBuffers = commandBuffers.data();
    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    VkResult result = vkQueueSubmit(m_queue, 1, &submitInfo, fence);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("VulkanQueue[{}]: submitCommandBuffers(2) failed (VkResult: {})", m_name, static_cast<int>(result));
        for (auto &cmdBuffer : preparedBuffers) {
            cmdBuffer->abortSubmit();
        }
        m_commandBuffers.clear();

        assert(result != VK_ERROR_DEVICE_LOST);
        return false;
    }

    // Complete all command buffers with timeline semaphore info
    for (auto &cmdBuffer : preparedBuffers) {
        cmdBuffer->completeSubmit(m_timelineSemaphore, signalValue);
    }

    m_commandBuffers.clear();
    return true;
}

bool VulkanQueue::submitQueue(std::shared_ptr<CommandBuffer> commandBuffer, VkSubmitInfo &submitInfo, VkFence fence)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);

    if (commandBuffer == nullptr) {
        RP_CORE_CRITICAL("VulkanQueue[{}](1) command buffer is nullptr!", m_name);
        return false;
    }

    // Atomically check state and transition to PENDING before vkQueueSubmit
    VkCommandBuffer commandBufferVk = commandBuffer->prepareSubmit();
    if (commandBufferVk == VK_NULL_HANDLE) {
        RP_CORE_ERROR("VulkanQueue[{}](1) Command buffer '{}' cannot be submitted", m_name, commandBuffer->getName());
        return false;
    }

    // Increment timeline value for this submission
    uint64_t signalValue = ++m_timelineValue;

    // Merge timeline semaphore with existing signal semaphores
    std::vector<VkSemaphore> signalSemaphores(submitInfo.pSignalSemaphores,
                                              submitInfo.pSignalSemaphores + submitInfo.signalSemaphoreCount);
    signalSemaphores.push_back(m_timelineSemaphore);

    std::vector<uint64_t> signalValues(submitInfo.signalSemaphoreCount, 0);
    signalValues.push_back(signalValue);

    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
    timelineInfo.pSignalSemaphoreValues = signalValues.data();
    timelineInfo.waitSemaphoreValueCount = submitInfo.waitSemaphoreCount;
    std::vector<uint64_t> waitValues(submitInfo.waitSemaphoreCount, 0);
    timelineInfo.pWaitSemaphoreValues = waitValues.data();

    submitInfo.pNext = &timelineInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBufferVk;
    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    VkResult result = vkQueueSubmit(m_queue, 1, &submitInfo, fence);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("VulkanQueue[{}](1) failed (VkResult: {})", m_name, static_cast<int>(result));
        commandBuffer->abortSubmit();
        assert(result != VK_ERROR_DEVICE_LOST);
        return false;
    }

    commandBuffer->completeSubmit(m_timelineSemaphore, signalValue);
    return true;
}

bool VulkanQueue::submitQueue(std::shared_ptr<CommandBuffer> commandBuffer, VkFence fence)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);

    if (commandBuffer == nullptr) {
        RP_CORE_CRITICAL("VulkanQueue[{}](2) command buffer is nullptr!", m_name);
        return false;
    }

    // Atomically check state and transition to PENDING before vkQueueSubmit
    VkCommandBuffer commandBufferVk = commandBuffer->prepareSubmit();
    if (commandBufferVk == VK_NULL_HANDLE) {
        RP_CORE_ERROR("VulkanQueue[{}](2) Command buffer '{}' cannot be submitted", m_name, commandBuffer->getName());
        return false;
    }

    // Increment timeline value for this submission
    uint64_t signalValue = ++m_timelineValue;

    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &signalValue;

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBufferVk;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_timelineSemaphore;

    VkResult result = vkQueueSubmit(m_queue, 1, &submitInfo, fence);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("VulkanQueue[{}](2) submitQueue failed (VkResult: {})", m_name, static_cast<int>(result));
        commandBuffer->abortSubmit();
        assert(result != VK_ERROR_DEVICE_LOST);
        return false;
    }

    commandBuffer->completeSubmit(m_timelineSemaphore, signalValue);
    return true;
}

void VulkanQueue::addCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer)
{
    std::lock_guard<std::mutex> lock(m_commandBufferMutex);
    m_commandBuffers.push_back(commandBuffer);
}

void VulkanQueue::waitIdle()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    vkQueueWaitIdle(m_queue);
}

VkResult VulkanQueue::presentQueue(VkPresentInfoKHR &presentInfo)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return vkQueuePresentKHR(m_queue, &presentInfo);
}
void VulkanQueue::clear()
{
    std::lock_guard<std::mutex> lock(m_commandBufferMutex);
    m_commandBuffers.clear();
}
} // namespace Rapture