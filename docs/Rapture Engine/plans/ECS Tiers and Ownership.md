# ECS Tiers and Ownership

> **Revision 2026-08-04.** [[Entity Types and Authoring Schema]] landed a node class tree above the ECS, which changes steps 2 and 3 below and answers two of the open questions. The authored/derived tiering, the ownership table and the signal mechanism are unaffected and still the plan. `HierarchyComponent` is deleted. Per-world singletons are answered: the environment is now an `Instance`, and terrain should follow.

**Decision: the engine stays on EnTT** (3.13, `Engine/vendor/_deps/entt-src/src/entt/config/version.h:6`). [[Custom Entity Component System]] is cancelled. This document is what is being done instead.

Related: [[Play Mode and Scene Serialization]], [[Prefab]], [[SceneRenderData]], [[Scene]], [[Entity]], [[Viewport]], [[ShadowMapping]], [[CascadedShadowMapping]]

## Why not a custom ECS

Everything the investigation wanted turned out to be independent of the storage layer. Derived data out of components, hierarchy with correct lifetime, transform propagation, a field description layer, change tracking, opaque handles — all of it sits *above* EnTT and can be built without replacing it.

What a custom ECS would add on top of that is two things: direct-indexed storage for near-universal components, which is a performance win in a system where a linear pass over 50k entities is already microseconds; and the ability to *enforce* that mutations go through a change-tracking accessor rather than merely offering one. Neither justifies owning a storage layer.

The five steps below also progressively isolate EnTT behind `Scene`, so if direct-indexed storage ever shows up in a profile, replacing the storage becomes a contained job instead of an engine-wide rewrite.

## The three tiers

Every piece of per-entity data falls into one of three tiers. The engine currently has all three living as components with identical rights, which is the root cause of most of what follows.

**Authored** — what a user typed or dragged. Mesh reference, material reference, light colour, shadow resolution, collider shape. Serialized, inspectable, undoable, and eventually visible to script. This is what "component" should mean.

**Derived** — anything reproducible from authored data plus assets, deterministically, with no user input and no information loss. Shadow maps, BLASes, GPU slots, physics body handles, culling results, cached matrices. Never serialized, never inspected, rebuilt on load.

**Assets** — shared, ref-counted, addressed by UUID, outliving any scene. Already outside the ECS and already correct.

The reproducibility test is not decoration; applying it finds bugs. `ShadowComponent`'s constructor takes `width` and `height` and never stores them (`Engine/src/components/Components.h:406`), and `CascadedShadowComponent` does the same with `numCascades` and `lambda` (`Components.h:431`). Those are authored values that exist only inside the derived object, so today the shadow map cannot be recreated from the component, and there is no field for an editor to bind a resolution control to.

**If something cannot be reproduced from the component, that is not derived data — it is a missing authored field.**

Reproducible does not mean cheap. A `BLAS` passes the test but costs real time to rebuild. That is a caching question (cook it into the asset), not a tiering question.

## Ownership

This is the part that has no answer today. Derived data currently lives in components, and a component's owner is the registry, which means nothing owns it in any useful sense — `Scene::onUpdate` and `DeferredRenderer` both reach into `ShadowComponent` and mutate it.

**None of these are singletons.** Each is an instance, owned by whatever owns its lifetime, and there are three lifetimes:

- **Per-asset** — derived from asset content and shared by every entity that references it. Owner: an asset-keyed cache.
- **Per-scene** — derived from entity data, independent of any camera. Owner: a scene-lifetime system.
- **Per-view** — anything computed from a camera. Owner: the `DeferredRenderer` that `Viewport` already holds (`Engine/src/viewport/Viewport.cpp:40`).

| Derived data | Lives today in | Correct lifetime | Owner |
|---|---|---|---|
| `BLAS` | `BLASComponent::blas`, `Components.h:392` | **per-asset** — one per mesh, not per entity | a member of `Mesh`, built lazily — no cache and no keying, since a BLAS is derived from the mesh and nothing else, and it then evicts with the mesh through `AssetRef` |
| `TLAS` | `Scene::m_tlas`, `Scene.h:100` | per-scene | scene-lifetime system |
| Mesh + light GPU data | `SceneRenderData`'s `GPUDataStore`s | per-scene | scene-lifetime system |
| Mesh GPU slot | `MeshComponent::renderDataSlot`, `Components.h:143` | per-scene | the mesh table's redirect array |
| `JPH::BodyID` | `RigidBodyComponent` | per-scene | `PhysicsSystem` (already correct) |
| Instance SSBO | `InstanceShapeComponent::instanceSSBO`, `Components.h:202` | per-scene | scene-lifetime system |
| `CascadedShadowMap` | `CascadedShadowComponent`, `Components.h:425` | **per-view** — cascades come from the camera frustum (`CascadedShadowMapping.h:70`) | `Viewport`'s renderer |
| `ShadowMap` | `ShadowComponent::shadowMap`, `Components.h:402` | per-scene *if* it can be made camera-independent — `updateViewMatrix` currently takes `cameraPosition` (`ShadowMapping.h:38`), which needs checking per light type | scene-lifetime system, or per-view if not |
| Camera GPU data | `SceneRenderData::m_cameras` | per-view | `Viewport`'s renderer |
| Culling results | `RenderPartition` | per-view | `Viewport`'s renderer |
| `CameraComponent::frustum` | component, `Components.h:69` | per-view | `Viewport`'s renderer |

Two rules fall out of the table.

**Ownership is not the same as driving.** `Scene` may hold per-scene render resources — something has to, and duplicating them per viewport is worse. What `Scene` must not do is *call into* them. `Scene::onUpdate:196` iterating shadow casters and calling `shadowMap->updateViewMatrix` is the thing to remove, not `Scene`'s ownership of the resource.

**Split by lifetime, not by struct.** `SceneRenderData` currently mixes per-scene data (meshes, lights) with per-view data (cameras), and `ShadowComponent` mixes authored config with a derived resource and a mutable dirty cache. Both split.

### The bug this explains

`ShadowComponent::needsUpdate` mutates — it stores the last light and transform generations and returns whether either changed (`Components.h:410`). It is called from `Scene.cpp:205` and again from `DeferredRenderer.cpp:379` in the same frame. Whichever runs first consumes the change and the second sees "unchanged". The renderer's call is `needsUpdate(...) || lightType == DIRECTIONAL`, so directional lights are masked from the effect; point and spot lights are not. Call order is not yet traced, so this may be benign today — but a mutating dirty check read from two subsystems is precisely what an owner would prevent.

## The mechanism

Derived data moves into **system-owned storage**, using `entt::storage<T>` declared as a member rather than a component in the registry:

```cpp
class ShadowSystem {
    entt::storage<ShadowMap> m_maps;
};
```

Same sparse set as a component pool. The difference is that it is not in the registry, so it is invisible to serialization, the inspector and script, and it has an owner with a name.

**The signals stay.** An earlier draft of this document proposed replacing `on_construct`/`on_destroy` with a per-frame reconcile pass that compares the table against a view. That was wrong: EnTT fires those signals reliably on every `emplace` and `erase`, so there is no event to miss, and a scan someone has to remember to call every frame is both more work and more fragile than a connection wired once at construction. The handlers do not disappear — they move from `SceneRenderData` to whichever system owns the resource. Two systems deriving two different things from the same component keep one pair each.

A reconcile pass is the right tool only when membership is a predicate over several inputs rather than the presence of a component — "active shadow casters in range that the quality setting allows a map for" — because then nothing is added or removed when the answer changes and there is no signal to hook. Nothing in this plan is that case.

Two things a raw signal does not cover, both already solved by hand in the codebase and worth having in one place:

**Creation cannot happen inside the handler.** Building a `ShadowMap` allocates a Vulkan image, and the handler runs mid-registry-operation on whatever thread called `emplace`. `Scene::onRigidBodyConstructed` already works around this by pushing onto `m_pendingRigidBodies` for `registerRigidBodies` to drain later. The construct handler records the entity; the system creates the resource at its own update point. Destroy handlers *can* run inline, since they only move a row into the retire ring.

**A system attaching to a populated scene gets no events.** Signals only report the future, so there is one catch-up pass over the existing entities when the system attaches. `SceneRenderData` already does exactly this at `SceneRenderData.cpp:174-179`.

**A retire ring** — a removed row is held for frames-in-flight before destruction, so a GPU resource outlives command buffers still referencing it. This already exists as `Scene::m_blasFreeBuckets` (`Scene.h:102`); it becomes a property of the table and moves to the system that owns the resource.

## The five steps

**1. Tier split.** Derived data out of components into system-owned storage, with the owners from the table above. Removes the `unique_ptr` and `shared_ptr` members from `ShadowComponent`, `CascadedShadowComponent`, `BLASComponent` and `InstanceShapeComponent`, and adds the authored fields that are currently thrown away in their constructors. The signal handlers move rather than disappear, and `MeshComponent::renderDataSlot` stays until the GPU store it indexes moves. Unblocks step 2, and removes the need for [[Play Mode and Scene Serialization]] to encode an authored/derived split by hand — everything in the registry is authored, and nothing else is reachable from the serializer.

The realistic size of this for shadows: drop two `unique_ptr` members, add `width`/`height` to `ShadowComponent` and `width`/`height`/`cascadeCount`/`lambda` to `CascadedShadowComponent`, move the two loops at `Scene.cpp:195-228` and the generation cache into one `ShadowSystem` owned by `Scene`, and point `SceneRenderData::updateShadows` and `DeferredRenderer`'s shadow pass at it. Most of the machinery this needs — dirty tracking, the static/dynamic split, deferred GPU free — already exists and is reused as-is.

**2. ~~Field description layer.~~ Superseded by [[Entity Types and Authoring Schema]].** A generic `describe(Archive&)` per component was the answer while the component set was open. It is not needed once a node class owns its own layer: each class serialises its fields and chains to its base, and the properties panel builds one collapsible header per class in the chain. Both the file format and the panel are covered without a reflection layer, and the hardcoded `ensure<XEditor>` chain is gone. Undo is the one consumer that still wants an addressable field, and it can be had by serialising the affected component before and after rather than by describing every field.

**3. Hierarchy split in two, neither of them `HierarchyComponent`.** The instance tree owns **child order**, because ordering is an authoring concern — outliner rows and deterministic file output. A `ParentComponent` holding a parent id and a depth owns the **parent link**, because propagation is engine work and the engine must never traverse instances. Propagation needs no children list: it reads its parent's already computed world matrix, so an upward link plus a depth sort of the pool makes the pass linear with every parent guaranteed done first. Depth counts `Node3D` ancestors, so folders are transparent. `HierarchyComponent` is **already deleted** — the instance tree replaced every reader of it. Still to do: the `ParentComponent`, the world matrix on `TransformComponent`, and deleting the flatten in `Prefab::instantiate` in the same change.

**4. Clean the wrapper.** `EntityView` is **deleted** — it was entirely unused, and its `operator*` re-looked-up every component the view had already found, making it ~10× slower than raw EnTT views. Remaining: remove `Entity`'s implicit conversions (`operator uint32_t`, `operator entt::entity`) so the handle is opaque and the id can never be assumed to mean anything; remove `EntityException` (`Entity.h:21`), since the codebase forbids exceptions; close the 32 `getRegistry()` call sites so EnTT stops leaking.

**5. Change tracking.** A write accessor that marks a dirty bit, replacing `Entity::markDirty` and the hand-rolled generation stamps in `Transforms` and `ShadowComponent`. `DirtyBitfield` already exists in `RenderPartition.h:22`, so this is largely about routing writes rather than new machinery. Depends on step 4 having closed the raw-registry paths, otherwise it is trivially bypassed.

Order matters for 1 and 2: both are blocking work that scene serialization is about to need, and doing serialization first means writing per-component functions by hand and then rewriting them.

## Open questions

- **Which per-scene system owns the scene-lifetime render resources?** `Scene` holding them is the smallest change and is defensible under "owns but does not drive", but a scene owning a TLAS is still the wrong layer. The alternative is an object keyed by scene and owned by whatever manages scenes.
- **Is the non-cascaded `ShadowMap` genuinely per-view?** `updateViewMatrix` takes `cameraPosition`, but a spot light's shadow frustum should be the light cone. If it is camera-independent for spot and point lights, those maps are per-scene and only the directional path is per-view.
- **What triggers the `BLAS` build** once it lives on `Mesh` — lazily on first request, or eagerly when the mesh finishes loading and acceleration structures are enabled. Lazy avoids paying for meshes that are never ray-traced; eager avoids a build stall mid-frame.
- **Does the mesh GPU table use swap-and-pop or a free list?** Swap-and-pop keeps the array compact but moves slots, which breaks anything caching an index across frames — the TLAS instance custom index is the case to check. A free list keeps slots stable at the cost of holes, which is fine for a buffer that is indexed rather than iterated.
- **`InstanceComponent`** holds `std::vector<TransformComponent>` and `std::vector<MaterialComponent>` with its own id counter (`Components.h:169`). Nothing here addresses it. Ten thousand grass instances should not be ten thousand entities, and this is the one case where the entity abstraction genuinely does not fit.
- **Per-world singletons** — `Scene::environmentEntity()` is "always present and not destroyable" (`Scene.h:47`), which is a special case leaking into the public API. Worth deciding which special entities become plain scene members.
