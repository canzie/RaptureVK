#include "Job.h"
#include "Counter.h"
#include "JobSystem.h"

namespace Rapture {

void JobContext::waitFor(Counter &c, int32_t targetValue)
{
    if (c.get() == targetValue) {
        return;
    }

    currentFiber->waitingOn = &c;
    currentFiber->waitTarget = targetValue;
    currentFiber->switchToScheduler();
    currentFiber->waitingOn = nullptr;
}

void JobContext::waitFor(Counter &c, int32_t targetValue, const TimelineSemaphore *semaphore, uint64_t semaphoreTargetValue)
{
    system->submitGpuWait(semaphore, semaphoreTargetValue, c);
    waitFor(c, targetValue);
}

void JobContext::run(const JobDeclaration &decl)
{
    system->run(decl);
}

void JobContext::run(const JobDeclaration &decl, Counter &waitCounter, int32_t waitTarget)
{
    system->run(decl, waitCounter, waitTarget);
}

void JobContext::runBatch(std::span<JobDeclaration> jobs, Counter &counter)
{
    counter.value.store(static_cast<int32_t>(jobs.size()), std::memory_order_relaxed);

    for (auto &decl : jobs) {
        decl.signalOnComplete = &counter;
        system->run(decl);
    }
}

} // namespace Rapture
