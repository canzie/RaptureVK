#ifndef RAPTURE__JOB_H
#define RAPTURE__JOB_H

#include "InplaceFunction.h"
#include "JobCommon.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Rapture {

struct Counter;
class TimelineSemaphore;

class JobSystem;
struct JobContext;
struct Fiber;

using JobFunction = InplaceFunction<void(JobContext &), 128>;

struct JobDeclaration {
    JobFunction function;
    JobPriority priority = JobPriority::NORMAL;
    QueueAffinity affinity = QueueAffinity::ANY;
    Counter *signalOnComplete = nullptr;
    const char *debugName = nullptr;

    JobDeclaration(const JobFunction &_func, JobPriority _prio, QueueAffinity _affinity, Counter *onComplete = nullptr,
                   const char *name = nullptr)
        : function(_func), priority(_prio), affinity(_affinity), signalOnComplete(onComplete), debugName(name)
    {
    }

    JobDeclaration() = default;
};

struct Job {
    JobDeclaration decl;

    // Dependency: wait until this counter reaches targetValue before starting
    Counter *waitCounter = nullptr;
    int32_t waitTarget = 0;

    Fiber *fiber = nullptr;

    Job(JobDeclaration _decl, Counter *_waitCounter, int32_t _waitTarget, Fiber *_fiber)
        : decl(_decl), waitCounter(_waitCounter), waitTarget(_waitTarget), fiber(_fiber)
    {
    }
    Job() = default;
};

/*
 * @brief Context used for every job.
 *
 *        Passing this context as every jobs function allows the jobs to both yield and spawn other jobs in a lightweight manner
 *
 * */
struct JobContext {
    JobSystem *system;
    Job *currentJob;
    Fiber *currentFiber;

    void waitFor(Counter &c, int32_t targetValue);
    void waitFor(Counter &c, int32_t targetValue, const TimelineSemaphore *semaphore, uint64_t semaphoreTargetValue);

    void run(const JobDeclaration &decl);
    void run(const JobDeclaration &decl, Counter &waitCounter, int32_t waitTarget);

    void runBatch(std::span<JobDeclaration> jobs, Counter &counter);
};

// Io callback - receives loaded data and success flag
// Runs on a worker fiber after IO completes
using IoCallback = InplaceFunction<void(std::vector<uint8_t> &&, bool), 192>;

struct IoRequest {
    std::filesystem::path path;
    IoCallback callback;
    JobPriority priority = JobPriority::NORMAL;
};

struct GpuWaitRequest {
    const TimelineSemaphore *semaphore;
    uint64_t waitValue;
    Counter *counter;  // Decrement when semaphore signals
};

} // namespace Rapture

#endif // RAPTURE__JOB_H
