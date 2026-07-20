# Asset & Editor Roadmap

Snapshot 2026-07-19. Braindump of the ordered path from the current state (docking + async import + GPU-deferred eviction landed) toward a `.rapt`-backed asset pipeline and, finally, Jolt physics. Captured so the order isn't lost. We start at the top and reassess as we go; later items are intentionally under-specced.

Related: [[Prefab]], [[Asset Metadata]], [[Project Serialization]], [[Material System Overhaul]].

---

## 1. Proper eviction queue — DONE

refcount-0 `LAZY`/`LAST` assets now go to a cold list (`PriorityQueue<AssetHandle>`, a stable wrapper over `std::priority_queue`) instead of freeing immediately; `IMMEDIATE` still frees eagerly. Priority is a base tier (`s_evictionPriority`) with an `int32_t` slot for a future size term. `drainColdList` frees on the `Telemetry` VRAM watermarks (soft >80% down to 75%, hard >90%), amortized per tick. Assets carry a stale-tolerant `sizeHintBytes`. Deferred: **TTL age-out** (needs iterating the queue) and the **size-weighted priority factor**.

## 2. Load an asset into a scene from the GUI — DONE (prefabs)

A prefab `.rasset` in the content browser now has a "Load in scene" context action that runs `Prefab::instantiate` into the panel's scene. The panel gets its scene from the workspace via `Panel::setContext` (base concrete, adopts the workspace's services + scene); `Workspace::addPanel` and the `BottomBar` hand it the context. Two correctness fixes fell out of the first serialization test:

- **glTF base material at startup**: `RE_GLTF_BASE_MATERIAL` (and the reserved white / flat-normal textures) are now built in `MaterialManager::createDefaultMaterials`, not lazily on glTF import — so a serialized `MaterialInstance` can resolve its base after a fresh launch without importing a glTF first. The loader just fetches it.
- **`getAsset` returns a falsy `AssetRef` on an unregistered handle** (was a ref to `Asset::null`, which read as truthy). This made every `if (!getAsset(h))` guard dead and let `AssetPtr<T>` assert on a missing dependency. Now missing deps degrade (e.g. a missing texture falls back to the graph default) instead of aborting.

Still TODO: mesh/other asset types into the scene, and drag-drop sharing this insert path. Known-and-accepted: if a session ends mid-import before a deferred async texture `.rasset` write lands, the material references an unregistered texture handle on reload — it degrades gracefully (default texture), and reimport fixes it. Not worth a fix unless it starts crashing.

## 3. Content browser cleanup

Searching, sorting, filtering — make browsing actually usable.

The asset-vs-file split is gone (2026-07-20): a single folder-based view rooted at the project directory (`getProjectDirectory()`), no separate flat "Assets" registry dump. Each `.rasset` entry is resolved through `AssetManager::findAssetByPath` and rendered exactly like the old asset tiles (type icon, colored type bar, display name, type label); unregistered `.rasset` files are skipped; raw files show a generic icon + extension. The context menu differentiates asset (Open) / raw file (Import) / folder. Virtual/builtin assets (no file on disk) are intentionally not listed. Search is in; **sort/filter and a refresh/rescan button** are still TODO.

## 4. Improve the import panel

Currently just import / cancel. Give it real options (see `AssetImportConfig.h`) and a better layout — more than a yes/no dialog.

## 5. Blob for compressed textures

On-disk blob storage for the BC-compressed textures we already generate, so cooked texture data is persisted rather than re-encoded every load.

## 6. The `.rapt` file

For the time being, `.rapt` simply facilitates **prefabs** and the **asset registry**. Not the full project format yet — just enough to persist those two. See [[Project Serialization]].

## 7. Asset picker widget (`AssetPicker` / `AssetDropdown`)

Icons/dropdowns for selecting assets, UE5-style: its own accent color, and later a thumbnail. This is its own little reusable widget. Used later in the property panels and the prefab material-table editor (#10).

## 8. Prefab material-table editor

A proper UI for editing the material table of a prefab.

## 9. Open question: models as prefab instances?

Should "models" become **instances of a prefab**, to make it easier to differentiate a prefab from one of its instances? Decide before #10 leans on it.

## 10. Use the picker in property panels + prefab editor

Wire the `AssetPicker` (#7) into the mesh and material property panels, and into the prefab material-table editor (#8).

## 11. Thumbnails

Asset thumbnails for the picker/content browser. Could land earlier than this if convenient.

## 12. Load assets from `.rapt` + blobs

Once the engine can load assets from our `.rapt` + blobs (rather than only re-importing source files), it's in a much better spot.

## 13. Small tweaks

A handful of small remaining polish items.

## Content pipeline sub-sequence (2026-07-19)

The `.rasset`/`content/` work broken into order (folds into #5–#7 above). Physical `.rasset` files, no
virtual folders; see [[Project Serialization]] revision note.

1. ~~glTF loader takes an optional prefab name~~ (DONE — falls back to file stem).
2. Import panel: show the input file path, the output path + name (auto-append `.rasset` if absent).
3. ~~`.rasset` write for textures, meshes, prefabs and materials (base + instances)~~ (DONE — routed through the two import request structs and the `s_serializeAsset`/`s_deserializeAsset` dispatch; shaders stay `.spv`).
4. ~~Fill the registry on project load by scanning `content/`~~ (DONE — `AssetManagerEditor::registerAssetDirectory` walks the tree calling `registerRaptureAsset` per `.rasset`, header+metadata only, no payload; called after init in `Application`). Open: the scan roots at `getContentDirectory()` while the browser roots at `getProjectDirectory()`, so `.rasset` files outside `content/` currently resolve to nothing (skipped) until the scan root is widened or a rescan lands.
5. ~~The path map~~ (DONE, turned around from the original `uuid → path`): the `.rasset` load path now lives on `AssetMetadata::assetPath` (runtime only, never serialized — provenance keeps the original source for reimport). A hashed `path → handle` index (`m_pathIndex` / `findAssetByPath`) is the reverse lookup the content browser uses. Rescan-on-refresh and file watchers still to come.
6. Editor file ops: rename / move / delete files, folders (see "Near-term order" below).
7. File watchers — our own OS abstraction (inotify / ReadDirectoryChangesW / FSEvents). Watches are **per-directory, recursively**, never per-file: inotify adds a watch per subdir (and watches dir-create to attach new ones), ReadDirectoryChangesW takes one handle with `bWatchSubtree`, FSEvents is natively recursive. Abstraction: a `DirectoryWatcher` over the content root emitting created/deleted/modified/renamed with paths, driving registry rescans.
8. ~~Load-into-scene, so prefabs can be instantiated from the browser~~ (DONE, see #2).

## Near-term order (2026-07-20)

Refined session plan after load-into-scene landed. Import-panel cleanup is deferred (design pass with Claude first).

1. **Context-menu Delete + Rename** (next).
   - *Rename* is just the on-disk `.rasset` file; update `assetPath` + `m_pathIndex`.
   - *Delete* removes the file and unregisters the asset, but respects dependency order via a generic dependency check:
     - Delete a **base material** -> its instances are marked invalid (they cannot function without a base).
     - Delete a **texture** -> dependent materials fall back to the default texture (safe, just looks off).
     - If the check deems a delete unsafe, prompt the user ("Are you sure? N assets depend on this").
2. **File watcher** (see sub-sequence #7) — per-directory platform abstraction.
3. **Thumbnails** (#11) — may be stale until reimport, that's fine.
4. **Small stuff** — a grab-bag session of polish.
5. **Physics — Jolt** (#14).

## 14. Physics — Jolt

Integrate Jolt (replacing the shelved Entropy). Enables click-to-pick ray casts and real physics.
