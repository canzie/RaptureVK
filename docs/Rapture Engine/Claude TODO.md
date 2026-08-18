# Claude TODO

My own list, not the roadmap. Things I broke, spotted, or left half-done and would pick up without being asked. Thomas's plans live in `plans/`; this is the stuff that would otherwise only exist in a session that has ended.

Delete an entry when it is done or turns out not to matter. Add the date when something goes on the list.

## Bugs I introduced and have not fixed

- **2026-08-17 — `Scene::restoreFrom` use-after-free.** `SceneLoadContext::finish()` runs *after* `s_destroyInstancesNotInSnapshot`, and destroying an object takes its whole subtree with it, so `m_loaded` can hold pointers to freed instances by the time `link`/`ready` walk it. Introduced when the load phases were added. Not observed crashing yet, which is why it is still here.

## Bugs I found but did not chase

- **2026-08-17 — command pools are keyed by `std::this_thread::get_id()`.** That was a valid per-recorder key while one job owned a worker for its whole duration. With fibers migrating and interleaving it is not: two jobs on one worker share a pool, and `CommandPool`'s index, `m_needsReset` and `m_pendingSignals` are all non-atomic. Key it by fiber or hand the recorder a pool through the job context.
- **2026-08-17 — glTF joint nodes still become `Node3D`s.** A skin's joints are nodes, so a skeleton appears twice: once as the joint list inside the `Skeleton` asset and once as a tree of `Node3D`s that nothing drives. The joint node indices are known in `loadSkin`; the node walk should skip them.
- **Skinned meshes cull against bind-pose bounds** (`SceneGeometryDraw`, TODO in place). An animated mesh whose joints leave those bounds pops. Cheap fix is per-clip bounds baked at import.

## Half-done

- **`Mobility` and `Ray Traced` still show for skeletal meshes** in the properties panel, via `Mesh3DEditor`, though `SkeletalMesh3D` makes both no-ops.
- **`AnimationComponent.h` is a stub** with an unrelated `FogType` enum next to it. Delete it rather than growing it — the real component shape is in the animation notes.

## Things I would do next if given the choice

1. **Overlay renderer.** Post-composite phase, instanced solids from `MeshPrimitives` plus a line batch. Unblocks bone rendering and the gizmo leaving Amethyst. See [[Renderer Restructure]], [[Editor Tools and Gizmos]].
2. **`populate` off the critical path** — ~200µs serial on the frame thread that nothing overlaps.
3. **CSM sharing the view's batches** instead of walking the scene itself.
