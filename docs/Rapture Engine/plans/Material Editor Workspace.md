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
> preview (decision 5); **W1 WorkspaceContext across all workspaces**; **viewport display decoupled**
> so each `ViewportPanel` drives its own `m_context.viewport` (per-panel register/resize, 1-per-panel
> lock); **W2 preview scene live** — `MaterialEditorWorkspace` owns a `MaterialPreview` scene (sphere +
> cubemap skybox + directional light) and its own offscreen viewport; **multi-active-scene refactor**
> (`SceneManager` active *set*, `activate/deactivateScene`, `getActiveScene` removed); **`EditorLayer`
> per-viewport** camera+controller. Next: base instance on the sphere (W2 remainder), then pipeline
> refresh (W3).

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
2. **Multiple scenes, no single "active scene." — DONE.** `SceneManager` now holds an active *set*
   (`std::vector<Scene*> m_activeScenes`) instead of one `m_activeScene`. `setActiveScene` →
   `activateScene`/`deactivateScene` (idempotent membership; `destroyScene` auto-deactivates);
   `getActiveScene()` is **removed** entirely (Project + SceneManager); `getActiveScenes()` +
   `isSceneActive()` added. The main loop (`Application.cpp:148`) pumps `onUpdate` for **every** active
   scene and no longer binds a scene to the primary viewport (that moved to `ViewportPanel`, below).
   `createScene` now names the `Scene` (was always "Untitled Scene"); the level scene's name lives in
   the `RAPTURE_DEFAULT_SCENE_NAME` macro (`SceneManager.h`) so listeners can filter by it. The `World`
   layer is untouched for now (still vestigial). Now split cleanly: an **active set** = scenes pumped +
   renderable; **editor focus** (which scene the Outliner/Properties act on) is carried per-panel via
   `WorkspaceContext.scene`, not a global.
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

### 4. WorkspaceContext + viewport decoupling — **DONE**
`WorkspaceContext` lives in `Editor/src/layers/panels/common.h` (which also absorbed `PanelServices`;
the old `PanelServices.h` was deleted):

```cpp
struct WorkspaceContext {
    Rapture::Scene* scene = nullptr;
    Rapture::Viewport* viewport = nullptr;
    Amethyst::DockingLayer* dockingLayer = nullptr;
    PanelServices services;
};
```

- **Workspaces create the context, panels receive it.** Workspace ctors still take `PanelServices`
  (and the level one a `Scene*` + `Viewport*`); each sets `m_context.services`, `setupBase` fills
  `m_context.dockingLayer`, and panels are built with `m_context`. `Panel` extracts `m_services` from
  the context so panel bodies are unchanged. The `ContentBrowser` popup ctor and `BottomBar` keep raw
  `PanelServices` (not launched from a workspace).
- **The `dynamic_cast` scene injection is gone.** `OutlinerPanel`/`PropertiesPanel` call
  `setScene(context.scene)` in their ctors. `AmethystLayer` passes the active scene + primary viewport
  to `LevelEditorWorkspace`; the material workspace will make its own (next step).
- **Each `ViewportPanel` drives its own viewport.** It reads `m_context.viewport` (no more
  `getPrimaryViewport()`), and owns its display in `updateViewportImage()`: pulls the viewport's
  render-target texture for the last-rendered slot, registers/unregisters via a new
  `services.unregisterTexture` with a per-slot `{AmTextureId, Texture*}` cache, and resizes its own
  viewport (debounced). `AmethystLayer`'s single-viewport feed block + `m_viewportTextureIds/Views`
  and their lifecycle were deleted.
- **1-per-panel lock:** `Viewport::EditorBinding` gained a `displayed` flag; a `ViewportPanel` asserts
  the viewport isn't already displayed, claims it on construct, releases on destroy — two panels can't
  drive the same viewport.

**DONE:** `MaterialEditorWorkspace::setupPreviewScene` creates its `MaterialPreview` scene via
`SceneManager` (holds a non-owning `Scene*`), populates it (sphere via `createSphere`, camera entity
set as main camera, directional light, `default.cubemap` skybox on the environment entity),
`activateScene`s it so the main loop pumps it, creates its own offscreen `Viewport` via
`ViewportManager` (`createRenderer(DEFERRED)`, `setScene`/`setCamera`), sets `m_context.scene`/`viewport`,
and adds a second `ViewportPanel`. Teardown = `destroyViewport` + `destroyScene` after `m_panels.clear()`.
DDGI is off on the preview viewport (`RenderSettings` `RENDER_USE_GLOBAL_ILLUMINATION` cleared).

### 5. Scenes: remove the single-active-scene coupling — **DONE (World kept)**
`getActiveScene()` is gone; every call site converted:
- `Application.cpp:148` — pumps `onUpdate` over `getActiveScenes()`; the `primaryViewport->setScene`
  special-case removed.
- `ViewportPanel` — binds `context.scene → its viewport` in the ctor (the binding that left the main
  loop); the gizmo reads `m_viewport->getScene()` (not a global) so a preview viewport's gizmo uses the
  preview scene.
- `EditorLayer` — rewritten **per-viewport**: `unordered_map<Viewport*, {Entity camera, unique_ptr<CameraController>}>`;
  `syncViewportControls()` lazily creates a camera+controller for each viewport with a scene (reusing an
  existing `viewport->getCamera()` if set — so the preview keeps its own camera), prunes controls for
  destroyed viewports, and drives only the hovered/capturing one. No more single `getPrimaryViewport()`
  camera that got hijacked when a second scene activated.
- `TestLayer` — bootstraps only `RAPTURE_DEFAULT_SCENE_NAME` (via `getScene(...)` in `onAttach`, and its
  `onSceneActivated` listener early-returns for other scenes), so activating the preview scene never
  dumps the level into it. `AmethystLayer` hands `LevelEditorWorkspace` the `DefaultScene` explicitly.
- `ImportPanel:145` is dead-commented, left as-is.
- `Project`/`World`/`getMainScene` kept (vestigial); only the single-active accessor was removed.

**Descriptor sizing (SCUFFED TEMP).** A second active scene exposed that the per-scene bindless arrays
in `DescriptorManager.cpp` (`CAMERA/LIGHT/SHADOW_DATA/MESH_DATA` SSBOs + `PROBE_VOLUME_DATA` UBO) were
sized to `3` = one scene's frames-in-flight, so scene #2 got "No free slots" and rendered black. Bumped
`3 → 6` (fits exactly 2 scenes) with `SCUFFED TEMP` comments — the real fix is sizing per active-scene
count. Pool sizes (`s_maxStorageBuffers` etc.) have ample headroom.

**Preview lighting (open).** The engine has **no IBL**, so the `default.cubemap` skybox is a backdrop
only — it does not light the sphere. With DDGI off the sphere is lit by the directional light + the
hardcoded `vec3(0.03)` ambient (`PBR.fs.glsl:207`; `IndirectLightingComponent`'s `AmbientSettings` is the
intended flat-ambient lever, `LightingPass.cpp:160` falls back to it when GI is absent). Making the
shadow side read soft without IBL = bump that ambient or add fill lights (studio rig). Not yet decided.

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
- **W1 — WorkspaceContext. [DONE]** Struct in `common.h` (absorbed `PanelServices`); workspaces build
  it, panels receive it; `dynamic_cast` scene injection deleted; viewport display decoupled per panel
  (`services.unregisterTexture`, per-slot cache, 1-per-panel `displayed` lock). The `getActiveScene`
  call-site cleanup that was deferred here is now **done** as part of W2 (decision 2 / section 5).
- **W2 — material preview. [preview scene DONE; base instance TODO]** `MaterialEditorWorkspace` owns a
  `MaterialPreview` scene + offscreen viewport + a second `ViewportPanel` (sphere, cubemap skybox,
  directional light, DDGI off). Came with the multi-active-scene refactor (`SceneManager` active set,
  `EditorLayer` per-viewport, descriptor bump) and the single-active-scene call-site cleanup that W1
  had deferred. Remaining: seed the base instance from defaults onto the sphere; settle preview lighting
  (no IBL — ambient/fill decision above).
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
