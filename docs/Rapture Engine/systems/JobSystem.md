# JobSystem

**Source: `Engine/src/jobs/JobSystem.h/.cpp`**

Fiber-based job system for parallel work execution. Static singleton with worker threads, IO thread, and GPU poll thread. Supports job batching, counters for dependency tracking, blocking wait with work stealing, and async IO requests.
