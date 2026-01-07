#include "JobSystem.h"

#include "Counter.h"
#include "Logging/TracyProfiler.h"
#include "Utils/rp_assert.h"
#include "WindowContext/VulkanContext/TimelineSemaphore.h"
#include "jobs/Job.h"
#include "jobs/WaitList.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <emmintrin.h>
#include <fstream>
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

    if (m_ioThread.joinable()) {
        m_ioThread.join();
    }

    if (m_gpuPollThread.joinable()) {
        m_gpuPollThread.join();
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

void workerThread(JobSystem *system, int32_t threadId)
{
    RAPTURE_PROFILE_THREAD(("Job Worker " + std::to_string(threadId)).c_str());

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

void ioThread(JobSystem *system, IoQueue *queue)
{
    RAPTURE_PROFILE_THREAD("IO Thread");

    while (!system->shouldShutdown()) {
        IoRequest request;

        if (!queue->pop(request)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        auto data = std::make_shared<std::vector<uint8_t>>();
        bool success = false;
        std::ifstream file(request.path, std::ios::binary);
        if (file) {
            file.seekg(0, std::ios::end);
            auto size = file.tellg();
            file.seekg(0, std::ios::beg);

            data->resize(static_cast<size_t>(size));
            if (!file.read(reinterpret_cast<char *>(data->data()), size)) {
                success = false;
            } else {
                success = true;
            }
        }

        // Move data to heap to avoid large lambda captures in JobFunction
        auto callbackPtr = new IoCallback(std::move(request.callback));

        system->run(JobDeclaration(
            [callbackPtr, data, success](JobContext &) {
                (*callbackPtr)(std::move(*data), success);
                delete callbackPtr;
            },
            request.priority, QueueAffinity::ANY, nullptr, "Io callback"));
    }
}

void gpuPollThread(JobSystem *system, GpuPollQueue *queue)
{
    RAPTURE_PROFILE_THREAD("GPU Poll Thread");

    constexpr uint64_t POLL_TIMEOUT_NS = 1 * 1000 * 1000;

    std::vector<GpuWaitRequest> pending;

    while (!system->shouldShutdown()) {
        GpuWaitRequest req;
        while (queue->pop(req)) {
            pending.push_back(req);
        }

        if (pending.empty()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        auto it = pending.begin();
        while (it != pending.end()) {
            if (it->semaphore->wait(it->waitValue, POLL_TIMEOUT_NS)) {
                it->counter->decrement();
                it = pending.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto &req : pending) {
        req.counter->decrement();
    }
}

JobSystem::JobSystem() : m_waitList(this)
{
    m_fiberPool.initializeFiberStacks();

    uint32_t maxThreads = std::thread::hardware_concurrency();
    uint32_t workerThreadCount = (std::max)(1u, maxThreads - 2u);
    workerThreadCount = 2;
    m_workers.resize(workerThreadCount);
    for (uint32_t i = 0; i < workerThreadCount; ++i) {
        m_workers[i] = std::thread(workerThread, this, static_cast<int32_t>(i));
    }

    m_ioThread = std::thread(ioThread, this, &m_ioQueue);
    m_gpuPollThread = std::thread(gpuPollThread, this, &m_gpuPollQueue);
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

void JobSystem::requestIo(std::filesystem::path path, IoCallback callback, JobPriority priority)
{
    m_ioQueue.push(IoRequest{std::move(path), std::move(callback), priority});
}

void JobSystem::submitGpuWait(const TimelineSemaphore *semaphore, uint64_t waitValue, Counter &counter)
{
    m_gpuPollQueue.push(GpuWaitRequest{semaphore, waitValue, &counter});
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
