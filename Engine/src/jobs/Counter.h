#ifndef RAPTURE__COUNTER_H
#define RAPTURE__COUNTER_H

#include <atomic>
#include <cstdint>

namespace Rapture {

class JobSystem;

struct Counter {
    std::atomic<int32_t> value{0};

    void increment(int32_t amount = 1);
    void decrement(int32_t amount = 1);

    /**
     * @brief Get the current value of the counter
     * @return The current value of the counter
     */
    int32_t get() const;

    /**
     * @brief Notify the wait list that the counter has changed
     * @param system The job system to notify
     */
    void notify(JobSystem *system);
};

} // namespace Rapture

#endif // RAPTURE__COUNTER_H
