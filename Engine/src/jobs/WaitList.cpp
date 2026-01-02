#include "WaitList.h"
#include "Counter.h"
#include "JobSystem.h"

namespace Rapture {

WaitList::WaitList(JobSystem *system) : m_system(system) {}

void WaitList::add(Job &&job)
{
    add(std::move(job), job.waitCounter, job.waitTarget);
}

void WaitList::add(Job &&job, Counter *counter, int32_t targetValue)
{
    if (counter->get() <= targetValue) {
        if (job.fiber) {
            m_system->getQueue().pushResume(std::move(job));
        } else {
            m_system->getQueue().push(std::move(job));
        }
        return;
    }

    WaitKey key{counter, targetValue};
    m_map.add(key, std::move(job));
    m_size.fetch_add(1, std::memory_order_relaxed);

    // Handle race: counter may have reached target while we were adding
    if (counter->get() == targetValue) {
        onCounterChanged(counter);
    }
}

void WaitList::onCounterChanged(Counter *counter)
{
    int32_t currentValue = counter->get();
    WaitKey key{counter, currentValue};

    auto predicate = [](const WaitKey &k, const Job &job) {
        return job.waitCounter == k.counter && job.waitTarget == k.targetValue;
    };

    std::vector<Job> readyJobs = m_map.stealMatching(key, predicate);

    if (readyJobs.empty()) {
        return;
    }

    m_size.fetch_sub(readyJobs.size(), std::memory_order_relaxed);

    for (Job &job : readyJobs) {
        if (job.fiber) {
            m_system->getQueue().pushResume(std::move(job));
        } else {
            m_system->getQueue().push(std::move(job));
        }
    }
}

size_t WaitList::size() const
{
    return m_size.load(std::memory_order_relaxed);
}

} // namespace Rapture
