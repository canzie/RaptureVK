# CommandPool

**Source: `Engine/src/gpu/command_buffers/CommandPool.h/.cpp`**

Manages `VkCommandPool` and allocated [[CommandBuffer]] instances with deferred reset via timeline semaphore tracking. Allocates command buffers on demand.

`CommandPoolManager` creates pools per config hash, managing one pool instance per frame-in-flight. On `beginFrame()`, marks all pools for reset. Has NVIDIA driver workaround (`RAPTURE_SKIP_COMMAND_POOL_RESET`).
