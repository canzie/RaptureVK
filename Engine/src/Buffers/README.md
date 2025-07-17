# Buffer Pool System Overview

This system manages Vulkan buffer allocations for vertex and index buffers using a pooling mechanism to improve performance through better memory locality, especially for Multi-Draw Indirect (MDI) rendering. It uses VMA (Vulkan Memory Allocator) for efficient sub-allocations within larger buffer arenas.

The main limitation of VMA that led to the need for this system was the lack of inherit support for direct larger shared buffers. It is recommended to combine the vertex, index and even uniform buffers into a single larger buffer for better locality and performance. Even tough vma might use the same buffer under the hood, we need more control here.

When reading the code you might come across different usage types (STATIC, DYNAMIC, STREAM), these are inspired from opengl to minimise vulkan verbosity.

## Key Components

### BufferAllocationRequest (Struct)
- **Purpose**: Defines the parameters for a buffer allocation, including size, type (VERTEX or INDEX), usage (STATIC, DYNAMIC, etc.), flags, alignment, and layout.
- **Role**: Used by clients (e.g., VertexBuffer or IndexBuffer) to request allocations from the pool.

### BufferAllocation (Struct)
- **Purpose**: Represents a sub-allocation within a larger buffer arena.
- **Role**: Holds the allocated memory slice (offset, size) and provides methods for data upload and device address retrieval. Automatically frees itself on destruction via RAII.

### BufferArena (Struct, std::enable_shared_from_this)
- **Purpose**: Manages a single large Vulkan buffer (arena) and handles sub-allocations using VMA's virtual blocks.
- **Role**: Creates a VkBuffer with specified size and usage flags. Provides thread-safe allocate/free operations with manual alignment handling if needed. Tracks compatibility with requests.

### BufferPoolManager (Singleton Class)
- **Purpose**: Central manager for multiple BufferArenas, organized by buffer layout hash.
- **Role**: Initializes with a VMA allocator, finds or creates suitable arenas for requests, and handles allocations/frees. Uses a map of buffer vertex layout hashes to arena lists for quick lookup.

## Features
- **Arena Sharing for Locality**: Arenas are shared between vertex and index buffers (usage flags include both VK_BUFFER_USAGE_VERTEX_BUFFER_BIT and VK_BUFFER_USAGE_INDEX_BUFFER_BIT) to keep related data in the same buffer, improving cache locality for MDI draws.
- **Manual Alignment Handling**: In `BufferArena::allocate`, if VMA's allocation isn't naturally aligned, it over-allocates and manually adjusts the offset, ensuring requirements like shader device address alignment are met without wasting space.
- **Upload Mechanism**: `BufferAllocation::uploadData` uses a staging buffer and command buffer submission for GPU-only memory, ensuring safe data transfer with minimal overhead. It falls back to direct mapping if possible (though currently always uses staging).
- **Compatibility Checks**: Arenas verify request compatibility via flags and usage, preventing mismatched allocations.
- **Scalable Arena Sizing**: `calculateArenaSize` dynamically sizes new arenas based on request size, starting from 64MB defaults up to 256MB max, balancing memory usage and fragmentation.

## Vertex Layout in Index Buffer Requests
Index buffers don't inherently need a layout, but the system requires one in `BufferAllocationRequest` to target the same arena as associated vertex buffers. The layout hash keys the arena map, grouping related vertex/index data in one buffer for locality (e.g., for a mesh's VBO and IBO). 

**Why Needed?** Without it, index buffers might allocate in separate arenas, reducing locality benefits for MDI. The layout acts as a "grouping key" – clients provide the vertex layout when requesting index allocations to ensure they are more likely to be allocated in the same arena. This odd choise might seem odd, but in reality the vertex and index buffers are created in the same scope meaning we just also pass along the vertex layout to the index buffer.

This design promotes efficient, locality-aware buffer management in Vulkan rendering pipelines.


## Extra Notes

