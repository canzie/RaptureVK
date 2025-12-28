#ifndef RAPTURE__JOBQUEUE_H
#define RAPTURE__JOBQUEUE_H

#include "Job.h"
#include "JobCommon.h"

#include <array>
#include <cstddef>

namespace Rapture {

class JobQueue {
  public:
    static constexpr size_t CAPACITY = 4096;

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
    // Use moodycamel::ConcurrentQueue or custom lock-free implementation
    // https://github.com/KjellKod/Moody-Camel-s-concurrentqueue
};

class PriorityQueueSet {
  public:
    void push(Job &&j); // Routes based on job.decl.priority
    /**
     * @brief Pop a job from the queue based on priority
     * @param out The job to pop
     * @return True if the job was popped, false if the queue is empty
     */
    bool pop(Job &out);

    bool isEmpty() const;

  private:
    JobQueue m_high;
    JobQueue m_normal;
    JobQueue m_low;
};

class AffinityQueueSet {
    std::array<PriorityQueueSet, AFFINITY_COUNT> m_queues; // Indexed by QueueAffinity

  public:
    void push(Job &&j); // Routes based on job.decl.affinity
    bool pop(Job &out, QueueAffinity preferredAffinity);
};

} // namespace Rapture

#endif // RAPTURE__JOBQUEUE_H