#ifndef RAPTURE__TIMELINE_SEMAPHORE_H
#define RAPTURE__TIMELINE_SEMAPHORE_H

#include <cstdint>
#include <span>
#include <vulkan/vulkan.h>

namespace Rapture {

class TimelineSemaphore {
  public:
    TimelineSemaphore();
    ~TimelineSemaphore();

    TimelineSemaphore(const TimelineSemaphore &) = delete;
    TimelineSemaphore &operator=(const TimelineSemaphore &) = delete;
    TimelineSemaphore(TimelineSemaphore &&other) noexcept;
    TimelineSemaphore &operator=(TimelineSemaphore &&other) noexcept;

    VkSemaphore handle() const { return m_semaphore; }
    uint64_t getValue() const;
    void signal(uint64_t value);

    // Returns true if signaled, false if timeout
    bool wait(uint64_t value, uint64_t timeoutNs = UINT64_MAX) const;

    // Wait for multiple semaphores - returns true if all signaled
    static bool waitAll(std::span<const TimelineSemaphore *> semaphores,
                        std::span<const uint64_t> values,
                        uint64_t timeoutNs = UINT64_MAX);

    // Wait for any semaphore - returns true if any signaled
    static bool waitAny(std::span<const TimelineSemaphore *> semaphores,
                        std::span<const uint64_t> values,
                        uint64_t timeoutNs = UINT64_MAX);

  private:
    VkSemaphore m_semaphore = VK_NULL_HANDLE;
};

} // namespace Rapture

#endif // RAPTURE__TIMELINE_SEMAPHORE_H
