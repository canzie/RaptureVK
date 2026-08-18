# MaterialData

**Source: `Engine/src/assets/materials/MaterialData.h`, mirror in `common/MaterialCommon.glsl`**

The small per-material header, one entry in the `MATERIAL_DATA_SSBO` arena indexed by a material's bindless index. After the [[Unified Material Graph]] flip it holds no surface inputs — just the graph handle (`graphId` + `graphDataOffset` into the `GRAPH_DATA_SSBO` slice), a `flags` bitset for mesh-attribute presence and routing, and a temporary terrain carve-out (tiling/height/slope) kept until the terrain path is reworked. All actual channels (albedo, textures, factors) live in the graph slice.

A material is read as `getMaterialData(idx)` → `evalSurfaceGraph(graphId, si, graphDataOffset)`. The old flattened fields (texIndices, per-channel factors, `SAMPLE_*` macros, `evalStaticSurface`) are gone.

This header also carries the surface-output field mirrors (`SURFACE_DATA_FIELDS`, `SURFACE_DATA_DIFFUSE_FIELDS`): the CPU-side list of what `SurfaceData` / `SurfaceDataDiffuse` expose plus each field's GLSL fallback — the handshake the compiler emits against.

See [[Material]], [[MaterialInstance]], [[Material Graph Compiler]].
