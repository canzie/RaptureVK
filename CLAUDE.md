# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## CRITICAL RULES (READ FIRST)

**NEVER build**: Only the user builds. Do not run cmake, make, or any build commands.

**Comments in headers**: Use Doxygen-style block comments for function documentation, NOT regular comments (`//`). Use this format:
```cpp
/**
 * @brief Description here
 */
void myFunction();
```
Do not over-comment obvious things like member variables. Keep comments minimal and meaningful.

## Build Commands

```bash
# Configure and build
mkdir build && cd build
cmake ..
cmake --build . --config Debug

# Run the editor
./bin/Debug/Rapture\ Editor
```

Prerequisites: C++20 compiler, CMake 3.16+, Vulkan SDK 1.3+ (set `VULKAN_SDK` env var). Linux/NVidia requires latest drivers for `VK_EXT_robustness2`.

## Architecture Overview

RaptureVK is a Vulkan 1.3 renderer/editor using modern extensions (Dynamic Rendering, Descriptor Indexing/Bindless, Ray Tracing). The codebase is split into:

- **Engine/** - Static library with core systems
- **Editor/** - ImGui-based editor application

### Key Systems

**ECS (EnTT)**: Data-oriented entity management. Components defined in `Engine/src/Components/Components.h`.

**Buffer Pool System** (`Engine/src/Buffers/`): Sub-allocates from large GPU arenas (64-256MB) using VMA virtual blocks. Vertex and index buffers share arenas for cache locality. See `Engine/src/Buffers/README.md`.

**MDI Batching** (`Engine/src/Renderer/MDIBatch.h`): Groups draws by buffer arena into single `vkCmdDrawIndexedIndirect` calls. Uses `ObjectInfo` structs for bindless mesh/material indices. See `Engine/src/Renderer/README.md`.

**DDGI** (`Engine/src/Renderer/GI/DDGI/`): Probe-based global illumination using GPU ray tracing. Work in progress.

**Physics - Entropy** (`Engine/src/Physics/`): Custom impulse-based rigid body engine with BVH collision detection.

**Asset Loading** (`Engine/src/AssetManager/`): Async multi-threaded pipeline supporting glTF 2.0.

## Code Standards

**Naming**:
- PascalCase for classes, camelCase for variables/methods, SCREAMING_SNAKE_CASE for constants
- Member variables: `m_userId`
- Static functions: prefix with `s_`

**Header guards**: Use `#ifndef RAPTURE__FILENAME_H` pattern, NOT `#pragma once`

**Error handling**: DO NOT throw exceptions - return empty/default values instead. Use RAII for resources. If something early or critical fails an assert is preferred

**Logging**: Use macros from `Engine/src/Logging/Log.h`. Do not include function names in messages.

**Memory**: Prefer smart pointers (`std::unique_ptr`, `std::shared_ptr`), use `std::move` for move semantics, avoid heap allocations where stack suffices.

**Casts**: Avoid C-style casts; use `static_cast`, `dynamic_cast`, `reinterpret_cast`.

## Namespace

All engine code is in the `Rapture::` namespace.

## ECS Design Philosophy

**Entity wrapper approach**: The `Entity` class (`Engine/src/Scenes/Entities/Entity.h`) is a lightweight handle (~12-16 bytes: entt::entity + Scene*). It's cheap to copy and pass by value. Avoid storing `shared_ptr<Entity>` - just store `Entity` directly.

**Avoid exposing EnTT directly**: The wrapper exists so EnTT can be swapped out. Don't use `entt::entity` or `entt::registry` outside the wrapper. Methods like `getHandle()` that return EnTT types should be avoided in engine code.

**Relationships are components**: EnTT doesn't have built-in hierarchy. Relationships between entities (parent-child, etc.) are expressed via components. This isn't a "hack" - it's the intended pattern. See `HierarchyComponent` for the current approach.

**Components are data**: Keep components as pure data structs where possible. Helper methods on components are fine for accessing/modifying that data. Complex logic belongs in systems or free functions, not component methods.

**Hierarchy component design** (`Engine/src/Components/HierarchyComponent.h`):
- Stores `Entity parent` + `std::vector<Entity> children` directly
- Free functions (`setParent`, `removeFromParent`) handle bidirectional sync
- No separate "node" class, no shared_ptr indirection
- Bidirectional storage is intentional - transform propagation needs fast parent→children traversal

**EntityView performance note**: The current `EntityView` wrapper in `Engine/src/Scenes/Entities/EntityView.h` has overhead compared to raw EnTT views. For performance-critical loops, using `registry.view<>().each()` directly is ~10x faster. This is a known issue to address.

## Radiance Cascades (Experimental)

**Author**: Alexander Sannikov (Grinding Gear Games). This is a DISTINCT technique - NOT related to DDGI, radiance caching, or path tracing.

**Full theory reference**: See `Engine/src/Renderer/GI/radiance_cascades/RADIANCE_CASCADES_THEORY.md`

### Core Insight: The Penumbra Hypothesis

The technique exploits an inverse relationship between spatial and angular resolution requirements:
- **Near light sources**: Need HIGH spatial resolution (many probes), LOW angular resolution (few rays)
- **Far from light sources**: Need LOW spatial resolution (few probes), HIGH angular resolution (many rays)

Because these are inversely proportional, total work (probes × rays) stays CONSTANT across cascades.

### Cascade Scaling (2D: 4x branching, 3D: 8x branching)

| Cascade | Probes | Rays/Probe | Range | Total Work |
|---------|--------|------------|-------|------------|
| C0 | N | R | [0, base] | N×R |
| C1 | N/4 | R×4 | [base, base×4] | N×R |
| C2 | N/16 | R×16 | [base×4, base×16] | N×R |

### Merging (Top-Down)

Cascades merge from highest (far, many rays) to lowest (near, many probes):
```
radiance = near.rgb + (far.rgb × near.alpha)
```
Where alpha = visibility (1.0 = ray missed, 0.0 = ray hit/occluded).

### Known Limitations
- Poor at sharp shadows (fundamental to the technique)
- Grid artifacts without bilinear interpolation
- Cascade transition artifacts without interval overlap

### Experiment Log

| Date | Attempt | Result | Notes |
|------|---------|--------|-------|
| 2025-12-19 | v1: Basic tracing | In Progress | No merging yet |
