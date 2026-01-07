#include "VulkanQueue.h"

#include "Buffers/CommandBuffers/CommandPool.h"
#include "Logging/Log.h"
#include <cassert>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan_core.h>

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

    if (vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, &m_immediateTimeSema) != VK_SUCCESS) {
        RP_CORE_ERROR("VulkanQueue[{}]: Failed to create timeline semaphore", m_name);
        throw std::runtime_error("Failed to create timeline semaphore");
    }
}

bool VulkanQueue::flush()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    std::lock_guard<std::mutex> lock2(m_cmdBatchMutex);

    if (m_cmdBufferBatch.empty()) {
        return true;
    }

    std::vector<VkCommandBuffer> commandBuffers;
    commandBuffers.reserve(m_cmdBufferBatch.size());

    for (auto *cmdBuffer : m_cmdBufferBatch) {
        commandBuffers.push_back(cmdBuffer->getCommandBufferVk());
    }

    uint64_t signalValue = m_nextTimelineValue.load() - 1;

    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &signalValue;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
    submitInfo.pCommandBuffers = commandBuffers.data();

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_timelineSemaphore;

    VkResult result = vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("VulkanQueue[{}]: flush failed (VkResult: {})", m_name, static_cast<int>(result));
        m_cmdBufferBatch.clear();
        assert(result != VK_ERROR_DEVICE_LOST);
        return false;
    }

    for (auto *cmdBuffer : m_cmdBufferBatch) {
        cmdBuffer->clearSecondaries();
    }

    m_cmdBufferBatch.clear();
    return true;
}

uint64_t VulkanQueue::addToBatch(CommandBuffer *commandBuffer)
{
    uint64_t signalValue = m_nextTimelineValue.fetch_add(1);

    if (commandBuffer->getCommandPool()) {
        commandBuffer->getCommandPool()->markPendingSignal(m_timelineSemaphore, signalValue);
    }

    for (auto *secCmdBuffer : commandBuffer->getSecondaries()) {
        if (secCmdBuffer->getCommandPool()) {
            secCmdBuffer->getCommandPool()->markPendingSignal(m_timelineSemaphore, signalValue);
        }
    }

    std::lock_guard<std::mutex> lock(m_cmdBatchMutex);
    m_cmdBufferBatch.push_back(commandBuffer);

    return signalValue;
}

bool VulkanQueue::submitQueue(CommandBuffer *commandBuffer, std::span<VkSemaphore> *signalSemaphores,
                              std::span<VkSemaphore> *waitSemaphores, VkPipelineStageFlags *waitStage, VkFence fence)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);

    if (commandBuffer == nullptr) {
        RP_CORE_CRITICAL("VulkanQueue[{}] command buffer is nullptr!", m_name);
        return false;
    }

    uint64_t signalValue = m_nextImmediateTimelineValue.fetch_add(1);

    std::vector<VkSemaphore> allSignalSemaphores;
    std::vector<uint64_t> signalValues;

    if (signalSemaphores && !signalSemaphores->empty()) {
        allSignalSemaphores.assign(signalSemaphores->begin(), signalSemaphores->end());
        // Binary semaphores use value 0
        signalValues.resize(signalSemaphores->size(), 0);
    }
    allSignalSemaphores.push_back(m_immediateTimeSema);
    signalValues.push_back(signalValue);

    VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();

    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.pSignalSemaphoreValues = signalValues.data();
    timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
    timelineInfo.waitSemaphoreValueCount = 0;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBufferVk;
    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(allSignalSemaphores.size());
    submitInfo.pSignalSemaphores = allSignalSemaphores.data();

    if (waitSemaphores && !waitSemaphores->empty()) {
        submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores->size());
        submitInfo.pWaitSemaphores = waitSemaphores->data();
        submitInfo.pWaitDstStageMask = waitStage;
    }

    VkResult result = vkQueueSubmit(m_queue, 1, &submitInfo, fence);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("VulkanQueue[{}] failed (VkResult: {})", m_name, static_cast<int>(result));
        assert(result != VK_ERROR_DEVICE_LOST);
        return false;
    }

    if (commandBuffer->getCommandPool()) {
        commandBuffer->getCommandPool()->markPendingSignal(m_immediateTimeSema, signalValue);
    }

    for (auto *secCmdBuffer : commandBuffer->getSecondaries()) {
        if (secCmdBuffer->getCommandPool()) {
            secCmdBuffer->getCommandPool()->markPendingSignal(m_immediateTimeSema, signalValue);
        }
    }

    commandBuffer->clearSecondaries();

    return true;
}

bool VulkanQueue::submitAndFlushQueue(CommandBuffer *commandBuffer, std::span<VkSemaphore> *signalSemaphores,
                                      std::span<VkSemaphore> *waitSemaphores, VkPipelineStageFlags *waitStage, VkFence fence)
{

    // TODO: properly lock/unlock the cmd buffer mutex here, or use the lockfree queue;
    if (commandBuffer == nullptr) {
        if (m_cmdBufferBatch.empty()) {
            RP_CORE_CRITICAL("Command buffer is nullptr! and nothing to flush", m_name);
            return false;
        }
        RP_CORE_WARN("CommandBuffer is not valid, only flushing, your fences will be ignored");
        return flush();
    } else if (m_cmdBufferBatch.empty()) {
        return submitQueue(commandBuffer, signalSemaphores, waitSemaphores, waitStage, fence);
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);

        uint64_t signalValue = m_nextImmediateTimelineValue.fetch_add(1);

        std::vector<VkSemaphore> allSignalSemaphores;
        std::vector<uint64_t> signalValues;

        if (signalSemaphores && !signalSemaphores->empty()) {
            allSignalSemaphores.assign(signalSemaphores->begin(), signalSemaphores->end());
            // Binary semaphores use value 0
            signalValues.resize(signalSemaphores->size(), 0);
        }
        allSignalSemaphores.push_back(m_immediateTimeSema);
        signalValues.push_back(signalValue);

        VkCommandBuffer immediateCommandBufferVk = commandBuffer->getCommandBufferVk();

        std::vector<VkCommandBuffer> commandBuffers;
        commandBuffers.reserve(m_cmdBufferBatch.size());

        for (auto *cmdBuffer : m_cmdBufferBatch) {
            commandBuffers.push_back(cmdBuffer->getCommandBufferVk());
        }

        VkTimelineSemaphoreSubmitInfo immediateTimelineInfo{};
        immediateTimelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        immediateTimelineInfo.pSignalSemaphoreValues = signalValues.data();
        immediateTimelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
        immediateTimelineInfo.waitSemaphoreValueCount = 0;

        VkSubmitInfo immediateSubmitInfo{};
        immediateSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        immediateSubmitInfo.pNext = &immediateTimelineInfo;
        immediateSubmitInfo.commandBufferCount = 1;
        immediateSubmitInfo.pCommandBuffers = &immediateCommandBufferVk;
        immediateSubmitInfo.signalSemaphoreCount = static_cast<uint32_t>(allSignalSemaphores.size());
        immediateSubmitInfo.pSignalSemaphores = allSignalSemaphores.data();

        if (waitSemaphores && !waitSemaphores->empty()) {
            immediateSubmitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores->size());
            immediateSubmitInfo.pWaitSemaphores = waitSemaphores->data();
            immediateSubmitInfo.pWaitDstStageMask = waitStage;
        }

        uint64_t batchSignalValue = m_nextTimelineValue.load() - 1;

        VkTimelineSemaphoreSubmitInfo batchTimelineInfo{};
        batchTimelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        batchTimelineInfo.signalSemaphoreValueCount = 1;
        batchTimelineInfo.pSignalSemaphoreValues = &batchSignalValue;

        VkSubmitInfo batchSubmitInfo{};
        batchSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        batchSubmitInfo.pNext = &batchTimelineInfo;
        batchSubmitInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
        batchSubmitInfo.pCommandBuffers = commandBuffers.data();
        batchSubmitInfo.signalSemaphoreCount = 1;
        batchSubmitInfo.pSignalSemaphores = &m_timelineSemaphore;

        std::vector<VkSubmitInfo> submits(2);
        submits[0] = immediateSubmitInfo;
        submits[1] = batchSubmitInfo;

        VkResult result = vkQueueSubmit(m_queue, static_cast<uint32_t>(submits.size()), submits.data(), fence);
        if (result != VK_SUCCESS) {
            RP_CORE_ERROR("VulkanQueue[{}](1) failed (VkResult: {})", m_name, static_cast<int>(result));
            assert(result != VK_ERROR_DEVICE_LOST);
            return false;
        }

        for (auto *cmdBuffer : m_cmdBufferBatch) {
            cmdBuffer->clearSecondaries();
        }

        if (commandBuffer->getCommandPool()) {
            commandBuffer->getCommandPool()->markPendingSignal(m_immediateTimeSema, signalValue);
        }

        for (auto *secCmdBuffer : commandBuffer->getSecondaries()) {
            if (secCmdBuffer->getCommandPool()) {
                secCmdBuffer->getCommandPool()->markPendingSignal(m_immediateTimeSema, signalValue);
            }
        }

        commandBuffer->clearSecondaries();

        m_cmdBufferBatch.clear();
    }

    return true;
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
    std::lock_guard<std::mutex> lock(m_cmdBatchMutex);
    m_cmdBufferBatch.clear();
}
} // namespace Rapture
