#ifndef RAPTURE__FIBER_H
#define RAPTURE__FIBER_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Rapture {

struct Job;
struct Counter;

struct FiberContext {
    // Platform-specific context (registers, etc.)
    // manual asm implementation for linux, windows we can use the win32 api
};

struct Fiber {
    void *stackBase;      // Allocated stack memory
    void *stackPointer;   // Current stack position
    FiberContext context; // Platform-specific context (registers, etc.)

    Job *currentJob;    // Job currently executing on this fiber
    Counter *waitingOn; // Counter this fiber is waiting on (if yielded)
    int32_t waitTarget; // Target value to resume at

    bool finished; // Job completed, fiber can be recycled

    void switchTo();          // Context switch TO this fiber
    void switchToScheduler(); // Context switch back to worker's scheduler
};
/*
void fiberEntryPoint(Fiber *fiber)
{
    JobContext ctx{&JobSystem::instance(), fiber->currentJob, fiber};

    fiber->currentJob->decl.function(ctx);

    fiber->finished = true;
    fiber->switchToScheduler();
}
*/

class FiberPool {
  public:
    static constexpr size_t FIBER_STACK_SIZE = 64 * 1024;
    static constexpr size_t FIBER_STACK_SIZE_LARGE = 512 * 1024;
    static constexpr size_t MAX_FIBERS = 128;
    static constexpr size_t MAX_LARGE_FIBERS = 32;

    Fiber *acquire();             // Get a free fiber (blocks if none available)
    bool tryAcquire(Fiber **out); // Non-blocking acquire
    void release(Fiber *fiber);   // Return fiber to pool

    size_t availableCount() const;

  private:
    struct FiberSlot {
        Fiber fiber;
        std::atomic<bool> inUse{false};
    };

    std::array<FiberSlot, MAX_FIBERS> m_fibers;
    std::atomic<uint32_t> m_availableCount{MAX_FIBERS};

    void initializeFiberStacks();
};

} // namespace Rapture

#endif // RAPTURE__FIBER_H
