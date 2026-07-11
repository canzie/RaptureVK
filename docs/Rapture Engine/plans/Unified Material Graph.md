# Unified Material Graph

**Related: [[Material System Overhaul]], [[Material Graph Compiler]], [[Material Graph Nodes]], [[MaterialData]], [[MaterialInstance]], [[Procedural Texture and Shader Editor]]**

The direction the material system is converging on: **the node graph *is* the material.** One representation, no static-vs-graph split. This evolves decisions 2/4 of [[Material System Overhaul]] (static stays a fixed struct) toward a single graph-based source of truth, while keeping the runtime unchanged.

---

## Core idea

Every material is a node graph. "Static" stops being a material *type* — it becomes "a graph that happens to be the standard PBR shape," produced by the glTF translator. External formats (glTF, MaterialX) are **translators, not storage**: they convert to our graph at import and the raw form never enters the engine.

## Three lifetimes

- **Disk** — a serialized graph (nodes + connections + canvas positions), MaterialX-style. The source of truth. Only form kept between sessions.
- **Runtime** — compiled `(graphId, GraphInstanceData)`. Compact, exactly what the renderer already consumes.
- **Editing** — deserialize → nodes in the editor → edit → recompile + reserialize. Nodes live in memory *only while a material is open*, not always.

## The load-bearing constraint: structural dedup

If every glTF material became its own compiled function, Sponza alone would bake dozens of `evalSurface_*` into `GBuffer.fs.glsl` → shader explosion. glTF materials are structurally identical (albedo map + normal + MR split + ao), differing only in *which* textures/values. So graphs must be **deduplicated by structure**: same topology → one compiled `graphId`; per-material differences (texture indices, factors) live in `GraphInstanceData`. Sponza collapses to ~one graphId with many instance slices. Hash graph structure in [[SurfaceGraphManager]] to key this. (The current hand-written static shader path is effectively "graphId 0 = standard PBR"; unification just makes that explicit.)

## Naming

The sink node is **PBR Surface** for now — it outputs albedo/normal/roughness/metallic/ao, i.e. standard metallic-roughness, *not* OpenPBR yet (the `SM_OPENPBR_STANDARD` id is aspirational). It conflates the shader closure and the output sink; split a separate **Material Output** sink off later, when multiple shading models (Unlit, Toon) warrant it.

---

## Expanded design (findings)

This section fleshes the vision into a buildable plan and answers the three questions it left open: what the disk format is, how glTF lowers into it, and whether the static "no graph data" flow survives. Everything below is checked against the current tree.

### A. The load-bearing distinction: one *conceptual* model, but three encodings

The slogan "the node graph *is* the material" is true at exactly **one** of three lifetimes. Keeping them separate is what makes this practical instead of a Sponza shader/arena explosion:

| Lifetime | Encoding | Count of forms | Why |
|----------|----------|----------------|-----|
| **Conceptual / authoring** | a node graph | **one** | satisfies "the graph is the material"; the editor always shows a graph |
| **Disk (serialized)** | compact *standard* record **or** full *graph* record | **two** | a serialized ~10-node standard-PBR graph per glTF material is pure JSON bloat + a matcher to undo it; store the common shape compactly |
| **Runtime (GPU)** | `MaterialData` header **+ optional** arena slice | **two** | the static path (`evalStaticSurface`) and the fat named struct are a hand-tuned specialization; keep them |

The current "two records, chosen by a flag" (in [[Material System Overhaul]] §2.4) is **not** contradicted by unification — it is the *runtime* row of this table. Unification only collapses the **disk + conceptual** rows.

### B. The reframe that makes it click: `MaterialData` *is* graph 0's instance pool

Right now the dispatch seam is (`GBuffer.fs.glsl:80-82`, verified):

```glsl
SurfaceData surf = matHasFlag(flags, MAT_FLAG_IS_GRAPH)
    ? evalSurfaceGraph(mat.graphId, si, mat.graphDataOffset)   // generated
    : evalStaticSurface(si, mat);                              // hand-written, GBuffer.fs.glsl:31
```

`evalStaticSurface` is **the compiled function of the canonical standard-PBR graph**, and `MaterialData` (112 B, `MaterialData.h:53-96`) is **that graph's instance pool** — just hand-laid-out with named slots (`texIndices0/1`, factors) instead of the compiler's packed `uint[]` arena. So:

- **Static is not a different *kind* of material. It is graph 0 — the one structure common enough to earn a bespoke hand-written eval and a bespoke instance layout** instead of a generated `evalSurface_N` + an arena slice.
- Every material carries a `MaterialData` regardless (the draw's `inMaterialIndex → u_materials[idx]` is always a `MaterialData`; a graph material uses its `flags`/`graphId`/`graphDataOffset` header and leaves the named slots unused). The arena slice is the *only* thing that is optional.

This is the honest version of "no static-vs-graph split": **on disk there is one model; at runtime there is one universal header (`MaterialData`) plus an optional graph slice.** Do not read the vision as "graph materials stop carrying a `MaterialData`" — they still do; it is the tiny stable header the MDI path already indexes.

### C. Node **elimination** vs node **offload** (the core question)

Two jobs wear the word "translate," and separating them answers *"how much do we offload to the graph vs eliminate the need for nodes":*

- **Node elimination** — the importer recognizes a material as the standard shape and writes a `MaterialData` directly. **Zero nodes are ever materialized, nothing is compiled, no arena slice is allocated.** The conceptual graph is *latent* (reconstructable in the editor on demand, §F), never stored as nodes.
- **Node offload** — the material is genuinely procedural (computed surface channels, layering, triplanar, real MaterialX upstream graphs). It lowers to a `MaterialGraph`, is deduped-and-compiled to a generated `evalSurface_N`, and gets an arena slice.

**Conclusion for glTF specifically: glTF import is 100% elimination and produces *no* graphs, ever.** glTF has no procedural-node concept — a glTF material is by construction "factors + plain texture samples + a tangent-space normal map," which is exactly the standard shape (see `glTFLoader.cpp:749-853` `loadMaterial`, which already only ever calls `setParameter`/`loadAndSetTexture` into `MaterialData`). The graph compiler exists for **(a)** in-editor node authoring and **(b)** MaterialX / future procedural imports — *not* for glTF. This is the single biggest simplification: the entire glTF path stays on the static fast lane, joins the one big MDI batch (`GBufferPass.cpp:166`), and touches none of the graph machinery.

So the ratio the vision asked to "find out": for glTF, **all elimination, zero offload.** Offload is a MaterialX-and-editor concern.

### D. The classifier — what "fits `MaterialData`" means (fits-static predicate)

Node elimination needs one predicate: *does this lower to the standard struct?* A material fits `MaterialData` iff every surface channel is one of:

1. a **constant factor** (`albedo`, `metallic`, `roughness`, `ao`, `emissive`, alpha), or
2. a **plain texture sample at the mesh UV** bound to a **named slot** — albedo / normal / metallic-roughness / ao / emissive / height / specular (the slots in `MaterialData.h:102-112` + `PARAM_REGISTRY`, `MaterialParameters.h:49-69`), optionally with a **uniform tiling scale** (`tilingScale`), and a tangent-space normal map.

Supported glTF extensions that still fit (map onto existing/near-future struct fields, no nodes):

- `KHR_materials_emissive_strength` → `emissive.a`.
- `KHR_texture_transform` **when it is a uniform scale/offset** → `tilingScale` (+ a future offset field). A *per-texture* transform that differs across maps does **not** fit → offload.
- `KHR_materials_ior` / `KHR_materials_specular` → the OpenPBR-base fields ([[OpenPBR and Deferred Materials]]) once RT3 lands (Phase 5). Until then, ignored or offloaded.

Anything else — a channel fed by *computed* values, triplanar, vertex-color math, multiple UV sets driving different maps, a KHR extension with no struct home — **fails the predicate and goes to the graph.** The predicate lives with the importer, not the compiler.

### E. Serialization — the disk format

Reuse the already-decided generic Rapture envelope (`.rapt` JSON dev / `.rasset` binary ship, [[Material Graph Compiler]] §7). A material asset serializes as **one of two `kind`s**:

```jsonc
// kind: "standard"  — the eliminated majority (all glTF, most Substance)
{
  "kind": "standard",
  "factors":  { "albedo": [r,g,b,a], "metallic": m, "roughness": r, "ao": a,
                "emissive": [r,g,b,strength], "tilingScale": s },
  "flags":    ["HAS_NORMAL_MAP", "NORMAL_BC5", ...],   // the MaterialData flag bits, by name
  "textures": { "albedo": "<assetRef>", "normal": "<assetRef>", ... }  // named slots
}

// kind: "graph"  — the offloaded procedural minority + anything authored in the editor
{
  "kind": "graph",
  "name": "MossyRock",
  "nodes":       [ { "id": 3, "type": "multiply_vec3",
                     "inputValues": { "1": [0.5,0.5,0.5,0] },   // authored per-input, sparse
                     "textures":    { "0": "<assetRef>" },       // per texture-input pin
                     "editorPos": [x,y] }, ... ],
  "connections": [ { "src": [1,0], "dst": [3,0] }, ... ],
  "outputNode":  7
}
```

Rules that keep this stable across sessions — each verified against a real hazard in the tree:

- **Textures serialize as stable asset references (path / asset UUID), never bindless indices.** Both `MaterialData.texIndices*` and `GraphInstanceData` hold *runtime-assigned* bindless indices that are meaningless next session. The importer already resolves refs → indices (`glTFLoader.cpp:145-162`, `loadAndSetTexture` → `setParameter(id, asset)`); serialization stores the ref, load re-resolves. The `AssetPtr<Texture>` on graph nodes (`MaterialGraph.h:23`) already holds the ref for eviction — serialize *that*, not the index.
- **Node types serialize by string `typeName`, not the `GraphNodeType` enum value** ([[Material Graph Compiler]] §7). Adding/reordering enum values must not break saved files.
- **Nothing compiler-derived is serialized.** No `graphId`, no pool offsets, no `GraphSlotMapping`, no generated GLSL — all recomputed at load (`graphId` is runtime-assigned by `SurfaceGraphManager::registerGraph`, `SurfaceGraphManager.cpp:35`). The disk holds only *authored* data: topology + authored values + texture refs. This is what lets structural dedup (§below) reassign ids freely.
- **Only *touched* input values serialize** (sparse `inputValues`, matching `GraphNode::inputValues` which is already `vector<optional<PinValue>>`, `MaterialGraph.h:22`). Untouched inputs fall to the node def's shared default at compile — no slot, no bytes. This is the same sparsity the arena budget depends on.

### F. Structural dedup — the find-or-compile that the vision hinges on

The vision's "load-bearing constraint" (dedup by structure) is **not yet built**: `SurfaceGraphManager::registerGraph` is append-only — `graphId = m_graphs.size()` (`SurfaceGraphManager.cpp:35`), so N identical Sponza graphs would bake N functions. To land the vision:

- Add a **structural hash** over *topology only* — node `type`s + connection wiring + output node — **excluding** authored values and texture bindings (those are instance data, not structure). `registerGraph` becomes **find-or-compile**: hash → if seen, return the existing `graphId`; else compile + append. Requires the slotted store in [[Material Graph Compiler]] §0b (so ids stay stable when a graph is removed).
- **Standard PBR is the degenerate top of this table:** its structural hash is "graph 0," which resolves not to a generated function but to `evalStaticSurface` + the `MaterialData` layout. The importer short-circuits it (§C elimination) rather than routing it through `registerGraph` at all — but conceptually it is graph 0, hash-bucket 0.
- Result: **Sponza collapses to one `graphId` with many `MaterialData` instances** (via elimination), and a scene of 50 identical procedural rocks collapses to one generated `evalSurface_N` with 50 arena slices (via dedup). Same principle, two implementations — the hand-written one for the universal shape, the generated one for the rest.

### G. Editor round-trip — explode / implode / promote / demote

Because a standard material stores *no nodes* (§C), the editor must synthesize them to show a graph, and collapse them to keep the fast path. This is the promote/demote boundary:

- **Explode (open a standard material):** build the canonical node layout from `MaterialData` via a fixed template — a `texture_sample` per bound named slot → factor `multiply` → `surface_output` channel; unbound slots become authored constants on the sink. No compile, no arena slice; purely a view.
- **Implode (save):** if the graph is *still* the standard shape (passes the §D predicate), re-collapse to a `kind:"standard"` record — stays on the static path. If the user added a procedural node, it **fails** the predicate.
- **Promote (demote is the reverse):** the first non-standard edit flips the material from `kind:"standard"` (no arena slice, `MAT_FLAG_IS_GRAPH` clear) to `kind:"graph"` (register-or-dedup → `graphId`, allocate an arena slice, set the flag). Stripping back to standard shape demotes it. This flag flip is the entire runtime cost of the boundary; `MaterialInstance::setGraph` already does the promote half (`MaterialInstance.h:76`), a `clearGraph` is owed for demote.

### H. What already exists vs what this needs (gap list)

Exists and reusable, verified: the runtime seam (`GBuffer.fs.glsl:80`), the compiler + node registry + arena (`materials/graph/`, `VirtualStorageBuffer`), `MaterialInstance::setGraph` (`MaterialInstance.h:76`), the glTF static importer (`glTFLoader.cpp:749`), sparse per-input authored values (`MaterialGraph.h:22`).

Owed for the unified model, roughly ordered:

1. **`.rapt`/`.rasset` material load+save** (both `kind`s) — the last slice of Phase 2b, currently the only unbuilt piece of the compiler chunk. Textures-as-refs, string typeNames (§E).
2. **Structural dedup / find-or-compile** in `SurfaceGraphManager` + the slotted id store ([[Material Graph Compiler]] §0b, §F). Without it the vision's dedup claim is aspirational.
3. **The fits-static predicate** as a shared classifier (§D), used by both the importer and editor implode.
4. **Editor explode/implode + `clearGraph` demote** (§G).
5. **MaterialX lowering** into `MaterialGraph` (Phase 4) — the *only* real offload source, since glTF is elimination-only.

### I. Converged model (supersedes the hedges in §B, §C, §E)

The whole thing, kept simple:

1. **Everything is a compiled graph.** No hand-written static path, no `evalStaticSurface`, no `MAT_FLAG_IS_GRAPH`. Runtime dispatch is pure `evalSurfaceGraph(graphId, si, base)`.

2. **One shading model, chosen only by the `shadingModelId` enum.** Everything — glTF included — is `SM_OPENPBR_STANDARD` for now. The surface node stamps it; the hand-written BRDF in the lighting pass (`BRDF.glsl`, Phase 1b) is keyed by it. The BRDF is the *only* non-graph piece, and it is not an exception: it is Axis 1 (per-light math in a later pass), downstream of the G-buffer, which a deferred graph cannot express. The graph owns Axis 2 (surface generation) entirely.

3. **External formats are import-only translators → our graph representation.** glTF, MaterialX, etc. translate *in* on load. We never store or re-export glTF. The Rapture material format is the only on-disk form (the "Disk" lifetime above).

4. **Base = graphId (a compiled graph + its parameter schema). Instance = a value slice.** Only instances go on meshes. glTF import creates **instances of one predefined glTF base**; each instance fills in what it has.

5. **Missing textures do *not* make new bases.** "Which textures exist" is instance data — an absent map is just the default texture bound in that slot (1×1 white for albedo/ao/MR, a flat-normal default for normal — the one texture to add). One glTF base, all its instances share it. (Splitting into per-texture-set bases to skip sampling defaults is a micro-opt; not now.)

6. **`MaterialData` → an 8-byte header `{ graphId, graphDataOffset }`.** All input data lives in the arena slice. `flags` evaporates (§C table). Named `setParameter(ALBEDO_MAP, …)` survives — the name→offset table moves from the global `PARAM_REGISTRY` onto the **base's schema** (the graph's named slots); call sites are unchanged, only the offset's source moves.

**Persistence.** Scene save writes the Rapture format: the base graphs in use (built-in bases like the glTF base are referenced by id, not re-serialized) + each instance as `{ base ref, named values, texture asset refs }` — texture *refs* not bindless indices, so they survive a restart (§E). Sponza on disk = one shared glTF base + N tiny instance records.

### J. Surface variants & pass tags

One graph compiles to **several GLSL functions, one per pass, all reading the same instance slice** — bind the material once, a different function parses it per pass:

- `evalSurface_<id>(si, base)` — full sink (albedo, normal, roughness, metallic, ao, + future OpenPBR). The G-buffer path.
- `evalSurfaceDiffuse_<id>(si, base)` — reduced sink (**albedo + emission**, geometric mesh normal, no normal-map). The RT / DDGI / radiance-cascade path.

The reduced variant is nearly free: the compiler runs its backward topo-sort / DCE from the smaller output set, so any node feeding *only* the dropped channels (roughness/metallic/ao) is pruned — fewer texture fetches and ALU in the hit shader, exactly where cost matters. Inputs shrink symmetrically: no normal map → no TBN → the diffuse variant's `SurfaceInputs` is just uv/worldPos/worldNormal, which `ProbeTrace.cs.glsl` already computes.

Each pass has its own generated dispatcher (`evalSurfaceGraph` for the G-buffer; `evalSurfaceGraphDiffuse`, `#include`d by the RT shaders), switching on `graphId`.

**Pass tags** live in the `MaterialData` header (a small bitset — *not* the dead structural `has_x` flags): `diffuseInterp` (default on; off → RT assumes white albedo + mesh normal + no emission), `isTransparent` (→ forward pass), `isTerrain`, etc. Tags decide which variants compile and which pass renders a material — genuine routing metadata, the one legitimate surviving use of per-material flags. Terrain and transparency are just node sets + tags, not special struct layouts.

**RT/DDGI change:** `MeshInfo` (`RtInstanceData.h:20-27`) stops carrying a CPU-flattened albedo / `AlbedoTextureIndex`; it carries `{ graphId, graphDataOffset, tags }`, and the hit shader runs `evalSurfaceGraphDiffuse(graphId, si, base)` itself — real per-node albedo + emission at diffuse cost, replacing the `getParameter(ALBEDO)` flattening in `RtInstanceData.cpp:87-93`. This is the "some level of material interpretation" the GI paths need, just the reduced variant.

### K. The glTF loader — how values get set (concrete)

Almost unchanged from today. A predefined **glTF base** (a compiled graph) publishes a schema naming its exposed params — `albedo`, `roughness`, `metallic`, `albedoMap`, `normalMap`, `metallicRoughnessMap`, `aoMap`, `emissive`, `emissiveMap`, `tilingScale` — i.e. today's `ParameterID` set. The loader (`glTFLoader.cpp:749-853` `loadMaterial`) becomes:

1. `auto base = MaterialManager::getMaterial("glTF");` — the predefined base (was `"PBR"`, `glTFLoader.cpp:774`).
2. `auto inst = std::make_unique<MaterialInstance>(base, name);` — allocates a value slice sized to the base schema, seeded with base defaults (white albedo, and **the default texture in every slot**, incl. a flat-normal default for the normal slot).
3. `inst->setParameter(ParameterID::ALBEDO, color);` / `setParameter(ROUGHNESS, r)` / `loadAndSetTexture(inst, ParameterID::ALBEDO_MAP, texIndex)` — **the exact call sites as today** (`glTFLoader.cpp:800, 845-847`).

The only thing that moved is *inside* `setParameter`: `id` → the base's **schema** → `(offset, type)` in the slice → write → flush to the arena. Texture params resolve the `AssetRef` → bindless index and retain the ref for eviction, as today (`glTFLoader.cpp:145-162`). A glTF material with no normal map simply never calls `setParameter(NORMAL_MAP)`, so that slot keeps the base default = the flat-normal texture — no `has_x` flag, no new base. **The loader reads as it does now; it just writes into a schema-addressed slice instead of named `MaterialData` struct fields.**

---

## Status (as of 2026-07-10)

Phases 0–2 are **done and the engine renders again** (raster + DDGI). Terrain is bridged, not yet reworked. What shipped, and where it diverged from the plan above:

- **Everything is a compiled graph** — the static path (`evalStaticSurface`, the `IS_GRAPH` branch, `SAMPLE_*`, per-channel factors) is gone. G-buffer runs pure `evalSurfaceGraph`, DDGI runs `evalSurfaceGraphDiffuse`.
- **`MaterialData` is a 24-byte header, not the planned 8-byte `{graphId, graphDataOffset}`.** `flags` survived (mesh-attribute presence + BC5 + terrain routing), and the terrain scalars (tiling/height/slope) are carried on it as a temporary carve-out. See [[MaterialData]].
- **The base's schema is a `ParameterID → offset` table**, built by whoever creates the base (the glTF loader for the glTF base; empty for the default). There is no auto-derived schema — §D's shared "fits-static predicate" and §F structural dedup are **not** built.
- **Two bases exist:** `"Default Material"` (a bare `SURFACE_OUTPUT` → Blender defaults, white/0.5/0/1) and `"glTF Base Material"` (a ~20-node graph the glTF loader authors and registers on first import). No per-texture-set bases; a missing map keeps the base default texture in that slot.
- **Missing-normal-map default is a real flat-normal texture** (`Texture::createDefaultFlatNormalTexture`, 1×1 `(0.5,0.5,1)`), bound to the glTF base's normal node so no-normal-map materials shade with the geometric normal. Node 6 is `NORMAL_MAP_RG`, so only R,G matter.
- **Emission is `vec4` (rgb + its own alpha) plus a separate `emissiveStrength` float** — not the planned `emissive.a = strength`.
- **RT/DDGI carries `materialIndex`, not `{graphId, graphDataOffset, tags}`.** `RtInstanceInfo` holds the material's bindless index and the hit shader reads the material SSBO directly — cleaner than the planned flatten. `MeshInfo` in `ProbeTrace.cs.glsl` mirrors it. No pass-tag bitset yet; `diffuseInterp`/`isTransparent`/`isTerrain` are unbuilt.
- **Two generated files:** `generated/SurfaceGraphs.glsl` (`evalSurfaceGraph`) and `generated/SurfaceGraphsDiffuse.glsl` (`evalSurfaceGraphDiffuse`), both regenerated by [[SurfaceGraphManager]]; committed baselines exist so shaders compile before generation.
- **Manager teardown order matters:** `MaterialManager::releaseGraphResources()` (drop graph texture refs) → `AssetManager::shutdown()` (destroy instances, free arena slots) → `MaterialManager::shutdown()` (free buffers). Getting this wrong asserts on `requestUnload`-after-shutdown or a non-empty VMA virtual block.

---

## Phasing

This is a large breaking refactor. The unit of progress is **not** "each commit compiles" — it is "each phase ends at a checkpoint where the engine renders again." Order it so the one destructive core (Phase 1) has a green checkpoint on each side.

**Phases 0–2: done. Phase 3 (terrain): bridged. Phases 4–6: not started.** See Status above.

**Phase 0 — prove the graph can express standard PBR (additive, nothing breaks).** The static path stays alive in parallel. Concrete order:
1. **Nodes.** Add what glTF-MR needs and the registry lacks today: `normal_map` (tangent→world TBN + BC5 reconstruct), the metallic-roughness channel split, emission. If the model can't express PBR, you find out cheaply here.
2. **Base graph + schema extraction (the real first invention).** Hand-author the glTF-MR `MaterialGraph`; tag its param-bearing nodes with stable names (`"albedoMap"`, `"albedo"`, `"roughness"`, …). Compile; walk the `GraphSlotMapping` and record `name → (offset, type)` for each tagged node — that map is the **base schema**. Now `base = { graph, graphId, schema, defaults }`.
3. **Render proof.** Instance it, fill the slice through the schema, put it on a mesh, compare to the static PBR sphere. **Checkpoint** — non-destructive, static path untouched.

**Phase 1 — the big flip (one destructive core; breaks RT + terrain, raster goes green).** `MaterialData` → `{ graphId, graphDataOffset, tags }`; delete `flags`/`texIndices`/factors, `SAMPLE_*`, `evalStaticSurface`, the `IS_GRAPH` branch. G-buffer = pure `evalSurfaceGraph(graphId, si, base)`. `BaseMaterial` gains the schema + `graphId`; `MaterialInstance` stores an arena value slice seeded from base defaults; `set/getParameter` route through the schema. glTF loader makes instances on the glTF base (same call sites). **Checkpoint: deferred/raster renders again.** Because glTF import targets the one predefined glTF base, Sponza is already 1 base + N instances here — no dedup needed yet.

**Phase 2 — surface variants → GI green.** Compiler emits `evalSurfaceDiffuse_<id>` + an `evalSurfaceGraphDiffuse` dispatcher (DCE from the reduced sink). `MeshInfo` → `{ graphId, graphDataOffset, tags }`; RT/DDGI/radiance-cascade hit shaders run the diffuse variant; delete the `RtInstanceData` flattening; add the `diffuseInterp` tag. **Checkpoint: GI works.**

**Phase 3 — terrain as a base → terrain green.** Terrain nodes (splat/triplanar/height-blend/slope) or a predefined terrain base; `isTerrain` tag routes the terrain pass/variant. **Checkpoint: terrain renders.** (Merge into P2 if high-priority; else it stays broken until here.)
> **Partly done (bridge only).** `terrain_gbuffer.fs.glsl` now samples each layer through `evalSurfaceGraph(mat.graphId, …)` on the triplanar UV instead of the removed `SAMPLE_*` macros, and still reads tiling/slope/height from the `MaterialData` header. It **compiles and runs**, but there is no `"Terrain"` base yet (`TerrainGenerator` looks one up and gets null), so terrain layers render with the default-graph fallback until a real terrain base + nodes land.

**Phase 4 — structural dedup / find-or-compile (pure optimization on a working system).** Hash topology → `registerGraph` reuses `graphId`; slotted graph store (holes on remove). Only matters once **distinct authored/imported structures** appear (user graphs, MaterialX) — glTF alone never needs it (all instances of one base).

**Phase 5 — serialization (`.rapt`/`.rasset`).** Save/load bases (graphs) + instances (`{ base ref, values, texture refs }`). glTF stays import-only.

**Phase 6 — real OpenPBR (separate large effort).** Grow `SurfaceData` + `open_pbr_surface` pins + the BRDF + RT3 + the shading-model switch.

Only the P1→P2 gap is knowingly broken (GI dark); you can sit at the P1 checkpoint indefinitely since raster renders.
