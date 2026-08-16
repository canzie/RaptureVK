#ifndef RAPTURE__WAITLIST_H
#define RAPTURE__WAITLIST_H

#include "Job.h"
#include "LockFreeBucketMap.h"

#include <cstddef>
#include <cstdint>

namespace Rapture {

class JobSystem;
struct Counter;

struct WaitKey {
    Counter *counter;
    int32_t targetValue;

    bool operator==(const WaitKey &other) const { return counter == other.counter && targetValue == other.targetValue; }
};

struct WaitKeyHash {
    size_t operator()(const WaitKey &key) const
    {
        size_t h1 = std::hash<void *>{}(key.counter);
        size_t h2 = std::hash<int32_t>{}(key.targetValue);
        return h1 ^ (h2 << 1);
    }
};

class WaitList {
  public:
    static constexpr size_t BUCKET_COUNT = 1024;

    WaitList(JobSystem *system);

    /**
     * @brief Add a job to the wait list
     * @param job The job to add (uses job.waitCounter and job.waitTarget)
     */
    void add(Job &&job);

    /**
     * @brief Add a job to the wait list with explicit counter/target
     * @param job The job to add
     * @param counter The counter to wait on
     * @param targetValue The value to wait for
     */
    void add(Job &&job, Counter *counter, int32_t targetValue);

    /**
     * @brief Called when counter value changes - moves ready jobs/fibers to queues
     * @param counter The counter that changed
     */
    void onCounterChanged(Counter *counter);

    size_t size() const;

  private:
    JobSystem *m_system;
    LockFreeBucketMap<WaitKey, Job, BUCKET_COUNT, WaitKeyHash> m_map;
    std::atomic<size_t> m_size{0};
};

} // namespace Rapture

#endif // RAPTURE__WAITLIST_H
