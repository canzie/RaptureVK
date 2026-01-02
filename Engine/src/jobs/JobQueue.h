#ifndef RAPTURE__JOBQUEUE_H
#define RAPTURE__JOBQUEUE_H

// concurrentqueue needs to know jobs size beforehand
#include "Job.h"
#include "JobCommon.h"
#include "concurrentqueue.h"

#include <array>
#include <cstddef>

namespace Rapture {

class JobQueue {
  public:
    static constexpr size_t CAPACITY = 4096;

    JobQueue() : m_queue(CAPACITY) {}

    /**
     * @brief Push a job onto the queue
     * @param j The job to push
     * @return True if the job was pushed, false if the queue is full
     */
    bool push(Job &&j);

    /**
     * @brief Pop a job from the queue
     * @param out The job to pop
     * @return True if the job was popped, false if the queue is empty
     */
    bool pop(Job &out);

    /**
     * @brief Get the size of the queue
     * @return The size of the queue
     */
    size_t size() const;

  private:
    moodycamel::ConcurrentQueue<Job> m_queue;
};

class PriorityQueueSet {
  public:
    /**
     * @brief Push a job onto the correct queue depending on job.decl.priority
     * @note Will NOT attempt to push to a different queue if the declared one is full
     */
    bool push(Job &&j);

    /**
     * @brief Push a resumed job (fiber) onto the resume queue
     * @note Resume queues have higher priority than regular queues
     */
    bool pushResume(Job &&j);

    /**
     * @brief Pop a job from the queue based on priority
     * @param out The job ref to pop to
     * @return True if a job was popped
     * @note Checks resume queues first, then regular queues
     */
    bool pop(Job &out);

    bool isEmpty() const;

  private:
    // Resume queues (checked first - for yielded fibers)
    JobQueue m_resumeHigh;
    JobQueue m_resumeNormal;
    JobQueue m_resumeLow;

    // Regular queues
    JobQueue m_high;
    JobQueue m_normal;
    JobQueue m_low;
};

class AffinityQueueSet {
    std::array<PriorityQueueSet, 5> m_queues; // Indexed by QueueAffinity

  public:
    void push(Job &&j); // Routes based on job.decl.affinity
    bool pop(Job &out, QueueAffinity preferredAffinity);
};

} // namespace Rapture

#endif // RAPTURE__JOBQUEUE_H
