#include "Fiber.h"
#include "JobSystem.h"
#include "logging/TracyProfiler.h"
#include "utils/rp_assert.h"
#include <cstdlib>
#include <cstring>
#include <thread>

#if !defined(NDEBUG) && defined(__linux__)
#define RAPTURE_FIBER_GUARD_PAGE 1
#include <sys/mman.h>
#include <unistd.h>
#endif // RAPTURE_FIBER_GUARD_PAGE

namespace Rapture {

#ifdef RAPTURE_FIBER_GUARD_PAGE
static size_t s_fiberGuardSize()
{
    static const size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return pageSize;
}
#endif // RAPTURE_FIBER_GUARD_PAGE

// Thread-local scheduler fiber (runs on native thread stack)
thread_local Fiber t_schedulerFiber{};
thread_local Fiber *t_currentFiber = nullptr;

// Forward declarations
extern "C" void fiber_switch(FiberContext *from, FiberContext *to);
extern "C" void fiber_entry_point();
extern "C" void fiberEntryPointImpl();

// x86-64 context switch - saves/restores callee-saved registers
asm(".text\n"
    ".globl fiber_switch\n"
    ".type fiber_switch, @function\n"
    "fiber_switch:\n"
    "    # rdi = from, rsi = to\n"
    "    \n"
    "    # Save current context\n"
    "    movq %rsp, 0(%rdi)\n"
    "    movq %rbx, 8(%rdi)\n"
    "    movq %rbp, 16(%rdi)\n"
    "    movq %r12, 24(%rdi)\n"
    "    movq %r13, 32(%rdi)\n"
    "    movq %r14, 40(%rdi)\n"
    "    movq %r15, 48(%rdi)\n"
    "    movq (%rsp), %rax     # Get return address\n"
    "    movq %rax, 56(%rdi)\n"
    "    \n"
    "    # Load new context\n"
    "    movq 0(%rsi), %rsp\n"
    "    movq 8(%rsi), %rbx\n"
    "    movq 16(%rsi), %rbp\n"
    "    movq 24(%rsi), %r12\n"
    "    movq 32(%rsi), %r13\n"
    "    movq 40(%rsi), %r14\n"
    "    movq 48(%rsi), %r15\n"
    "    movq 56(%rsi), %rax   # Jump to saved rip\n"
    "    jmp *%rax\n"
    ".size fiber_switch, .-fiber_switch\n");

// Entry point for new fibers
asm(".text\n"
    ".globl fiber_entry_point\n"
    ".type fiber_entry_point, @function\n"
    "fiber_entry_point:\n"
    "    call fiberEntryPointImpl\n"
    "    ud2\n"
    ".size fiber_entry_point, .-fiber_entry_point\n");

extern "C" void fiberEntryPointImpl()
{
    Fiber *fiber = t_currentFiber;

    RAPTURE_PROFILE_FIBER_ENTER("Job Fiber");

    JobContext ctx{&JobSystem::instance(), &fiber->currentJob, fiber};

    fiber->currentJob.decl.function(ctx);

    fiber->finished = true;
    fiber->switchToScheduler();

    RP_UNREACHABLE();
}

void Fiber::switchTo()
{
    t_currentFiber = this;
    fiber_switch(&t_schedulerFiber.context, &this->context);
    t_currentFiber = nullptr;
}

void Fiber::switchToScheduler()
{
    RAPTURE_PROFILE_FIBER_LEAVE;
    fiber_switch(&this->context, &t_schedulerFiber.context);
    RAPTURE_PROFILE_FIBER_ENTER("Job Fiber");
}

void initializeFiber(Fiber *fiber)
{
    void *stackTop = static_cast<char *>(fiber->stackBase) + FiberPool::FIBER_STACK_SIZE;

    uintptr_t stackAddr = reinterpret_cast<uintptr_t>(stackTop);
    stackAddr &= ~0xFull;
    // NOTE: Do NOT subtract 8 here. The x86-64 ABI requires RSP to be 16-byte
    // aligned BEFORE a CALL instruction. Since fiber_entry_point does
    // "call fiberEntryPointImpl", RSP must be 16-aligned when we enter.

    fiber->stackPointer = reinterpret_cast<void *>(stackAddr);

    memset(&fiber->context, 0, sizeof(FiberContext));
    fiber->context.rsp = fiber->stackPointer;
    fiber->context.rip = reinterpret_cast<void *>(&fiber_entry_point);

    fiber->finished = false;
    fiber->waitingOn = nullptr;
    fiber->waitTarget = 0;
}

Fiber *createSchedulerFiber()
{
    memset(&t_schedulerFiber.context, 0, sizeof(FiberContext));
    return &t_schedulerFiber;
}

FiberPool::~FiberPool()
{
    for (size_t i = 0; i < MAX_FIBERS; ++i) {
        if (m_fibers[i].fiber.stackBase != nullptr) {
#ifdef RAPTURE_FIBER_GUARD_PAGE
            size_t guardSize = s_fiberGuardSize();
            munmap(static_cast<char *>(m_fibers[i].fiber.stackBase) - guardSize, FIBER_STACK_SIZE + guardSize);
#else
            std::free(m_fibers[i].fiber.stackBase);
#endif // RAPTURE_FIBER_GUARD_PAGE
        }
    }
}

Fiber *FiberPool::acquire()
{
    while (true) {
        for (size_t i = 0; i < MAX_FIBERS; ++i) {
            bool expected = false;
            if (m_fibers[i].inUse.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
                m_availableCount.fetch_sub(1, std::memory_order_relaxed);
                return &m_fibers[i].fiber;
            }
        }
        std::this_thread::yield();
    }
}

bool FiberPool::tryAcquire(Fiber **out)
{
    for (size_t i = 0; i < MAX_FIBERS; ++i) {
        bool expected = false;
        if (m_fibers[i].inUse.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
            m_availableCount.fetch_sub(1, std::memory_order_relaxed);
            *out = &m_fibers[i].fiber;
            return true;
        }
    }
    return false;
}

void FiberPool::release(Fiber *fiber)
{
    for (size_t i = 0; i < MAX_FIBERS; ++i) {
        if (&m_fibers[i].fiber == fiber) {
            m_fibers[i].inUse.store(false, std::memory_order_release);
            m_availableCount.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
}

size_t FiberPool::availableCount() const
{
    return m_availableCount.load(std::memory_order_relaxed);
}

void FiberPool::initializeFiberStacks()
{
    for (size_t i = 0; i < MAX_FIBERS; ++i) {
        Fiber &fiber = m_fibers[i].fiber;

#ifdef RAPTURE_FIBER_GUARD_PAGE
        size_t guardSize = s_fiberGuardSize();
        void *mapping =
            mmap(nullptr, FIBER_STACK_SIZE + guardSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        RP_ASSERT(mapping != MAP_FAILED, "Failed to mmap fiber stack");
        int guardResult = mprotect(mapping, guardSize, PROT_NONE);
        RP_ASSERT(guardResult == 0, "Failed to protect fiber guard page");
        fiber.stackBase = static_cast<char *>(mapping) + guardSize;
#else
        fiber.stackBase = std::aligned_alloc(16, FIBER_STACK_SIZE);
        if (!fiber.stackBase) {
            std::abort();
        }
#endif // RAPTURE_FIBER_GUARD_PAGE

        initializeFiber(&fiber);
        m_fibers[i].inUse.store(false, std::memory_order_relaxed);
    }
}

} // namespace Rapture
