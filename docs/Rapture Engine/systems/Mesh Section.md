# Mesh Section

A **section** is one contiguous run of a mesh's indices, drawn on its own. It is the smallest thing that can be given a material of its own, so a mesh is cut into a section wherever what draws it changes.

`MeshSection` lives in `Engine/src/assets/meshes/Mesh.h` and holds only `firstIndex` and `indexCount`, both relative to the mesh's own index data. It carries no material — the geometry layer knows nothing about materials. `Mesh` always synthesises one section covering everything when a caller does not cut its geometry, so [[MeshPrimitives]] and pre-section blobs need no special case.

## Sections are per material, not per primitive

A glTF `mesh` is an array of `primitives`, each carrying exactly one material reference. Sponza is **one** mesh with **103** primitives and **25** materials.

The importer merges those 103 primitives into one mesh asset with **25** sections — one per distinct material, in first-appearance order — rather than 103. This matches what Unreal shows when you open a Sponza static mesh: 25 material slots, not 103.

The consequence worth knowing: concatenating two primitives' index runs into one section means their indices have to be rebased into the merged vertex buffer, so a merged mesh over 65535 vertices is promoted to 32-bit indices. `glTF2Loader::mergePrimitives` decides this per mesh.

Because section index *is* material slot, there is no slot indirection anywhere — section `i` is drawn with slot `i`.

## Where the material lives

- `AMesh` holds `std::vector<AssetHandle> m_materialSlots`, one per section. This is the material the mesh is drawn with **where nothing else is chosen**.
- `Mesh3D` holds its own `std::vector<AssetHandle> m_materials`, one per section, filled from the mesh's slots by `adoptMaterialSlots` when a mesh is set. A run this object already had a material for keeps it.
- `MaterialComponent` holds the resolved `std::vector<Ref<AMaterialInstance>>` the renderer reads.

See [[MaterialInstance]] and [[Material]].

## Raster path

`SceneGeometryDraw::populate` walks a mesh's sections and calls `MDIBatch::addMeshSection` once per section. The batch's `firstIndex` is the mesh's index-allocation offset plus the section's `firstIndex`; `vertexOffset` is unchanged, since the whole mesh shares one vertex allocation.

**No shader change was needed.** A section is a draw, and the material index was already per-draw: `MDIBatch` smuggles the draw index through `cmd.firstInstance`, `instanceCount` is 1 so `gl_InstanceIndex` equals it, and `GBuffer.vs.glsl` reads `u_batchInfo[...].objects[gl_InstanceIndex]`. Note that `MeshGPUData.materialIndex` is dead — `SceneRenderData::updateMeshes` writes 0 to it and nothing reads it; the live one is `ObjectInfo.materialIndex`.

## Ray tracing path

A mesh builds **one** BLAS with **one geometry per section**, not one BLAS per section. `VkAccelerationStructureBuildGeometryInfoKHR` takes a geometry array, so it is still a single acceleration structure and a single TLAS instance — several BLASes would mean several instances, which is the thing sections exist to avoid.

Each geometry's `primitiveOffset` is `section.firstIndex * indexSize`, so the hardware reports `primitiveIndex` **relative to the run that was hit**. The hit shader therefore has to add the run's `firstIndex` back before fetching the triangle.

`RtInstanceData` builds a second SSBO of `RtGeometryInfo { materialIndex, firstIndex }`, one entry per section of every traced instance, laid out in the same order the BLAS holds its geometries. `RtInstanceInfo::firstGeometry` says where an instance's runs start in it, so a hit resolves its run as:

```
GeometryInfo run = Geometries[meshInfo.firstGeometry + rayQueryGetIntersectionGeometryIndexEXT(q, true)];
```

Only `ProbeTrace.cs.glsl` implements this. `radiance_cascades/Trace.cs.glsl` has its own stale copy of the `MeshInfo` mirror and is not referenced from any C++, so it was left alone.

## Still open

- **Per-section opacity.** `VK_GEOMETRY_OPAQUE_BIT_KHR` is per geometry, which is the other reason to want a multi-geometry BLAS: an alpha-masked run (Sponza's curtains, foliage) should drop the bit so its cutouts are actually tested, while the stone around it stays opaque. `BLAS::createGeometry` currently forces the bit on every run — it needs the material's blend mode to reach it.
- **Attribute unions cost memory.** `s_layoutCoveringEveryPrimitive` widens every primitive of a mesh to the union of their attributes and zero-fills what a primitive does not carry. Fine when primitives agree (Sponza), wasteful if one primitive of a large mesh carries an extra UV set.
- **Accessor sharing is not deduplicated.** Two primitives in different meshes may name the same glTF accessor; merging copies that data into both meshes. The old per-primitive path had the same behaviour.

Related: [[Scene Object]], [[AssetManager]], [[MaterialData]]
