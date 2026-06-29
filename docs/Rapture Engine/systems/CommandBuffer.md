# CommandBuffer

**Source: `Engine/src/buffers/command_buffers/CommandBuffer.h/.cpp`**

Wraps `VkCommandBuffer` with primary and secondary level support. Has static factory methods for batch allocating multiple buffers. Secondary buffers support dynamic rendering inheritance. Lifecycle managed by [[CommandPool]] (pool handles reset and free).
