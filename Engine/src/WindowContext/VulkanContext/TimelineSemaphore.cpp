#include "TimelineSemaphore.h"

#include "WindowContext/Application.h"

namespace Rapture {

TimelineSemaphore::TimelineSemaphore()
{
    auto &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    VkSemaphoreTypeCreateInfo typeInfo{};
    typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = 0;

    VkSemaphoreCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &typeInfo;

    vkCreateSemaphore(device, &createInfo, nullptr, &m_semaphore);
    m_owning = true;
}

TimelineSemaphore::TimelineSemaphore(VkSemaphore existingSemaphore) : m_semaphore(existingSemaphore), m_owning(false) {}

TimelineSemaphore::~TimelineSemaphore()
{
    if (m_semaphore != VK_NULL_HANDLE && m_owning) {
        auto &app = Application::getInstance();
        VkDevice device = app.getVulkanContext().getLogicalDevice();
        vkDestroySemaphore(device, m_semaphore, nullptr);
    }
}

TimelineSemaphore::TimelineSemaphore(TimelineSemaphore &&other) noexcept
    : m_semaphore(other.m_semaphore), m_owning(other.m_owning)
{
    other.m_semaphore = VK_NULL_HANDLE;
    other.m_owning = false;
}

TimelineSemaphore &TimelineSemaphore::operator=(TimelineSemaphore &&other) noexcept
{
    if (this != &other) {
        if (m_semaphore != VK_NULL_HANDLE && m_owning) {
            auto &app = Application::getInstance();
            VkDevice device = app.getVulkanContext().getLogicalDevice();
            vkDestroySemaphore(device, m_semaphore, nullptr);
        }
        m_semaphore = other.m_semaphore;
        m_owning = other.m_owning;
        other.m_semaphore = VK_NULL_HANDLE;
        other.m_owning = false;
    }
    return *this;
}

uint64_t TimelineSemaphore::getValue() const
{
    auto &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    uint64_t value = 0;
    vkGetSemaphoreCounterValue(device, m_semaphore, &value);
    return value;
}

void TimelineSemaphore::signal(uint64_t value)
{
    auto &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    VkSemaphoreSignalInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    signalInfo.semaphore = m_semaphore;
    signalInfo.value = value;

    vkSignalSemaphore(device, &signalInfo);
}

bool TimelineSemaphore::wait(uint64_t value, uint64_t timeoutNs) const
{
    auto &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_semaphore;
    waitInfo.pValues = &value;

    VkResult result = vkWaitSemaphores(device, &waitInfo, timeoutNs);
    return result == VK_SUCCESS;
}

bool TimelineSemaphore::waitAll(std::span<const TimelineSemaphore *> semaphores, std::span<const uint64_t> values,
                                uint64_t timeoutNs)
{
    if (semaphores.empty() || semaphores.size() != values.size()) {
        return true;
    }

    auto &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    std::vector<VkSemaphore> handles(semaphores.size());
    for (size_t i = 0; i < semaphores.size(); i++) {
        handles[i] = semaphores[i]->handle();
    }

    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = static_cast<uint32_t>(handles.size());
    waitInfo.pSemaphores = handles.data();
    waitInfo.pValues = values.data();

    VkResult result = vkWaitSemaphores(device, &waitInfo, timeoutNs);
    return result == VK_SUCCESS;
}

bool TimelineSemaphore::waitAny(std::span<const TimelineSemaphore *> semaphores, std::span<const uint64_t> values,
                                uint64_t timeoutNs)
{
    if (semaphores.empty() || semaphores.size() != values.size()) {
        return true;
    }

    auto &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    std::vector<VkSemaphore> handles(semaphores.size());
    for (size_t i = 0; i < semaphores.size(); i++) {
        handles[i] = semaphores[i]->handle();
    }

    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.flags = VK_SEMAPHORE_WAIT_ANY_BIT;
    waitInfo.semaphoreCount = static_cast<uint32_t>(handles.size());
    waitInfo.pSemaphores = handles.data();
    waitInfo.pValues = values.data();

    VkResult result = vkWaitSemaphores(device, &waitInfo, timeoutNs);
    return result == VK_SUCCESS;
}

} // namespace Rapture
