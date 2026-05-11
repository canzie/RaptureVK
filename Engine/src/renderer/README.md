Related files: [`MDIBatch.h`](./MDIBatch.h), [`MDIBatch.cpp`](./MDIBatch.cpp)

# Multi-Draw Indirect (MDI) Batching System

This document provides an overview of the Multi-Draw Indirect (MDI) batching system, designed to efficiently render large numbers of objects by minimizing CPU overhead and API calls. The system is tightly integrated with the `BufferPool` to leverage memory locality for optimal performance.

## Core Concepts

The primary goal of this system is to group individual draw calls into larger "batches" that can be submitted to the GPU with a single `vkCmdDrawIndexedIndirect` command. This is ideal for scenes with many static meshes, as it significantly reduces the number of state changes and draw calls the CPU needs to issue per frame. The systems utilising this went from over 1ms of cpu time to less than 300microseconds. preparing 1 entity for drawing went from ~25 mircoseconds to < 1 mircoseconds. GPU timers also got reduced by more than 1ms 

(these measurements were made in debug mode with validation layers enabled, and single buffering)

## Key Components

### `MDIBatch`
- **Purpose**: Represents a collection of draw commands that share the same vertex buffer arena and index buffer arena
- **Role**:
    - Collects `VkDrawIndexedIndirectCommand` parameters for each mesh added to it.
    - Stores per-draw `ObjectInfo` (mesh and material indices) for bindless rendering in shaders.
    - Manages its own GPU buffers: one for the indirect commands and another for the `ObjectInfo`.
    - Handles GPU buffer creation, resizing (with a power-of-2 growth strategy), and data uploads.

### `MDIBatchMap`
- **Purpose**: A manager class that organizes multiple `MDIBatch` instances.
- **Role**:
    - Acts as the main entry point for the rendering system.
    - Groups meshes into the appropriate `MDIBatch` based on their source buffer arenas. It generates a unique 64-bit key from the 32-bit vertex and index buffer arena IDs to ensure that all meshes using the same pair of buffers are batched together.
    - At the start of a frame, it clears all existing batch data to prepare for new commands.

### `ObjectInfo` (Struct)
- **Purpose**: A simple struct passed to the GPU containing bindless indices.
- **Role**:
    - `uint32_t meshIndex`: An index into a global buffer of mesh data (e.g., transformations).
    - `uint32_t materialIndex`: An index into a global UBO of material data.
    - This allows shaders to fetch the correct resources for each object within a batch using the draw's unique ID.

## Rendering Workflow

1.  **Frame Start**: The renderer calls `MDIBatchMap::beginFrame()` to clear the command lists of all batches from the previous frame.
2.  **Object Culling & Collection**: The scene is traversed (using the ECS). For each visible mesh:
    a. `MDIBatchMap::obtainBatch()` is called with the mesh's vertex and index buffer allocations. The map uses the buffer arena IDs to find an existing batch or create a new one.
    b. `MDIBatch::addObject()` is called on the obtained batch. This generates and stores a `VkDrawIndexedIndirectCommand` and an `ObjectInfo` struct on the CPU. The command's offsets (`firstIndex`, `vertexOffset`) are calculated based on the mesh's allocation within the large buffer arenas.
3.  **Buffer Upload**: After collecting all objects, the renderer iterates through the `MDIBatchMap` and calls `uploadBuffers()` on each non-empty batch. This commits the CPU data to the GPU storage buffers, creating or resizing them as needed.
4.  **Drawing**: For each `MDIBatch` in the map:
    a. Bind the shared vertex and index buffers from the buffer arena used by the batch.
    b. Provide the object buffer indices via pushconstants
    c. Issue a single `vkCmdDrawIndexedIndirect` call, providing the batch's indirect buffer and its draw count.

## Features

- **Synergy with `BufferPool`**: The entire system's efficiency relies on the `BufferPool`. By batching based on buffer arena IDs, it ensures that draws using data that is already close together in memory are rendered together, maximizing cache hits on the GPU.
- **Bindless Rendering Support**: The `ObjectInfo` buffer is a key enabler for bindless rendering. The `MDIBatch` registers its `ObjectInfo` SSBO with a `DescriptorManager`, which places it in a descriptor array. The shader can then use `gl_InstanceIndex` to index into the correct `ObjectInfo` struct and fetch its mesh/material data.


## Notes

The current system does the batching and culling on the CPU. The reason for this is that the current implementation is not a bottleneck, when the time comes to render larger open worlds, this system can be moved to a compute shader pass. No other systems will be majorly affected by this change, allowing for a smooth transition.
