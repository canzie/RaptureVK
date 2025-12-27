# Job System Design Document

## Overview

A fiber-based job system for RaptureVK inspired by Naughty Dog's GDC presentations. The system targets operations in the **50+ microsecond** range - coarse enough to amortize scheduling overhead while enabling parallelism for render passes, physics, asset processing, etc.

## Goals

1. **Static thread pool** - No dynamic thread creation. Prevents OS thread thrashing.
2. **Priority queues** - High, Normal, Low priority for scheduling control.
3. **Atomic counter dependencies** - Naughty Dog-style synchronization without heavy mutexes.
4. **Fiber-based yielding** - Jobs can yield mid-execution with stack preserved.
5. **Queue affinity** - Route jobs to Graphics, Compute, Transfer, or Any queue.
6. **Parallel command buffer recording** - Record GBuffer, Shadow, Lighting passes concurrently.

## Architecture

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                              Job System                                       │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                               │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │                         Priority Queues (per affinity)                   │ │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐          │ │
│  │  │  Graphics Queue │  │  Compute Queue  │  │ Transfer Queue  │          │ │
│  │  │  [H] [N] [L]    │  │  [H] [N] [L]    │  │  [H] [N] [L]    │          │ │
│  │  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘          │ │
│  └───────────┼────────────────────┼────────────────────┼────────────────────┘ │
│              │                    │                    │                      │
│              └────────────────────┼────────────────────┘                      │
│                                   ▼                                           │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │                         Worker Threads                                   │ │
│  │   [W0] [W1] [W2] [W3] ... [Wn-1]  (n = CPU cores - 2)                   │ │
│  │   Each worker has a scheduler fiber + can run job fibers                 │ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
│                                                                               │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │                          Fiber Pool                                      │ │
│  │   [F0] [F1] [F2] ... [F255]  (pre-allocated, 64KB stack each)           │ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
│                                                                               │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │                    Partitioned Wait List                                 │ │
│  │   Counter* ──► [Job, Job, Job]    (O(1) lookup by counter)              │ │
│  │   Counter* ──► [Job]                                                     │ │
│  │   Counter* ──► [Job, Job]                                                │ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
│                                                                               │
│  ┌────────────┐                                                              │
│  │  IO Thread │ ──► Dispatches file read completions as jobs                │
│  └────────────┘                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

## Thread Configuration

| Thread Type | Count | Purpose |
|-------------|-------|---------|
| Main Thread | 1 | Game loop, job submission, Vulkan queue submission |
| IO Thread | 1 | File I/O operations, dispatches processing jobs |
| Worker Threads | `cores - 2` | Execute jobs from priority queues |

**Why `cores - 2`?** Main thread + IO thread occupy 2 cores. Workers fill the rest. On a 16-core CPU: 14 workers. On 4-core: 2 workers (minimum viable).

```cpp
uint32_t workerCount = std::max(2u, std::thread::hardware_concurrency() - 2);
```

## Core Types

### Counter

The fundamental synchronization primitive. Jobs wait for counters to reach specific values.

```cpp
struct Counter {
    std::atomic<int32_t> value{0};

    void increment(int32_t amount = 1);
    void decrement(int32_t amount = 1);
    int32_t get() const;

    // Called internally when value changes - notifies wait list
    void notify(JobSystem* system);
};
```

**Lifetime rules:**
- Frame-scoped counters: Use `FrameCounterPool`, automatically reset each frame
- Persistent counters: Must outlive all jobs that reference them
- Always call `waitFor()` before letting a counter go out of scope

### Queue Affinity

Route jobs to appropriate Vulkan queues for optimal scheduling.

```cpp
enum class QueueAffinity : uint8_t {
    Any = 0,       // Can run on any worker (default)
    Graphics,      // Prefer workers with graphics queue access
    Compute,       // Prefer workers with compute queue access
    Transfer       // Prefer workers with transfer queue access (async uploads)
};
```

**Usage:**
- `Any`: General CPU work, most jobs
- `Graphics`: Command buffer recording, pipeline operations
- `Compute`: Dispatch compute shaders, GPU readbacks
- `Transfer`: Buffer/image uploads, async asset streaming

### Job Priority

```cpp
enum class JobPriority : uint8_t {
    Low = 0,       // Background work, can be starved
    Normal = 1,    // Default priority
    High = 2       // Latency-sensitive (frame-critical rendering)
};
```

### Job Declaration

```cpp
// Efficient callable storage - avoids std::function heap allocation
template<typename Sig, size_t Size = 48>
class InplaceFunction;  // Implementation omitted

using JobFunction = InplaceFunction<void(JobContext&), 48>;

struct JobDeclaration {
    JobFunction         function;
    JobPriority         priority = JobPriority::Normal;
    QueueAffinity       affinity = QueueAffinity::Any;
    Counter*            signalOnComplete = nullptr;
    const char*         debugName = nullptr;
};
```

### Job

```cpp
struct Job {
    JobDeclaration      decl;

    // Dependency: wait until this counter reaches targetValue before starting
    Counter*            waitCounter = nullptr;
    int32_t             waitTarget = 0;

    Fiber*              fiber = nullptr;
};
```

### Fiber

```cpp
struct Fiber {
    void*               stackBase;        // Allocated stack memory
    void*               stackPointer;     // Current stack position
    FiberContext        context;          // Platform-specific context (registers, etc.)

    Job*                currentJob;       // Job currently executing on this fiber
    Counter*            waitingOn;        // Counter this fiber is waiting on (if yielded)
    int32_t             waitTarget;       // Target value to resume at

    bool                finished;         // Job completed, fiber can be recycled

    void switchTo();                      // Context switch TO this fiber
    void switchToScheduler();             // Context switch back to worker's scheduler
};
```

### Fiber Pool

Pre-allocated fiber pool to avoid runtime allocation.

```cpp
class FiberPool {
public:
    static constexpr size_t FIBER_STACK_SIZE = 64 * 1024;  // 64KB per fiber
    static constexpr size_t MAX_FIBERS = 256;              // ~16MB total

    Fiber* acquire();                     // Get a free fiber (blocks if none available)
    bool tryAcquire(Fiber** out);         // Non-blocking acquire
    void release(Fiber* fiber);           // Return fiber to pool

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
```

### Job Context

Passed to every job function. Provides yielding and spawning capabilities.

```cpp
struct JobContext {
    JobSystem*          system;
    Job*                currentJob;
    Fiber*              currentFiber;     // nullptr for Task jobs

    // Yield this fiber until counter reaches value
    // ONLY valid for Fiber jobs - asserts on Task jobs
    void waitFor(Counter& c, int32_t targetValue);

    // Spawn child jobs
    void run(const JobDeclaration& decl);
    void run(const JobDeclaration& decl, Counter& waitCounter, int32_t waitTarget);

    // Batch spawn with automatic counter setup
    Counter* runBatch(std::span<JobDeclaration> jobs);

    // Check if running as fiber (can yield)
    bool canYield() const { return currentFiber != nullptr; }
};
```

## Priority Queue Implementation

Lock-free MPMC (multi-producer multi-consumer) queues per affinity.

```cpp
class JobQueue {
public:
    static constexpr size_t CAPACITY = 4096;

    bool push(Job&& j);           // Returns false if full
    bool pop(Job& out);           // Returns false if empty
    size_t size() const;

private:
    // Use moodycamel::ConcurrentQueue or custom lock-free implementation
    // https://github.com/KjellKod/Moody-Camel-s-concurrentqueue
};

class PriorityQueueSet {
    JobQueue m_high;
    JobQueue m_normal;
    JobQueue m_low;

public:
    void push(Job&& j);           // Routes based on job.decl.priority
    bool pop(Job& out);           // Checks high → normal → low
    bool empty() const;
};

class AffinityQueueSet {
    std::array<PriorityQueueSet, 4> m_queues;  // Indexed by QueueAffinity

public:
    void push(Job&& j);           // Routes based on job.decl.affinity
    bool pop(Job& out, QueueAffinity preferredAffinity);
};
```

## Partitioned Wait List

Jobs waiting on counters are stored in a map keyed by counter pointer. This gives O(1) lookup when a counter updates.

```cpp
class WaitList {
public:
    void add(Job&& job);
    void add(Fiber* fiber);       // For yielded fibers

    // Called when counter value changes - moves ready jobs/fibers to queues
    // Only scans jobs waiting on THIS counter - O(jobs_per_counter) not O(total_jobs)
    void onCounterChanged(Counter* counter, AffinityQueueSet& queues, FiberPool& fiberPool);

    size_t size() const;

private:
    mutable std::shared_mutex m_mutex;

    // Partitioned by counter - O(1) lookup
    std::unordered_map<Counter*, std::vector<Job>> m_waitingJobs;
    std::unordered_map<Counter*, std::vector<Fiber*>> m_waitingFibers;

    void dispatchReadyJobs(Counter* counter, int32_t currentValue, AffinityQueueSet& queues);
    void dispatchReadyFibers(Counter* counter, int32_t currentValue, AffinityQueueSet& queues);
};
```

**Why partition by counter?**

Without partitioning (old design):
```cpp
void checkAndDispatch() {
    for (auto& job : m_allWaitingJobs) {  // O(n) - scans ALL jobs
        if (job.waitCounter->get() == job.waitTarget) {
            dispatch(job);
        }
    }
}
```

With partitioning (new design):
```cpp
void onCounterChanged(Counter* counter, ...) {
    auto it = m_waitingJobs.find(counter);  // O(1) lookup
    if (it == m_waitingJobs.end()) return;

    for (auto& job : it->second) {          // Only jobs waiting on THIS counter
        if (counter->get() == job.waitTarget) {
            dispatch(job);
        }
    }
}
```

## Worker Thread Loop

```cpp
void workerThreadFunc(JobSystem* system, uint32_t workerId, QueueAffinity preferredAffinity) {
    // Initialize this worker's scheduler fiber (runs on native thread stack)
    thread_local Fiber* t_schedulerFiber = nullptr;
    thread_local Fiber* t_currentFiber = nullptr;

    t_schedulerFiber = createSchedulerFiber();

    while (!system->shouldShutdown()) {
        Job job;
        if (!system->popJob(job, preferredAffinity)) {
            // No work available - spin briefly then yield
            for (int spin = 0; spin < 32; spin++) {
                _mm_pause();  // CPU hint: we're spinning
            }
            std::this_thread::yield();
            continue;
        }

        // Check if job is waiting on a counter
        if (job.waitCounter && job.waitCounter->get() != job.waitTarget) {
            system->addToWaitList(std::move(job));
            continue;
        }


        // ═══════════════════════════════════════════════════════
        // FIBER: Run on dedicated fiber stack
        // ═══════════════════════════════════════════════════════
        Fiber* fiber = job.fiber;

        if (!fiber) {
            // New fiber job - acquire from pool
            fiber = system->m_fiberPool.acquire();
            fiber->currentJob = system->allocateJob(std::move(job));
            fiber->finished = false;
            fiber->waitingOn = nullptr;

            initializeFiber(fiber, &fiberEntryPoint);
        }

        // Switch to fiber - runs until yield or completion
        t_currentFiber = fiber;
        fiber->switchTo();
        t_currentFiber = nullptr;

        if (fiber->finished) {
            // Job completed - signal and release fiber
            if (fiber->currentJob->decl.signalOnComplete) {
                fiber->currentJob->decl.signalOnComplete->decrement();
            }
            system->freeJob(fiber->currentJob);
            system->m_fiberPool.release(fiber);
        }
        else if (fiber->waitingOn) {
            // Fiber yielded - add to wait list
            system->addFiberToWaitList(fiber);
        }
        if (fiber->finished) {
            // Job completed - signal and release fiber
            if (fiber->currentJob->decl.signalOnComplete) {
                fiber->currentJob->decl.signalOnComplete->decrement();
            }
            system->freeJob(fiber->currentJob);
            system->m_fiberPool.release(fiber);
        }
        else if (fiber->waitingOn) {
            // Fiber yielded - add to wait list
            system->addFiberToWaitList(fiber);
        }
        
    }
}

void fiberEntryPoint(Fiber* fiber) {
    JobContext ctx{
        JobSystem::instance(),
        fiber->currentJob,
        fiber
    };

    fiber->currentJob->decl.function(ctx);

    fiber->finished = true;
    fiber->switchToScheduler();  // Return to worker

    // Never reached - fiber is recycled
}
```

## Yielding Implementation

```cpp
void JobContext::waitFor(Counter& c, int32_t targetValue) {
    assert(currentFiber && "waitFor() called on Task job - use Fiber type!");

    // Check if already satisfied
    if (c.get() == targetValue) {
        return;  // No need to yield
    }

    // Record what we're waiting for
    currentFiber->waitingOn = &c;
    currentFiber->waitTarget = targetValue;

    // Switch back to scheduler - worker will add us to wait list
    currentFiber->switchToScheduler();

    // ═══════════════════════════════════════════════════════════════
    // EXECUTION RESUMES HERE when counter reaches target
    // Full stack is preserved - local variables still valid!
    // ═══════════════════════════════════════════════════════════════

    currentFiber->waitingOn = nullptr;
}
```

## Counter Notification

When a counter changes, it notifies the wait list to dispatch ready jobs.

```cpp
void Counter::decrement(int32_t amount) {
    int32_t newValue = value.fetch_sub(amount, std::memory_order_release) - amount;
    notify(JobSystem::instance());
}

void Counter::notify(JobSystem* system) {
    // This triggers O(1) lookup + dispatch of jobs waiting on THIS counter
    system->m_waitList.onCounterChanged(this, system->m_queues, system->m_fiberPool);
}
```

## Frame Counter Pool

Per-frame counter allocation to avoid lifetime issues.

```cpp
class FrameCounterPool {
public:
    static constexpr size_t COUNTERS_PER_FRAME = 256;

    void init(uint32_t framesInFlight);

    Counter* acquire();           // Get a counter for this frame
    void beginFrame();            // Reset current frame's counters
    void endFrame();              // Advance to next frame

private:
    struct FrameCounters {
        std::array<Counter, COUNTERS_PER_FRAME> counters;
        std::atomic<uint32_t> nextIndex{0};
    };

    std::vector<FrameCounters> m_frames;
    uint32_t m_currentFrame = 0;
};
```

**Usage:**
```cpp
void Renderer::renderFrame() {
    auto& js = jobs();

    // Get frame-scoped counter - automatically recycled next frame
    Counter* passesDone = js.frameCounters().acquire();
    passesDone->value = 4;

    // ... dispatch jobs ...

    js.waitFor(*passesDone, 0);
}
```

## Batch Helpers

Reduce boilerplate for common fan-out/fan-in patterns.

```cpp
// In JobContext:
Counter* JobContext::runBatch(std::span<JobDeclaration> jobs) {
    Counter* counter = system->m_frameCounters.acquire();
    counter->value = static_cast<int32_t>(jobs.size());

    for (auto& decl : jobs) {
        decl.signalOnComplete = counter;
        system->run(decl);
    }

    return counter;
}

// Usage:
void dispatchShadowMaps(JobContext& ctx, std::span<ShadowMap*> shadowMaps) {
    std::vector<JobDeclaration> jobs;
    jobs.reserve(shadowMaps.size());

    for (auto* sm : shadowMaps) {
        jobs.push_back({
            .function = [sm](JobContext&) { sm->record(); },
            .priority = JobPriority::High,
            .type = JobType::Task,
            .affinity = QueueAffinity::Graphics,
            .debugName = "ShadowMap"
        });
    }

    Counter* allDone = ctx.runBatch(jobs);
    ctx.waitFor(*allDone, 0);  // Requires Fiber job type
}
```

## IO Thread Design

The IO thread handles blocking file I/O, then dispatches processing to workers.

```cpp
void ioThreadFunc(JobSystem* system) {
    while (!system->shouldShutdown()) {
        IORequest req;
        if (system->m_ioQueue.pop(req)) {
            // Blocking read (fine - dedicated thread)
            auto data = readFileBlocking(req.path);

            // Dispatch processing to worker with Transfer affinity
            system->run({
                .function = [data = std::move(data), cb = std::move(req.callback)]
                           (JobContext& ctx) {
                    cb(data, ctx);
                },
                .priority = req.priority,
                .type = JobType::Fiber,  // Callback might wait for GPU
                .affinity = QueueAffinity::Transfer,
                .debugName = "IOCallback"
            });
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
```

## API Surface

```cpp
namespace Rapture {

class JobSystem {
public:
    // Lifecycle
    static void init();
    static void shutdown();
    static JobSystem& instance();

    // Job submission
    void run(const JobDeclaration& decl);
    void run(const JobDeclaration& decl, Counter& waitCounter, int32_t waitTarget);

    // Batch submission
    void runBatch(std::span<JobDeclaration> jobs);
    Counter* runBatch(std::span<JobDeclaration> jobs, Counter& completionCounter);

    // Blocking wait (main thread sync points)
    // Processes other jobs while waiting to avoid deadlock
    void waitFor(Counter& c, int32_t targetValue);

    // Frame counter pool
    FrameCounterPool& frameCounters() { return m_frameCounters; }

    // IO requests
    void requestFileRead(const std::filesystem::path& path,
                         std::function<void(std::span<uint8_t>, JobContext&)> callback,
                         JobPriority priority = JobPriority::Normal);

    // Frame lifecycle (call from main thread)
    void beginFrame();
    void endFrame();

    // Statistics
    struct Stats {
        uint64_t jobsExecuted;
        uint64_t jobsPending;
        uint64_t fibersInUse;
        uint64_t waitListSize;
    };
    Stats getStats() const;

private:
    std::vector<std::thread> m_workers;
    std::thread m_ioThread;

    AffinityQueueSet m_queues;
    WaitList m_waitList;
    FiberPool m_fiberPool;
    FrameCounterPool m_frameCounters;

    std::atomic<bool> m_shutdown{false};
};

// Convenience global accessor
inline JobSystem& jobs() { return JobSystem::instance(); }

} // namespace Rapture
```

## Platform-Specific Fiber Implementation

### Windows

```cpp
#ifdef _WIN32
#include <Windows.h>

void initializeFibers() {
    // Convert main thread to fiber (required for SwitchToFiber)
    ConvertThreadToFiber(nullptr);
}

Fiber* createFiber(size_t stackSize, void (*entryPoint)(void*)) {
    Fiber* fiber = new Fiber();
    fiber->handle = CreateFiber(stackSize, entryPoint, fiber);
    return fiber;
}

void Fiber::switchTo() {
    SwitchToFiber(this->handle);
}

void destroyFiber(Fiber* fiber) {
    DeleteFiber(fiber->handle);
    delete fiber;
}
#endif
```

### Linux (using boost.context for performance)

```cpp
#ifdef __linux__
#include <boost/context/fiber.hpp>

namespace ctx = boost::context;

struct Fiber {
    ctx::fiber context;
    // ... other members

    void switchTo() {
        context = std::move(context).resume();
    }
};

Fiber* createFiber(size_t stackSize, void (*entryPoint)(Fiber*)) {
    Fiber* fiber = new Fiber();
    fiber->context = ctx::fiber(
        std::allocator_arg,
        ctx::fixedsize_stack(stackSize),
        [fiber, entryPoint](ctx::fiber&& sink) {
            fiber->schedulerContext = std::move(sink);
            entryPoint(fiber);
            return std::move(fiber->schedulerContext);
        }
    );
    return fiber;
}
#endif
```

**Why boost.context?**
- `makecontext`/`swapcontext` are deprecated and slow (~1000 cycles)
- boost.context uses hand-written assembly (~100 cycles)
- Same performance as Windows fibers

## Usage Example: Parallel Render Pass Recording

```cpp
void DeferredRenderer::recordFrame(Scene* scene, uint32_t frameIndex) {
    auto& js = jobs();

    // ═══════════════════════════════════════════════════════════════
    // This is a FIBER job because it waits for child jobs
    // ═══════════════════════════════════════════════════════════════

    // Get frame-scoped counters
    Counter* shadowsDone = js.frameCounters().acquire();
    Counter* passesDone = js.frameCounters().acquire();

    // Count shadow maps
    auto shadowMaps = gatherShadowMaps(scene);
    shadowsDone->value = static_cast<int32_t>(shadowMaps.size());

    // Dispatch shadow map recording (parallel, Task type - no yielding needed)
    for (auto* sm : shadowMaps) {
        js.run({
            .function = [sm, scene, frameIndex](JobContext&) {
                sm->recordSecondary(scene, frameIndex);
            },
            .priority = JobPriority::High,
            .type = JobType::Task,
            .affinity = QueueAffinity::Graphics,
            .signalOnComplete = shadowsDone,
            .debugName = "ShadowMap"
        });
    }

    // Dispatch render pass recording (parallel)
    passesDone->value = 4;

    js.run({
        .function = [this, scene, frameIndex](JobContext&) {
            m_gbufferPass->recordSecondary(scene, frameIndex);
        },
        .priority = JobPriority::High,
        .type = JobType::Task,
        .affinity = QueueAffinity::Graphics,
        .signalOnComplete = passesDone,
        .debugName = "GBuffer"
    });

    js.run({
        .function = [this, scene, frameIndex](JobContext&) {
            m_lightingPass->recordSecondary(scene, frameIndex);
        },
        .priority = JobPriority::High,
        .type = JobType::Task,
        .affinity = QueueAffinity::Graphics,
        .signalOnComplete = passesDone,
        .debugName = "Lighting"
    });

    js.run({
        .function = [this, frameIndex](JobContext&) {
            m_skyboxPass->recordSecondary(frameIndex);
        },
        .priority = JobPriority::High,
        .type = JobType::Task,
        .affinity = QueueAffinity::Graphics,
        .signalOnComplete = passesDone,
        .debugName = "Skybox"
    });

    js.run({
        .function = [this, scene, frameIndex](JobContext&) {
            m_instancedShapesPass->recordSecondary(scene, frameIndex);
        },
        .priority = JobPriority::High,
        .type = JobType::Task,
        .affinity = QueueAffinity::Graphics,
        .signalOnComplete = passesDone,
        .debugName = "InstancedShapes"
    });

    // Wait for all recordings (main thread processes other jobs while waiting)
    js.waitFor(*shadowsDone, 0);
    js.waitFor(*passesDone, 0);

    // Execute in order on primary command buffer (must be sequential)
    executeRecordedPasses();
}
```

## Usage Example: Asset Loading with Fiber

```cpp
void AssetManager::loadMeshAsync(const std::filesystem::path& path,
                                  std::function<void(std::shared_ptr<Mesh>)> onComplete) {
    auto& js = jobs();

    js.run({
        .function = [path, onComplete](JobContext& ctx) {
            // ═══════════════════════════════════════════════════════
            // FIBER JOB: This job waits for GPU upload to complete
            // ═══════════════════════════════════════════════════════

            // 1. Read file (could also use IO thread)
            auto fileData = readFile(path);

            // 2. Parse mesh data (CPU work)
            auto meshData = parseMeshData(fileData);

            // 3. Create GPU buffers and initiate upload
            auto mesh = std::make_shared<Mesh>();
            Counter* uploadDone = ctx.system->frameCounters().acquire();
            uploadDone->value = 2;  // vertex + index buffer

            // Async vertex buffer upload
            ctx.run({
                .function = [mesh, &meshData, uploadDone](JobContext&) {
                    mesh->vertexBuffer = createVertexBuffer(meshData.vertices);
                    // Upload happens on Transfer queue, signals when done
                },
                .affinity = QueueAffinity::Transfer,
                .signalOnComplete = uploadDone
            });

            // Async index buffer upload
            ctx.run({
                .function = [mesh, &meshData, uploadDone](JobContext&) {
                    mesh->indexBuffer = createIndexBuffer(meshData.indices);
                },
                .affinity = QueueAffinity::Transfer,
                .signalOnComplete = uploadDone
            });

            // 4. Wait for uploads (fiber yields here, stack preserved)
            ctx.waitFor(*uploadDone, 0);

            // 5. Finalize and callback
            mesh->bounds = calculateBounds(meshData);
            onComplete(mesh);
        },
        .priority = JobPriority::Normal,
        .type = JobType::Fiber,  // MUST be Fiber - we call waitFor()
        .debugName = "LoadMesh"
    });
}
```

## Memory Budget

```
Component               | Size          | Notes
------------------------|---------------|----------------------------------
Fiber stacks            | 16 MB         | 256 fibers × 64KB each
Job queues              | ~1 MB         | 4096 jobs × 3 priorities × 4 affinities
Wait list               | ~100 KB       | Depends on concurrent waits
Frame counters          | ~24 KB        | 256 counters × 3 frames × 32 bytes
------------------------|---------------|----------------------------------
Total                   | ~18 MB        | Negligible vs GPU memory
```

## Implementation Phases

### Phase 1: Core System (No Fibers Yet)
- [ ] `Counter` with atomic operations and notification
- [ ] `JobDeclaration` and `Job` types
- [ ] `AffinityQueueSet` with priority queues (use moodycamel::ConcurrentQueue)
- [ ] Worker thread pool with affinity hints
- [ ] Basic `run()` and blocking `waitFor()`
- [ ] `FrameCounterPool`

### Phase 2: Partitioned Wait List
- [ ] `WaitList` with counter-keyed partitioning
- [ ] Counter notification triggers wait list check
- [ ] Dependency waiting in jobs (pre-execution check)

### Phase 3: Fiber Support
- [ ] `FiberPool` with pre-allocated stacks
- [ ] Platform fiber implementation (Windows + Linux/boost.context)
- [ ] `JobType::Fiber` execution path
- [ ] `JobContext::waitFor()` yielding

### Phase 4: IO Integration
- [ ] IO thread with request queue
- [ ] File read API
- [ ] Integration with AssetManager

### Phase 5: Renderer Integration
- [ ] Per-thread command pools (already have `threadId` in config)
- [ ] Parallel secondary buffer recording
- [ ] Shadow map parallel dispatch
- [ ] Validate thread safety of render passes

### Phase 6: Transfer Queue Uploads
- [ ] Dedicated transfer queue detection
- [ ] Deferred staging buffer destruction (timeline semaphore tracking)
- [ ] Async buffer/texture uploads
- [ ] Integration with BufferPool

### Phase 7: Polish
- [ ] Tracy profiling integration
- [ ] Statistics and debugging tools
- [ ] Stress testing and tuning
- [ ] Documentation

## File Structure

```
Engine/src/jobs/
├── JOB_SYSTEM_DESIGN.md        (this document)
├── JobSystem.h                  (public API)
├── JobSystem.cpp                (main implementation)
├── JobTypes.h                   (Counter, JobDeclaration, JobContext, etc.)
├── JobQueue.h                   (lock-free priority queues)
├── JobQueue.cpp
├── WaitList.h                   (partitioned wait list)
├── WaitList.cpp
├── FiberPool.h                  (fiber management)
├── FiberPool.cpp
├── Fiber_Win32.cpp              (Windows fiber implementation)
├── Fiber_Linux.cpp              (Linux/boost.context implementation)
├── FrameCounterPool.h           (per-frame counter allocation)
└── FrameCounterPool.cpp
```

## References

- [Parallelizing the Naughty Dog Engine Using Fibers](https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine) - GDC 2015
- [Destiny's Multithreaded Rendering Architecture](https://www.gdcvault.com/play/1021926/Destiny-s-Multithreaded-Rendering) - GDC 2015
- [moodycamel::ConcurrentQueue](https://github.com/cameron314/concurrentqueue) - Lock-free MPMC queue
- [boost.context](https://www.boost.org/doc/libs/release/libs/context/doc/html/index.html) - Fast context switching
