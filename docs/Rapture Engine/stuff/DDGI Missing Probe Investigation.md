# DDGI Missing Probe Investigation

Black square tiles and blue-ish patches in DDGI irradiance in the middle of Sponza, visible in raw irradiance mode.

## The cause

`m_ProbeOffsetTexture` was never cleared. `DynamicDiffuseGI::clearTextures` transitions and clears the radiance, visibility and classification atlases and does not mention the relocation offset atlas at all, so it began life as uninitialised VRAM.

Two things then kept the garbage alive:

`ProbeRelocation.cs.glsl` is a read-modify-write. It loads the probe's current offset, derives `fullOffset` from it, and only accepts the new value when `dot(normalizedOffset, normalizedOffset) < 0.2025`. A large garbage offset fails that clamp, so the shader stores the value it loaded straight back, every frame, forever.

`DynamicDiffuseGI::relocateProbes` also raised its pre-write barrier with `oldLayout = VK_IMAGE_LAYOUT_UNDEFINED` every frame, which explicitly permits the driver to discard the image contents — on the one texture in the system whose algorithm depends on reading back what it wrote last frame. `Texture` tracks its own layout and offers a `getImageMemoryBarrier(newLayout, src, dst)` overload; the DDGI code called the two-argument version and hardcoded `UNDEFINED`. Harmless for the classification atlas, which every probe overwrites each frame. Not harmless here.

A probe holding a garbage offset is displaced arbitrarily far from its grid slot, so it is simply absent where it should be, and the cell it should have covered has nothing valid to interpolate from, which is the black tile. Those same probes also classify as `INACTIVE_NO_GEOMETRY`, because `ProbeClassification.cs.glsl` computes its plane intersections from `DDGIGetProbeWorldPosition`, which applies that same garbage offset.

Uninitialised VRAM holds whatever the previous allocations left behind, which is why this depended on load order: deserialising a whole world allocates and frees a very different set of resources than dropping one asset into a live scene, so the atlas landed on differently dirtied memory. That is why it looked like something was cached for an entire run, why re-importing did not clear it, and why delete then save then reopen worked.

## How it was actually found

By drawing the probes. `RENDER_SHOW_DDGI_PROBES` had existed in `RenderSettings.h` and `RenderFlags.glsl` for a long time and was read by nothing, so every attempt to reason about probe state from source was guesswork, and all of it was wrong. The first screenshot from [[DDGI Probe Debug Pass]] showed the probes were not inactive at those locations, they were missing from the grid entirely, which pointed straight at probe positions rather than anything to do with assets, geometry or the acceleration structure.

Build the observability before theorising.

## Eliminated along the way, with evidence

- **Probe volume coverage.** The volume genuinely does not cover Sponza, spanning 21.4 x 10.5 x 9.45 against a scene of 29.8 x 12.5 x 18.3, roughly 31% of the bounding box. Real, still worth fixing, but the artifacts sat in the centre of the volume.
- **TLAS instance identity.** `instanceCustomIndex` was the array position while `RtInstanceData` rebuilt on an instance count comparison, so a removal plus an addition in one frame silently desynced the mesh table. Real bug, fixed with stable slots, unrelated to this.
- **Shadow maps.** Probe rays use ray-traced visibility, not shadow maps.
- **A second RT renderer.** Every preview workspace passes `enableAccelerationStructures = false`.
- **Cooked geometry.** All 103 cooked Sponza meshes parse clean out of their `.rasset` files: consistent attribute tables, POSITION at offset 0, stride matching the summed attribute sizes, index counts matching index bytes, positions inside Sponza's bounds.
- **Meshes missing from the structure.** Every primitive deserialises with `rayTraced: true` and the log shows the TLAS building with all 103 instances, no failures.
- **A bisect** over `139afc0..a822cb1` landed on `a822cb1`, but every good verdict was taken on the import path and the bad end on a `.rapt` load. Two different paths, so the result meant nothing. Do not trust it.

## Still open

`MeshAllocatorParams::indexType` carries two incompatible conventions. The importer writes the glTF accessor `componentType` (5123 / 5125) at `glTFLoader.cpp:596`, `Mesh::serializeGeometry` writes a `VkIndexType` (0 / 1) at `Mesh.cpp:149`, and the cooked Sponza content holds 102 of the former and one of the latter. Consumers disagree too: `Mesh.cpp:109` tests against `VK_INDEX_TYPE_UINT32` while the shader's `fetchTriangleIndices` tests against 5125, and since `RtInstanceData.cpp:96` feeds it `getIndexType()`, the shader's 32-bit index branch is unreachable. Sponza is entirely 16-bit so nothing breaks today.
