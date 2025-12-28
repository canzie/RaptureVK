#ifndef RAPTURE__JOB_H
#define RAPTURE__JOB_H

#include "Counter.h"
#include "Fiber.h"
#include "JobCommon.h"

#include <span>

namespace Rapture {

template <typename Sig, size_t Size = 48> class InplaceFunction; // Implementation omitted

using JobFunction = InplaceFunction<void(JobContext &), 48>;

struct JobDeclaration {
    JobFunction function;
    JobPriority priority = NORMAL;
    QueueAffinity affinity = ANY;
    Counter *signalOnComplete = nullptr;
    const char *debugName = nullptr;
};

struct Job {
    JobDeclaration decl;

    // Dependency: wait until this counter reaches targetValue before starting
    Counter *waitCounter = nullptr;
    int32_t waitTarget = 0;

    Fiber *fiber = nullptr;
};

struct JobContext {
    JobSystem *system;
    Job *currentJob;
    Fiber *currentFiber; // nullptr for Task jobs

    // Yield this fiber until counter reaches value
    // ONLY valid for Fiber jobs - asserts on Task jobs
    void waitFor(Counter &c, int32_t targetValue);

    // Spawn child jobs
    void run(const JobDeclaration &decl);
    void run(const JobDeclaration &decl, Counter &waitCounter, int32_t waitTarget);

    // Batch spawn with automatic counter setup
    Counter *runBatch(std::span<JobDeclaration> jobs);

    // Check if running as fiber (can yield)
    bool canYield() const { return currentFiber != nullptr; }
};

} // namespace Rapture

#endif // RAPTURE__JOB_H