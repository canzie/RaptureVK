# UI Asset Import — Current State & Known Issues

Snapshot from wiring up "load a glTF purely from the UI" (2026-06-26). This works end to end — pick a file in the [[file_editor|file browser]], an import panel appears, hit Import and the model loads into the active scene — but several parts are deliberately rough and are noted here so they get cleaned up later.

---

## The flow today

1. Menu → **Open File Explorer** spawns a secondary OS window holding a `FileBrowser` (`Editor/src/layers/panels/FileBrowser.cpp`).
2. Picking a file fires `FileBrowser::onConfirm`, which:
   - creates an `ImportPanel` (`Editor/src/layers/panels/ImportPanel.cpp`) floating in the **main** window's overlay, and
   - the file-browser window closes itself.
3. `ImportPanel` does its own import (`glTF2Loader::load` + `Scene::buildTLAS`) and removes its own popup.

---

## Awkward state — ownership & closing

`AmethystLayer` owns **both** panels (`std::unique_ptr<FileBrowser> m_fileBrowser`, `std::unique_ptr<ImportPanel> m_importPanel`) and that's where the awkwardness lives:

- **`ImportPanel` can't free its own C++ object.** It owns its UI (the `Popup`) and closes it itself, but the heap object is owned by `AmethystLayer`. So after a self-close it fires an `onClose` *notification*, and `AmethystLayer` reaps the husk one frame later (`m_reapImportPanel` checked after `m_window.tick()`). The husk lingers until then.
- **`FileBrowser` still delegates its window close.** It doesn't own the secondary OS window, so "close" routes back out through a callback to `requestClose()` on the window context. This contradicts the principle that a panel should close itself — it only delegates because it wasn't handed the means to close its own window.
- **Re-entrancy tax everywhere.** Both panels defer their destructive actions (folder navigation / table rebuild; import + popup removal) by one tick, because doing them inside a button/​row input callback would destroy the in-flight frame mid-`EventSignal::fire()`. Works, but it's a pattern we're repeating by hand.

**Direction:** once the file browser itself becomes an in-app overlay (not a separate OS window), both panels can own their full lifetime + close uniformly, and the husk-reaping / `requestClose` indirection goes away. Ties into the planned drag-to-spawn-window work in [[Multi-Window (Multiview) Architecture]].

---

## TLAS must move out of the import path

`ImportPanel::doImport` manually calls `scene->buildTLAS()` after loading, because the glTF loader (`Engine/src/loaders/gltf/glTFLoader.cpp`) registers a BLAS per mesh but never builds the TLAS, and the per-frame `Scene::updateTLAS()` only refits existing instances. Forcing every caller to remember `buildTLAS()` is wrong — **the loader (or scene-mutation layer) should own this**, not each UI action. There's a `TODO` on the call site.

It's also a full rebuild on the UI thread with no sync against in-flight frames — see validation note below; the real fix is an incremental / properly-synced update.

### Quiet the "TLAS is not initialized" warning

Now that a scene can legitimately have no geometry (TestLayer's auto-load is commented out), `Scene::buildTLAS()` hitting `if (!m_tlas)` spams `RP_CORE_ERROR("TLAS is not initialized")` — e.g. TestLayer still calls `buildTLAS()` on an empty scene. Demote/guard this so an empty scene isn't an error.

---

## File browser needs a scrollbar

The list area (`Amethyst::Table` in `FileBrowser::setupListArea`) does not scroll — long directories overflow off the bottom. Needs a scrolling container / scrollbar around the table rows.

---

## Anti-aliasing

No AA on the scene render right now — edges are visibly jagged. It went unnoticed under the old ImGui setup but is obvious now. Needs some form of AA later (MSAA or a post-process pass like FXAA/TAA).

---

## Known validation error on import

Importing a model (reproduced with Sponza and adamHead) triggers repeated:

```
vkUpdateDescriptorSets(): pDescriptorWrites[0].pBufferInfo[0].offset (10280) must be a
multiple of device limit minStorageBufferOffsetAlignment (16) when descriptorType is
VK_DESCRIPTOR_TYPE_STORAGE_BUFFER.
```

Offsets seen: 10280, 20600, 39960, 51560, 61880, 81240, 92840, 103160, 122520 — none are multiples of 16. A `STORAGE_BUFFER` descriptor is being written with a sub-allocation offset that isn't aligned to `minStorageBufferOffsetAlignment`. Likely in the per-mesh/material SSBO sub-allocation done during load (buffer pool arenas — see `Engine/src/buffers/`). Rendering still works, but the descriptor offsets need to be rounded up to the alignment. Not import-UI-specific; it's the load/buffer path, surfaced now that import is reachable from the UI.

---

## Checklist

- [ ] Move `buildTLAS()` out of `ImportPanel` into the loader / scene-mutation layer
- [ ] Incremental + frame-synced TLAS update instead of full rebuild
- [ ] Quiet `"TLAS is not initialized"` for empty scenes
- [ ] Scrollbar for the file browser list
- [ ] Align `STORAGE_BUFFER` sub-allocation offsets to `minStorageBufferOffsetAlignment`
- [ ] Unify panel close/ownership once the file browser is an in-app overlay
- [ ] Add anti-aliasing to the scene render (MSAA or FXAA/TAA)
