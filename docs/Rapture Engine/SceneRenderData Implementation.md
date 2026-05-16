# SceneRenderData (Implementation)

Implementation notes for [[SceneRenderData]], documenting what was built, deviations from the design, and bugs encountered.

**Status**: Core system working (2025-05-16). Meshes, lights, cameras, shadows all flowing through partitioned SSBOs.

## File layout

Differs from the design doc — files live directly in `Engine/src/renderer/` rather than a `gpu_data/` subdirectory.

```
Engine/src/renderer/SceneRenderData.h/.cpp
Engine/src/renderer/RenderPartition.h/.cpp    (RenderPartition + GPUDataStore + DirtyBitfield)
Engine/src/renderer/GPUDataStructs.h
```

## Deleted files

Old per-entity GPU buffer system, fully replaced:

```
Engine/src/components/systems/object_data_buffers/MeshDataBuffer.h/.cpp
Engine/src/components/systems/object_data_buffers/LightDataBuffer.h/.cpp
Engine/src/components/systems/object_data_buffers/CameraDataBuffer.h/.cpp
Engine/src/components/systems/object_data_buffers/ShadowDataBuffer.h/.cpp
Engine/src/components/systems/object_data_buffers/ObjectDataBase.h/.cpp
Engine/src/components/systems/ObjectDataBuffer.h
```

## Deviations from design

### Scene ownership
Design called for `scene.initRenderData(renderContext, frameCount)` as a separate call. Implementation constructs `SceneRenderData` directly in the `Scene` constructor.

### GPU data structs
`CameraGPUData` is simpler than designed — just `view` + `projection` (128 bytes), no `viewProjection`/`positionAndNear`/`forwardAndFar`. `MeshGPUData` has no `_pad` field (80 bytes).

### GPUDataStore
The design described `RenderPartition` as the top-level type. Implementation splits into `RenderPartition<T>` (dense slot map + dirty tracking) and `GPUDataStore<T>` (owns the partitions + per-frame SSBOs + descriptor registration). `GPUDataStore` is the type that `SceneRenderData` holds.

### SignalBridge
EnTT signal connections are wrapped in a `SignalBridge` pimpl struct to avoid leaking EnTT types into the header. Design doc didn't specify this.

### ShadowSystem / BLASSystem / SkyboxSystem
Not yet implemented as separate internal systems. Shadow metadata goes through `GPUDataStore<ShadowGPUData>` like other data types. Per-entity GPU resources (shadow maps, BLAS, skybox textures) are still managed by their respective components for now — this is a known anti-pattern tracked separately (see [[Improvements]]).

## Bugs encountered

### Camera SSBO not registered (root cause of garbled geometry)
`DescriptorSetBindingLocation::NONE` and `CAMERA_DATA_SSBO` were both value `0`. `GPUDataStore::registerSSBOs()` has a guard `if (m_bindingLocation == NONE) return;` which caused it to skip descriptor registration for camera SSBOs entirely. The shader read from descriptor index `UINT32_MAX` — garbage data.

**Fix**: Changed `NONE` to `9999` so it no longer collides with any real binding location.

**Symptom**: Scene geometry garbled, screen split into quadrants with random colors. Narrowed via shader debugging — identity model matrix + camera from SSBO still produced garbage, confirming camera data was the problem.

## Shader changes

GBuffer vertex/fragment shaders now read from bindless SSBOs via push constant indices:

```glsl
layout(push_constant) uniform PushConstants {
    uint batchInfoBufferIndex;   // -> ObjectInfo SSBO in set 0 binding 6
    uint cameraSSBOIndex;        // -> CameraGPUData SSBO in set 0 binding 0
    uint cameraSlotIndex;        // -> slot within camera SSBO
    uint meshSSBOIndex;          // -> MeshGPUData SSBO in set 2 binding 0
} pc;
```
