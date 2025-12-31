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
    m_shutdown.exchange(true);
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
            // fiber->currentJob = ;
            fiber->finished = false;
            fiber->waitingOn = nullptr;

            // initialie fiber
        }
    }
}

JobSystem::JobSystem() : m_waitList(this)
{

    uint32_t maxThreads = std::thread::hardware_concurrency();
    uint32_t workerThreadCount = (std::max)(1u, maxThreads - 2u);
    m_workers.resize(workerThreadCount);
    for (uint32_t i = 0; i < workerThreadCount; ++i) {
        m_workers[i] = std::thread(workerThread, this);
    }
}

void JobSystem::run(const JobDeclaration &decl, Counter &waitCounter, int32_t waitTarget)
{
    Job job(decl, &waitCounter, waitTarget, nullptr);

    m_queues.push(std::move(job));
}

} // namespace Rapture
