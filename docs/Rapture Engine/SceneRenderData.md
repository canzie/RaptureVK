# SceneRenderData (Design)

GPU-side mirror of a scene's ECS data. Replaces per-entity GPU buffers (`MeshDataBuffer`, `LightDataBuffer`, `CameraDataBuffer`, `ShadowDataBuffer`) with shared, partitioned SSBOs managed by a single system.

See [[SceneRenderData Implementation]] for what was actually built and any deviations.

**Location**: `Engine/src/renderer/`

## Problem

Components currently own GPU resources and create them in constructors:
- `MeshComponent` owns `shared_ptr<MeshDataBuffer>` — per-entity GPU buffer
- `LightComponent` owns `shared_ptr<LightDataBuffer>` — per-entity GPU buffer
- `CameraComponent` owns `shared_ptr<CameraDataBuffer>` — per-entity GPU buffer
- `ShadowComponent` owns `unique_ptr<ShadowMap>` — GPU render target in constructor
- `BLASComponent` builds acceleration structures in constructor
- `SkyboxComponent` triggers asset loads in constructor

This couples components to GPU infrastructure, prevents headless/serialization use, and means every entity pays for its own GPU buffer allocation.

## Design

### Pull-based, not push

Components are inert data. They don't know SceneRenderData exists. Each frame, SceneRenderData iterates the registry views, reads component data, and packs it into shared SSBOs. A new entity shows up — next frame it's in the view. An entity is destroyed — next frame it's gone.

No registration calls, no callbacks from components, no IDs returned to components.

### EnTT lifecycle signals

SceneRenderData hooks `on_construct` / `on_destroy` signals on the registry to track when entities gain or lose renderable components. These fire per-entity with `(registry&, entity)` — no filtering needed.

```cpp
registry.on_construct<MeshComponent>().connect<&SceneRenderData::onMeshAdded>(this);
registry.on_destroy<MeshComponent>().connect<&SceneRenderData::onMeshRemoved>(this);
```

Signal handlers allocate/free slots in the partition. The per-frame update is still pull-based (reads component data), but the slot lifecycle is event-driven.

### Static/Dynamic partitioning

Entities are tagged with zero-cost empty components:
```cpp
struct StaticTag {};   // rarely moves (buildings, terrain, props)
struct DynamicTag {};   // moves every frame (characters, projectiles)
// no tag = unclassified, treated as dynamic
```

EnTT stores empty types with zero bytes per entity — just membership tracking via sparse set.

Two partitions per data type, each with different update strategies:
- **Static partition**: dirty bitfield + generation counter check. Only changed entries get uploaded. Vast majority of entities, near-zero per-frame cost when nothing moves
- **Dynamic partition**: rebuilt every frame unconditionally. Small entity count, so full rebuild is fine

Views use direct positive queries, no excludes:
```cpp
registry.view<MeshComponent, TransformComponent, StaticTag>()   // static meshes
registry.view<MeshComponent, TransformComponent, DynamicTag>()  // dynamic meshes
```

### GPU layout — one SSBO, two regions

The shader sees a single flat SSBO. Static and dynamic data are concatenated at upload time:

```
SSBO layout (one descriptor binding):
[ static_0 | static_1 | ... | static_S | dynamic_0 | dynamic_1 | ... | dynamic_D ]
```

MDI draw commands set `firstInstance` to the global slot index:
- Static entity with partition slot `i`: `firstInstance = i`
- Dynamic entity with partition slot `j`: `firstInstance = staticCount + j`

Shader accesses `meshData.entries[gl_BaseInstance]` — no awareness of the split.

```glsl
layout(set = 0, binding = X) readonly buffer MeshData {
    MeshGPUData entries[];
} meshData;

void main() {
    MeshGPUData obj = meshData.entries[gl_BaseInstance];
    // ...
}
```

### Buffer sizing

Initial allocation should be generous to avoid early resizes. Growth strategy: double capacity when full. Resizing requires re-uploading everything (including statics), but the buffer converges to a stable size quickly as the scene populates.

### Multi-frame in flight

N copies of the SSBO (one per frame in flight). Each frame writes to `ssbos[frameIndex % N]`. Previous frames' buffers are untouched — no synchronization issues.

For the static partition's dirty bitfield: N bitfields, one per frame. When an entity gets dirty, set the bit in all N bitfields. Each frame consumes and clears its own bitfield.

## Data structures

### Slot map (inside RenderPartition)

Dense slot map — O(1) add/remove, always packed, no gaps in SSBO:

```
Dense array:    [data_0, data_1, data_2, ...]      <- this IS the SSBO contents
Dense->Entity:  [entityA, entityC, entityB, ...]    <- reverse mapping
Sparse array:   [entity_id -> dense_index]          <- O(1) lookup, indexed by entity ID
```

- **Add**: append to end of dense array, set `sparse[entityId] = length - 1`. O(1)
- **Remove**: swap removed entry with last entry in dense array, update swapped entity's sparse entry. O(1). Dense array stays packed

Uses `UINT32_MAX` as sentinel for empty sparse slots. Sparse array grows on demand, indexed by `entt::to_entity()` (entity ID without version bits).

### Dirty bitfield

Cache-friendly bit array for tracking which static slots need re-upload:

```cpp
std::vector<uint64_t> m_words;  // 64 slots per word
```

10K entities = 160 bytes of bitfield. Fits in L1. Scanning uses `__builtin_ctzll` (hardware trailing zero count) — one instruction finds the next dirty slot in a block of 64.

For the static partition where almost nothing changes, scanning 160 bytes of zeros and skipping everything is near-instant.

### Generation tracking

Parallel array alongside the dense data, one `generation_t` per slot. Each frame, compare against the component's current generation. If different, set the dirty bit and update the stored generation.

```cpp
std::vector<generation_t> m_lastSeenGenerations;  // indexed by dense slot
```

This is sequential memory access over contiguous arrays — cache-line friendly.

## GPU data structs

```cpp
struct MeshGPUData {
    glm::mat4 modelMatrix;
    uint32_t materialIndex;
    uint32_t vertexBufferFlags;
    uint32_t entityId;
    uint32_t _pad;
};

struct LightGPUData {
    glm::vec4 positionAndRange;      // xyz = position, w = range
    glm::vec4 directionAndType;      // xyz = direction, w = float-encoded type
    glm::vec4 colorAndIntensity;     // xyz = color, w = intensity
    float innerConeAngle;
    float outerConeAngle;
    uint32_t entityId;
    uint32_t _pad;
};

struct CameraGPUData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 viewProjection;
    glm::vec4 positionAndNear;       // xyz = position, w = near
    glm::vec4 forwardAndFar;         // xyz = forward, w = far
};
```

## Ownership and lifecycle

```
Scene
  └── unique_ptr<SceneRenderData>
        ├── RenderPartition<MeshGPUData>    m_staticMeshes
        ├── RenderPartition<MeshGPUData>    m_dynamicMeshes
        ├── RenderPartition<LightGPUData>   m_staticLights
        ├── RenderPartition<LightGPUData>   m_dynamicLights
        ├── RenderPartition<CameraGPUData>  m_cameras
        ├── RenderPartition<ShadowGPUData>  m_shadows (metadata)
        ├── ShadowSystem                    (owns shadow map textures)
        ├── BLASSystem                      (owns acceleration structures)
        ├── SkyboxSystem                    (owns skybox texture lifecycle)
        └── EnTT signal connections
```

**Scene owns it**. Created via `scene.initRenderData(renderContext, frameCount)` after engine init. RenderContext is passed in once at construction — no globals, no `Application::getInstance()`.

Renderers (per-viewport) access it through the scene: `scene.getRenderData()`. Multiple viewports sharing a scene share the same SceneRenderData.

## Per-frame update flow

Called once per frame before rendering, from the renderer or application loop:

```
SceneRenderData::onUpdate(frameIndex)
  1. Hierarchy dirty propagation (top-down BFS from dirty roots)
  2. Static mesh partition:
     a. Scan dense entity list, compare generation counters
     b. Changed entries: pack data, set dirty bit
     c. uploadDirty(frameIndex) — only dirty slots written to GPU
  3. Dynamic mesh partition:
     a. Iterate view, pack all entries unconditionally
     b. uploadAll(frameIndex) — full write to GPU
  4. Repeat for lights, cameras (same pattern)
  5. Concatenate static + dynamic into the unified SSBO
```

## Component changes (what gets stripped)

After this refactor, components become pure data:

| Component | Before | After |
|---|---|---|
| `MeshComponent` | `Mesh*` + `shared_ptr<MeshDataBuffer>` | `Mesh*` + flags |
| `LightComponent` | light params + `shared_ptr<LightDataBuffer>` | light params only |
| `CameraComponent` | camera data + `shared_ptr<CameraDataBuffer>` | camera data only |
| `ShadowComponent` | config + `unique_ptr<ShadowMap>` | config only (see below) |
| `BLASComponent` | builds BLAS in constructor | mesh reference, system builds |
| `SkyboxComponent` | loads texture in constructor | texture path/ref, system loads |

No component constructor touches the GPU. No component stores a GPU resource. No component needs `RenderContext`.

## Per-entity GPU resources (textures, acceleration structures, etc.)

Shadow metadata (matrices, bias, cascade splits, bindless texture index) is just SSBO data — same partition pattern as mesh/light data. No special treatment.

The separate problem is per-entity GPU resources that aren't SSBO data: textures (shadow maps, skybox cubemaps, procedural textures), acceleration structures (BLAS), and similar. These are individual Vulkan objects (`VkImage`, `VkAccelerationStructureKHR`) with their own memory, descriptors, and lifetimes.

### The pattern

Components are config/intent. The `on_construct` signal triggers a system that creates the GPU resource. The component can receive a reference back to it (handle, AssetRef, bindless index).

Example: `ShadowComponent` holds resolution, cascade count, bias. On construct, the shadow system creates the depth image(s) and writes the bindless index back to the component. The component never called Vulkan itself.

Same for `BLASComponent` (holds mesh reference, system builds the acceleration structure), `SkyboxComponent` (holds texture path, system loads via AssetManager).

### Internal organization — systems

To keep SceneRenderData from becoming a god class, per-component-type logic is organized into internal systems. Each system is a small class that:
- Defines the `on_construct` / `on_destroy` callbacks for its component type
- Owns the GPU resources it creates (or delegates to existing managers)
- Handles per-frame updates if needed (e.g. re-rendering a shadow map when its light moves)

SceneRenderData owns the systems and wires their callbacks to the registry at construction.

```
SceneRenderData
  ├── RenderPartition<MeshGPUData>       (static + dynamic)
  ├── RenderPartition<LightGPUData>      (static + dynamic)
  ├── RenderPartition<CameraGPUData>
  ├── RenderPartition<ShadowGPUData>     (metadata SSBO)
  ├── ShadowSystem                       (owns shadow map textures, re-render decisions)
  ├── BLASSystem                         (owns acceleration structures)
  ├── SkyboxSystem                       (owns skybox texture lifecycle)
  └── EnTT signal connections
```

Each system receives `RenderContext` through SceneRenderData. The exact interface between systems and SceneRenderData (base class, free functions, or just classes with a known API) is an implementation detail — start simple, refine if needed.

### Vulkan constraints for per-entity GPU resources

These resources share common lifecycle requirements:
- **Creation** needs `VkDevice`, `VmaAllocator`, and often `CommandPoolManager` (for layout transitions, uploads, builds)
- **Bindless registration** needs `DescriptorManager` to get a bindless index for shader access
- **Persistence** — they survive across frames, unlike SSBO data that's updated per-frame
- **Deferred destruction** — can't destroy while any in-flight frame still references them. Must defer by N frames (frames in flight count)
- **Dirty tracking** — some resources need re-creation or re-rendering when their config changes (shadow map when light moves, BLAS when mesh deforms). Generation counters on the source components drive this

## Files involved

New files:
```
Engine/src/renderer/gpu_data/SceneRenderData.h/.cpp
Engine/src/renderer/gpu_data/RenderPartition.h        (template, header-only)
Engine/src/renderer/gpu_data/DirtyBitfield.h/.cpp
Engine/src/renderer/gpu_data/GPUData.h
```

Modified files:
```
Engine/src/components/Components.h          — strip GPU buffer members
Engine/src/scenes/Scene.h/.cpp              — add SceneRenderData ownership + initRenderData()
Engine/src/scenes/Scene.cpp                 — onUpdate() shrinks dramatically
Engine/src/renderer/DeferredRenderer.cpp    — consume SceneRenderData SSBOs instead of per-entity buffers
Engine/src/renderer/MDIBatch.h/.cpp         — set firstInstance from partition slot indices
```

Removed after migration:
```
Engine/src/components/systems/object_data_buffers/MeshDataBuffer.h/.cpp
Engine/src/components/systems/object_data_buffers/LightDataBuffer.h/.cpp
Engine/src/components/systems/object_data_buffers/CameraDataBuffer.h/.cpp
Engine/src/components/systems/object_data_buffers/ShadowDataBuffer.h/.cpp
Engine/src/components/systems/object_data_buffers/ObjectDataBase.h/.cpp
Engine/src/components/systems/ObjectDataBuffer.h
```
