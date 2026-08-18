# Material System Overhaul

**Related: [[Material]], [[MaterialData]], [[MaterialInstance]], [[GBufferPass]], [[LightingPass]], [[OpenPBR and Deferred Materials]], [[Procedural Texture and Shader Editor]], [[Unified Material Graph]]**

The plan for replacing the current static-only material system with a two-path system: an OpenPBR-superset static struct for the common case, and a codegen-based node-graph path for procedural surfaces. Supersedes the interpreter design in `Engine/src/materials/PROCEDURAL_MATERIALS_DESIGN.md` (kept for reference; its interpreter is explicitly rejected below).

---

## 1. Goals and non-goals

**Goals**
- Adopt OpenPBR as the shading model (layered BSDF, richer parameter set) as far as deferred allows.
- A node-graph surface path that can express arbitrary per-pixel computation (noise, triplanar, layer blending) — the thing the current static path fundamentally cannot do.
- Compile graphs to **GLSL** (no runtime interpreter), cached by graph hash, async-compiled so the editor never stalls.
- Load the trivial-surface majority of MaterialX / glTF straight into the static struct; leave real MaterialX graphs to the graph compiler (parser added later).
- Keep MDI: static materials stay one big batch; graph materials add a handful of pipelines.

**Non-goals (for this overhaul)**
- No runtime MaterialX / ShaderGen embedding. MaterialX is a *format and node-library reference*, not a runtime.
- No transparency / transmission / SSS in deferred. Those wait for the future forward+ pass or the RT path.
- No node-editor UI polish. The editor needs new Amethyst widgets; that is a separate, later chunk.

---

## 2. Proposed decisions

These came out of design discussion and are the spine of the doc, until something proves them wrong:

1. **Codegen only, no interpreter.** Graphs compile to GLSL, stitched into the ubershader shell, cached by hash, compiled on a background thread with last-good-pipeline swap. Rationale below in [[#8. Why codegen, not the interpreter]].
2. **Static path stays a fixed fat struct.** `MaterialData` is grown for OpenPBR but stays hand-authored. Anything it cannot express goes through the graph. No data-driven/dynamic static struct.
3. **Two axes, kept orthogonal.**
   - *Axis 1 — shading model* (how light interacts): lives in the deferred lighting pass, hand-written, selected per-pixel by a shading-model ID. Never graph-generated.
   - *Axis 2 — surface generation* (how the G-buffer inputs are computed): the only thing the graph + codegen ever produce. Never touches the BRDF.
4. **Two separate material records.** `MaterialData` (named, static) and `GraphInstanceData` (anonymous pool, graph). A material is one kind or the other, chosen by a flag. The graph pool is never overloaded with static semantics and vice-versa.
5. **One canonical struct + a shading-model ID byte**, not a struct per shading model. Extra models (coat, sheen) reinterpret shared G-buffer channels keyed by the ID (UE's trick), not new structs.
6. **Single ubershader with a `graphID` switch** — all generated graph functions live in one `generated/SurfaceGraphs.glsl`, the shell dispatches by `graphID`. **One pipeline for all graphs.** No per-graph pipeline explosion, so the MDI batch-by-pipeline rework is NOT needed (graphs differ only by `MaterialData`/`GraphInstanceData`). Revisit only if the mega-switch shader ever gets too heavy.
7. **Material + graph data live in one growable SSBO arena** (not per-instance UBOs), indexed by material ID. Reuse the `BufferPool` / `SceneRenderData` infra.

---

## 3. Current state (verified)

- **`MaterialData`** (`Engine/src/materials/MaterialData.h:50-87`): flat 96-byte std140 struct, glTF metallic-roughness. Fields: `albedo` (vec4), `roughness/metallic/ao/flags`, `emissive` (vec4), `texIndices0`/`texIndices1` (two uvec4 = **8 fixed-purpose texture slots**), `tilingScale/heightBlend/slopeThreshold/_pad`.
- **Texture slots are named and fixed** (`MaterialData.h:96-105`): tex0 = albedo/normal/metallicRoughness/ao; tex1 = emissive/height/specular/splatMap. Each holds a bindless index into `u_textures[]`.
- **`MaterialInstance`** (`MaterialInstance.cpp`): one `UniformBuffer` per instance, bindless-indexed into `MATERIAL_UBO` (`DescriptorSet.h:46`, = set 1 binding 0). `setParameter` writes bindless indices into the fixed slots via `PARAM_REGISTRY` offsets (`MaterialParameters.h:57-68`).
- **`MaterialCommon.glsl`** (`Engine/assets/shaders/glsl/common/MaterialCommon.glsl`): the `SAMPLE_*` macros are the ceiling of the static path — sample one named texture at the mesh UV, multiply by a constant. No place to express computed surfaces.
- **G-buffer** (`GBufferPass.cpp:115-125, 483-520`): **3 color targets** + depth/stencil — RT0 `RG16F` oct normal, RT1 `RGBA8_SRGB` albedo, RT2 `RGBA8_UNORM` (metallic, roughness, ao, **1 free byte**), depth `D24S8`.
- **Lighting** (`DeferredLighting.fs.glsl:230-258`): single hardcoded GGX metallic-roughness BRDF, no shading-model branch.
- **MDI batches by arena, not pipeline** (`GBufferPass.cpp:255-264`): `obtainBatch(vboAlloc, iboAlloc, ...)` keys batches by VBO/IBO arena only. A **single** `m_pipeline` is bound once (`GBufferPass.cpp:166`) for all entity batches (+ a separate terrain pipeline). The "batch MDI by pipeline" idea in `Engine/MDI and new material system.md` is **not implemented** — it is the main plumbing this overhaul adds.
- Per-draw data: `ObjectInfo { meshIndex, materialIndex }` (`MDIBatch.h`), `materialIndex = material->getBindlessIndex()` (`GBufferPass.cpp:262`), surfaced to the fragment shader as `inMaterialIndex` (`GBuffer.fs.glsl:17`).

---

## 4. Architecture overview

```
                          ┌─────────────────────────────────────────┐
   Axis 2: surface gen    │   Ubershader shell (GBuffer.fs.glsl)     │
                          │                                          │
  static material ─────►  │   if (IS_GRAPH) evalSurfaceGraph(...)    │  ──► SurfaceData
  graph material  ─────►  │   else          evalStaticSurface(...)   │       (albedo, normal,
                          │                                          │        roughness, metallic,
                          │   write G-buffer + shadingModelID        │        ao, shadingModelID)
                          └─────────────────────────────────────────┘
                                            │  G-buffer (incl. shading-model ID)
                                            ▼
                          ┌─────────────────────────────────────────┐
   Axis 1: shading model  │   Deferred lighting (DeferredLighting)   │
                          │   switch (shadingModelID) { openpbr... } │  ──► lit HDR
                          └─────────────────────────────────────────┘
```

Both material kinds converge on one `SurfaceData` and one G-buffer. The lighting pass only ever sees the G-buffer + the shading-model ID; it never knows or cares whether a static struct or a generated graph produced the pixel.

---

## 5. Axis 1 — OpenPBR shading model

### 5.1 Struct growth
Grow `MaterialData` from 96B to ~160B (still trivial: 160B x hundreds of materials = nothing). Add the OpenPBR base-layer parameters that fit deferred:
- `base_color` (reuse `albedo.rgb`), `base_metalness` (reuse `metallic`)
- `specular_weight`, `specular_color`, `specular_roughness` (split from base roughness), `specular_ior` (drives F0)
- `base_diffuse_roughness` (optional; Oren-Nayar / EON diffuse)
- `emission_luminance` + `emission_color` (reuse `emissive`)

Keep `MaterialCommon.glsl`'s struct in lockstep (the `static_assert` at `MaterialData.h:89` guards size — update it).

### 5.2 Shading-model ID (land from day one)
Add a `shadingModelID` byte written into the G-buffer even while there is only one model. Cheap forward-compat: adding coat/sheen later is a new enum value + a reinterpreted channel, not a G-buffer redesign.

Enum (CAPITAL_CASE per project style): `SM_UNLIT`, `SM_OPENPBR_STANDARD`, later `SM_OPENPBR_COAT`, `SM_OPENPBR_SHEEN`, ...

### 5.3 BRDF rewrite
Replace the single GGX in `DeferredLighting.fs.glsl:230-258` with an OpenPBR-standard eval (OpenPBR Fresnel, energy compensation, dielectric specular from `specular_ior`), dispatched on `shadingModelID`. Structure the lighting as `evalBRDF(SurfaceData surf, LightData light)` with a `switch` so extra models slot in cleanly.

### 5.4 G-buffer change (concrete, files touched)
Current 3 targets have one free byte in RT2. `shadingModelID` fits there for now — **OpenPBR-base needs no new target**, only the free byte. Add RT3 only when coat/anisotropy/spec-color demands it. When RT3 is needed, the touch-list is:
- `GBufferPass.cpp:115-125` `getFramebufferSpecification()` — push RT3 format.
- `GBufferPass.cpp:483-520` `createTextures()` — new per-frame texture.
- `GBufferPass.cpp:378-432` `beginDynamicRendering()` — `m_colorAttachmentInfo[3]`, `colorAttachmentCount = 4`.
- `GBufferPass.cpp:440-481` barrier arrays `[4] → [5]`, and the terrain pipeline mirror.
- `GBufferPass.cpp:607-625, 744-756` `colorBlending.attachmentCount = 4` in **both** pipelines (main + terrain).
- `GBuffer.fs.glsl` new `layout(location = 3) out`; `DeferredLighting.fs.glsl` new sampler handle + push-constant.

Emission goes straight into the HDR lighting target (additive), not a G-buffer channel.

---

## 6. Axis 2 — surface graph + codegen

### 6.1 Two records
- **`MaterialData`** — named slots, static path only (Section 5). A graph material leaves the named texture slots unused; it *computes* albedo/normal/etc.
- **`GraphInstanceData`** — anonymous pool for graph materials:

```cpp
// starter: fixed-array version
struct GraphInstanceData {
    uint graphID;                          // which generated evalSurface_* to call
    uint textures[GRAPH_MAX_TEXTURES];     // compiler-assigned generic slots (start = 16)
    glm::vec4 constants[GRAPH_MAX_CONSTS];  // constants + exposed params (start = 16)
};
```

Slots have **no** semantics — the compiler assigns `noise → textures[1]`, `mossColor → constants[0]`, etc., and records the mapping so the editor knows where to write each node's value.

### 6.2 The offset indirection
The draw already carries `inMaterialIndex → u_materials[idx]` (a `MaterialData`). For a graph material, `MaterialData` stores where its slice lives:

```
draw → inMaterialIndex → u_materials[idx]  (MaterialData)
                             │ if MAT_FLAG_IS_GRAPH:
                             ├─ graphID          (which function)
                             └─ graphDataOffset  (base into u_graphData[])
                                      ▼
                         u_graphData[graphDataOffset + k]   (this instance's slice)
```

- **Relative offsets (k = 0,1,2…)** are baked into the generated GLSL (compile-time, per graph *type*).
- **Base offset `graphDataOffset`** is per-instance runtime data, stored in `MaterialData` (add explicit `uint graphID; uint graphDataOffset;` fields when growing the struct — self-documenting, avoids reusing named slots).

**Fixed-array vs SSBO-slice:**
- *Option A (start here):* fixed `GraphInstanceData[16/16]` array. No byte offset needed — a `graphInstanceIndex` indexes it by element. Limit = 16, global. Simplest.
- *Option B (migration when hit):* variable-length slice in one shared SSBO arena (reuse `BufferPool` / `SceneRenderData` infra). Store explicit `graphDataOffset`; each graph declares its own footprint; limit ≈ buffer size (gone). Because layout is *data* decoupled from generated logic, this migration is a buffer-layout change, not a shader rewrite.

Start A, treat hitting 16 as the signal to move to B.

**Update: Option B is now implemented.** The pool is a flat `uint` arena sub-allocated in 32-byte blocks by a `VirtualStorageBuffer` (VMA virtual block), and `MaterialData` stores `graphDataOffset` (a uint base offset). Option A's fixed `vec4[16]/uint[16]` struct is gone. Details in [[Material Graph Compiler]] §0a.

### 6.3 Graph compiler
**Full design: [[Material Graph Compiler]].** `Engine/src/materials/graph/` (new, snake_case folder):
- `MaterialGraph` — typed nodes + connections + exposed params (the `PROCEDURAL_MATERIALS_DESIGN.md` data structures are a fine starting point; drop everything bytecode-related).
- `MaterialGraphCompiler` — topo-sort → emit one `evalSurface_<name>(SurfaceInputs, GraphInstanceData, out SurfaceData)` GLSL function. First pass assigns texture/constant pool slots; second pass emits straight-line GLSL. Constant folding / DCE / CSE are free from the downstream GLSL compiler (add later if wanted).
- Data-driven node registry (`NodeDefinition` + `glslTemplate`): adding a node is a data entry, not a compiler edit. See the compiler doc for the placeholder language and coercion rules.
- Output: all functions into `generated/SurfaceGraphs.glsl` + a `evalSurfaceGraph(graphID, ...)` dispatcher `switch`. No bytecode, no register file.
- Serialization: the generic Rapture asset format (`.rapt` readable JSON / `.rasset` binary), not a bespoke per-type extension.

### 6.4 Codegen example
Graph "MossyRock" (rock with noise-driven moss) compiles to:

```glsl
// generated/SurfaceGraphs.glsl  (DO NOT EDIT)
void evalSurface_MossyRock(SurfaceInputs si, GraphInstanceData g, out SurfaceData surf) {
    vec3  _n0 = texture(u_textures[g.textures[0]], si.uv).rgb;                    // rock
    float _n1 = texture(u_textures[g.textures[1]], si.uv * g.constants[1].x).r;  // noise
    vec3  _n2 = mix(_n0, g.constants[0].rgb, _n1);                               // moss blend
    surf.albedo         = _n2;
    surf.normal         = si.worldNormal;
    surf.roughness      = mix(0.9, 0.4, _n1);
    surf.metallic       = 0.0;
    surf.shadingModelID = SM_OPENPBR_STANDARD;
}
```

Inserted into the shell:

```glsl
// GBuffer.fs.glsl
#include "common/MaterialCommon.glsl"
#include "generated/SurfaceGraphs.glsl"

void main() {
    MaterialData mat = u_materials[inMaterialIndex].data;
    SurfaceData surf;
    if (matHasFlag(mat.flags, MAT_FLAG_IS_GRAPH)) {
        GraphInstanceData g = fetchGraphInstance(mat.graphDataOffset);
        evalSurfaceGraph(mat.graphID, buildSurfaceInputs(), g, surf);
    } else {
        surf = evalStaticSurface(mat, u_textures, inTexCoord);   // today's SAMPLE_* path
    }
    gNormal     = octEncodeNormal(surf.normal);
    gAlbedoSpec = vec4(surf.albedo, 1.0);
    gMaterial   = vec4(surf.metallic, surf.roughness, surf.ao, packShadingModel(surf.shadingModelID));
}
```

### 6.5 Async compile + last-good swap
- glslang **must not** run on a job-system fiber (overflows the 64KB fiber stack — see [[no-glslang-on-fibers]]). Compile on a dedicated background thread.
- On graph edit: debounce → background compile the new pipeline → keep rendering the last-good pipeline until ready → atomic swap. Viewport never stalls.

---

## 7. MDI stays as-is (single-ubershader decision)

**Decided: single ubershader with a `graphID` switch → one pipeline for all materials.** All generated graph functions live in one `generated/SurfaceGraphs.glsl`; the shell `switch`es on `graphID`. Graph materials differ from each other and from static materials **only by data** (`MaterialData` + `GraphInstanceData`), so:

- The current MDI batching (by VBO/IBO arena, single pipeline bound once at `GBufferPass.cpp:166`) **is untouched.** No batch-key change, no moving the pipeline bind into the loop.
- The "batch MDI by pipeline" idea in `Engine/MDI and new material system.md` stays unbuilt — we don't need it.

The only cost is a fatter fragment shader (the mega-`switch` over all graphs). Acceptable; the driver keeps the taken case uniform per draw since `graphID` is per-material-constant. Revisit (splitting into per-graph pipelines + the batching rework) only if that shader's register pressure / compile time becomes a measured problem.

---

## 8. Why codegen, not the interpreter

The `PROCEDURAL_MATERIALS_DESIGN.md` per-pixel bytecode interpreter is rejected:
- Dynamic register indexing (`regs[dynIdx]` over 8 vec4, 3 reads/op, in a loop) spills to scratch or expands to cndmask ladders — a real per-pixel perf cliff.
- Hard 16-op / 6-const / 4-tex ceiling breaks on the third serious material (triplanar + noise + layering > 16 ops).
- Dual backends (bytecode encoder + GLSL emitter) must stay bit-identical or live preview lies.

Codegen: unlimited instruction count (just GLSL lines), full driver optimization, one backend. "Live editing without recompile" is solved by async compile + last-good swap instead. This is EEVEE's model; the interpreter is Cycles' SVM (offline-only). Our bindless textures remove EEVEE-legacy's sampler-count wall, so our practical ceiling is far above any real material.

---

## 9. MaterialX and glTF import

- **Trivial surface (90% case):** a MaterialX doc whose surface is a single `standard_surface` / `open_pbr_surface` with constant + plain-texture inputs, and all glTF — lower 1:1 into `MaterialData`. No graph.
- **Real graph:** upstream nodes feeding the surface node → lower into `MaterialGraph` → compiler. MaterialX's surface node maps to our shading-model ID; its upstream graph maps to our surface-gen graph — the same two-axis split, which is why the cut is clean.
- Parser is a later phase; design `MaterialGraph` node types against the MaterialX standard node library so lowering is mechanical.

---

## 10. Editor (node editor) — in progress

The canvas skeleton (nodes, pins, drag-connect wires, pan) and the per-node control layer (typed-variant dropdown) are built — see **Phase 3a / 3b** in section 11 for the detail (`Editor/src/layers/panels/NodeEditorPanel.{h,cpp}`, `MaterialEditorWorkspace.{h,cpp}`). Done with existing Amethyst widgets (`Frame`, `Shape`, `Spline`, `ContextMenu`, `Dropdown`, `UIDragDetector`) plus one Amethyst fix (middle-mouse delivery). Still to come: inline default-value editors, hooking the canvas to the `MaterialGraph` IR + compile, background grid, zoom. See [[Procedural Texture and Shader Editor]]. Depends on the `MaterialGraph` IR (exists, Phase 2b).

---

## 11. Phased sequencing

**Phase 0 — material-system plumbing (behavior-preserving).** Do this first because both OpenPBR and codegen sit on it, and doing it now avoids growing/reworking the struct twice. Two behavior-preserving refactors (acceptance test = "renders identically"):
- **0a — SSBO migration. [DONE]** Replaced the per-instance `UniformBuffer` + bindless-UBO-array with one `MaterialData` SSBO indexed by material ID, via a new generic `FreeListStorageBuffer` (`Engine/src/buffers/FreeListStorageBuffer.{h,cpp}`) — fixed-capacity SSBO of uniform-size elements, free-list `allocate`/`free`/`write`, self-registers into a descriptor binding, `unique_ptr`-owned (no `shared_ptr`). `MaterialManager` owns the arena (`MAX_MATERIALS = 4096`); `MaterialInstance` holds a slot. Descriptor set 1 binding is now `STORAGE_BUFFER × 1` (`MATERIAL_DATA_SSBO`). Shared GLSL block + `getMaterialData(index)` accessor centralized in `MaterialCommon.glsl`; `GBuffer.fs`/`terrain_gbuffer.fs` call it. Reuse target for the graph pool (`GraphInstanceData`).
- **0b — SurfaceData shell split. [DONE]** G-buffer write refactored into `evalStaticSurface(si, mat) → SurfaceData` (`GBuffer.fs.glsl`); `main` fills a `SurfaceInputs`, calls it, writes the targets. Shared `SurfaceInputs`/`SurfaceData` contract in `MaterialCommon.glsl`. No visual change; the `if (IS_GRAPH)` seam is now a one-line branch.

**Phase 1 — OpenPBR-base.**
- **1a — shading-model id plumbing. [DONE]** `shadingModelId` written into RT2's free `.a` byte end-to-end (both G-buffer shaders), carried on `SurfaceData`. `SM_UNLIT`/`SM_OPENPBR_STANDARD` enum + pack/unpack live in a shared `common/ShadingModels.glsl`. No new render target (RT3 stays Phase 5). Behavior-preserving (lighting ignored `.a`).
- **1b — BRDF rewrite + lighting-shader split. [DONE]** Extracted the BRDF into `common/BRDF.glsl` (height-correlated Smith visibility replacing the separable `k=(r+1)²/8` term, GGX, Schlick Fresnel, energy-conserving diffuse) and tone mapping into `common/Tonemapping.glsl`. `DeferredLighting.fs` now unpacks `shadingModelId` and shades via `evalBRDF(id, ...)` — the shading-model switch is live. Unified the lighting shader's include path to `glsl/` (ddgi includes now `ddgi/…`). Slightly different (more correct) specular — the intended visual payoff.
  - **Deferred:** multi-scatter energy compensation (wants a precomputed DFG/BRDF-integration LUT — a small future compute pass); OpenPBR specular richness (ior/color/weight, split `specular_roughness`) waits for RT3 in Phase 5.

**Phase 2 — surface graph, codegen only.** Split into:
- **2a — plumbing + hand-written graph. [DONE, proven on a lit sphere]** `GraphInstanceData` (16 tex / 16 const generic pool, `alignas(16)`, 320 B) in its own `FreeListStorageBuffer` at `GRAPH_DATA_SSBO` (set 1/binding 1). `MaterialData` → 112 B (`alignas(16)`) with `MAT_FLAG_IS_GRAPH` + `graphId` + `graphInstanceIndex`. `MaterialInstance::setGraph(graphId, data)` allocates a graph slot, uploads the pool, flags the material. `GBuffer.fs` branches `IS_GRAPH ? evalSurfaceGraph(graphId, si, gii) : evalStaticSurface(...)`; hand-written graph 0 + dispatcher in `generated/SurfaceGraphs.glsl`. Dispatch / index / pool upload all verified.
- **2b — DONE (pending build), except JSON serialization. Full design: [[Material Graph Compiler]].** Compiler (`Engine/src/materials/graph/`): `MaterialGraphTypes` (`PinType`/`ResourceKind`/`PinDef`/`NodeDefinition`), `NodeRegistry` (data-driven, `registerBuiltins` starter set, adding a node = a `registerNode` data entry), `MaterialGraph` IR (`GraphNode`/`GraphConnection`), `MaterialGraphCompiler::compile` (topo-sort → resource-slot pass → emit pass with template substitution + type coercion → wrap + sink, split into `s_topoSort`/`s_assignResources`/`s_emitSurfaceBody`, `MATERIAL_GRAPH_COMPILER_VERSION` stamped in output). `SurfaceGraphManager` (plain instance class, **owned by `MaterialManager`** via `s_surfaceGraphManager` + `getSurfaceGraphManager()` ref, NOT a singleton) registers graphs → graphId and emits `generated/SurfaceGraphs.glsl` with a `@`-style version banner. `TestLayer` authors the `fract(position*scale)*tint` graph, registers it, writes the file, seeds the sphere from `getDefaults` — the graph-0 flip that replaces the hand-written `evalSurface_Test`. Note: the running G-buffer shader only reloads on a viewport resize for now (clean gbuffer re-eval is 2c). **Left: `.rapt`/`.rasset` JSON load/save.**
- **2c — async compile + last-good swap** (background thread, not a fiber; debounce; keep last-good pipeline until the new one is ready).

**Phase 3 — node editor.** Amethyst node canvas; live compile on edit.
- **3a — canvas skeleton + nodes + connections. [DONE, pending build]** `NodeEditorPanel` (`Editor/src/layers/panels/NodeEditorPanel.{h,cpp}`) hosted by `MaterialEditorWorkspace` (`.h/.cpp`, single docked panel). Pieces:
  - **Add menu.** Right-click the canvas background opens a categorised `ContextMenu` (nested submenus) built from an editor-side catalog that maps `GraphNodeType`s into `Input / Utilities(Math{Float,Integer,Vector}, Vector) / Geometry / Color / Output`. Geometry holds the readers (position/normal/tangent/bitangent/uv) + normal map; constants are an `Input > Constant` submenu; texture-sample parked under Input until the Texture category (default assets) lands.
  - **Node visual.** Header coloured by menu category (`s_categoryColor`); pins pulled straight from `NodeRegistry::get(type)`'s `NodeDefinition` — outputs first (socket right, right-aligned label), then inputs (socket left) top-down. Sockets are `Shape(PRIMITIVE_CIRCLE)` coloured per `PinType` (`s_pinColor`) with a dark border; node has a 2px outward `OUTLINE` border; header crimped inside it. Dynamic height, fixed width. `NodeRegistry::registerBuiltins()` called in the ctor (idempotent).
  - **Virtual canvas.** `m_content` is a size-0, non-clipping transform anchor under the clipping `m_canvas` viewport; nodes/wires are its children in graph space. **Pan** = middle-drag → one write to `m_content` offset (O(1), no per-node updates); needed an Amethyst fix: `window.cpp` `onMouseButton` was dropping middle mouse — added `MOUSE_BUTTON_3 → InputType::MOUSE_BUTTON_3`.
  - **Drag nodes.** `UIDragDetector` per node (`DragMode::FREE`); header/title `propagate` so the whole node is a drag handle; sockets consume clicks so they don't start a node drag.
  - **Connections.** Pin registry `m_pins` is a generic `Rapture::FreeList<PinView>` (`Engine/src/utils/FreeList.h`) — stable, reusable slot ids so a node's pins can be rebuilt without shifting any other node's ids (each `PinView`: socket, label, node id, direction, slot index, `PinType`, analytic `localOffset`). Press a socket → capture-drag a rubber-band, highlight a valid drop target, validate-on-release. Drop is gated by `canConnect`: opposite direction, different node, and **same `PinType`** — the editor does not allow cross-type wires (no coercion; the compiler's coercion table is not exposed here). Wires are `LINEAR` `Spline`s (`showKnots=false`) in a `m_wireLayer` under the nodes, coloured by the output pin's type; inputs hold ≤1 wire, outputs fan out. `refreshNodeWires` re-points a node's wires on drag; pan needs zero wire updates (children of `m_content`).
- **3b — control layer + typed-variant dropdown. [DONE, pending build]** Grouped nodes (the math/utility ops) render one control row directly under their outputs, at the plain `NODE_ROW_HEIGHT` (color/texture pickers that want a taller row are a later exception). Its `Amethyst::Dropdown` swaps the node between typed variants (`Multiply → Float/Integer/Vector`, etc.) in place via `changeNodeType`: the frame/header/dropdown persist and only the **pins** are rebuilt (`clearNodePins` + `layoutPins`), so no tick-deferral is needed. Wires touching the node are re-pointed to the rebuilt pins when the same-side slot survives with the **same type**, else dropped — a Mix `Float→Vector` keeps its scalar `t` wire and drops `a`/`b`. The variant catalogue is an editor-side table (`s_variantGroups`); the add-menu categories are unchanged, the dropdown is convenience on top. Constants are deliberately **not** grouped (no inputs to preserve, type is the node — they get an inline value editor instead). Still deferred: clamp-style **checkboxes** (need an engine template/variant to back them).
  - **Deferred (roughly in order):**
    1. **Inline input editors.** For an **unconnected** input with no upstream node, show an inline editor for its default: a **drag** widget for a scalar (`float`/`int`), **hidden when the pin is connected**. Vectors are trickier — they need their own row(s) with an editor and no visible pin for the sub-components (a pinless row per component, or a compound widget), so they cost body height with nothing on the socket side. Feeds the pin's `defaultValue`.
    2. **Compile hookup.** Wire the canvas to the real `MaterialGraph` IR (build nodes/connections from edits) → `MaterialGraphCompiler` → compile/apply to the material (on-demand first, live-on-edit after). This is the payoff that makes the editor real.
    3. **Nice background.** A grid/dot canvas backdrop behind the content layer (pans/scales with it).
    4. **Zoom.** (Deprioritised.) Pending Amethyst: a `contentScale` on `UIBase2D`, applied at `pushData`/glyph emission + pointer inverse for hit-testing (decided, not built). Text sharpness handled separately (quad-scale now, snap-reshape/MSDF later).
    - Also: curved wires (flip `CurveType` + tangent knots); detach-on-drag from a connected input; pin/wire cleanup on node **deletion** (the `FreeList` makes pin removal clean).

**Phase 4 — MaterialX / glTF importer.** Trivial-surface lowering first, then graph lowering.

**Phase 5 — deferred extras behind the ID.** RT3 + coat/sheen/anisotropy as new shading-model IDs + reinterpreted channels. Transmission/SSS wait for forward+ / RT.

---

## 12. Open questions / risks

- **Mega-switch shader cost** (Section 7). Single-ubershader is decided; the thing to watch is the switch's register pressure / compile time as graph count grows. Revisit only if measured.
- **Normal handling in graphs** — tangent-space vs world-space output, and the TBN build (`GBuffer.fs.glsl:48-72`) must be exposed to `SurfaceInputs`.
- **Exposed-param editing without recompile** — constants live in `GraphInstanceData`, so slider drags are data writes (no recompile); only topology changes recompile. Confirm the editor distinguishes the two.
- **Terrain path** (`terrain_gbuffer.*`) shares the G-buffer layout — any RT3 / shadingModelID change must mirror into the terrain pipeline and shaders.
```
