#ifndef RAPTURE__WAITLIST_H
#define RAPTURE__WAITLIST_H

#include "Counter.h"
#include "Fiber.h"
#include "JobQueue.h"

#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace Rapture {

class WaitList {
  public:
    WaitList(JobSystem *system);
    /**
     * @brief Add a job to the wait list
     * @param job The job to add
     */
    void add(Job &&job);

    /**
     * @brief Add a fiber to the wait list
     * @param fiber The fiber to add
     */
    void add(Fiber *fiber);

    /**
     * @brief Called when counter value changes - moves ready jobs/fibers to queues
     * @param counter The counter to check
     * @param queues The queues to dispatch to
     * @param fiberPool The fiber pool to dispatch to
     */
    // Only scans jobs waiting on THIS counter - O(jobs_per_counter) not O(total_jobs)
    void onCounterChanged(Counter *counter);

    size_t size() const;

  private:
    mutable std::shared_mutex m_mutex;

    JobSystem *system;

    // Partitioned by counter - O(1) lookup
    std::unordered_map<Counter *, std::vector<Job>> m_waitingJobs;
    std::unordered_map<Counter *, std::vector<Fiber *>> m_waitingFibers;

    void dispatchReadyJobs(Counter *counter, int32_t currentValue, AffinityQueueSet &queues);
    void dispatchReadyFibers(Counter *counter, int32_t currentValue, AffinityQueueSet &queues);
};

} // namespace Rapture

#endif // RAPTURE__WAITLIST_H
