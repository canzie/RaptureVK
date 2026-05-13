# Engine Improvement Plan

Snapshot from architecture review (2025-05-09). Items are grouped by system, roughly priority-ordered within each group.

---

## Static / Singleton Architecture

The engine has 9+ static-class singletons with manual init/shutdown ordering in `Application.cpp`. Misorder causes use-after-free on Vulkan handles.

- [x] Create `RenderContext` struct that holds non-owning pointers to managers (BufferPoolManager, CommandPoolManager, DescriptorManager). VulkanContext owns the managers, RenderContext is a copyable view
- [x] `DeferredRenderer` converted to instanced, inherits `Renderer` base class, takes RenderContext in constructor
- [x] Viewport/ViewportManager system: one renderer per viewport, Application owns ViewportManager
- [x] Render passes, shadows, MDIBatch, RtInstanceData receive RenderContext via constructor (injection)
- [ ] **Second decoupling pass — explicit dependency injection for leaf types** (see details below)
- [ ] VulkanContext already follows the instance pattern — use it as the reference

**Decided**: One renderer per viewport, owned by Viewport. See Viewport Architecture below.

### Second Decoupling Pass — Design & Instructions

**Problem**: 36 engine .cpp files still call `Application::getInstance()` or `Application::getRenderContext()` to reach services (CommandPoolManager, DescriptorManager, BufferPoolManager, VkDevice, VmaAllocator, queue indices). This creates hidden coupling and init-order fragility.

**Goal**: Core engine code should not access Application at all (exceptions: AssetManager, JobSystem).

**Pattern decided**: Leaf types (buffers, textures, shaders, etc.) receive the services they need as explicit parameters — either in the constructor or in the function that triggers the service call. They do NOT store RenderContext. They store only the specific service pointer(s) they actually use.

**Two tiers**:
- **Large systems** (Renderer, render passes, shadows, MDIBatch, RtInstanceData): already receive `RenderContext` via constructor. Done.
- **Leaf resource types** (StorageBuffer, Texture, Shader, MaterialInstance, etc.): add the needed services (e.g. `DescriptorManager*`, `CommandPoolManager*`, `VmaAllocator`) as constructor params or method params. Store them only when needed for cleanup (e.g. destructor needs DescriptorManager to free bindless slots).

**Reference implementation**: `StorageBuffer` (`Engine/src/buffers/StorageBuffer.h/.cpp`) — takes `DescriptorManager*` in constructor (default nullptr), stores it, uses it in `getBindlessIndex()` and destructor. No Application access.

**What to pass instead of Application**:
- `VkDevice` and `VmaAllocator` — already passed to most constructors, just need to stop re-fetching them
- `VulkanContext*` or `const VulkanContext&` — for things like queue indices, device, extensions (vkCmdSetVertexInputEXT etc.). Don't decompose into individual values when the file needs several things from it
- `CommandPoolManager*` — for GPU upload/transition operations (Texture, BLAS, TLAS)
- `DescriptorManager*` — for bindless registration (buffers, textures, acceleration structures)
- Shader paths (`Project::getProjectShaderDirectory()`) — needs design thought, possibly scene->world->project chain. Defer this for now

**Error handling for missing services**: `RP_ASSERT(ptr != nullptr, "message")` + `RP_CORE_ERROR(...)` + early return. Assert catches it in debug, error log + return handles release.

**Files to update** (36 total, excluding AssetManager/JobSystem):
```
acceleration_structures/BLAS.cpp, TLAS.cpp
buffers/BufferPool.cpp, Buffers.cpp, IndexBuffer.cpp, VertexBuffer.cpp
buffers/command_buffers/CommandBuffer.cpp, CommandPool.cpp
buffers/descriptors/DescriptorBinding.cpp, DescriptorSet.cpp
components/systems/object_data_buffers/ObjectDataBase.cpp
generators/terrain/TerrainCuller.cpp, TerrainGenerator.cpp
generators/textures/ProceduralTextures.cpp
materials/MaterialInstance.cpp
meshes/Mesh.cpp
physics/Entropy.cpp
pipelines/ComputePipeline.cpp, GraphicsPipeline.cpp
renderer/Frustum.cpp, MDIBatch.cpp
renderer/gi/ddgi/DynamicDiffuseGI.cpp
renderer/passes/GBufferPass.cpp, InstancedShapesPass.cpp, LightingPass.cpp
renderer/passes/SkyboxPass.cpp, StencilBorderPass.cpp
renderer/shadows/CascadedShadowMapping.cpp, ShadowMapping.cpp
render_targets/SceneRenderTarget.cpp
scenes/Scene.cpp
shaders/Shader.cpp
textures/Texture.cpp
utils/TextureFlattener.cpp
window_context/vulkan_context/TimelineSemaphore.cpp
```

**Approach**: work through files sequentially — one at a time, review each before moving to the next. Reference implementation: StorageBuffer.

---

## Viewport Architecture (Implemented)

One renderer per viewport. Viewport owns the renderer instance. ViewportManager (owned by Application) owns the viewports.

- [x] `Viewport` class: holds `unique_ptr<Renderer>`, scene reference, camera entity, dimensions, RenderContext copy
- [x] `ViewportManager` class: owns viewports, `drawAll()` iterates active viewports, `createViewport()`/`destroyViewport()` API
- [x] `Renderer` base class with `drawFrame(scene, camera)` interface, `DeferredRenderer` inherits it
- [x] `RendererType` enum for renderer creation (currently only DEFERRED)
- [x] Application creates primary viewport at startup, sets active scene each frame
- [ ] Scene-viewport binding is currently set every frame in the main loop — should be event-driven or set once on scene change
- [ ] Editor needs to manage its own viewports for multi-viewport support (split views, floating windows)
- [ ] Resize handling: viewport should react to swapchain/window resize events

---

## ECS Philosophy

The stated philosophy (CLAUDE.md) is: components are data, logic belongs in systems. Current state violates this in several ways.

- [ ] Implement [[SceneRenderData]] system — GPU-side mirror of scene ECS data, replaces per-entity GPU buffers
- [ ] Strip GPU resources from components — remove `MeshDataBuffer`, `LightDataBuffer`, `CameraDataBuffer`, `ShadowDataBuffer` from components, SceneRenderData owns the shared SSBOs instead
- [ ] Stop doing I/O/GPU work in component constructors (`SkyboxComponent`, `BLASComponent`, `TerrainComponent` currently call AssetManager or build GPU resources on construction)
- [ ] Shadow system refactor — `ShadowComponent`/`CascadedShadowComponent` become config-only, shadow map GPU resources owned by rendering system (see [[SceneRenderData#Shadows — Open Design Question]])
- [ ] Extract `Scene::onUpdate()` into named systems — most of its body moves into `SceneRenderData::onUpdate()`, remainder splits into transform propagation, terrain update, TLAS update
- [ ] Naming convention: `onUpdate` = expected to be called every frame (continuous), `update` = callable on-demand
- [x] Unify change detection — generation counters (`generation_t = uint64_t`) now used across Transforms, TransformComponent, and LightComponent. Consumers (buffers, bounding box, shadow components, TLAS instances) track last-seen generation internally
- [x] Fix `TransformComponent::isDirty` raw pointer — replaced with generation counter, no more raw pointer or bool dirty flags

---

## Resource Cleanup / Unloading

The engine currently has no mechanism for unloading resources when they're no longer needed (e.g., level transitions).

- [ ] Implement deferred GPU resource destruction — resources flagged for deletion should survive until all in-flight frames referencing them have completed (frame-delayed destruction queue)
- [ ] Add scene/level unload path that tears down scene-specific GPU resources (textures, buffers, acceleration structures) while keeping engine-global resources alive
- [ ] AssetManager needs an unload/evict path — currently assets are loaded and never freed
- [ ] Consider reference-counted asset eviction: when `AssetRef` use count hits 0, schedule for deferred unload after N frames

---

## Asset System

- [ ] Make `AssetRef::useCount` atomic — currently `uint32_t` but copied across threads, causing data races
- [ ] Do NOT use `shared_ptr` for asset ownership — ownership is clear (AssetManager owns), others borrow via AssetRef. Atomic counter is sufficient.
- [ ] Note: `std::atomic<uint32_t>` is not copyable/movable. Solution: store the atomic in `AssetMetadata` (which is not copied), and have `AssetRef` hold a raw pointer to it (which is the current design, just needs the type changed to atomic)
- [ ] Replace `new AssetManagerEditor()` in `AssetManager::init()` with `unique_ptr`
- [ ] Add thread safety to AssetManager methods (concurrent `importAsset` calls can corrupt internal maps)
- [ ] Stop using `Ref` suffix typedefs to hide pointer semantics (e.g., `AssetRef`). Existing ones stay, but new code should use explicit pointer types so ownership is visible at the call site

---

## Descriptor System

The global static descriptor pool with hardcoded maximums is limiting.

- [ ] The set 0-3 reuse pattern (predefined common bindings) stays — it reduces duplicates and simplifies development
- [ ] Set 4+ for custom per-pipeline bindings stays — this is intentional flexibility
- [ ] Consider replacing the static magic numbers in `DescriptorSetBindingLocation` (SET*100+BINDING encoding) with something more explicit, or at minimum document the encoding clearly
- [ ] Move away from a single global `VkDescriptorPool` — consider per-frame pools or at least separate pools for different lifetimes (persistent vs. per-frame)
- [ ] The pool currently uses hardcoded max values that keep getting bumped (s_maxBuffers = 20000, s_maxTextures = 16000). Consider dynamic pool growth or multiple pools.

---

## Event System

Current system uses string-keyed lookup + `typeid().name()` per event access. Goal: high-perf events with support for user-defined custom runtime events.

- [ ] Replace string-based event lookup with compile-time type IDs or hashed IDs for built-in events
- [ ] For user-defined runtime events (e.g., animation triggers): use a hashed string ID (`uint64_t`) so users can create events by name without modifying engine code
- [ ] Consider: built-in events use compile-time dispatch (zero-cost), custom events use hashed lookup (fast but not zero-cost)
- [ ] Add listener ordering guarantees (currently iterates unordered_map — non-deterministic)
- [ ] Ensure listeners are cleaned up on component/system destruction (currently some register but never unregister)

---

## Exceptions

CLAUDE.md says no exceptions. There are 25+ throw sites remaining.

- [ ] Replace all `throw std::runtime_error` in Vulkan init code (`VulkanContext.cpp`, `VulkanQueue.cpp`) with `RP_ASSERT` + early return
- [ ] Replace throws in `DescriptorSet.cpp` (7 nearly identical throws in `updateUsedCounts`) with `RP_ASSERT`
- [ ] Replace throws in `GraphicsPipeline.cpp` with `RP_ASSERT` + return
- [ ] Entity wrapper: throwing versions (`getComponent`, `addComponent`, `removeComponent`) — keep the API but replace throws with `RP_ASSERT`. The `tryGetComponent` variants remain the preferred API.
- [ ] Remove `EntityException` class

---

## Shader System

Current system uses shaderc for runtime SPIRV compilation.

- [ ] Move to a lighter-weight compilation path (offline precompilation or simpler runtime compiler)
- [ ] Ignore shader optimization passes for now — correctness first
- [ ] Consider batching shader compilation via the job system for faster startup

---

## Buffer / Command Pool System

These are in good shape. Minor items only.

- [ ] `CommandPoolConfig::hash()` truncates `size_t` to `uint32_t` — could cause hash collisions. Use `size_t` for `CommandPoolHash`
- [ ] `getCommandPool(hash, frameIndex)` ignores the `frameIndex` parameter — fix or remove the parameter
- [ ] `BufferAllocation::uploadData` blocks the GPU with `waitIdle()` — eventually move to transfer queue + timeline semaphores (low priority, not blocking development)
- [ ] The NVIDIA busy-wait workaround (`RAPTURE_SKIP_COMMAND_POOL_RESET`) could potentially be moved to a job-based approach later

---

## Physics (Entropy)

**SHELVED** — Will be replaced with Jolt Physics when physics work resumes. Do not invest time in Entropy.

---

## Consistency / Cleanup

- [ ] Migrate remaining `#pragma once` files to `#ifndef RAPTURE__` header guards (76 files use pragma, 54 use guards)
- [ ] Remove remaining exceptions (see Exceptions section)
- [ ] Audit `shared_ptr<Entity>` usage — should be plain `Entity` everywhere (currently in StencilBorderPass, GameEvents)
- [ ] Render passes store `const RenderContext *m_rc` (pointer) while MDIBatch/RtInstanceData/Viewport store `RenderContext m_rc` (copy) — pick one pattern and unify

---

## Not Prioritized / Future

- Descriptor set caching (reuse identical layouts) — noted in DescriptorSet.h TODO
- EntityView wrapper performance (10x overhead vs raw EnTT views) — known, documented in CLAUDE.md
- Multi-threaded asset loading thread safety
- Shader hot-reload
