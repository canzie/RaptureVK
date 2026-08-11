# Change Tracking

Replacing every ad-hoc "did this change" mechanism in the engine with one revision log, and taking the GPU upload path off full-region copies. This is the design from [[Custom Entity Component System]] section 8, expressed on EnTT, so none of it is blocked on owning the storage layer.

Everything in Part 1 is cited against current source. Where a claim is traced but unverified at runtime it says so.

---

# Part 1 — What exists now

## 1.1 The two write paths

**From outside** — a scene object setter:

`Node3D::setPosition` (`Node3D.cpp:81-93`) writes `component->local[3]` and calls `updateWorldTransform` (`Node3D.cpp:200-215`), which composes `parent->worldTransform() * local`, calls `entity.markDirty()`, and immediately recurses the subtree (`Node3D.cpp:222-233`). `Entity::markDirty` (`Entity.cpp:8-20`) forwards to `SceneRenderData::markDirty` (`SceneRenderData.cpp:432-459`), which probes four component types — `MeshComponent`, light via `Light_tryGetLight` (itself three probes), `ShadowComponent`, `CascadedShadowComponent` — and for each present one converts `renderDataSlot` to a local slot and calls `markDirtyAllFrames` (`RenderPartition.cpp:130-135`), setting the bit in *every* frame-in-flight bitfield.

**From inside** — grab a reference and write. Nothing happens. There is no interception. `Entity::patchComponent` (`Entity.h:164-173`) wraps `registry.patch<T>`, and nothing calls it; no `registry.on_update<T>()` listener is connected anywhere. EnTT's own change hook is present and entirely unused, and a parallel `markDirty` was built instead.

`on_update` fires only from `basic_registry::patch` (`entt.hpp:24491-24494`); `replace` is implemented as `patch`, and `emplace_or_replace` routes to `patch` when the component exists. It does **not** fire on writes through `get<T>()` or a view's reference — there is no memory-write tracking. So `on_update` carries the same contract as `markDirty` and would not have closed this hole either.

## 1.2 The mechanisms, and where they disagree

| Mechanism | Trigger | Read by | Cleared |
|---|---|---|---|
| Per-slot per-frame `DirtyBitfield` | `markDirtyAllFrames`, `allocateSlot`, `freeSlot` | `updateMeshes`/`updateLights`/`updateShadows` static paths, `upload`'s any-dirty test | `upload` |
| Unconditional per-frame repack of DYNAMIC | nothing — always | — | n/a |
| Unconditional per-frame upload of DYNAMIC region | nothing — always | — | n/a |
| Whole-static-region upload gated by any-dirty | the bitfield | — | same call |
| `LightComponent::m_generation` | only the four `LightComponent` setters (`Components.h:180`, `:186`, `:191`, `:196`) | both shadow `needsUpdate` | never — monotonic |
| `ShadowComponent::needsUpdate` | compare of generation + `world` | `Scene.cpp:245`, `DeferredRenderer.cpp:364` | **consumed on read** |
| `CascadedShadowComponent::needsUpdate` | same | `DeferredRenderer.cpp:387` | **consumed on read** |
| `updateMeshes` value compare → event publish | `data.modelMatrix != transform->world` | `RtInstanceData` | n/a |
| `RtInstanceData::m_dirtyTransforms` | the publish above | `patchDirty` | end of `patchDirty` |
| `Scene::updateTLAS` full matrix compare | iterates every TLAS instance | `TLAS::updateInstances` | n/a |
| `Scene::m_tlasDirty` | add/remove RT instance | `Scene::onUpdate` | after build |
| `Environment::m_lastApplied` | frame poll | skybox regeneration | each poll |

Four of these answer the same question — did this entity's world transform change. They disagree. A direct `TransformComponent::world` write updates the TLAS geometry (the `updateTLAS` poll catches it) but not the mesh SSBO (no `markDirty`) and not `RtInstanceInfo::modelMatrix` (its publish is gated on the bitfield), so the acceleration structure and the ray-hit shading data end up describing different worlds.

## 1.3 Two confirmed silent bugs

**Spot and point shadow maps never re-render when the light moves.** `needsUpdate` (`Components.h:271-280`) mutates its own `m_lastLightGeneration` and `m_lastWorld` and returns `true` once. `Scene::onUpdate` consumes it at `Scene.cpp:245` to refresh the light view matrix. `DeferredRenderer` then asks the *same mutating method* at `DeferredRenderer.cpp:364` to decide whether to re-render the map, and always gets `false`, because `m_project->onUpdate` runs before `m_viewportManager->drawAll()` (`Application.cpp:220`, `:222`). Directional lights escape via the `|| Light_getLightType(...) == DIRECTIONAL` term on the same line; spot and point have no such escape. Verified by reading the call order; not reproduced in the editor.

**Changing a spotlight's range or cone angle does not update its shadow projection.** `SpotLight3D::setRange` (`SpotLight3D.cpp:38-49`) writes `light->range` on `SpotLightComponent` and calls `markDirty()`, so the light SSBO updates — but `LightComponent::m_generation` is bumped only by the four setters on `LightComponent` itself (`Components.h:180`, `:186`, `:191`, `:196`). `needsUpdate` compares that generation and the world matrix, neither of which changed, so `ShadowMap::updateViewMatrix` — which derives the spot projection from range and outer cone angle — never runs. The inner and outer cone setters have the same shape.

## 1.4 The upload path throws the granularity away

`DirtyBitfield::forEachDirty` is never used to narrow a GPU copy anywhere in the codebase. `GPUDataStore::upload` (`RenderPartition.cpp:207-226`) copies the **entire** static region if any static bit is set, and the **entire** dynamic region unconditionally with no dirty gate and no clear.

Combined with `markDirtyAllFrames` setting the bit in all frames, one gizmo drag of one static mesh costs `framesInFlight` consecutive full-region copies. At 50k meshes and `sizeof(MeshGPUData)` = 80 B that is 4 MB × 3 = **12 MB of memcpy to move one 80-byte record**. Sponza is ~400 nodes, so real numbers today are far smaller; the shape is what matters.

The dynamic partition's dirty bits *are* maintained correctly — `allocateSlot` sets them and `SceneRenderData::markDirty` is mobility-agnostic, looking up `mesh->mobility` and marking whichever partition holds the entity — they are simply never read and never cleared, so they sit permanently set.

## 1.5 Mobility as a partition key

`Mobility` is defined in `EntityCommon.h:11-15` and stored per component: `MeshComponent::mobility` (`Components.h:121`, default `STATIC`), `LightComponent::mobility` (`:169`, default `STATIC`), both shadow components (`:265`, `:293`, default `DYNAMIC`). It selects which of two `RenderPartition`s inside a `GPUDataStore` a slot lives in, with the SSBO laid out as `[static region][dynamic region]` and each growing independently (`RenderPartition.h:177-182`).

Costs of that specific use:

- `getGlobalSlot`/`getLocalSlot` translation on **24** call sites, with `getLocalSlot` feeding `DirtyBitfield::set`, which has no bounds check.
- Growing the static region shifts every dynamic slot's global index, so `ensureCapacity` must call `notifyAllSwaps` (`RenderPartition.cpp:271`) to make every component recompute `renderDataSlot`.
- Changing mobility at runtime frees the slot against one partition and reallocates against the other (`SceneRenderData.cpp:238-290`). Assigning `component->mobility` directly instead desynchronises `mobility` from `renderDataSlot`.
- Cameras ignore it and are hard-wired to the dynamic partition (`SceneRenderData.cpp:322-323`, `:333-334`).
- Nothing derives it. `RigidBodyComponent` construction does not set it and `setSimulatedWorldTransform` does not either, so a physics-driven mesh defaults to `STATIC`.

The saving it buys — statics skip the repack and the copy — evaporates the moment one static object moves, which is the editor's most common operation.

## 1.6 Broadcast events

`AssetEvents::onMeshTransformChanged` (`AssetEvents.h:39-42`) resolves through `EventRegistry::getEventBus`, which builds a `std::string` from `typeid(...).name()` and does two `unordered_map<std::string, …>` lookups per publish (`Events.h:84-102`). `EventBus::publish` (`Events.h:36-42`) then walks every listener.

`RtInstanceData` registers two listeners in its constructor and its destructor is empty (`RtInstanceData.cpp:22-30`, `:32`). Each `DeferredRenderer` constructs one. Three consequences: a listener leak per renderer; a dangling `this` after a viewport is destroyed; and — because the bus is global and keyed on raw `EntityID` — a mesh moving in scene A marking a colliding entity id in a renderer serving scene B, whose `patchDirty` then constructs `Entity(entID, &scene)` against the wrong scene.

## 1.7 Smaller findings in the same area

- `MeshComponent::isEnabled` is written by `Mesh3D::setVisible` and read only by `Mesh3D::isVisible`. `SceneGeometryDraw::populate`, `ShadowMap::render` and the CSM equivalent never test it, so hiding a mesh does nothing.
- `LightComponent::isActive` never reaches the GPU — `LightGPUData` (`GPUDataStructs.h:22-27`) has no active flag and `packLight` writes all lights regardless. Only `CascadedShadowMapping.cpp:454` reads it.
- `MeshGPUData::materialIndex` is hard-coded to `0` (`SceneRenderData.cpp:497`, `:517`); the real index travels through `ObjectInfo` in the MDI batch.
- `Camera3D::setFieldOfView`/`setNearPlane`/`setFarPlane` call `markDirty()`, and `SceneRenderData::markDirty` never touches the camera store — the calls are dead. Harmless only because cameras are repacked unconditionally.
- Moving a camera node does not move the camera. `TransformComponent::world` updates, but `CameraComponent::camera`'s view matrix is refreshed only by `CameraController` or `PlayerController`, and `updateCameras` reads the cached view matrix. An unpossessed camera renders from its old position.
- The class comment at `RenderPartition.h:77` claims a "sparse array maps EntityID to dense index" and no such member exists; the reverse map lives on the component as `renderDataSlot`.
- SSBOs are written during `Scene::onUpdate` (`Scene.cpp:270`) inside `Project::onUpdate` (`Application.cpp:220`), but the fence for that frame index is not waited until `SwapChain::acquireImage` inside `drawFrame` (`Application.cpp:222`) — after the write. Offscreen viewports never acquire at all. Traced in code, not confirmed with synchronisation validation.

---

# Part 2 — Target design

Full design in [[Custom Entity Component System]] sections 7 and 8. Summarised here as it applies to migration.

**One `ChangeLog` per scene**, holding per aspect: a revision array indexed by entity index, a per-entity last-logged-frame stamp for dedupe, and a ring of entity ids with a monotonic `head`.

**Aspects** split by what set of derivations is invalidated together: `TRANSFORM_WORLD`, `MESH_BINDING`, `MATERIAL_BINDING`, `LIGHT_PARAMS`, `SHADOW_SETTINGS`, `CAMERA_PARAMS`, `VISIBILITY`.

**Consumers hold a cursor** and pull `since(aspect, cursor)`, which returns the span `[cursor, head)` or signals `resynced` if the ring wrapped past them. Nothing broadcasts. A cursor belongs to a **destination**, so N frames in flight means N cursors on the same store, and each asks for everything since *it* last uploaded.

**On EnTT the announce is a convention**, since `view.get<T>()` still hands out a mutable reference. `Entity::write<T>()` returns an RAII proxy that calls `touch` on scope exit, and `getComponent`/`tryGetComponent` become const-returning. Making that convention into a compiler-enforced guarantee is the one thing that requires owning the storage, and it is the only reason left to write a custom ECS.

---

# Part 3 — Migration

Ordered so each step leaves the engine working.

### Step 1 — Fix the two live bugs first, standalone

Neither fix depends on anything below, and both are currently shipping.

Split the mutating `needsUpdate` into a non-mutating `hasChanged` plus an explicit `acknowledge`, or give each of the two callers its own stored stamp. And bump the light generation from `SpotLight3D`/`PointLight3D`'s derived setters. Do this before the refactor so the refactor is not credited with fixing them, and so a regression is attributable.

### Step 2 — Build `ChangeLog`, unconsumed

Add the log and the aspects. Call `touch(ASPECT_TRANSFORM_WORLD)` from `Node3D::updateWorldTransform` alongside the existing `markDirty`. Nothing reads it yet. Both mechanisms run in parallel, which makes the next step diffable — a debug assert can compare the log's set against the bitfield's set each frame and catch anything the log misses.

### Step 3 — Move upload to ranged copies

Replace `GPUDataStore::upload`'s two coarse copies with: derive dirty slots from the span since this frame's cursor, set bits in one reusable scratch `DirtyBitfield` (kept because it yields slots in ascending order, which is what run-coalescing wants), coalesce runs, and copy per run under a policy — full copy if `dirtyBytes > total * FULL_UPLOAD_RATIO`, or `runs > MAX_UPLOAD_RUNS`, or `total < FULL_UPLOAD_FLOOR`.

Map once per upload rather than per range. `Buffers.cpp:46-61` maps and unmaps per `addData` today, and `RtInstanceData::patchDirty` calls it per instance.

Cameras and the shadow SSBO take the full-copy branch permanently and drop their tracking — cameras genuinely change every frame, and cascade matrices are already recomputed unconditionally from the camera at `Scene.cpp:265`.

The per-frame `m_dirtyBitfields` vector collapses to one scratch, since "which frames still need this" is now the per-destination cursor's job.

### Step 4 — Delete the mobility partition

Not the concept — see [[Custom Entity Component System]] section 10, where mobility survives as an authored hint driving shadow-map caching, device-local placement and non-updatable acceleration structures. This step deletes only its use as an SSBO partition key.

`GPUDataStore` holds one `RenderPartition`. `getPartition(Mobility)` (31 call sites), `getGlobalSlot`/`getLocalSlot` (24 sites) and the two-region layout all go; `renderDataSlot` *becomes* the dense index. `ensureCapacity` takes one count, and `notifyAllSwaps`-on-growth disappears entirely because growing a single region shifts no index. The four mobility movers on `SceneRenderData` and the `Mesh3D`/`Light3D` `setMobility` paths go with it.

This is where the volume is: **170** references to `mobility`/`MOBILITY_*`/`setMobility` across Engine and Editor, including inspector dropdowns, serialization and the glTF import path. The 24 slot conversions are the risk — a missed one does not crash, it reads the wrong object's matrix.

### Step 5 — Convert consumers to cursors

`RtInstanceData`, `updateTLAS`, and both shadow components. Delete `AssetEvents::onMeshTransformChanged` and its two publish sites, the `updateTLAS` full compare loop (`Scene.cpp:567-592`), `needsUpdate` and its stamps, and `LightComponent::m_generation`.

`RtInstanceData`'s two constructor listeners go, which removes the leak, the dangling `this` and the cross-scene aliasing structurally — the log is per-scene, so a cursor cannot address another scene's entities.

### Step 6 — Const-ify the accessors

`getComponent`/`tryGetComponent` return const; add `Entity::write<T>()`. Delete `Entity::markDirty`, `SceneRenderData::markDirty` and the 30 call sites — most vanish because the neighbouring write becomes a `write<T>()`.

Last because it is the largest mechanical diff and the least interesting, and because it will surface places that write components for reasons nobody remembers.

### Not in scope, tracked separately

- Single `DEVICE_LOCAL` buffer plus staging ring instead of N host-visible copies. Independent of the log, and the better answer to the write-before-fence hazard in 1.7. Worth doing after step 3.
- `MeshComponent::isEnabled` and `LightComponent::isActive` are write-only today. Either wire them (`VISIBILITY` aspect and a cull test; an active bit in `LightGPUData`) or remove them so the UI stops lying. Decide, do not leave.
- `EventSignal`'s re-entrancy bug and its unconditional `erase_if` per fire — worth fixing on their own merits.
- Multi-viewport CSM fits cascades to `m_activeController->viewCamera()` only (`Scene.cpp:209`, `:261-265`), so a second viewport gets cascades for the wrong frustum. Orthogonal, but it is another reason the shadow SSBO is genuinely per-frame data rather than something to track.

---

# Part 4 — Risks

**Deferred transform propagation changes observable behaviour.** If step 2 later moves propagation to a deferred flush, `node->setPosition(p)` followed by `child->worldTransform()` returns stale data until the flush. The gizmo path (`ViewportPanel.cpp:524-567`) reads only the selected node's world so it is safe; every other read site would need auditing. The eager propagation currently in `Node3D` does not have this problem, so deferring is optional and should be a separate decision.

**The resync path will be rarely exercised.** Ring overflow must be forced deliberately in a test, or the one code path that guarantees correctness under load will be the one that is wrong.

**Revision arrays are sized by the highest entity index ever used**, not by live count.

**Aspect granularity is a guess.** If `TRANSFORM_WORLD` proves too coarse — the TLAS caring about rotation and translation while the bounding box cares about scale — it needs splitting. Start coarse and split on evidence.

**Step 4 is the one that can silently corrupt.** Everything else fails loudly or wastes work.
