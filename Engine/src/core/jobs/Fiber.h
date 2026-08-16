#ifndef RAPTURE__FIBER_H
#define RAPTURE__FIBER_H

#include "Job.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Rapture {

struct Job;
struct Counter;

// x86-64 ABI callee-saved registers + stack/instruction pointer
struct FiberContext {
    void *rsp; // Stack pointer
    void *rbx;
    void *rbp;
    void *r12;
    void *r13;
    void *r14;
    void *r15;
    void *rip; // Instruction pointer (return address)
};

struct Fiber {
    void *stackBase;      // Allocated stack memory
    void *stackPointer;   // Current stack position
    FiberContext context; // Platform-specific context (registers, etc.)

    Job currentJob;     // Job currently executing on this fiber
    Counter *waitingOn; // Counter this fiber is waiting on (if yielded)
    int32_t waitTarget; // Target value to resume at

    bool finished; // Job completed, fiber can be recycled

    void switchTo();
    void switchToScheduler();

    Fiber() = default;
    // copying is the LAST thing we want
    Fiber(Fiber const &) = delete;
    void operator=(Fiber const &other) = delete;
};

void initializeFiber(Fiber *fiber);
Fiber *createSchedulerFiber();

class FiberPool {
  public:
    static constexpr size_t FIBER_STACK_SIZE = 64 * 1024;
    static constexpr size_t FIBER_STACK_SIZE_LARGE = 512 * 1024;
    static constexpr size_t MAX_FIBERS = 128;
    static constexpr size_t MAX_LARGE_FIBERS = 32;

    FiberPool() = default;
    ~FiberPool();

    FiberPool(const FiberPool &) = delete;
    FiberPool &operator=(const FiberPool &) = delete;
    FiberPool(FiberPool &&) = delete;
    FiberPool &operator=(FiberPool &&) = delete;

    Fiber *acquire();             // Get a free fiber (blocks if none available)
    bool tryAcquire(Fiber **out); // Non-blocking acquire
    void release(Fiber *fiber);   // Return fiber to pool

    size_t availableCount() const;

    void initializeFiberStacks();

  private:
    struct FiberSlot {
        Fiber fiber;
        std::atomic<bool> inUse{false};
    };

    std::array<FiberSlot, MAX_FIBERS> m_fibers;
    std::atomic<uint32_t> m_availableCount{MAX_FIBERS};
};

} // namespace Rapture

#endif // RAPTURE__FIBER_H
