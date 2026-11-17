#include "physics/JoltFiberJobSystem.h"

#include "core/jobs/JobSystem.h"
#include <rapture/core/utils/rp_assert.h>

namespace Rapture::physics {

JoltFiberJobSystem::JoltFiberJobSystem(uint32_t maxJobs, uint32_t maxBarriers)
{
    JobSystemWithBarrier::Init(maxBarriers);
    m_jobs.Init(maxJobs, maxJobs);
}

int JoltFiberJobSystem::GetMaxConcurrency() const
{
    // the thread waiting on a barrier runs jobs too
    return static_cast<int>(jobs().getWorkerCount()) + 1;
}

JPH::JobHandle JoltFiberJobSystem::CreateJob(const char *inName, JPH::ColorArg inColor, const JobFunction &inJobFunction,
                                             JPH::uint32 inNumDependencies)
{
    JPH::uint32 index = m_jobs.ConstructObject(inName, inColor, this, inJobFunction, inNumDependencies);
    RP_ASSERT(index != decltype(m_jobs)::cInvalidObjectIndex, "out of Jolt jobs");

    Job *job = &m_jobs.Get(index);
    JobHandle handle(job);

    if (inNumDependencies == 0) {
        QueueJob(job);
    }

    return handle;
}

void JoltFiberJobSystem::QueueJob(Job *inJob)
{
    // held until it has run, since the barrier may release its own reference first
    inJob->AddRef();

    Rapture::JobDeclaration declaration(
        [inJob](JobContext &context) {
            (void)context;
            inJob->Execute();
            inJob->Release();
        },
        JobPriority::HIGH, QueueAffinity::ANY, nullptr, "Jolt");
    // collision detection keeps whole contact manifolds on the stack, well past 64 KB
    declaration.stackSize = FiberStackSize::SIZE_512_KB;
    jobs().run(declaration);
}

void JoltFiberJobSystem::QueueJobs(Job **inJobs, JPH::uint inNumJobs)
{
    for (JPH::uint i = 0; i < inNumJobs; ++i) {
        QueueJob(inJobs[i]);
    }
}

void JoltFiberJobSystem::FreeJob(Job *inJob)
{
    m_jobs.DestructObject(inJob);
}

} // namespace Rapture::physics
