# Editor Restructure

Full rebuild of `Editor/` as a codebase: file layout, panel and workspace lifetime, a reusable widget layer, an inspector that nests, and editor/project settings including UI scale. Nothing here is about what the editor looks like rendered.

Source of truth for the work. Every claim below is checked against the tree at the time of writing and cited.

## Status

2026-11-15, branch `editor-restructure`: Phase 1 done apart from the rig family, which is postponed until the skeleton and animation side of the editor is rethought. The editor builds and runs. Line citations in the "Why now" and Phase 1 sections describe the tree before the restructure and point at files that have since moved or gone.

What Phase 1 ended up as, where it differs from the text below:

- The two hosts are concrete classes named `WindowManager` (secondary windows, file dialog, import panel) and `WorkspaceManager` (open workspaces, asset routing, restore and store), both in `context/`. `EditorContext` points at them directly; there are no interface bases.
- `AmethystLayer` still draws every UI window, `WindowManager` included through a draw callback, until Phase 5 moves that into the engine.
- A workspace is told its content area by the shell and places its own dock below the hotbar, since the dock is a layer and gets no absolutes from its parent.
- A panel can be built outside a tab bar (`Panel::attachTo`) and moved into one later, which is how the bottom bar's content browser docks into a workspace.
- `ScenePreviewHost` became `PreviewScene`, held by `PreviewWorkspace`. The world editor keeps its own viewport.
- Material preview is built from scene objects. Its skybox is still a cubemap imported from disk, left until preview backgrounds move to grids and gradients.
- `AnimationsWorkspace` stays an empty tab: the engine has no animation asset class yet.

## Why now

Three flaws are load-bearing and block everything downstream.

The **1:1 asset-to-workspace assumption** is encoded in `AmethystLayer::m_assetWorkspaces` (`AmethystLayer.h:167`), and `assetHandleFor()` answers "what is this workspace showing" by reverse-scanning that map (`AmethystLayer.cpp:263-271`). Scripting already broke it: `openAssetWorkspace` early-outs on `ASSET_SCRIPT` before its own switch (`AmethystLayer.cpp:513-520`), and `ScriptingWorkspace` keeps a private `m_openScripts` map with its own open/focus/close (`ScriptingWorkspace.h:34`, `.cpp:113-153`). That makes three copies of the same "map subject to tab" logic.

The **inspector cannot nest and cannot hold two sections of one type**, because `PropertySectionList::ensure<T>` keys on `std::type_index` (`property_sections.h:216-238`) and `relayout()` stacks a flat vector by summing `HEADER_HEIGHT + bodyHeight()` (`property_sections.cpp:361-381`). Geometry is computed by the list, not by the section, so a section containing a section is invisible to the layout.

There is **no reusable widget layer**, so the same compositions are retyped. Six identical workspace hotbar setups, and they already disagree: `LevelEditorWorkspace.cpp:127-130` and `SkeletonWorkspace.cpp:100-103` use `EDITOR_HOTBAR_PADDING` (8.0f, `EditorLayout.h:9`) while `ScriptingWorkspace.cpp:56-59`, `SkeletalMeshWorkspace.cpp:65-68`, `MaterialEditorWorkspace.cpp:159-162` and `TextureGeneratorWorkspace.cpp:79-82` use a literal `6.0f`. Three separate search bars (`PropertiesPanel.cpp:111`, `ContentBrowserPanel.cpp:535`, `FileBrowser.cpp:316`).

## Settings and scale: what the research changed

The feature splits in two halves with completely different mechanics. This was not obvious before checking Amethyst.

### Theme (colours, fonts) is hot and needs no rebuild

`UIObject::resolveStyle()` is **public and virtual** (`Amethyst/libamethyst/src/components/ui_object.h:142`) and overridden by 20 components. `Style::load(path)` replaces the global singleton wholesale (`src/modules/style.h:214`). So after loading a different theme, walking the tree and calling `resolveStyle()` + `markDirty()` on every node restyles in place. No re-init, no rebuild.

Fonts are the same story. The glyph atlas is keyed by `(FontId, pixelSize)` and rasterises on demand (`src/modules/glyph_atlas.h:39-43`, `tableKey` at :217), so a new font size costs a new atlas group and nothing else. No re-init needed there either.

**What would block it:** instance-level overrides set through `setBaseStyleProperties` beat the resolved class, and the editor has 44 inline `fromHex` calls plus raw `Color4` literals across 16 files, 19 in `color_field.cpp` alone. Every one of those is a pixel the theme cannot reach. Themeability is gated on moving them to classes, not on any Amethyst work.

**Not scheduled.** Recorded because the constraint is real and cheap to forget, but user-chosen colours are explicitly not wanted yet. Scale is the feature.

`@property` variables in `.ams` expand at **parse time** (`src/parsers/am_theme/am_theme_parser.cpp:715`, header doc at `am_theme_parser.h:26-29`). There is no C++ API to set one. So "user picks an accent colour" means writing a generated `.ams` and calling `Style::load` on it, not poking a variable. That is acceptable and is the design.

### Scale lives in Amethyst

Amethyst lays the tree out in logical pixels and scales it onto the framebuffer. `Window::setLogicalToPhysicalScale` holds physical pixels per logical pixel, the GLFW backend sets it from the monitor's content scale on `registerWindow` and again when the window changes monitor, the vertex shader applies it, and text and SVGs rasterise at their physical size. The editor sizes each window with `Window::setFramebufferSize(swapchain extent)` and every number it authors (config entries, literals in constructors, theme sizes) is in logical pixels and needs no scaling of its own.

That removed the editor-side scale: no per-number scaling rule, no `applyMetrics()` pass, no `EDITOR_CONFIG_UI_SCALE`. Anything outside the Amethyst tree that compares against UI positions converts to logical pixels itself, e.g. the shortcut registry's cursor.

### EditorConfig: one table, not a settings class per consumer

Panels and widgets need somewhere to read defaults from. That place is a single enum-keyed table on `EditorContext`, and it absorbs what would otherwise have been three separate mechanisms: `EditorLayout.h`'s `constexpr` constants, a settings object, and a separate `Metrics` type.

The key set is closed and known at compile time, so it is a `constexpr` array indexed by the enum, not a hash map.

```cpp
enum EditorConfigKey {
    EDITOR_CONFIG_HOTBAR_HEIGHT,
    EDITOR_CONFIG_HOTBAR_PADDING,
    EDITOR_CONFIG_SEPARATOR_WIDTH,
    EDITOR_CONFIG_ICON_SIZE,
    ...
    EDITOR_CONFIG_COUNT
};

struct EditorConfigEntry {
    std::string_view key;          // serialisation name
    EditorConfigType type;         // FLOAT / INT / BOOL / STRING
    EditorConfigValue fallback;    // used when nothing is stored
};

constexpr EditorConfigEntry EDITOR_CONFIG_TABLE[EDITOR_CONFIG_COUNT] = { ... };
```

This is also what fixes the existing inconsistency where four workspaces use a literal `6.0f` for hotbar padding and two use `EDITOR_HOTBAR_PADDING` (8.0f). There stops being a second place to get the number from.

Stored values live in `launch.cfg` beside the executable, which `LauncherConfig` already reads and writes via `SerialDocument` (`LauncherConfig.cpp:11-57`). `LauncherConfig` grows into `EditorConfig` rather than gaining a sibling.

Two other stores stay as they are and are not part of this:

- **`EditorState`** — per project, what was open. Works today, in the project's opaque editor section (`Project.h:128,134`).
- **`ProjectSettings`** — per project, engine-owned. Startup world already lives there (`Project.h:105`). Engine work, not editor work.

## Target layout

```
Editor/src/
  app/          EditorApp, main, relaunch, EditorSettings, EditorState, EditorShell
  context/      EditorContext, WindowManager, WorkspaceManager
  ui/           the editor's widget vocabulary, workspace-agnostic
    EditorWidget.h
    fields/     AssetField, ObjectField, ColorField, SearchField, FieldTable
    controls/   IconButton, Toolbar, SegmentedControl, ModeDropdown, NavButtons
    views/      ListView<T>, SceneTreeView, DirectoryTreeView, AssetTile
    chrome/     Breadcrumb, EmptyState, tab labels, section headers
    Theme.h     class-name constants, no colour literals
  document/     Document, UndoStack, dirty tracking
  inspector/    Inspector, Section, SectionRegistry, concrete sections
  workspaces/   Workspace, SingleDocumentWorkspace, MultiDocumentWorkspace, ScenePreviewHost
  panels/       Panel + concrete panels
  gizmos/       unchanged, already good
  runtime/      EditorViewLayer, PlaySession driver
```

`layers/` disappears. Only four files in it are Layers today; the rest are panels, workspaces, a bottom bar and a launcher.

## Phases

Ordered so each one unblocks the next. No phase depends on a later one.

**Working style:** the core changes go in as one move, not as a chain of individually-shippable steps. Keeping the tree buildable after every edit costs more than it is worth while the foundations are being replaced. Intermediate working states are fine where they fall out naturally, but they are not a goal. This changes once the restructure is done and the work turns to cleanup and new features.

### Phase 1 — lifetime and hosting

The foundation. No visual change, and it is what makes the inspector rewrite possible.

- `Panel` base that owns its own lifecycle: root creation, `addClass`, clipping, destroy connection, attach, and tab removal in the destructor. Kills 8 identical destructors and 12 identical constructor preambles. Fixes the `AssetDetailsPanel` bug where `m_root` and `m_rootDestroyConn` are redeclared (`AssetDetailsPanel.h:200-201`), shadowing `Panel::m_root` so `Panel::root()` returns nullptr and the re-dock overload silently no-ops.
- `onShown()` / `onHidden()` on `Panel`, so a `ViewportPanel` on a hidden tab stops rendering its viewport.
- `Workspace` forks in two, on **binding** rather than on how many assets are held. A scripting workspace with no script open is still valid; a mesh workspace with no mesh is not.
  - `Workspace` — knows nothing about assets. `ScriptingWorkspace`, `TextureGeneratorWorkspace` and `AnimationsWorkspace` derive straight from it.
  - `AssetWorkspace : Workspace` — takes the asset in its constructor, exists because of it, and closes when it is deleted. World, material, module, static mesh, skeletal mesh and skeleton derive from this.
- `ScenePreviewHost` as a **member** rather than a base. `AssetPreviewWorkspace` currently conflates "is for one asset" with "has a preview scene", which is why Material and Module editors could not reuse it and each rolled their own `setupPreviewScene` — three copies.
- **No workspace registry, table, factory or kind enum.** Routing is a plain switch on `AssetType` in the one place that opens assets. What breaks the asset-to-workspace lock is deleting `m_assetWorkspaces` (`AmethystLayer.h:167`) and the reverse scan in `assetHandleFor()` (`AmethystLayer.cpp:263-271`), not replacing the switch. Opening an asset always makes a workspace; gathering several scripts into one is that workspace's own business, through a file tree panel it does not have yet.
- Nothing is indexed by asset, so one asset open in several workspaces is legal rather than a case to defend against.
- `EditorContext` replacing `PanelServices`. Today it is seven `std::function`s rebuilt per workspace and copied again into every `Panel`, so a five-panel workspace holds six copies of seven closures all wrapping the same `AmethystLayer`.
- **The rig family.** Skeleton, skeletal mesh and animation become one family over a shared rig preview: a base owning the preview scene, viewport and details panel, with three thin subclasses adding only their own panels. This is already half true — `SkeletonWorkspace` and `SkeletalMeshWorkspace` both derive from `AssetPreviewWorkspace` and both build exactly `ViewportPanel` + `AssetDetailsPanel`, with `SkeletonWorkspace` adding a hierarchy panel, a pose and a gizmo on top. It also gives `AnimationsWorkspace` something to be, since today it is a stub with `setupBase("Animations")` and an empty `saveLayout()`, and it is the natural home for the `EDITOR_MODE_POSE` that is already defined and unused.
- **"Level" is not a word this engine uses.** The engine has `World`, `AWorld`, `Project::saveWorld` and `getStartupWorld`, and no level type anywhere. `LevelEditorWorkspace` becomes `WorldEditorWorkspace`.

#### Defects to fix while porting, not carry over

Found by reading the workspaces rather than by porting them.

**Every open workspace renders every frame, visible or not.** This is the expensive one.

`ViewportManager::drawAll()` already skips inactive viewports (`ViewportManager.cpp:32-37`), and `Viewport::m_active` defaults to `true` (`Viewport.h:151`) — but `Viewport::setActive` is never called anywhere in the editor or the engine. The only `setActive` callers in the tree are `Light3D`, `World` and a scene node. So the gate exists and nothing ever closes it, and a full deferred render runs per open workspace per frame no matter which tab is in front.

`Viewport::EditorBinding::displayed`, documented as "A ViewportPanel is bound to this viewport" (`Viewport.h:69`), is set to `true` once by `ViewportPanel.cpp:109`, never set back to `false`, and never read by anything — a second dead mechanism aimed at the same problem.

Ticking has the same shape: `AmethystLayer::onUpdate` loops every workspace, and each preview workspace calls `m_previewScene->onUpdate(dt)` unconditionally, so every preview scene simulates whether or not it is on screen.

Both mechanisms are already there and simply unwired, so the fix is to wire them from `Panel::onShown`/`onHidden` rather than to add a third flag. `displayed` should then be deleted or become the thing `setActive` is driven from, not left as a third parallel notion.

**TextureGeneratorWorkspace is being deleted**, which takes its texture leak, its `dynamic_cast` panel scans and its shader force-load with it. `ShaderInputsPanel` goes too — it has no other user.

**MaterialEditorWorkspace**

- Its preview viewport is created at the **full swapchain extent** (`.width = extent.width, .height = extent.height`, `MaterialEditorWorkspace.cpp:115-125`) for what is a small preview pane, so a full-screen offscreen target is allocated per open material, times `framesInFlight`. `AssetPreviewWorkspace` uses fixed 1280x720 for the same job. Size it to the pane.
- Re-imports the default cubemap from disk every time a material workspace opens (`importAsset(skyboxPath)`, `:106-107`). Should be a reserved asset, the way `AssetPreviewWorkspace` uses `RE_GRID_MATERIAL_INSTANCE`.
- Builds its preview scene out of raw ECS components — `TransformComponent`, `CameraComponent`, `DirectionalLightComponent` (`:93-103`) — while `AssetPreviewWorkspace` builds the same thing from `addDefaultContent()` and scene objects. Two ways to make a preview scene, and this one goes under the scene object layer the engine moved to.
- The dock name is a literal in two places (`:35` and `:66`): the save path uses `m_dockingLayer->name`, the restore path uses the string. They can drift.

**ModuleEditorWorkspace**

- Resolves its asset twice per open: once to validate in `create` (`:37`), once again in the constructor (`:52`).

**SkeletonWorkspace** is clean. `SkeletonGizmo` pools its batches through `nextBatch`/`submit` and does not allocate per frame after warmup.

### Phase 2 — the widget layer

Depends on Phase 1 for `EditorContext`.

Three incompatible widget shapes exist today: subclassing a component (`SegmentedControl : Amethyst::Frame`), building into a parent scope (`AssetPicker`, `InstancePicker`, `ColorField`), and returning a build callback (`iconTabLayout`, `componentHeaderLayout`, `ModeDropdown_add`). Standardise on the scope-building shape; reserve subclassing for things that genuinely override `draw()`, which today is only `SegmentedControl`.

`EditorWidget` base fixes what all three currently miss: `AssetPicker` and `ColorField` hold `m_root` with no destroy connection, so they dangle if their parent frame dies. Every `Panel` already handles this; the widgets do not.

Amethyst is retained-mode. A component is build-once plus `set*` methods, not a render function. No diffing, no rebuild-on-change. Lists are the exception and get a pooled `ListView<T>`, which `ContentBrowserPanel` already hand-rolled once (`acquirePoolItem` / `releasePoolItems`, `ContentBrowserPanel.h:177-178`) and the three tree panels each hand-rolled differently.

Build order inside the phase: `Theme.h` and `Metrics.h` first (they are what everything else reads), then `Toolbar`, `SearchField`, `IconButton`, `EmptyState`, then `FieldTable`, then `ListView<T>`.

### Phase 3 — inspector

Depends on Phase 2 for `FieldTable`.

- Key sections by `(sectionType, subjectKey)` instead of `std::type_index`, which is what unblocks N material sections on one mesh.
- `build()` may declare child sections, and `contentHeight()` recurses. Nesting falls out.
- A registry keyed on the engine type replaces the 18-call `ensure<>` if-list in `PropertiesPanel::refresh()` (`PropertiesPanel.cpp:260-296`).
- Collapse `ComponentEditorBase` and `AssetEditorBase` into one `Section` over a subject variant. They duplicate `sync()`, `setSubject()` and `m_subjectChanged` today, and `PropertiesPanel::ensure` / `AssetDetailsPanel::ensure` are the same template written twice.
- Move the nine `rowX` helpers off the section base (`property_sections.h:59-165`) into `FieldTable`, so anything can lay out a labelled field.

### Phase 4 — settings

Depends on Phase 1 (`EditorConfig` on `EditorContext`).

- Every `EditorLayout.h` constant becomes an `EDITOR_CONFIG_` entry. Launcher and file-explorer window sizes move out; they are not layout.
- Settings UI as a workspace, not a modal. It is a document like anything else.

### Phase 5 — shell split

Depends on Phase 1.

`AmethystLayer` is 841 + 176 lines doing eight things. Split four ways:

- **`UiRenderLayer`**, moved to the engine — Amethyst backend, descriptor pool, dynamic rendering, per-window draw and record, swapchain resize. Removes the last raw Vulkan from `Editor/` (`createUiDescriptorPool`, `beginDynamicRendering`, `endDynamicRendering`, hand-written `vkCmdPipelineBarrier`).
- **`WindowManager`** — secondary windows, file dialog, import dialog. Done in Phase 1, minus the drawing.
- **`EditorShell`** — menu bar, workspace tab bar, bottom bar. Workspace open/close/restore already lives in `WorkspaceManager`.
- **`EditorState`** — unchanged.

### Phase 6 — dedup

Depends on Phase 2 for `ListView<T>` and `SceneTreeView`.

- Three tree panels collapse onto one widget. `OutlinerPanel`, `SceneObjectTreePanel` and `SkeletonHierarchyPanel` each carry a row-to-object vector, `instanceForRow`, `onRowClicked`, `selectRowFor`, a DFS builder, a deferred-delete flag and a deferred-refresh flag. About 940 lines doing one job three times.
- `ContentBrowserPanel` (1406) and `FileBrowser` (1039) both implement read-directory, back/forward history, search filter and selection with different data models. One `DirectoryModel`, two renderers.

### Phase 7 — undo

Depends on Phase 3.

Every property row commits directly inside its own `onValueChanged` lambda, at roughly 40 sites. Once rows go through `FieldTable`, every commit routes through `document.undo.push(...)` in one place. Doing this after Phase 3 is nearly free; doing it later means 40 hand-edits plus whatever is added in between.

## Moves to the engine

1. The Amethyst-over-Vulkan pass (Phase 5).
2. Secondary window management. Half is already in `Application` / `RenderWindow`; the other half is `AmethystLayer::SecondaryWindowContext`.
3. Content mutation: create folder, create asset of a class, create script, rename on disk, delete. Split today between `ContentBrowserPanel` (`createFolder`, `createModule`, `createScript`, `renameItem`, `ContentBrowserPanel.h:128-148`) and `ScriptingWorkspace::createScript` (`ScriptingWorkspace.h:24`).
4. Asset presentation table. `AssetRegistry` already owns `displayName` and `fileExtension`; colour and icon belong beside them, not in `panels/components/asset_visuals`.
5. Play-in-editor as a session object, replacing `LevelEditorWorkspace` reaching for `Application::getLayer("Editor Layer")` by string to detach a sibling layer (`LevelEditorWorkspace.cpp:262-289`).
6. Editor camera and fly controls (`EditorLayer::adoptViewport`, `s_mapEditorCameraInput`).
7. `TextBuffer`. It implements `Amethyst::TextSourceBase` and has no editor dependency, so it belongs in Amethyst.

Moving **out** of the engine: `Viewport::EditorBinding::hovered` and `displayed` (`Viewport.h:66-70`) are UI facts stored on a render object. Once (6) lands, `controller` stays and the two bools go.

## Missing features, for scheduling

Undo/redo (Phase 7). Multi-selection — `EntitySelection` holds one entity and every consumer assumes it. Copy, paste, duplicate. Dirty tracking and save prompts, which only `TextEditorPanel` has today (`isModified()`, `TextEditorPanel.h:52`). A panel registry, since the Window menu entries are all empty lambdas (`AmethystLayer.cpp:390-397`). Keybinding UI, noting `ShortcutRegistry::bindShortcut` exists and nothing calls it. Thumbnails, noting `AssetContextMenuAID::thumbnail` exists and nothing produces one. Drag and drop. Property search, which is built and never read (`PropertiesPanel.cpp:113`).

## Still open

- Where the undo stack lives now that there is no document object: per workspace, or per asset shared across the workspaces showing it.
- Closing an `AssetWorkspace` when its asset is deleted has no hook. There is no asset-removed signal anywhere — `AssetManager`, `AssetRegistry` and `AssetManagerEditor` expose no `EventSignal`, and `ProjectEvents` only covers serialise and register. Asset deletion itself does not exist yet either (the content browser's Delete entry is an empty lambda), so this lands together with delete.
- Whether `Viewport::EditorBinding::displayed` stays as the one-panel-per-viewport guard or goes with the other UI facts on the viewport.
- The scripting workspace needs a file tree panel, since opening a second script into an existing workspace is now its job rather than the router's.
- Whether the generated theme `.ams` is written to the project or beside the executable.
- A user-chosen zoom on top of the monitor scale. The backend overwrites the window's scale on every content scale change, so a multiplier would need a home in Amethyst rather than a second `setLogicalToPhysicalScale` call from the editor.
- The rig family: skeleton, skeletal mesh and animation workspaces sharing a rig preview. Postponed until the editor side of skeletons and animation is rethought.

## Related

[[Editor UX Polish Roadmap]], [[Asset & Editor Roadmap]], [[Editor Picking and Tool Overlay]], [[Project Serialization]]
