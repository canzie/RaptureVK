#ifndef RAPTURE__JOB_H
#define RAPTURE__JOB_H

#include "InplaceFunction.h"
#include "JobCommon.h"

#include <cstdint>
#include <span>

namespace Rapture {

struct Counter;

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

    // Yield this fiber until counter reaches value
    void waitFor(Counter &c, int32_t targetValue);

    // Spawn child jobs
    void run(const JobDeclaration &decl);
    void run(const JobDeclaration &decl, Counter &waitCounter, int32_t waitTarget);

    // Batch spawn with automatic counter setup
    void runBatch(std::span<JobDeclaration> jobs, Counter &counter);
};
} // namespace Rapture

#endif // RAPTURE__JOB_H
