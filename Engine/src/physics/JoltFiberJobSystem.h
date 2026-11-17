#ifndef RAPTURE__JOLT_FIBER_JOB_SYSTEM_H
#define RAPTURE__JOLT_FIBER_JOB_SYSTEM_H

#include <Jolt/Jolt.h>

#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

#include <cstdint>

namespace Rapture::physics {

/**
 * @brief Runs Jolt's jobs as jobs of the engine's job system
 */
class JoltFiberJobSystem final : public JPH::JobSystemWithBarrier {
  public:
    /**
     * @brief Creates the job system
     * @param maxJobs How many Jolt jobs can exist at once
     * @param maxBarriers How many barriers can exist at once
     */
    JoltFiberJobSystem(uint32_t maxJobs, uint32_t maxBarriers);

    int GetMaxConcurrency() const override;
    JobHandle CreateJob(const char *inName, JPH::ColorArg inColor, const JobFunction &inJobFunction,
                        JPH::uint32 inNumDependencies = 0) override;

  protected:
    void QueueJob(Job *inJob) override;
    void QueueJobs(Job **inJobs, JPH::uint inNumJobs) override;
    void FreeJob(Job *inJob) override;

  private:
    JPH::FixedSizeFreeList<Job> m_jobs;
};

} // namespace Rapture::physics

#endif // RAPTURE__JOLT_FIBER_JOB_SYSTEM_H
