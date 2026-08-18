# Editor UX Polish Roadmap

Snapshot 2026-08-13, replacing the 2026-07-04 version. Phase 1 of [[Asset & Editor Roadmap]] is "editor / UI work" — this is what that actually means now.

Guiding principle is unchanged: make the current loop feel finished before building anything new.

**Next up: #1, thumbnails.** #5 (editor state persistence) and #4 (workspaces on demand) landed 2026-08-13.

## Corrections to the 2026-07-04 snapshot

The old doc's first three items are largely done and its claims no longer hold:

- **Outliner context menu (old #1) — DONE.** `OutlinerPanel.cpp` has a `m_contextMenu`, a `m_renameInput`, and visibility filtering through `VisibilityComponent::inOutliner`.
- **TLAS-in-import (old #3) — DONE.** The old doc quoted a `scene->buildTLAS()` hack in `ImportPanel.cpp`. There is no TLAS reference anywhere in `Editor/` any more. The import panel is also no longer a yes/no dialog: it has an output-name input and a "place in its own subfolder" checkbox (`ImportPanel.cpp:142-175`). Only the `AssetImportConfig.h` options half is still open, and it is now bottom of the list.
- **Jolt physics (old #4) — DONE**, see [[project_physics_jolt]].
- **Asset deletion / eviction (old "deletion is its own design thread") — DONE**, see [[project_asset_eviction]].

Everything below is verified against the source at the time of writing.

---

## 1. Thumbnails

Highest value. **An attempt was already started and stopped midway**, see [[project_thumbnails]] for what landed (`ViewportConfig`/`RendererConfig`, `allowReadback` on offscreen targets, `enableAccelerationStructures` gating) versus what was never begun. Justification is in [[Asset Metadata]]: Sponza's content browser is 103 entries named `Sponza_Node_Primitive_0..102`, so for unnamed glTF content a thumbnail is the *only* affordance — no text search can find the curtain.

**The consumer side already exists.** `AssetContextMenuAID` carries a `thumbnail` field and `AssetContextMenuAIV::bind` (`context_menus.cpp:96-101`) already branches on it, falling back to the type SVG when it is `AM_INVALID_TEXTURE`. Picker and dropdown rows light up the moment something produces the texture. What is missing is only the producer.

### Reuse `DeferredRenderer`, do not write a thumbnail renderer

A dedicated lightweight renderer was considered on the grounds that the deferred path is heavy for a 64px image, with `SceneQueryRenderer` as the precedent. That precedent does not carry: `SceneQueryRenderer` is 439 lines *because it does no shading*, writing entity id and depth into an SSBO with no materials, lights or IBL. A thumbnail must be shaded, and shading is split across `GBufferPass.cpp` (735 lines of bindless material fetch) and `LightingPass.cpp` (359 lines of OpenPBR, IBL and lights). A forward thumbnail shader duplicates both, and the node-graph codegen in [[project_material_overhaul]] would then need a second output layout so thumbnails do not disagree with the material editor.

The cost being avoided is largely not there. At 256² the pass chain covers ~65k pixels, and the expensive parts are already switchable: `RendererConfig::enableAccelerationStructures = false` skips `RtInstanceData` and `DynamicDiffuseGI` at construction, and `RENDER_USE_GLOBAL_ILLUMINATION` is per view. `MaterialEditorWorkspace.cpp:121-123` runs this exact configuration for its material ball today. One persistent 256² viewport is reused for every asset.

A dedicated renderer becomes the right call if thumbnails ever need bulk generation at import time, or generation off the main thread. Neither is true now.

### Getting the image out needs no renderer changes

`Renderer::getSceneRenderTarget().getTexture(getLastRenderedFrameIndex())` is the composited image. `Texture::readbackData()` (`Texture.cpp:670`) transitions `SHADER_READ_ONLY → TRANSFER_SRC`, copies to a staging buffer and transitions back — which is the layout the renderer already leaves an offscreen target in after `transitionToShaderReadLayout`. With `allowReadback = true` this works untouched.

The offscreen target is `TextureFormat::RGBA16F` (`DeferredRenderer.cpp:204-206`), but `Composite.fs.glsl:19-27` has already applied exposure, `GT7ToneMapping` and optional sRGB encode, so the stored values are display-referred. CPU side is a half to float widen and a clamp to 8 bit, no tonemapping.

`readbackData()` ends in `graphicsQueue->waitIdle()` (carries its own TODO to become a fence wait). That queue stall, not the renderer, is the real cost — so generation is main thread and rate limited to one per frame.

### Render settings and lighting

GI off, ambient occlusion on, acceleration structures off at construction.

There is no flat ambient colour term in the engine. Ambient today means IBL from `Environment`'s skybox (`setSkybox` + `skyIntensity`) plus `GroundTruthAmbientOcclusionPass`, and `LightingPass.cpp:177` pulls `environment->getImageBasedLighting()` independently of whether `SkyboxPass` draws that cubemap — which is what makes "lit by the environment, black background" possible at all.

A constant ambient would be enough for meshes and scene objects, which are staged with a clay material and so only have a diffuse response. It is not enough for materials: a metal has almost no diffuse response and is nearly all specular reflection of its surroundings, so with no environment a metal ball is black with two or three pinprick highlights. Since materials are the main reason thumbnails are wanted, IBL from a small studio cubemap is the recommendation. `MaterialEditorWorkspace.cpp:105-110` already loads `default.cubemap` for exactly this.

A single overhead sun leaves half of a sphere black. The rig that avoids it puts the key **over the camera's shoulder** rather than above the subject, so the lit side faces the viewer:

- **Key** — directional, roughly 30–45° up and 30° to one side of the camera axis. Does the shaping.
- **Fill** — opposite side at 20–30% of the key, keeps the dark side readable without flattening it.
- **Rim** — behind and above, catches the silhouette edge so the subject separates from a flat background. Matters more here than usual, given the background is plain.

With IBL supplying ambient, key plus rim is often enough. `DeferredLighting.fs.glsl:509-510` loops over `lightStaticCount + lightDynamicCount`, so this is three `DirectionalLightComponent`s and no shader change.

### Background

**Plain for now.** A checkerboard of `#1A1A1A` and `#414141` is wanted eventually, but neither route there is worth taking yet.

There is no coverage mask coming out of the renderer — `Composite.fs.glsl:27` writes `vec4(color, 1.0)` and `GBufferPass.cpp:247` clears to `(0,0,0,1)`. The two routes were an emissive camera-facing checker quad in the scratch scene, which needs no renderer changes, and coverage into alpha, which stores thumbnails transparent and lets the editor draw the backdrop. The second is the better end state, since restyling the background would not mean regenerating every thumbnail, but it touches `Composite.fs.glsl` and the lighting pass. Revisit it when a forward renderer exists, where coverage in alpha falls out naturally rather than being retrofitted onto the deferred composite.

Plain is close to free: `DeferredLighting.fs.glsl:435-437` already writes `vec4(0.0, 0.0, 0.0, 1.0)` for pixels with no geometry.

**The skybox no longer draws over it.** The scratch scene needs a skybox for IBL but must not render it, or the cubemap becomes the background. `Environment::isSkyboxEnabled()` is that switch, the World Settings checkbox drives it, and `DeferredRenderer::recordSecondaries` now hands the pass a null texture when it is off, so `hasActiveSkybox()` goes false and the pass is skipped. This was listed as a blocker and a standalone bug; it is neither now.

### Per type staging

One scratch scene, restaged per asset.

| Type | Staging |
| --- | --- |
| Texture | no 3D at all, a separate path that downscales and encodes |
| Cubemap | octahedral projection, which makes it 2D like a texture — the oct mapping is already in `ddgi/IrradianceCommon.glsl` |
| Material / instance | sphere, `MaterialEditorWorkspace::showMaterialOnSphere` is the precedent |
| Mesh | default material, framed to bounds |
| Scene object | instantiate the subtree, framed to bounds |
| World | its own camera where it has one, otherwise framed to bounds |

Not a checkerboard material for meshes — a checkerboard reads as *missing texture* in every other tool. `RE_DEFAULT_MATERIAL_INSTANCE` (`ReservedAssets.h:16`) is the neutral clay option, and for a mesh the thumbnail carries the silhouette, not the surface.

### Framing

One shared helper across mesh, scene object and world, so every thumbnail is shot the same way. Fixed camera direction, 3/4 view and slightly above, $\hat{d} = \text{normalize}(1, 0.6, 1)$ from the bounds centre.

Union the staged bounds into a centre $c$ and radius $r$. A thumbnail is square, so the horizontal and vertical half-angles are equal, and with vertical FOV $\theta$ the distance at which the bounding sphere exactly touches the frustum edges is

$$d = \frac{r}{\sin(\theta/2)}$$

Pad it so nothing kisses the border, $d' = 1.1\,d$, and fit the planes around the result:

$$n = \max(0.01,\; d' - r), \qquad f = d' + r$$

Camera sits at $c + \hat{d}\,d'$ looking at $c$. A non-square target would need $d = r / \sin(\min(\theta_x, \theta_y)/2)$, which does not come up here.

**Bounds cannot come from `MeshComponent::worldBoundingBox`.** That field is only refreshed during a render — `SceneGeometryDraw.cpp:56` and the two shadow passes — so it is stale or unset before the first frame, which is exactly when framing needs it. Compute it directly from `mesh->getBoundsMin()` / `getBoundsMax()` transformed by `transform.world`, which is what `MeshComponent::updateWorldBoundingBox` does anyway (`Components.h:146-151`).

### Cache

This supersedes the render-at-import wording in [[Asset Metadata]] for meshes and materials. Import-time capture only works for things being decoded anyway, and cannot produce a thumbnail for a material or a scene object, which have nothing to decode.

Cache location is `Project::getThumbnailDirectory()` (`Project.h:115`), which resolves to `<cache>/thumbnails` and is created in `Project.cpp:126` — note the directory is `thumbnails`, not the `thumbs` the older docs say. Derived data, deletable, never in the `.rapt`.

### What the engine actually needs

Verified against source. Two real renderer changes, both small.

1. **Honour `Environment::isSkyboxEnabled()`** — `DeferredRenderer.cpp:323-329`, as above. Also un-breaks the World Settings checkbox.
2. **Split render-on-demand from render-every-frame** — `ViewportManager::drawAll()` (`ViewportManager.cpp:28-33`) renders every viewport unconditionally, while `Viewport::drawFrame()` (`Viewport.cpp:52-59`) is what early-returns on `!m_active`. A thumbnail viewport wants the inverse: skipped by the frame loop, rendered when asked. Move the `m_active` check from `drawFrame` into `drawAll`; `drawFrame` has one caller today (`ViewportManager.cpp:31`).

Needed but outside the renderer:

3. **`STB_IMAGE_WRITE_IMPLEMENTATION` in a translation unit** — the header is vendored at `Engine/vendor/_deps/stb-src/stb_image_write.h`, but nothing in `Engine/src` or `Editor/src` defines the macro, so no PNG writer is linked.
4. **Cubemap to octahedral compute shader** — nothing comparable exists. `Generators/` holds `IrradianceConvolution.cs.glsl` and `SpecularPrefilter.cs.glsl`, both cubemap to cubemap. The only genuinely new GPU work here, and it serves cubemap thumbnails alone, so it goes last.

Already present and needing no change: readback (`Texture::readbackData()`, `Texture.cpp:670`), `allowReadback` plumbed `ViewportConfig` to `RendererConfig` to `SceneRenderTarget.cpp:61`, acceleration-structure gating (`Renderer.h:24`), per-view GI flag, multiple directional lights, `Viewport::setCamera`, `RE_DEFAULT_MATERIAL_INSTANCE` (`ReservedAssets.h:16`), `PanelServices::registerTexture` (`common.h:32`), `stb_image_resize2.h` for CPU downscale, and `Project::getThumbnailDirectory()` (`Project.h:115`).

### Still open

- Flat ambient versus IBL — recommendation above is IBL, on the strength of the metal case.
- Whether the background ever becomes a checkerboard, and by which of the two routes.

## 2. Asset picker

Two separate problems in `Editor/src/layers/panels/components/asset_picker.{h,cpp}`.

**Taller picker rows — DONE 2026-08-13.** The picker root is sized `UDim2::fromScale(1.0f, 1.0f)` (`asset_picker.cpp:170`), so its height comes from the property table row it sits in. That row height could not be set per row: `Table` was uniform-stride by construction, positioning every row from `vi * rowStride`.

`Table` now takes a height at row creation — `addRow(float height = 0.0f)` plus a `row(height, fn)` scope overload, 0 meaning inherit the table's `rowHeight`. Heights are stored as sparse overrides (`vector<pair<uint32_t, float>>`), so a uniform table allocates nothing, and the old `m_computedRowHeight` / `vi * rowStride` math is gone: `arrange()` and `updateSeparators()` already walked rows in order, so each carries a running `y`. New `Table::contentHeight()` sums the alive rows, which `PropertySection::fieldTable` now uses instead of `rowCount() * ROW_HEIGHT`.

Side effect worth remembering: the old multiply ignored row separators, so section bodies were about 1px per row shorter than the table's own canvas. They are now correct, and therefore slightly taller than before.

The picker row is 52px with a 28px preview (`PICKER_ROW_HEIGHT`, `PICKER_PREVIEW_SIZE`), and `AssetPickerConfig::previewSize` replaced the hardcoded `PREVIEW_SIZE` so the label's left offset follows the icon.

**Bug: opening the dropdown a second time loses the icons.** `open()` calls `rebuildItems()` on every open (`asset_picker.cpp:153`), which builds fresh `AssetContextMenuAID` items and hands them to `m_menu->setItems()`. `buildMenu()` early-returns when `m_menu` already exists (`asset_picker.cpp:246-249`), so the menu and its pooled row views outlive the items they were bound to. `bind()` does call `setSvg` on every bind, so the row is asking for the icon — the failure is somewhere in row reuse or SVG texture lifetime across `setItems()`, most likely on the Amethyst side. Not diagnosed yet.

## 3. World icon — DONE 2026-08-13

`WorldSettingsPanel.cpp:72` used `Icons::SVG_SETTINGS`, a filled ring with radiating rectangular teeth that reads as a sun at 16px. Replaced with a new `Icons::SVG_WORLD` globe. The dead `SVG_MODULE` and `SVG_PREFAB` icons were removed at the same time, along with `components/searchbar.h`, which declared a `searchbar()` that was never implemented or included anywhere.

## 4. Workspaces should not all open at startup

`AmethystLayer::setupWorkspaces` constructs all four unconditionally (`AmethystLayer.cpp:332-335`): Level Editor, Texture Generator, Scripting, Animations. Only the level editor earns a tab on launch. The other three should open on demand, the way `SceneObjectEditorWorkspace` and `MaterialEditorWorkspace` already do.

Feeds directly into #5 — "which workspaces are open" is part of the state that should be restored, not hardcoded.

## 5. Real editor state persistence

What exists today saves only dock split geometry. `saveLayout()` writes `m_dockingLayer->saveConfig()` into the `Amethyst::LayoutConfig` singleton keyed by dock name (`LevelEditorWorkspace.cpp:187-190`), and each workspace independently calls `LayoutConfig::instance().loadFromFile("layout.conf")` from its own constructor (`LevelEditorWorkspace.cpp:82`). `ScriptingWorkspace` and `AnimationsWorkspace` override `saveLayout()` to do nothing at all (`ScriptingWorkspace.h:10`, `AnimationsWorkspace.h:10`).

So the splits come back but nothing else does. **Which panels were open is not saved** — every panel is constructed unconditionally in the workspace constructor (`LevelEditorWorkspace.cpp:56-67`), so closing one is forgotten on restart. Neither is the open world; `AmethystLayer.cpp:328` carries the TODO for exactly that, falling back to the project's startup world.

Wanted: one editor-state file covering open workspaces, open panels per workspace, the active workspace, and the open world, loaded once rather than per-workspace-constructor. `LauncherConfig` is the precedent for editor-owned settings that outlive a project, but this state is per-project, so it belongs beside the project rather than beside the executable.

## 6. Project settings menu

No project settings UI exists. It should not be a dockable panel — it is a modal-ish thing you open, change, and close, so the precedent is `ImportPanel` rather than anything in the dock.

## 7. Bug: two white lines around the content browser search bar

1px white lines above and below the search bar's container. Cause unknown.

Ruled out so far:
- `.content-browser-section` (`theme.ams:286-288`) sets only `background-color: @bg-panel`, no border.
- `.searchbar` (`theme.ams:182-192`) has a 1px border, but `@border-3` is `#343434`, not white.
- Amethyst's style defaults are not the source: `borderPixelSize` default-initialises to `0`, and `Color3`'s default constructor is black (`libamethyst/src/modules/color.h:47`), so an unstyled frame is black, not white.

The section frame is built at `ContentBrowserPanel.cpp:432-441` inside a `UIListLayout`-driven `m_contentPane`. Next thing to check is what shows through between list-layout slots.

---

## 8. UI scaling — approach undecided

A scale factor for the whole editor, so the UI is usable at different display densities. **Which side it lives on is not decided**, both options below are live.

The one thing that is settled: **marking the window dirty does not apply a scale by itself.** `UDim::resolve` is `scale * parentSize + offset` (`libamethyst/src/components/common.h:78`, with `UDim2` at `:100` and `UDim4` at `:120`), and `offset` is stored in the component's properties, baked at construction from constants like `ROW_ICON_SIZE = 13.0f`. A relayout re-runs the same arithmetic over the same stored numbers and produces a pixel-identical result. Whatever the approach, something has to change the numbers or the point they are consumed at.

### Option A — editor side, rebuild the tree

Constants read a scale config at construction; changing the scale tears down and rebuilds the workspaces. Nothing in Amethyst changes.

The teardown machinery already exists and is exercised whenever a workspace tab is closed: `AmethystLayer::closeWorkspace` (`AmethystLayer.cpp:411`) erases the workspace, and `Workspace::~Workspace` sets `m_teardown` to break the panel-dtor / `onDestroy` cycle (`Workspace.cpp:8-11`, `:64-65`). A rebuild is `saveLayout()` on each workspace, clear `m_workspaces`, run `setupWorkspaces()` again.

What it needs:
- Rebuild drained on a frame boundary, not inside the callback that changed the scale — the same reason `OutlinerPanel` defers through `m_pendingRefresh`, since the signal arrives from inside a context menu callback.
- `setupWorkspaces()` builds `m_workspaceTabBar` itself (`AmethystLayer.cpp:309-336`), so calling it twice creates two. Split that out or tear the tab bar down too.
- A `waitIdle` at the engine choke point before dropping viewport panels, which own GPU resources.
- Dock layout already round-trips through `saveLayout()` / `applyConfig`, and selection lives in `EntitySelection` outside the tree, so both survive a rebuild for free.

Cost: every pixel constant becomes scale-aware. `ROW_HEIGHT`, `PICKER_ROW_HEIGHT`, `CONTROL_HPAD`, `ROW_ICON_SIZE`, `EDITOR_MENU_BAR_HEIGHT` and the rest are `static constexpr float` today, so this touches every panel file.

Gap: **theme-set font sizes do not scale this way.** Sizes the editor sets inline (`.fontSize = 12.0f`) scale fine, but `theme.ams` also sets them on classes (`.searchbar { font-size: 13px }`) and those come from the parser. Staying editor-side means scaling px values as the theme is loaded.

### Option B — Amethyst side, scale in resolve()

Multiply `offset` by a global factor at the one place offsets become pixels — the three `resolve()` functions in `common.h`. Dirty-the-window then genuinely works, because the multiply happens during layout rather than at construction.

The layout half is about three lines. The tail is the cost: **text does not go through UDim.** `fontSize` is a plain float that becomes `pixelSize` directly and drives atlas rasterization (`text_label.cpp:127`), so scaled boxes would keep unscaled text. Same for every other non-UDim pixel value — `borderPixelSize`, `cornerRadius`, `separatorWidth`, `Table::rowHeight`, `headerHeight`, `disclosureTriangleSize`, TabBar `barThickness`, SVG raster size. Each needs the factor applied where it is consumed.

### Related, and smaller than either

**Theme changes have the same staleness problem.** `resolveStyle()` runs only from property setters and when `effectiveGuiState()` changes (`ui_object.cpp:237-239`); there is no theme-version counter, so a live theme reload would not re-resolve colours. Fix is a version int next to `m_lastResolvedGuiState`, compared the same way. Worth doing regardless of which scaling option wins.

## 9. Raised 2026-08-13, not yet specced

**A proper header for `TreeView` and `Table`.** Both draw headers today — `Table` has `showHeader` / `headerHeight` / `arrangeHeader`, and the outliner turns its on with three columns (`OutlinerPanel.cpp:84-90`) — but they were not designed, they were made to work. No design yet for what a real one is: sort affordances, resize handles, alignment, styling.

**Resetting a value back to its default.** Wanted at least for transforms. Undecided where the affordance lives: per-row (a revert arrow appearing on hover, `Icons::SVG_RESET` already exists), per-section on the collapsible header, or in a row context menu. Needs a source of truth for "default" first — for a transform that is identity, but for a component field in general it is whatever the class constructs with.

## Bottom of the list

Wanted, but explicitly deprioritised.

**Content browser sort / filter.** Search is already in; sort, filter and a refresh/rescan button are not (see [[Asset & Editor Roadmap]] #3). **There is no design for this yet** — that comes before any implementation.

**Import panel options.** Surface `AssetImportConfig.h` in the panel: scale, generate colliders, import materials/textures, merge meshes, target parent. The panel already has real fields to grow from.
