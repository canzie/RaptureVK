# Renderer Restructure

> Status: **built** (2026-08-17), except the items under "Still open". Everything here is a proposal
> that survived contact with the code, not a settled architecture — the parts that turned out wrong
> are recorded as such.

**Related: [[Editor Picking and Tool Overlay]], [[Editor Tools and Gizmos]], [[Rendering and GI Roadmap]], [[G-Buffer Expansion]]**

## What this was for

One renderer owned the frame. It held the swapchain, the render target, the queues, the command pool, the frame index, and it submitted and presented. A second renderer was therefore a second submission into a second target, which makes a forward pass for transparency, an editor overlay, or a depth prepass impossible to express — each of them wants to write into the *same* depth and the *same* colour as the deferred renderer, in one submission.

The reframing that made it tractable: **a renderer is an ordered set of passes, nothing more.** Everything else it used to own belongs to whatever owns the frame.

## The shape

**`DrawManager`** owns one view's frame: the target, depth, HDR scene colour, the command pool, the frame index, acquire, the single primary buffer, the composite, and submit/present. A `Viewport` owns exactly one.

**`Renderer`** is down to a name and two calls:

```cpp
virtual const char *name() const = 0;
virtual void recordSecondaries(const RenderPassContext &context, JobContext &jobContext) = 0;
virtual void replay(const RenderPassContext &context, CommandBuffer *primaryCb) = 0;
```

Renderers are placed explicitly rather than by call order — `addRenderer(renderer, phase, order)` refuses an occupied slot, `setRenderer` replaces one. Each phase keeps a vector of `{order, renderer}` sorted on insert; iteration is the per-frame path and insertion happens once at setup, so a vector beats a map at these sizes.

**Phases exist because the composite is a boundary, not a terminator.** `DRAW_PHASE_PRE_COMPOSITE` draws into HDR scene colour and gets tonemapped; `DRAW_PHASE_POST_COMPOSITE` draws into the output image in the colours it wrote. An editor overlay must be the second kind: `Composite.fs.glsl` applies exposure and GT7 tonemapping, so a gizmo authored as a specific colour would otherwise dim as the scene brightened.

**Recording is parallel, replay is serial.** Every renderer's `recordSecondaries` runs as a job; the manager joins them and then calls `replay` in phase order on the frame thread. The split is not a performance choice — the layout transitions are tracked as one linear sequence (below), so emitting them from several threads would make the source stage and access nondeterministic.

Renderers currently in the list: `ShadowRenderer` (0), `DepthPrepassRenderer` (1), `DeferredRenderer` (2), all pre-composite.

## Barriers

`Texture` tracks a `TextureState` — layout plus the stage and access that last touched it — instead of a bare layout. `getBarrier2(usage, discardContents)` builds the transition and records the new state, with `TextureUsage` naming the intent (sampled fragment/compute, storage compute, colour attachment, depth attachment).

A pass declares what it reads through `fillInputs`, and `RenderPass::beginRendering` emits inputs and attachments together in one `vkCmdPipelineBarrier2`. That deleted three hand-rolled transition blocks — `GBufferPass::transitionToShaderReadableLayout`, `CompositePass::transitionSceneColorForSampling`, and `ComputeResource::readableAfter` — because a producer no longer has to guess what its consumer wants. `synchronization2` had to be enabled on the device; it was not before.

The v1 `getImageMemoryBarrier` overloads still exist and record a conservative `ALL_COMMANDS` state, so DDGI and the shadow maps stay correct while unconverted.

## Sharing

`RenderPassTargets` split in two. The manager owns depth and `sceneColorHdr` and publishes them; `DeferredRenderer::buildPassContext` copies that and adds its own four g-buffer textures. No signature changed, because every pass already read both through `context.targets`.

`SceneGeometryDraw` moved to the manager as `context.opaqueGeometry`, so the prepass and the g-buffer batch the scene once between them. A renderer drawing a different set — transparent materials, say — batches its own. `SceneQueryRenderer` already does exactly that, culling against a region frustum rather than the camera.

## The prepass

`DepthPrepass` fills shared depth with its own vertex-only shader that computes `gl_Position` and nothing else. It applies the transform in the same order the g-buffer does (`model * pos`, then `view`, then `proj`) rather than concatenating the matrices, so the depths compare equal and the g-buffer can load rather than clear and test `LESS_OR_EQUAL`.

Terrain is not in the prepass — it draws through a separate pipeline inside `GBufferPass` — so it gets no early-z. Correctness is unaffected because the g-buffer still writes depth.

## What this cost to get right

Recorded because each one is a class of mistake, not a one-off:

- **Deferring the resize is load-bearing.** It was replaced with a plain `waitIdle` on the reasoning that waiting covers it. It does not: an offscreen frame ends in `addToBatch`, so the command buffer is recorded but *unsubmitted*, and `waitIdle` does not wait for work that has not been submitted. Destroying its textures invalidated a buffer still headed for the queue.
- **Uploading at bind time breaks under parallel recording.** `bindBatch` called `uploadBuffers`, which was fine with one consumer. With the prepass and the g-buffer recording concurrently, two jobs mapped and unmapped the same VMA allocation and tripped an assert inside VMA. Upload belongs with populate, which happens once.
- **A dynamic state is part of the pipeline contract.** The prepass pipelines omitted `VK_DYNAMIC_STATE_VERTEX_INPUT_EXT` while `bindBatch` still called `vkCmdSetVertexInputEXT`.
- **A job that waits must yield.** `JobSystem::waitFor` is the blocking main-thread wait; from a fiber it has to be `JobContext::waitFor`, or the worker is held.
- **Profiler bookkeeping is real bookkeeping.** Fiber tracking was compiled out by an `&& false` in `TracyProfiler.h`, and every fiber entered Tracy under the same name. Neither mattered while one long job owned a worker; both broke the moment fibers actually interleaved.

## Performance found along the way

- `BufferLayout::hash()` allocated a `std::string` per attribute per mesh per frame, because `MDIBatchKey` began hashing the layout. `BufferAttribute::type` became `BufferAttributeType` and `getVkFormat` a table lookup, which took the per-mesh cost from ~3.5µs to ~1.4µs.
- The world bounding box was recomputed for every mesh every frame, twice — once in `populate` and again in the CSM's own walk, which does not even read it. It is now driven off journal bookmarks on `CHANNEL_TRANSFORM_WORLD` and `CHANNEL_MESH_BINDING`.
- GTAO ran at full resolution with 24 dependent depth taps per pixel and strides up to 32 pixels, costing ~4ms at 4K. Half resolution took it to a fraction of that. The blob format was left alone throughout — `MeshBlobHeader` has no version field, so the attribute type still goes to disk as a string and converts at the boundary.

## Still open

- **The overlay.** Nothing draws arbitrary 3D primitives. Instanced solids out of `MeshPrimitives` plus a line batch, in the post-composite phase, depth-tested against shared depth. This is what [[Editor Picking and Tool Overlay]] section 4 needs and what bone rendering needs.
- **`populate` is serial.** ~200µs on the frame thread ahead of every job, and the shadow renderer does not even consume it.
- **CSM walks the scene itself.** It builds its own batches every frame rather than sharing the view's, which is the largest remaining chunk of shadow recording. It needs a set that is not culled to the camera frustum, since a caster outside the view still casts into it.
- **Command pools are keyed by thread id.** That was a valid per-recorder key while one job owned a worker; with fibers migrating it no longer is.
- **GTAO wants a linear depth pyramid** so a long step reads a coarse level, and a depth-aware upsample so the half-res result does not halo at depth discontinuities. Both are marked TODO in place.
- **`BufferLayout::hash()` is still recomputed per mesh.** Caching it requires `BufferLayout` to stop being a public-data struct so there is something to invalidate on.
