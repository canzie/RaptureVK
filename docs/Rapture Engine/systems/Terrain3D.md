# Terrain3D

**Source: `Engine/src/scene/instances/Terrain3D.cpp`**

The [[Instance]] class for a generated heightfield, streamed in chunks around the camera. Attaches a `TerrainComponent`, serialises the grid settings, and is what the editor's add menu creates.

> [!warning] Stopgap, not a pattern to copy
> This exists to get terrain back into a scene after the old `TestLayer` bootstrap was removed. It wraps the terrain system as it stands rather than fixing it, so do not use it as the reference for how a scene object should own a subsystem. The cleanup below is expected.

### What is wrong with it

**The component owns the generator.** `TerrainComponent` holds a `unique_ptr<TerrainGenerator>` and builds it in its constructor, so it is not plain data. Two consequences show up directly in `Terrain3D`:
- `config()` reads through `component->generator->getConfig()`, because the component never keeps the config it was built from.
- `setConfig()` replaces the whole component to change one value, destroying and re-initialising the generator. Editing `heightScale` from a properties panel would rebuild the terrain.

The generator belongs on the scene object; the component should hold the data needed to recreate it and nothing else.

**Heightmaps are not part of the config.** `TerrainComponent`'s constructor calls `generateDefaultNoiseTextures()`, so what a scene file stores is the chunk grid settings, not the terrain. Two projects with the same config only agree because the noise happens to be deterministic. The heightmap needs to be either a cooked asset or a serialised recipe, the same rule the procedural skybox follows in [[Environment]].

**The transform does nothing.** `Terrain3D` derives from `Node3D` so it has a position, but nothing reads it. The heightfield is anchored at the world origin inside `terrain_compute_bounds.cs.glsl`, which maps world XZ onto the noise textures, and the chunk window follows the camera:

```glsl
ivec2 cameraChunk = ivec2(floor(pc.cameraPos / pc.chunkSize));
ivec2 coord = cameraChunk + ivec2(localX, localZ);
```

The camera following is correct, that is what decides which chunks exist. The origin belongs on the heightfield sampling instead, so moving the node moves the landscape. That is an origin push constant threaded through the bounds compute shader and both the gbuffer and CSM vertex shaders, so the drawn surface, the shadow surface and the chunk bounds agree.

**Only one terrain draws.** `DeferredRenderer` renders `*terrainView.begin()`, the first `TerrainComponent` in the registry, while `Scene::onUpdate` ticks every one of them. A second `Terrain3D` updates and culls but never appears.

### Related

[[Scene]], [[Node3D]], [[CascadedShadowMapping]] — terrain uses a separate shadow pipeline with `VK_CULL_MODE_NONE` while everything else is front face culled, sharing one depth bias constant.
