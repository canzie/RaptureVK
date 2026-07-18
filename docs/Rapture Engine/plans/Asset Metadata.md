# Asset Metadata

> Status: **partially implemented** (2026-07-18). Refines the [[Project Serialization]] plan's
> `## Asset lifetime: reload from metadata` section — that plan stays the source of truth for the
> flow; this one defines what a registry entry actually holds and what import does.
>
> **Built:** the provenance/reload-source split on `AssetMetadata` (`Asset.h` — `provenance`
> optional + `hasBlob` bool), the mesh reload source as a `BlobStore` blob (`BlobStore.h`,
> `Mesh::serialize`/`deserialize`), reload-on-demand (`AssetManagerEditor::loadFromMetadata`), and
> the self-contained project dir (`Project.h` — `getBlobDirectory`, `getCacheDirectory`).
> **Not built:** the import-panel flow (import still loads straight into the scene), cook-then-evict,
> thumbnails, reimport, texture BC cooking.

**Related: [[Project Serialization]], [[Prefab]], [[AssetManager]], [[Asset]], [[AssetRef]]**

## Goal / intent

Pressing **Import** must not put anything in memory. It registers metadata, writes cooked blobs and
thumbnails to the project dir, and ends with nothing resident. Dragging an asset into a scene is
what loads it, and loading is a blob read — never a re-parse of the source file. The same metadata
that makes the first load work makes an evicted asset reloadable, so the two are one mechanism.

---

## Locked decisions

1. **Provenance and reload source are separate fields.** They answer different questions and are
   allowed to hold different values for the same asset. See [[#The two fields]].
2. **The blob is the source of truth for loading. Provenance is never on the load path.** It exists
   only for reimport, is advisory, and is allowed to dangle. An asset with no provenance, or with
   provenance pointing at a deleted file, still loads forever.
3. **Import does the full decode, streaming, then evicts.** Import is the one moment the user has
   explicitly asked for work and will watch a progress bar. See [[#Import].
4. **The project dir is self-contained.** Blobs, thumbnails and the `.rapt` live in it; source art
   (the `.gltf`, the `.png`) lives outside and is not required to open, load, or ship a project.
5. **Reimport matches sub-assets by index, corroborated by name.** glTF offers no stable key; index
   is the only one that always exists. See [[#Reimport]].
6. **Blobs and the cache are separate.** Thumbnails are derived data — deletable at any time,
   costing one re-render. Blobs are the mesh **reload source**, so they are not throwaway: deleting
   a blob is safe only while the source art is present to re-cook from. That is why they live in
   `blobs/` (`Project.h` `getBlobDirectory`), *outside* `.cache/` (`getCacheDirectory`, holding
   `thumbnails/`). Neither is in the `.rapt` or version control.

---

## The two fields

The registry entry carries both. Confusing them is what makes this design feel stuck.

| | Provenance | Reload source |
|---|---|---|
| Answers | Where did this come from? | How do I get the bytes back right now? |
| Used by | Reimport only | Every load, including post-eviction |
| Frequency | Once per import, by hand | Any time, possibly mid-frame |
| Allowed to dangle | **Yes** | No |
| Lives on | The import record (per source file) | The asset entry |

Per type:

| Type | Provenance | Reload source |
|---|---|---|
| Texture | import record + image index | cooked BC blob (v1 may stay the `.png` — see [[Project Serialization]]) |
| Mesh | import record + `{node, mesh, primitive}` | a `hasBlob` flag; the bytes live in the `BlobStore` under the asset handle |
| Material instance | import record + material index | inline `MaterialInstanceDescription` (planned) |
| Prefab | import record | inline node list — it *is* its own description |

A mesh's reload source must not be its glTF ref. Bringing back one 200KB primitive would re-parse a
multi-megabyte JSON, read the whole `.bin`, and decode accessors — paid **per evicted mesh**. For
Sponza that is up to 103 times. The blob read is one `pread` plus an upload.

**Implemented differently than first drafted.** The metadata does *not* carry a fat locator
(`{blobId, offset, size, layout, counts, bounds}`). It carries only `hasBlob`; the `BlobStore`
(`BlobStore.h`) owns the `handle → {offset, length}` map and the partition files
(`blob_NNNN.rblob`), while the mesh owns its own self-describing byte layout
(`Mesh::serialize`/`deserialize` — layout, counts and bounds live *in* the blob header, not the
metadata). This keeps the metadata tiny and lets the store defragment or relocate a blob without
touching any asset's metadata. `AssetManagerEditor::loadFromMetadata` branches on `hasBlob`: set →
`BlobStore::read` + a type-dispatched decode; unset → the file importer.

---

## Import

Import is a full decode and it ends with nothing resident:

1. Parse the source (JSON + buffers + images).
2. Per primitive, **streaming**: decode → write blob → upload → render thumbnail → drop.
3. Register metadata for every mesh / texture / material / prefab.
4. Evict.

Streaming per primitive keeps the peak at one primitive plus the source buffer rather than all 103
at once. Same end state, bounded peak; it matters the first time someone imports something
enormous.

**Why not lazy (metadata at import, cook on first load).** It was considered and rejected. It moves
the decode to first drag-in — a worse moment than import, because the user is placing something in
a scene and gets a stall instead of a progress bar they asked for. The end state of both designs is
identical (metadata + blobs on disk, nothing resident), so lazy buys import latency at the cost of
the one interaction that should feel instant.

**The drag right after an import is the common case, and it is free.** With `EVICT_HINT_LAZY`
(see the residency section of [[Project Serialization]]) a use count of 0 sends the asset to the
cold LRU list rather than freeing it, so the just-imported data is still resident. It hits memory,
not even disk, and is freed later under real budget pressure.

### Thumbnails

Rendered at import as a by-product of a decode already happening, written to `cache/thumbs/<uuid>`.

They are not optional polish — for an unnamed glTF they are the **only** affordance. Sponza's
content browser is 103 entries named `Sponza_Node_Primitive_0..102` (the node is unnamed, the mesh
is unnamed, there is no `extras`). No name exists to recover; finding the curtain by text is
impossible. See [[Prefab]] for the naming evidence.

Never stored in the `.rapt` — base64 blobs would destroy the one property that file needs to keep
(human-readable and git-diffable).

---

## Reimport

### There is no stable key

glTF node / mesh / primitive indices are not stable across a re-export, and names are frequently
absent:

| File | nodes named | meshes named | materials named |
|---|---|---|---|
| Sponza | 0/1 | 0/1 | 0/25 |
| adamHead | 80/80 | 76/76 | 13/13 |
| MetalRoughSpheres | 0/6 | 5/5 | 0/1 |
| cornell | 1/1 | 1/1 | 1/1 |

Sponza has zero names anywhere. A content hash is no help either: it is stable only when the
geometry is unchanged, which is exactly the case where you are not reimporting.

So the problem is unsolvable in general. That is why UE5's reimport duplicates rather than
incompetence. What makes it tolerable is [[#Locked decisions|decision 2]] — provenance is advisory,
so an imperfect key costs a bad reimport, never a broken project.

### Index is the key, name is the corroboration

- Match on index.
- Both sides named and **agreeing** → confident.
- Both sides named and **disagreeing** → the file was reordered. Flag it, do not take the match.
- Either side unnamed (Sponza, always) → take the index; nothing else exists to check against.
- Widespread disagreement → flag the whole reimport, ask the user.

Name as validator rather than key is what patches pure-index's one nasty failure: index is stable
across the common reimport (a texture tweak, one mesh's geometry adjusted) but shifts **silently**
when a mesh is inserted or reordered, matching everything after the insertion point wrong — the
curtain quietly becomes a pillar. A silent mismatch is worse than no match. adamHead gets real
validation for free; Sponza gets index-only, which is all it can ever have.

### Only the referenced subset matters

Preserving a UUID matters exactly insofar as something points at it. If no scene references
`Sponza_Primitive_57`, minting it a fresh UUID costs nothing. So reconciliation only has to be
careful about the referenced subset — which is small, and worth showing:

> 12 matched, 3 new, 1 missing and used in 2 scenes

Two commands:
- **Reimport** (default): match against the existing import record, update in place, preserve
  matched UUIDs, report the diff.
- **Import as new**: mint everything fresh under a new import record. For deliberately keeping both
  versions.

The diff plus that escape hatch is what makes this not-UE5.

### The import record

One per source file, holding source path + import config + timestamp. Provenance lives **here**,
not on each sub-asset. Sub-assets carry only the weak key (`{node, mesh, primitive}` + name if the
file had one), used for nothing except reconciliation.

Import options are a short deliberate list, because reimport must honor every one of them: which
glTF scene, texture format / srgb, generate thumbnails, and the kit-split toggle (see [[Prefab]]).
Options that change the asset graph shape are a maintenance burden — reimport has to reconcile two
shapes — so the bar for adding one is high.

---

## Open refinement points

- Blob granularity: one blob per import (all primitives back to back) vs one per mesh. Lean: per
  import, since that is what the cook emits anyway.
- Whether textures cook to BC blobs in v1 or stay `.png` + decode-and-compress on every load. The
  serialization plan currently says `.png`; the metadata shape is identical either way, so this is
  a drop-in later.
- Cache invalidation: source mtime vs content hash, and whether a stale cache auto-reimports or
  just warns.
- Where the cache dir lives relative to the `.rapt`, and its exclusion from version control.
