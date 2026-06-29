# GBufferPass

**Source: `Engine/src/renderer/passes/GBufferPass.h/.cpp`**

First pass in the [[DeferredRenderer]] deferred shading pipeline. Writes G-buffer textures. Uses [[MDI Batching]] for entity draws and indirect multidraw for [[Terrain]]. Part of [[DeferredRenderer]].
