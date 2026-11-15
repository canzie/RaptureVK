#include <rapture/gpu/vulkan_context/VulkanQueue.h>

#include <cassert>
#include <cstdint>
#include <rapture/app/Application.h>
#include <rapture/core/utils/Log.h>
#include <rapture/gpu/command_buffers/CommandPool.h>
#include <set>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace Rapture {

static void s_markCommandBufferSubmitted(CommandBuffer *commandBuffer, VkSemaphore timelineSemaphore, uint64_t signalValue)
{
    commandBuffer->getCommandPool()->markPendingSignal(timelineSemaphore, signalValue);

    for (const CommandBufferLease &secondary : commandBuffer->getSecondaries()) {
        secondary->getCommandPool()->markPendingSignal(timelineSemaphore, signalValue);
    }

    commandBuffer->clearSecondaries();
}

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

bool VulkanQueue::submitBatchThen(const VkSubmitInfo2 *submitAfterBatch, VkFence fence)
{
    std::vector<VkCommandBufferSubmitInfo> batchCommandBuffers;
    batchCommandBuffers.reserve(m_cmdBufferBatch.size());
    for (CommandBuffer *commandBuffer : m_cmdBufferBatch) {
        VkCommandBufferSubmitInfo commandBufferInfo{};
        commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferInfo.commandBuffer = commandBuffer->getCommandBufferVk();
        batchCommandBuffers.push_back(commandBufferInfo);
    }

    VkSemaphoreSubmitInfo batchSignal{};
    batchSignal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    batchSignal.semaphore = m_timelineSemaphore;
    batchSignal.value = m_batchSignalValue;
    batchSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    std::vector<VkSubmitInfo2> submits;
    if (!m_cmdBufferBatch.empty()) {
        VkSubmitInfo2 batchSubmit{};
        batchSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        batchSubmit.commandBufferInfoCount = static_cast<uint32_t>(batchCommandBuffers.size());
        batchSubmit.pCommandBufferInfos = batchCommandBuffers.data();
        batchSubmit.signalSemaphoreInfoCount = 1;
        batchSubmit.pSignalSemaphoreInfos = &batchSignal;
        submits.push_back(batchSubmit);
    }
    if (submitAfterBatch != nullptr) {
        submits.push_back(*submitAfterBatch);
    }

    if (submits.empty() && fence == VK_NULL_HANDLE) {
        return true;
    }

    VkResult result = vkQueueSubmit2(m_queue, static_cast<uint32_t>(submits.size()), submits.data(), fence);

    if (result == VK_SUCCESS && !m_cmdBufferBatch.empty()) {
        m_lastSubmittedValue.store(m_batchSignalValue, std::memory_order_release);
    }

    m_cmdBufferBatch.clear();
    m_batchSignalValue = 0;

    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("VulkanQueue[{}]: submission failed (VkResult: {})", m_name, static_cast<int>(result));
        if (result == VK_ERROR_DEVICE_LOST) {
            Application::getInstance().getVulkanContext().logDeviceFaultInfo();
        }
        assert(result != VK_ERROR_DEVICE_LOST);
        return false;
    }

    return true;
}

bool VulkanQueue::flush()
{
    std::lock_guard<std::mutex> queueLock(m_queueMutex);
    std::lock_guard<std::mutex> batchLock(m_cmdBatchMutex);

    if (m_cmdBufferBatch.empty()) {
        return true;
    }

    return submitBatchThen(nullptr, VK_NULL_HANDLE);
}

uint64_t VulkanQueue::addToBatch(CommandBuffer *commandBuffer)
{
    std::lock_guard<std::mutex> batchLock(m_cmdBatchMutex);

    // the value is reserved now, any submission reserving a later one sends the batch ahead of itself
    if (m_cmdBufferBatch.empty()) {
        m_batchSignalValue = m_nextTimelineValue++;
    }

    s_markCommandBufferSubmitted(commandBuffer, m_timelineSemaphore, m_batchSignalValue);

    m_cmdBufferBatch.push_back(commandBuffer);

    return m_batchSignalValue;
}

uint64_t VulkanQueue::submitQueue(CommandBuffer *commandBuffer, const SubmitSynchronization &synchronization)
{
    if (commandBuffer == nullptr) {
        RP_CORE_CRITICAL("VulkanQueue[{}] command buffer is nullptr!", m_name);
        return 0;
    }

    std::vector<VkSemaphoreSubmitInfo> waitInfos;
    waitInfos.reserve(synchronization.waits.size());
    for (const SemaphoreWait &wait : synchronization.waits) {
        VkSemaphoreSubmitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfo.semaphore = wait.semaphore;
        waitInfo.value = wait.value;
        waitInfo.stageMask = wait.stageMask;
        waitInfos.push_back(waitInfo);
    }

    std::vector<VkSemaphoreSubmitInfo> signalInfos;
    signalInfos.reserve(synchronization.signals.size() + 1);
    for (const SemaphoreSignal &signal : synchronization.signals) {
        VkSemaphoreSubmitInfo signalInfo{};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfo.semaphore = signal.semaphore;
        signalInfo.value = signal.value;
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalInfos.push_back(signalInfo);
    }

    VkCommandBufferSubmitInfo commandBufferInfo{};
    commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandBufferInfo.commandBuffer = commandBuffer->getCommandBufferVk();

    std::lock_guard<std::mutex> queueLock(m_queueMutex);
    std::lock_guard<std::mutex> batchLock(m_cmdBatchMutex);

    uint64_t signalValue = m_nextTimelineValue++;

    VkSemaphoreSubmitInfo timelineSignal{};
    timelineSignal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    timelineSignal.semaphore = m_timelineSemaphore;
    timelineSignal.value = signalValue;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signalInfos.push_back(timelineSignal);

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
    submit.pWaitSemaphoreInfos = waitInfos.data();
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandBufferInfo;
    submit.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
    submit.pSignalSemaphoreInfos = signalInfos.data();

    if (!submitBatchThen(&submit, synchronization.fence)) {
        return 0;
    }

    m_lastSubmittedValue.store(signalValue, std::memory_order_release);

    s_markCommandBufferSubmitted(commandBuffer, m_timelineSemaphore, signalValue);

    return signalValue;
}

void VulkanQueue::waitForTimelineValue(uint64_t value) const
{
    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_timelineSemaphore;
    waitInfo.pValues = &value;

    vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX);
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

} // namespace Rapture
