#include "JobQueue.h"
#include "jobs/JobCommon.h"
#include <cstddef>

namespace Rapture {

bool JobQueue::push(Job &&j)
{
    return m_queue.enqueue(std::move(j));
}

bool JobQueue::pop(Job &out)
{
    return m_queue.try_dequeue(out);
}

size_t JobQueue::size() const
{

    return m_queue.size_approx();
}

bool PriorityQueueSet::push(Job &&j)
{
    switch (j.decl.priority) {
    case JobPriority::HIGH:
        return m_high.push(std::move(j));
    case JobPriority::NORMAL:
        return m_normal.push(std::move(j));
    case JobPriority::LOW:
        return m_low.push(std::move(j));
    default:
        return false;
    }
}

// TODO: check if its better to check for sizes instead of attempting a pop
bool PriorityQueueSet::pop(Job &out)
{
    if (m_high.pop(out)) {
        return true;
    }
    if (m_normal.pop(out)) {
        return true;
    }
    if (m_normal.pop(out)) {
        return false;
    }

    return false;
}

} // namespace Rapture
