# Material Editor Workspace

> Status: **in progress** (design 2026-07-12). Captures the editor-side layer that turns a
> compiled material graph into something authored, previewed, and instanced in the editor. Builds
> directly on [[Project Serialization]] (this is largely its S2 + S6, plus new workspace/preview
> and pipeline-refresh work). Five decisions are locked below; the rest is proposed and refinable.
> Zero-assumption: every claim cites the source it was verified against.
>
> **Done so far:** graph retained on `BaseMaterial`; `loadGraph` implemented (graph → canvas
> reconstruction); `selectMaterial` shows the selected material's base graph; "New" button seeds an
> empty graph; per-type node display names; texture samples shown as `TEXTURE_SAMPLE` nodes with a
> preview (decision 5).

**Related: [[Project Serialization]], [[Material System Overhaul]], [[Material Graph Compiler]], [[Scene]], [[Viewport]]**

## Goal / intent

Make a material graph a first-class editable thing in the editor:
- open a **base material** and draw/edit its node graph on a canvas,
- **preview** it live on a sphere in a self-contained little scene owned by the workspace,
- create and edit **material instances** (named-parameter overrides) of that base,
- keep every place that consumes the generated surface GLSL (g-buffer, terrain, probe trace)
  **safe and up to date** when a graph mutates the GPU material layout.

Most of the plumbing already exists. This doc is mostly wiring + three genuinely new pieces
(base instance, workspace preview scene/context, pipeline refresh).

---

## Locked decisions (this doc)

1. **The base instance is not enforced yet.** Every base material has one default instance seeded
   from the graph defaults so it can be shown on the preview sphere before any user instance
   exists. It is *not* made undeletable/immutable in v1 — if it goes missing we recreate it
   cheaply from `SurfaceGraphManager::getDefaults(graphId)`. Enforcement (a delete-guard) is a
   later refinement only if it proves necessary.
2. **Multiple scenes, no "main scene."** `SceneManager` already owns many named scenes
   (`SceneManager.h:23`, `unordered_map<string, unique_ptr<Scene>>`). We drop the
   `World`/`getMainScene` coupling and pass a `Scene*` explicitly (workspace/viewport), rather than
   pulling a single global active scene. The `World` layer becomes vestigial and can be removed
   once the call sites are converted (enumerated below).
3. **Instance params are edited in two places.** A dedicated instance panel inside the material
   workspace (focused editing with live preview) **and** a compact inline editor in the Properties
   panel when a mesh's material is selected (quick tweaks without opening the workspace).
4. **The authored graph is retained on the `BaseMaterial`, not the manager.** A base owns its source
   graph directly (`getGraph()`); the manager stays a pure compiler over `CompileResult`s. See
   Graph retention below.
5. **Texture samples are shown as `TEXTURE_SAMPLE` nodes with a preview, not un-lowered to Image
   nodes.** An Image node's `Color` output is vec3 while a `TEXTURE_SAMPLE` is vec4; substituting one
   for the other creates vec3/vec4 mismatches wherever the graph uses the full vec4 (e.g. a
   channel-packed metallic-roughness feeding a `SPLIT_VEC4`). The sample node instead keeps its real
   vec4 `out` pin, hides the meaningless number drag on its `tex` (TEXTURE) pin, and shows a texture
   preview; the texture arrives through the `tex` pin. Node display names come from one per-type
   source (`s_nodeDisplayName`), not a menu-catalog search.

---

## What already exists (do not rebuild)

- **Graph → canvas and back.** `buildGraph()` extracts a compiler-ready graph from the canvas
  (`NodeEditorPanel.h`). Its inverse `loadGraph(const MaterialGraph&)` was declared-only; it is now
  implemented (see reconstruction below). The remaining link was *where the source graph comes
  from* — solved by graph retention below.
- **Per-workspace renderer + scene + camera + target.** `Viewport` already owns its own
  `Renderer`, `Scene*`, camera, `RenderSettings`, and `SceneRenderTarget`
  (`Viewport.h:30-88`); `ViewportManager` holds a list with a primary (`ViewportManager.h`). A
  material workspace's private preview is `createViewport(...) + setScene(previewScene)` — **no new
  render infrastructure**.
- **The editor already displays a viewport's output** by registering the render target's image as
  an Amethyst texture and handing it to `ViewportPanel` (`AmethystLayer.cpp:202-227`). A second
  viewport feeds a second `ViewportPanel` the same way.
- **Instances-as-assets, named params, registry-scan material list** — all locked in
  [[Project Serialization]] (`material_instances` section, `MaterialInstanceDescription`,
  `ParameterID`-keyed params).

---

## New / to-build

### 1. Graph retention — **DONE (retention part)**
Today `SurfaceGraphManager::registerGraph` compiles and **discards** the authored graph
(`SurfaceGraphManager.cpp:37-45`; the manager stores only `CompileResult`, `SurfaceGraphManager.h:58`).

**Decision (2026-07-12): the authored graph is retained on the `BaseMaterial`, not the manager.**
A base *is* the thing the editor opens, so it owns its source graph directly; the manager stays a
pure compiler/dispatcher over `CompileResult`s. `BaseMaterial` now stores a `MaterialGraph m_graph`
(a value copy — its `AssetPtr<Texture>` node inputs carry `AssetRef`s, so the copy pins the graph's
textures) and exposes `getGraph()`. `MaterialManager::createMaterial` takes the graph by value and
moves it into the base; both existing callers (`createDefaultMaterials`,
`glTFLoader s_obtainGltfBaseMaterial`) pass their local graph. This is what
`selectMaterial`/`loadGraph` will pull into the canvas.

The `uuid → graphId` map from S2 of [[Project Serialization]] is still needed for
save/load, but is independent of where the graph is stored.

Both authoring origins converge on this one store:
- **Editor-authored** base → its `MaterialGraph` is retained on compile.
- **glTF / translated** base → the loader *already builds a real `MaterialGraph`*
  (`glTFLoader.cpp:807-808`) and registers it under `RE_GLTF_BASE_GRAPH`. Retaining it is the same
  code path. There is **no separate "reconstruct the graph for imported materials" mechanism** —
  the graph already exists; it just needs keeping.

### 1b. Graph → editor reconstruction — **DONE**
`loadGraph` (was declared-only) now rebuilds the canvas from a `MaterialGraph`:
- **1:1 node spawn.** Each graph node spawns as itself (no un-lowering), laid out in columns by
  longest distance from the output (`s_computeLevels`), with authored constant/input values carried
  onto the pins and connections rebuilt through a graph-id → editor-id map.
- **`selectMaterial`** resolves the picked material instance → `getBaseMaterial()` → `getGraph()`
  and calls `loadGraph`, so selecting any material shows its **base** graph.
- **"New"** button in the material bar clears the canvas and drops a bare `SURFACE_OUTPUT` node
  (graph only, not yet registered with `MaterialManager`).
- **Display names** come from one per-type source (`s_nodeDisplayName`), replacing a menu-catalog
  reverse-search that fell back to raw enum spellings (`NORMAL_MAP_RG` → "Normal Map (RG)"). Colors
  from `s_nodeCategoryColor`.
- **Texture samples** (decision 5): a `TEXTURE_SAMPLE` shows its bound texture via a preview reusing
  the extracted `addTexturePreview` helper (stored in the existing `textureData`, no new fields);
  the `tex` pin no longer draws a number widget; the texture round-trips through `buildGraph`
  (whose Image-lowering branch is now gated on `type == NONE` so a real sample is not double-lowered).

### 2. Base instance (default preview instance)
At `BaseMaterial` creation, build one `MaterialInstance` seeded from
`SurfaceGraphManager::getDefaults(graphId)` (method exists, `SurfaceGraphManager.h:41`) + its
texture refs (`getTextureRefs`, `SurfaceGraphManager.h:55`). The preview sphere binds this instance
when no user instance is selected. `BaseMaterial` knows its base-instance handle; a
`ensureBaseInstance()` recreates it from defaults if absent (cheap, per decision 1). No
delete-guard, no immutability flag in v1.

### 3. Duplicate a base
Mechanical and cheap: deep-copy the retained `MaterialGraph`, mint a new UUID, `registerGraph`
→ new `graphId`, build a new `BaseMaterial` + its base instance. Exposed as a **Duplicate** action
in the content browser and the node-editor material bar.

### 4. WorkspaceContext + preview scene
Replace the `dynamic_cast`-scanning scene injection in `AmethystLayer.cpp:156-167` with a context
the `Workspace` owns and passes to each panel it builds:

```cpp
struct WorkspaceContext {
    Rapture::Scene* scene = nullptr;         // optional; the scene this workspace operates on
    Rapture::Viewport* viewport = nullptr;   // the workspace's render viewport (optional)
    Amethyst::DockingLayer* dockingLayer = nullptr;
    PanelServices services;                  // fold the existing PanelServices in
};
```

- `LevelEditorWorkspace` → context points at the level scene + the primary viewport.
- `MaterialEditorWorkspace` → owns a private preview scene
  (`sceneManager.createScene("mat_preview_<uuid>")`: sphere + skybox + light) and its own
  offscreen `Viewport` rendering it. Its `ViewportPanel` shows that viewport's target; swapping the
  previewed instance = re-binding the sphere's material.
- Panels stop reaching for a global "active scene"; they read `context.scene`.

### 5. Scenes: remove the main-scene coupling
`getActiveScene()`/main-scene call sites to convert (verified 2026-07-12):
- `Project.cpp:13-20` — creates `DefaultWorld` + `setActiveWorld`. Becomes: create the level scene
  directly, hand it to the level workspace.
- `Project.h:30,34,38,40` — `getActiveScene` + `createWorld`/`setActiveWorld`/`getActiveWorld`
  passthroughs. Trim to scene ownership.
- `Application.cpp:148`, `EditorLayer.cpp:39`, `AmethystLayer.cpp:156`, `ViewportPanel.cpp:318`,
  `ImportPanel.cpp:145`, `TestLayer.cpp:122,292` — each currently pulls the single global active
  scene; each should instead receive its scene from the workspace/viewport context.
- `World`/`getMainScene` (`World.h:65-74`, `SceneManager.h:121-140`) become removable once the
  above no longer depend on "the active world's main scene."

### 6. Pipeline refresh on material mutation (the OOB-safety piece)
Consumers of the generated surface GLSL — the closed list (verified via `evalSurface` grep):
- `GBuffer.fs.glsl` → `GBufferPass` main pipeline (`GBufferPass.cpp:658-679`, shader is an
  **asset** re-importable from the project shader dir; pipeline built in `createPipeline`).
- `terrain/terrain_gbuffer.fs.glsl` → `GBufferPass` terrain pipeline (`createTerrainPipeline`).
- `ddgi/ProbeTrace.cs.glsl` → DDGI probe-trace compute pipeline.

Refresh chain when a graph is added/changed:
1. `SurfaceGraphManager` recompiles + `writeGeneratedFiles` (regenerates the `.glsl`).
2. Re-import the affected shader assets (they `#include` the generated files).
3. **Recreate** the affected pipelines.

Mechanism: a single deliberate signal — extend `AssetEvents` with `onSurfaceShaderRebuilt`
(`AssetEvents.h` already has `onMaterialChanged`/`onMaterialInstanceChanged` as the pattern). Each
pipeline owner subscribes and rebuilds its pipeline on fire.

**OOB safety:** adding/changing a material can resize/reshape the `GraphInstanceData` /
`MaterialData` SSBO arenas that these shaders index. The layout change and the pipeline rebuild
must not straddle a frame, or a stale pipeline reads the new/short arena → out-of-bounds. So the
rebuild is **synchronous**: `vkDeviceWaitIdle` (or the existing per-frame fence wait) → regenerate
→ recreate pipelines → resume. This is the one place we accept a stall; material edits are an
authoring action, not a hot path.

### 7. Instance param editing (decision 3)
- **MaterialInstancePanel** in the material workspace: lists the selected base's instances, edits
  the picked instance's named params (`ParameterID` → typed value via
  `MaterialInstance::setParameter`, `MaterialInstance.h:37`), previews live on the sphere. Includes
  "new instance" / "duplicate instance."
- **Properties inline**: when a mesh's material component is selected, a compact param editor for
  its instance, plus a **"Open in Material Editor"** button that opens/focuses the material
  workspace on that base. Same button lives in the **content browser** context menu for a material.

---

## Suggested stage order

- **W0 — graph retention + reconstruction. [DONE, except `uuid → graphId`]** Graph retained on
  `BaseMaterial`; `loadGraph`, `selectMaterial`, "New", display names, texture-sample preview all
  in. The `uuid → graphId` map still comes with serialization S2.
- **W1 — WorkspaceContext**: introduce the struct, route scenes through it, delete the
  `dynamic_cast` scene injection. Convert the `getActiveScene` call sites (decision 2). Low-risk,
  unblocks the preview.
- **W2 — material preview**: `MaterialEditorWorkspace` owns a preview scene + offscreen viewport +
  a second `ViewportPanel`; base instance seeded from defaults on the sphere.
- **W3 — pipeline refresh**: `onSurfaceShaderRebuilt` + synchronous rebuild in `GBufferPass`
  (both pipelines) and DDGI probe trace. Verify no validation OOB on a live material edit.
- **W4 — instance editing UI**: MaterialInstancePanel + Properties inline editor + "Open in
  Material Editor" from Properties and the content browser.
- **W5 — authoring niceties**: Duplicate base, new instance/duplicate instance, content-browser
  material actions.

## Open refinement points

- Whether the preview scene is one-per-open-base or a single shared preview scene the workspace
  re-points (lean: one preview scene per `MaterialEditorWorkspace`, swap the sphere's instance).
- Exact `WorkspaceContext` ownership of the `Viewport` (workspace-owned vs `ViewportManager`-owned
  with the workspace holding a raw `Viewport*`). Lean: `ViewportManager` owns, workspace references.
- Whether base-instance recreation needs any user-visible signal, or is silent.
- If/when the base instance needs real delete-protection (decision 1 defers this).
- Debounce policy for W3: a burst of edits shouldn't trigger N synchronous rebuilds — coalesce to
  one rebuild per idle/frame boundary.
