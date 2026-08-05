# Play Mode and Scene Serialization

Working notes from the 2026-08-01 design session. Records the decisions and the ordered passes so
they are not re-derived. Not a full plan doc — each pass gets detail when it starts.

> **Revision 2026-08-04.** [[Entity Types and Authoring Schema]] replaced the per-component serialization model. The unit is now a **node class**, each serialising its own layer and chaining to its base, so "per-component functions applied to an existing component" and the authored/derived split by hand both fall away — a node's class determines its components, and derived state is unreachable from a class's `serialize`. The file is a walk of the instance tree from the scene's hidden root, and references serialise as indices into that walk, which keeps "no persistent entity ids" intact. Reading needs a `name -> factory` registry, which does not exist yet. Everything about play mode itself — in-place restore, no duplicated world, selective keep, checksum reconciliation — is unaffected.

**Related: [[Project Serialization]], [[Asset Metadata]], [[Prefab]], [[Scene]], [[Entity]], [[Entity Types and Authoring Schema]], [[Asset & Editor Roadmap]]**

## Goal

Two editor capabilities, in order:

1. **Ray picking** — click a mesh in the viewport to select its entity.
2. **Play vs edit state** — enter play, simulate, leave play and get the scene back as it was, with
   the option to selectively keep individual entities' changes.

Play mode needs a scene snapshot, which needs scene serialization, which is why the ordering below
runs through the file formats.

## Decisions

**No duplicated world.** Play runs in-place on the live scene. Duplicating would mean duplicating
every piece of derived state — `SceneRenderData` slots, a `BLAS` per mesh, a `ShadowMap` per
shadowing light, `TerrainGenerator`, the TLAS, every physics body.

**The snapshot is a serialized document, not a blob.** Selective keep ("keep this entity's
transform") requires the snapshot to be addressable per entity per component, which a blob is not.
Whether that document is written to disk is irrelevant to play mode — it only has to exist.

**Restore is in place, not a reload.** The expensive part of a reload is not parsing, it is tearing
down and rebuilding derived state, plus the asset churn when every `AssetRef` drops to zero. So
stopping play walks the snapshot and overwrites authored component fields on entities that were
never destroyed. Handles stay valid, so render slots, BLASes, shadow maps and asset refs all
survive, and editor selection survives with them. Divergence is a small diff: entities spawned
during play are destroyed, entities destroyed during play are recreated, components added or
removed are reconciled.

Selective keep then costs nothing — it is skipping a subtree during the restore walk.

**No persistent entity ids.** Entity references serialize as per-scene local indices, resolved at
load through a `std::vector<EntityID>` the loader builds and discards. No id component, no
`uuid → entity` side table. Cross-scene entity references do not exist by design — one registry
per scene. While play is running the snapshot is keyed by live handle, since nothing was destroyed;
on disk that same data is array indices.

**Components split into authored and derived.** Only authored fields serialize. Derived state
(`renderDataSlot`, `BLASComponent`, `ShadowComponent`, `InstanceShapeComponent`,
`TerrainComponent`'s generator, `RigidBodyComponent::bodyId`, `CameraComponent::camera`) is rebuilt.
Where a system holds authored data the component does not have, either move that data onto the
component or give the system its own serialize functions — decided per case, not by a blanket rule.

**Per-component functions apply fields to an existing component**, rather than constructing one, so
the full-load path and the in-place restore path share them instead of forking.

**Scenes are assets.** A scene is registered in the asset registry, which gives it a UUID, a content
browser entry, and the dependency machinery that scene export will need.

**Reconciliation is by checksum.** Serialized scenes are not kept in memory long, so opening an
already-open scene compares the on-disk checksum against the hash the live scene recorded when it
was loaded or last saved. Requires a **deterministic writer** — same scene must produce identical
bytes, or every comparison reports a false difference. The hash and a dirty flag live on the live
`Scene`, not in the evictable asset payload, and only dirty-live + changed-on-disk is a real
conflict worth prompting about. Resolution is whole-scene; per-entity merge is out of scope.

## G-buffer slots beyond four

Four colour attachments is the Vulkan guaranteed minimum (`maxColorAttachments` and
`maxFragmentOutputAttachments`). Attachments past that are **optional and hard-dropped**: a device
that cannot afford one loses the feature it carries outright. No fallback path, no approximation —
consumers compile the feature out. Optional slots therefore only ever carry uncommon things.

In practice the budget is binary, not a spectrum. Every real driver reports 8 (verified 8 on the
RTX 4070 here); nothing ships reporting 5, 6 or 7. So there are exactly two tiers, named by us and
passed to the G-buffer shaders as a bare define: `GBUFFER_ATTACHMENT_COUNT_MIN` (4, the guaranteed
floor) and `GBUFFER_ATTACHMENT_COUNT_ALL` (every target the G-buffer wants). `ALL` grows as slots
are added; the C++ side is one `maxColorAttachments >= ALL` check at init.

Slot 4 (`gbufferE`) carries the **entity id**, which is what ray picking reads back. It is a plain
engine feature, not editor-only and not behind a build macro — a game can use a per-pixel entity id
for outlines, hover, or screen-space hit tests just as well as the editor can.

Entity ids are stored **biased by one**, so the cleared value of zero reads as "no entity" — the
attachment clear path writes floats, so a zero clear is the only sentinel available without
widening it.

## Ordered passes

1. **G-buffer optional-slot scheme + editor slot + ray picking + `shared_ptr<Entity>` removal.**
   ← current
2. **Serialization of scenes, entities and components**, and the `.rasset` scene type.
3. **Writing to disk.**
4. **The `.rapt` project format.**
5. Play mode on top of the snapshot; then character controller and spring arm.
6. Scene merging, efficient reload, export-with-dependencies.

`shared_ptr<Entity>` is being removed in pass 1 because picking touches the same selection path:
`EntitySelectedEvent`'s payload, `GBufferPass`, `StencilBorderPass`, `ViewportPanel`.
