
#ifndef RAPTURE__JOB_SYSTEM_H
#define RAPTURE__JOB_SYSTEM_H

#include "Counter.h"
#include "Job.h"
#include "WaitList.h"
#include "jobs/Fiber.h"
#include "jobs/JobQueue.h"

#include <atomic>
#include <thread>
#include <vector>

namespace Rapture {

class JobSystem {
  public:
    static void init();
    static void shutdown();
    static JobSystem &instance();

    void run(const JobDeclaration &decl);
    void run(const JobDeclaration &decl, Counter &waitCounter, int32_t waitTarget);

    void runBatch(std::span<JobDeclaration> jobs);
    Counter *runBatch(std::span<JobDeclaration> jobs, Counter &completionCounter);

    // Blocking wait (main thread sync points)
    // Processes other jobs while waiting to avoid deadlock
    void waitFor(Counter &c, int32_t targetValue);

    bool shouldShutdown();

    PriorityQueueSet &getQueue() { return m_queues; }
    WaitList &getWaitList() { return m_waitList; }
    FiberPool &getFiberPool() { return m_fiberPool; }

    // Frame counter pool
    // FrameCounterPool& frameCounters() { return m_frameCounters; }

    // Io request - reads file on dedicated thread, then spawns job with data
    void requestIo(std::filesystem::path path, IoCallback callback,
                   JobPriority priority = JobPriority::NORMAL);

    // GPU poll - submit a semaphore wait request, counter decrements when signaled
    void submitGpuWait(const TimelineSemaphore *semaphore, uint64_t waitValue, Counter &counter);

    // Frame lifecycle (call from main thread)
    void beginFrame();
    void endFrame();

    struct Stats {
        uint64_t jobsExecuted;
        uint64_t jobsPending;
        uint64_t fibersInUse;
        uint64_t waitListSize;
    };
    Stats getStats() const;

  private:
    JobSystem();
    void close();

  private:
    std::vector<std::thread> m_workers;
    std::thread m_ioThread;
    std::thread m_gpuPollThread;

    PriorityQueueSet m_queues;
    WaitList m_waitList;
    FiberPool m_fiberPool;
    IoQueue m_ioQueue;
    GpuPollQueue m_gpuPollQueue;

    std::atomic<bool> m_shutdown{false};
};

inline JobSystem &jobs()
{
    return JobSystem::instance();
}

} // namespace Rapture

#endif // RAPTURE__JOB_SYSTEM_H
