#include "JobSystem.h"

#include "Counter.h"
#include "Utils/rp_assert.h"
#include "jobs/Job.h"
#include "jobs/WaitList.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <emmintrin.h>
#include <memory>
#include <thread>

namespace Rapture {

static std::unique_ptr<JobSystem> s_instance = nullptr;

void JobSystem::init()
{
    if (s_instance != nullptr) {
        return;
    }

    s_instance = std::unique_ptr<JobSystem>(new JobSystem());
}

void JobSystem::close()
{
    m_shutdown.store(true, std::memory_order_release);

    for (auto &worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void JobSystem::shutdown()
{
    s_instance->close();
}

JobSystem &JobSystem::instance()
{
    RP_ASSERT(s_instance != nullptr,
              "Job System has not been initialised yet!, please call 'JobSystem::init()' before trying to obtain the instance");
    return *s_instance;
}

void workerThread(JobSystem *system)
{
    thread_local Fiber *t_schedulerFiber = createSchedulerFiber();
    (void)t_schedulerFiber;

    while (!system->shouldShutdown()) {
        Job job;

        if (!system->getQueue().pop(job)) {
            for (auto spin = 0; spin < 32; spin++) {
                _mm_pause();
            }
            std::this_thread::yield();
            continue;
        }

        if (job.waitCounter && job.waitCounter->get() != job.waitTarget) {
            system->getWaitList().add(std::move(job));
            continue;
        }

        Fiber *fiber = job.fiber;

        if (fiber == nullptr) {
            fiber = system->getFiberPool().acquire();
            job.fiber = fiber;
            initializeFiber(fiber);
        }

        // after returning from the waitlist, we need to set the current job back
        // so this cannot be set in the if above
        fiber->currentJob = std::move(job);
        fiber->switchTo();

        if (fiber->finished) {
            if (fiber->currentJob.decl.signalOnComplete) {
                fiber->currentJob.decl.signalOnComplete->decrement();
            }
            system->getFiberPool().release(fiber);
        } else if (fiber->waitingOn != nullptr) {
            system->getWaitList().add(std::move(fiber->currentJob), fiber->waitingOn, fiber->waitTarget);
            continue;
        }
    }
}

JobSystem::JobSystem() : m_waitList(this)
{
    m_fiberPool.initializeFiberStacks();

    uint32_t maxThreads = std::thread::hardware_concurrency();
    uint32_t workerThreadCount = (std::max)(1u, maxThreads - 2u);
    m_workers.resize(workerThreadCount);
    for (uint32_t i = 0; i < workerThreadCount; ++i) {
        m_workers[i] = std::thread(workerThread, this);
    }
}

void JobSystem::run(const JobDeclaration &decl)
{
    Job job(decl, nullptr, 0, nullptr);
    m_queues.push(std::move(job));
}

void JobSystem::run(const JobDeclaration &decl, Counter &waitCounter, int32_t waitTarget)
{
    Job job(decl, &waitCounter, waitTarget, nullptr);

    if (waitCounter.get() <= waitTarget) {
        m_queues.push(std::move(job));
    } else {
        m_waitList.add(std::move(job));
    }
}

bool JobSystem::shouldShutdown()
{
    return m_shutdown.load(std::memory_order_acquire);
}

// TODO: Consider having main thread do small jobs while waiting, or yield
void JobSystem::waitFor(Counter &c, int32_t targetValue)
{
    while (c.get() != targetValue) {
        _mm_pause();
    }
}

void JobSystem::beginFrame() {}

void JobSystem::endFrame() {}

JobSystem::Stats JobSystem::getStats() const
{
    return Stats{.jobsExecuted = 0,
                 .jobsPending = 0,
                 .fibersInUse = FiberPool::MAX_FIBERS - m_fiberPool.availableCount(),
                 .waitListSize = m_waitList.size()};
}

} // namespace Rapture
