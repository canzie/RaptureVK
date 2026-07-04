# Editor UX Polish Roadmap

Snapshot 2026-07-04. Goal: assets can now be imported from the GUI, but the editing loop around them is rough. Before adding new features, round out the existing experience so it feels complete. Ordered by priority; the first two are specced in detail, the rest are roadmap.

Guiding principle: make the current import/edit loop feel finished before building anything new.

---

## 1. Outliner context menu (rename / delete / delete-keep-children)

**Where**: `Editor/src/layers/panels/OutlinerPanel.{h,cpp}`

Currently the outliner rows are plain `textButton`s with a left-click select (`s_onEntityClicked`, `OutlinerPanel.cpp:12`). There is no right-click menu. The content browser already demonstrates the pattern to copy — `Amethyst::ContextMenu` owned by the panel, shown via `showContextMenu(pos, items)` (`ContentBrowserPanel.cpp:529-541`), triggered from an `onMouseButton2DownCb` on the row's action component.

- [ ] Add `Amethyst::ContextMenu *m_contextMenu` to the panel, created once in the ctor (mirror `setupContextMenu`)
- [ ] On each entity row, set `onMouseButton2DownCb` to select the entity, then `showContextMenu` with the items below
- [ ] **Rename** — inline rename of the `TagComponent::tag`. Simplest first pass: a small popup/text input seeded with the current name; on commit write the tag and `refresh()`. (If inline tree-cell editing is too fiddly, a tiny rename popup is fine.)
- [ ] **Delete** — delete entity *and* its subtree. Use `destroyHierarchy(entity)` (`HierarchyComponent.h:86`), NOT `entity.destroy()` directly (see gotcha)
- [ ] **Delete (keep children)** — reparent children up, then delete just this entity:
  1. capture `parent = hier.parent` (may be invalid = root)
  2. for each `child` in a *copy* of `hier.children`: `setParent(child, parent)` (`HierarchyComponent.h:46`) — moves child under the grandparent, or to root if parent invalid
  3. `removeFromParent(entity)` then `entity.destroy()`
- [ ] `refresh()` the tree after any mutation, and clear the selection if the deleted entity was selected

**Gotcha (verified)**: `Scene::destroyEntity` (`Scene.cpp:107`) only calls `m_registry.destroy(handle)` — it does **not** touch `HierarchyComponent`. Calling it on an entity that is in a parent's `children` vector, or that has children, leaves dangling `Entity` handles that later `isValid()`-check-fail or crash traversal. Always go through `destroyHierarchy` / `removeFromParent` / `setParent` so both sides of the parent↔child link stay consistent. The environment entity cannot be destroyed (guarded in `destroyEntity`) — the menu should not offer delete for it.

---

## 2. Make the content browser context-menu actions work

**Where**: `Editor/src/layers/panels/ContentBrowserPanel.cpp` (asset menu `:667-675`, file menu `:737-744`)

Both menus currently push items with empty lambdas (`ContextMenuItem::action("Open", [] {})`). Wire them up.

**Asset browser (`refreshAssetBrowser`, `:636`)** — the loop already has `handle` + `metadata`; capture them into the lambdas.
- [ ] **Open** — dispatch by `metadata->assetType`:
  - Mesh / model → instantiate into the active scene (same end state as import, minus the file load). Needs a scene-insert path from an already-loaded asset handle.
  - Material → open the material editor workspace (see `Editor/src/layers/workspaces/MaterialEditorWorkspace.h`)
  - Other types → no-op for now (leave the item out rather than showing a dead "Open")
- [ ] **Rename** — rename the asset's registry name (`metadata->getName()`); refresh
- [ ] **Delete** — remove from the asset registry. Depends on AssetManager having an unload/evict path — it currently does **not** (see `Improvements.md` → Resource Cleanup). May need a minimal "remove metadata entry" first, full GPU eviction later. Flag this dependency; do not fake it.

**File browser (`refreshFileBrowser`, `:684`)** — Import already exists as a menu item on files.
- [ ] **Import** — reuse the existing `ImportPanel` flow instead of a stub
- [ ] **Rename / Delete** — filesystem rename / delete of the actual file (with the usual "are you sure" for delete)

Note: opening a mesh into the scene shares the TLAS-rebuild problem below — route both through the same insert-then-rebuild path so we only fix it once.

---

## 3. Expand the import panel + fix TLAS-in-import

**Where**: `Editor/src/layers/panels/ImportPanel.{h,cpp}`, `Engine/src/asset_manager/`

Right now import is yes/no only (`doImport`, `ImportPanel.cpp:143`) and the panel manually rebuilds the TLAS afterward with a self-admitted hack:

```
// ImportPanel.cpp:157
// TODO: having to manually rebuild the TLAS here is shit, the loader should handle this.
scene->buildTLAS();
```

- [ ] Move TLAS build/update out of the UI: the loader / scene-insert path should mark the TLAS dirty and let the scene rebuild once per frame if dirty, instead of the panel calling `scene->buildTLAS()` explicitly. Any mesh-add path (import *and* "Open from content browser") benefits.
- [ ] Add real import options (`AssetImportConfig.h` already exists) — e.g. scale, generate colliders, import materials/textures y/n, merge meshes, target parent entity. Surface them in the panel and pass through to the loader.

---

## 4+. Bigger items (roadmap, not yet specced)

- [ ] **Drag assets into the editor/viewport** — drag from content browser → drop into viewport/outliner to instantiate. Depends on #2's scene-insert path.
- [ ] **Jolt physics** — replace shelved Entropy (`Improvements.md` → Physics is SHELVED for exactly this). Needed for mouse ray-casts (click-to-pick in viewport) and general physics.
- [ ] **Selection outline** — fix the stencil-buffer border (`Engine/src/renderer/passes/StencilBorderPass`, see [[StencilBorderPass]]) or replace with another method (jump-flood outline, etc.).
- [ ] **Material system overhaul + node editor** — full rework of [[Material]] / [[MaterialData]] / [[MaterialInstance]], culminating in a material node editor. Largest item; do last.

---

## Deletion is its own design thread (biggest cost)

Deletion has never been looked at, so it is the deepest part of this work and the one most likely to be done wrong if rushed. Do it once, cleanly, not as per-call-site hacks. Two distinct kinds:

- **Entity deletion** (outliner, #1) — hierarchy-aware. `Scene::destroyEntity` (`Scene.cpp:107`) only drops the entt handle and leaves dangling `Entity` handles in parent/child links. Every entity delete must go through `destroyHierarchy` / `removeFromParent` / `setParent`. This part is achievable now.
- **Asset deletion / eviction** (content browser, #2) — has **no** engine support yet. AssetManager loads and never frees (`Improvements.md` → Resource Cleanup / Asset System). Doing it *cleanly* needs: (a) an AssetManager evict/unload path, (b) `AssetRef` use-count checks so we don't delete a referenced asset, and (c) deferred GPU destruction so a resource survives until all in-flight frames stop referencing it. That is a real subsystem, not a one-liner.

**Plan**: for the first pass, scope content-browser "Delete" to removing the registry entry (and guard against deleting an in-use asset), and split the full evict + deferred-GPU-destruction work out as its own task. Don't fake full deletion in the menu handler.

## Cross-cutting notes

- The content browser is the reference for context menus, item pooling, and selection — mirror it, don't reinvent.
- Anything that adds a mesh to the scene must share one "insert + mark TLAS dirty" path (#3). Import, content-browser Open, and drag-drop all hit it.
