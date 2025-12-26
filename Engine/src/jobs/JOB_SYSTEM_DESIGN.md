# Job System Design Document

## Overview

A lightweight job system for RaptureVK inspired by Naughty Dog's GDC presentations. The system targets operations in the **100+ microsecond** range - not fiber-level granularity, but coarse enough to amortize scheduling overhead while enabling parallelism for render passes, physics, asset processing, etc.

## Goals

1. **Static thread pool** - No dynamic thread creation. Prevents OS thread thrashing.
2. **Priority queues** - High, Normal, Low priority for scheduling control.
3. **Atomic counter dependencies** - Naughty Dog-style synchronization without heavy mutexes.
4. **Job yielding** - Jobs can yield and resume later (without fiber complexity).
5. **Parallel command buffer recording** - Record GBuffer, Shadow, Lighting passes concurrently.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Job System                               │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────┐  ┌─────────┐  ┌─────────┐                          │
│  │  HIGH   │  │ NORMAL  │  │   LOW   │   Priority Queues        │
│  │  Queue  │  │  Queue  │  │  Queue  │   (lock-free MPMC)       │
│  └────┬────┘  └────┬────┘  └────┬────┘                          │
│       │            │            │                                │
│       └────────────┼────────────┘                                │
│                    ▼                                             │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                    Worker Threads                         │   │
│  │   [W0] [W1] [W2] [W3] ... [Wn-1]  (n = CPU cores - 2)    │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌────────────┐      ┌─────────────────────────────────────┐    │
│  │  IO Thread │ ───▶ │  Dispatches actual work as jobs     │    │
│  └────────────┘      └─────────────────────────────────────┘    │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                     Wait List                            │    │
│  │   Jobs waiting on counters (checked on counter update)   │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

## Thread Configuration

| Thread Type | Count | Purpose |
|-------------|-------|---------|
| Main Thread | 1 | Game loop, job submission, Vulkan submission |
| IO Thread | 1 | File I/O operations, dispatches processing jobs |
| Worker Threads | `cores - 2` | Execute jobs from priority queues |

**Why `cores - 2`?** Main thread + IO thread occupy 2 cores. Workers fill the rest. On a 16-core CPU: 14 workers. On 4-core: 2 workers (minimum viable).

```cpp
uint32_t worker_count = std::max(2u, std::thread::hardware_concurrency() - 2);
```

## Core Types

### Counter

The fundamental synchronization primitive. A job can wait for a counter to reach a specific value.

```cpp
struct Counter {
    std::atomic<int32_t> value{0};

    void increment(int32_t amount = 1);
    void decrement(int32_t amount = 1);
    int32_t get() const;

    // Called internally when value changes - checks wait list
    void notify();
};
```

### Job

```cpp
enum class JobPriority : uint8_t {
    Low = 0,
    Normal = 1,
    High = 2
};

using JobFunction = std::function<void(JobContext&)>;

struct JobDeclaration {
    JobFunction         function;
    JobPriority         priority = JobPriority::Normal;
    Counter*            signalOnComplete = nullptr;  // Decrement this counter when done
    const char*         debugName = nullptr;         // For profiling
};

struct Job {
    JobDeclaration      decl;

    // Dependency: wait until this counter reaches targetValue
    Counter*            waitCounter = nullptr;
    int32_t             waitTarget = 0;

    // Yield state (if job yielded)
    bool                yielded = false;
    std::any            yieldState;  // Continuation data
};
```

### Job Context

Passed to every job function. Allows yielding and spawning child jobs.

```cpp
struct JobContext {
    JobSystem*          system;
    Job*                currentJob;

    // Yield this job - will be re-queued when counter reaches value
    void waitFor(Counter& c, int32_t targetValue);

    // Spawn child jobs
    void run(const JobDeclaration& decl);
    void runBatch(std::span<JobDeclaration> jobs, Counter& completionCounter);

    // Get/set continuation state for yielded jobs
    template<typename T>
    void setState(T&& state);

    template<typename T>
    T& getState();
};
```

## Priority Queue Implementation

Lock-free MPMC (multi-producer multi-consumer) queues. All workers pull from the same global queues - no work stealing needed since queues are shared.

```cpp
class JobQueue {
public:
    bool push(Job&& j);           // Returns false if full
    bool pop(Job& out);           // Returns false if empty

private:
    // Chase-Lev deque or similar lock-free structure
    // Fixed capacity (e.g., 4096 jobs per queue)
};

class PriorityQueueSet {
    JobQueue m_high;
    JobQueue m_normal;
    JobQueue m_low;

public:
    void push(Job&& j);   // Routes to correct queue based on priority
    bool pop(Job& out);   // Checks high → normal → low
};
```

## Worker Thread Loop

```cpp
void workerThreadFunc(JobSystem* system, uint32_t workerId) {
    while (!system->shouldShutdown()) {
        Job j;
        if (system->popJob(j)) {
            // Check if job is waiting on a counter
            if (j.waitCounter && j.waitCounter->get() != j.waitTarget) {
                // Not ready - add to wait list
                system->addToWaitList(std::move(j));
                continue;
            }

            // Execute job
            JobContext ctx{system, &j};
            j.decl.function(ctx);

            // If job yielded, it's already re-queued to wait list
            if (j.yielded) {
                continue;
            }

            // Signal completion
            if (j.decl.signalOnComplete) {
                j.decl.signalOnComplete->decrement();
            }
        } else {
            // No work - brief sleep or spin
            std::this_thread::yield();
        }
    }
}
```

## Wait List

Jobs waiting on counters go here. When a counter changes, we check if any waiting jobs are now ready.

```cpp
class WaitList {
    std::mutex m_mutex;  // Acceptable here - not hot path
    std::vector<Job> m_waitingJobs;

public:
    void add(Job&& j);

    // Called when any counter updates - moves ready jobs to queues
    void checkAndDispatch(PriorityQueueSet& queues);
};
```

**Optimization opportunity**: Partition wait list by counter pointer to avoid scanning all waiting jobs on every counter update.

## Dependency Patterns

### Pattern 1: Simple Chain

```cpp
Counter gbufferDone;

// Submit GBuffer job
system.run({
    .function = recordGBufferPass,
    .signalOnComplete = &gbufferDone
});

// Lighting waits for GBuffer
system.run({
    .function = recordLightingPass,
    .waitCounter = &gbufferDone,
    .waitTarget = 0
});
```

### Pattern 2: Fan-Out / Fan-In (Parallel Jobs)

```cpp
Counter allShadowMapsDone;
allShadowMapsDone.value = 4;  // 4 shadow cascades

// Dispatch 4 shadow map jobs in parallel
for (int i = 0; i < 4; i++) {
    system.run({
        .function = [i](JobContext& ctx) { recordShadowCascade(i); },
        .signalOnComplete = &allShadowMapsDone
    });
}

// Lighting waits for all shadows
system.run({
    .function = recordLightingPass,
    .waitCounter = &allShadowMapsDone,
    .waitTarget = 0
});
```

### Pattern 3: Batch with Continuation

```cpp
void processMeshBatch(JobContext& ctx) {
    auto& state = ctx.getState<MeshBatchState>();

    // Process next chunk
    for (int i = 0; i < 64 && state.current < state.meshes.size(); i++) {
        processSingleMesh(state.meshes[state.current++]);
    }

    // More work? Yield and continue later
    if (state.current < state.meshes.size()) {
        ctx.waitFor(someCounter, 0);  // Re-queue immediately (counter already 0)
    }
}
```

## Command Buffer Recording Strategy

### The Problem

Render passes must **execute** in order (GBuffer → Shadow → Lighting → Post), but can be **recorded** in parallel.

### Solution: Parallel Recording with Ordered Submission

```cpp
struct frame_render_jobs {
    counter gbuffer_recorded;
    counter shadows_recorded;
    counter lighting_recorded;
    counter post_recorded;

    VkCommandBuffer gbuffer_cmd;
    VkCommandBuffer shadow_cmd;
    VkCommandBuffer lighting_cmd;
    VkCommandBuffer post_cmd;
};

void dispatch_frame_recording(job_system& system, frame_render_jobs& frame) {
    // Initialize counters
    frame.gbuffer_recorded.value = 1;
    frame.shadows_recorded.value = 1;
    frame.lighting_recorded.value = 1;
    frame.post_recorded.value = 1;

    // All recording jobs run in parallel (no dependencies between recordings)
    system.run({
        .function = [&](job_context&) {
            record_gbuffer(frame.gbuffer_cmd);
        },
        .priority = job_priority::high,
        .signal_on_complete = &frame.gbuffer_recorded
    });

    system.run({
        .function = [&](job_context&) {
            record_shadows(frame.shadow_cmd);
        },
        .priority = job_priority::high,
        .signal_on_complete = &frame.shadows_recorded
    });

    system.run({
        .function = [&](job_context&) {
            record_lighting(frame.lighting_cmd);
        },
        .priority = job_priority::high,
        .signal_on_complete = &frame.lighting_recorded
    });

    system.run({
        .function = [&](job_context&) {
            record_post_process(frame.post_cmd);
        },
        .priority = job_priority::high,
        .signal_on_complete = &frame.post_recorded
    });
}

void submit_frame(frame_render_jobs& frame) {
    // Main thread waits for all recordings (can use a combined counter)
    // Then submits in correct order

    VkCommandBuffer cmds[] = {
        frame.gbuffer_cmd,
        frame.shadow_cmd,
        frame.lighting_cmd,
        frame.post_cmd
    };

    VkSubmitInfo submit{};
    submit.commandBufferCount = 4;
    submit.pCommandBuffers = cmds;
    vkQueueSubmit(queue, 1, &submit, fence);
}
```

### Alternative: Secondary Command Buffers

For even finer parallelism within a pass:

```cpp
void record_gbuffer_parallel(job_system& system, VkCommandBuffer primary) {
    counter all_secondaries_done;
    all_secondaries_done.value = 4;  // 4 worker threads

    std::array<VkCommandBuffer, 4> secondaries;
    // Allocate secondaries from pool...

    // Each worker records part of the scene
    for (int i = 0; i < 4; i++) {
        system.run({
            .function = [&, i](job_context&) {
                record_gbuffer_portion(secondaries[i], i, 4);
            },
            .signal_on_complete = &all_secondaries_done
        });
    }

    // Wait then execute
    system.wait_for(all_secondaries_done, 0);  // Blocking wait on main

    vkCmdExecuteCommands(primary, 4, secondaries.data());
}
```

## IO Thread Design

The IO thread does minimal work - just the actual I/O syscalls. Processing is dispatched to workers.

```cpp
void io_thread_func(job_system* system) {
    while (!system->should_shutdown()) {
        io_request req;
        if (io_queue.pop(req)) {
            // Do the actual I/O (blocking is fine - this thread is dedicated)
            auto data = read_file(req.path);

            // Dispatch processing to worker
            system->run({
                .function = [data = std::move(data), callback = req.callback]
                           (job_context&) {
                    callback(data);
                },
                .priority = req.priority
            });
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
```

## API Surface

```cpp
namespace rapture {

class job_system {
public:
    // Lifecycle
    void init();      // Creates threads
    void shutdown();  // Joins threads, clears queues

    // Job submission
    void run(const job_declaration& decl);
    void run(const job_declaration& decl, counter& wait_counter, int32_t wait_target);

    // Batch submission (more efficient for many jobs)
    void run_batch(std::span<job_declaration> jobs);
    void run_batch(std::span<job_declaration> jobs, counter& completion_counter);

    // Blocking wait (for main thread sync points)
    void wait_for(counter& c, int32_t target_value);

    // IO requests
    void request_file_read(const std::filesystem::path& path,
                           std::function<void(std::vector<uint8_t>)> callback,
                           job_priority priority = job_priority::normal);

    // Statistics
    struct stats {
        uint64_t jobs_executed;
        uint64_t jobs_pending;
        uint64_t wait_list_size;
    };
    stats get_stats() const;

private:
    std::vector<std::thread> m_workers;
    std::thread m_io_thread;
    priority_queue_set m_queues;
    wait_list m_wait_list;
    std::atomic<bool> m_shutdown{false};
};

// Global accessor (initialized at engine startup)
job_system& jobs();

} // namespace rapture
```

## Usage Example: Frame Rendering

```cpp
void Renderer::render_frame() {
    auto& js = jobs();

    // Counters for this frame
    counter culling_done;
    counter recording_done;

    culling_done.value = 1;
    recording_done.value = 4;  // 4 passes

    // 1. Culling (must complete before recording)
    js.run({
        .function = [this](job_context&) { cull_scene(); },
        .priority = job_priority::high,
        .signal_on_complete = &culling_done
    });

    // 2. Parallel recording (waits for culling)
    js.run({
        .function = [this](job_context&) { record_gbuffer(); },
        .priority = job_priority::high,
        .signal_on_complete = &recording_done
    }, culling_done, 0);

    js.run({
        .function = [this](job_context&) { record_shadows(); },
        .priority = job_priority::high,
        .signal_on_complete = &recording_done
    }, culling_done, 0);

    js.run({
        .function = [this](job_context&) { record_lighting(); },
        .priority = job_priority::high,
        .signal_on_complete = &recording_done
    }, culling_done, 0);

    js.run({
        .function = [this](job_context&) { record_post(); },
        .priority = job_priority::high,
        .signal_on_complete = &recording_done
    }, culling_done, 0);

    // 3. Main thread waits for all recording, then submits
    js.wait_for(recording_done, 0);
    submit_command_buffers_in_order();
}
```

## Open Questions / Design Decisions

### 1. Yielding Without Fibers

**Current plan**: Yielded jobs return from their function. State is stored in `Job::yieldState`. When re-executed, the job must manually check state and resume.

**Alternative**: Coroutines (C++20). More ergonomic but adds complexity.

**Recommendation**: Start simple with manual state. Add coroutine support later if needed.

### 2. Queue Capacity

Fixed-size queues prevent allocation but can overflow under heavy load.

**Recommendation**: 4096 jobs per queue. Assert on overflow during development. In release, spin-wait if full.

### 3. Main Thread as Worker

Main thread could process jobs when waiting.

**Recommendation**: Yes - `waitFor()` should process jobs while waiting, not just spin.

### 4. Per-Frame Counters

Counters need to be reset each frame. Pool of counters per frame-in-flight?

**Recommendation**: Use a `FrameAllocator` for counters. Reset pool each frame.

### 5. Thread Affinity

Pin workers to specific cores?

**Recommendation**: Not initially. Modern schedulers are good. Add if cache performance is an issue.

## Implementation Phases

### Phase 0: Secondary Command Buffers (Pre-requisite)
- [ ] Refactor render passes to record into secondary command buffers
- [ ] Execute secondaries from primary buffer in correct order
- [ ] Validate this works sequentially before parallelizing

### Phase 1: Core System
- [ ] `Counter` struct with atomic operations
- [ ] `Job` and `JobDeclaration` types
- [ ] Lock-free priority queues (can use `moodycamel::ConcurrentQueue` initially)
- [ ] Worker thread pool
- [ ] Basic `run()` and `waitFor()`

### Phase 2: Dependencies
- [ ] Wait list implementation
- [ ] Counter change notifications
- [ ] Dependency waiting in jobs

### Phase 3: IO Integration
- [ ] IO thread
- [ ] File read requests
- [ ] Integration with AssetManager

### Phase 4: Renderer Integration
- [ ] Command buffer recording jobs
- [ ] Per-frame counter pool
- [ ] Culling parallelization

### Phase 5: Polish
- [ ] Profiling integration (Tracy?)
- [ ] Statistics and debugging
- [ ] Coroutine support (if needed)

## References

- [Parallelizing the Naughty Dog Engine Using Fibers](https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine) - GDC 2015
- [Destiny's Multithreaded Rendering Architecture](https://www.gdcvault.com/play/1021926/Destiny-s-Multithreaded-Rendering) - GDC 2015
- [moodycamel::ConcurrentQueue](https://github.com/cameron314/concurrentqueue) - Lock-free MPMC queue

## File Structure

```
Engine/src/jobs/
├── JOB_SYSTEM_DESIGN.md    (this document)
├── job_system.h            (public API)
├── job_system.cpp          (implementation)
├── job_types.h             (counter, job_declaration, job_context)
├── job_queue.h             (lock-free priority queues)
├── job_queue.cpp
├── wait_list.h             (waiting job management)
└── wait_list.cpp
```
