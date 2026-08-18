# Editor Picking and Tool Overlay

**Related: [[Editor Tools and Gizmos]], [[Viewport]], [[GBufferPass]], [[SelectionOutlinePass]], [[InstancedShapesPass]], [[Gizmo]], [[Amethyst]], [[G-Buffer Expansion]]**

Replaces the g-buffer entity id target and the Amethyst gizmo with two separate hit-test mechanisms, an injectable render pass interface, and an immediate-mode 3D overlay the editor draws its tools with.
Zero-assumption: every claim about current state is cited to source.

---

## 1. Starting state

> **Revised 2026-08-17.** Sections struck below have since been built or removed. The pick-query
> path exists as `SceneQueryRenderer` (`Engine/src/renderer/query/`), the id attachment and
> `SelectionOutlinePass` are gone, and the pass-ordering problem was solved by the
> [[Renderer Restructure]] rather than by an injection interface. What remains open is the overlay
> itself, sections 4 and 5.

**The gizmo lives in Amethyst.** `Engine/vendor/Amethyst/libamethyst/src/components/widgets/gizmo.cpp` is 1157 lines of 2D canvas drawing, screen-space distance tests and ray-plane constraint solving. `gizmo.cpp:153-192` defines a `GizmoCanvas` that captures the mouse through `Window::captureMouse` (`components/window.h:52`). The editor drives it from `ViewportPanel::updateGizmo` (`ViewportPanel.cpp:431-479`), which takes its matrices from `scene->getMainCamera()` (`ViewportPanel.cpp:460`) rather than from the viewport hosting it.

**Picking is coupled to the gizmo's rectangle.** `ViewportPanel::onViewportPressed` (`ViewportPanel.cpp:396-429`) early-outs on `m_gizmo->isHovered()`, then derives render-target pixels from `m_gizmo->canvas()`'s absolute position and size.

**There is still no facility for drawing arbitrary 3D primitives.** `InstancedShapesPass` has since been deleted; nothing replaced it. This remains the gap section 4 fills.

**Pass ordering is no longer fixed.** A `DrawManager` owns the ordered renderer list and a phase split around the composite, so an overlay is added rather than injected — see [[Renderer Restructure]].

**The TLAS is for ray tracing only** and is not a picking structure. `Viewport.h:23-30` gates acceleration structures per viewport via `ViewportConfig::enableAccelerationStructures`, so it is not universally present either.

---

## 2. The shape of the design

**Two hit-test mechanisms, divided by who drew the thing, joined only by one comparator.**

Anything the *renderer* draws is picked from a GPU id render. Anything the *editor* draws is picked analytically on the CPU. The line is not gizmos-versus-entities, which is fuzzy at light icons and bone joints — it is renderer-drawn versus editor-drawn, and it coincides with three independent fault lines at once:

- **Closed form versus arbitrary geometry.** The editor already computed the screen-space form of everything it draws, in order to draw it. The renderer's geometry has no closed form: it is skinned, instanced, GPU-culled, and lives in buffer arenas.
- **Cursor-tracking versus scene-anchored.** Tool handles move with the cursor and with tool state, so a readback answer about a completed frame is wrong exactly when it matters. Scene geometry moves only when the scene or camera moves, so a cached id render is *exact* until something invalidates it.
- **Tolerance-dominant versus coverage-dominant.** A three-pixel arrow needs a ten-pixel catch radius and pixel-exactness is meaningless. A mesh needs pixel-exactness at its silhouette and tolerance is a small correction.

Every case lands unambiguously. Gizmo axes, rings, plane quads, scale boxes, light and camera icons, probe volume corners, spline control points, bone joints, collider extents: editor-drawn, analytic. Scene meshes at any granularity, including sub-mesh sections: renderer-drawn, id render. Whatever tool is built next is editor-drawn by definition and needs no renderer change to become pickable.

The engine never learns what a gizmo is, what selection is, or what a tool is. It gains two general facilities — a scene query render and a pass injection interface — and the editor assembles picking, selection and tools out of them.

---

## 3. The scene path: pick queries

### 3.1 The query is a screen region, not a point

The pick primitive is a **frustum built from a screen-space region**, which is what CAD kernels have always done and what makes point-pick, marquee and lasso the same operation:

| Interaction | Region | Frustum |
|---|---|---|
| Click / hover | odd-width square aperture around the cursor, e.g. 11×11 | narrow sliver |
| Marquee | the drag rectangle | box |
| Lasso | the polygon's bounding rect, polygon rasterized as a mask | box, masked on resolve |

A point query with a one-pixel aperture is exact and intolerant; the aperture *is* the tolerance, and it costs nothing to widen because the cost is bounded by the region, not the screen.

### 3.2 What the render does, and the depth question

**It does not use a depth buffer, and depth test and depth write are both off.**

A depth buffer would keep only the frontmost fragment per pixel, which is precisely the information loss that makes the current id target unable to serve click-to-cycle or occluded outlines. Instead each pixel of the small target owns a **bounded list of `(id, depth)` entries** — a fixed depth of eight or sixteen, with an atomic per-pixel counter and an overflow flag. Every fragment that survives rasterization appends. Depth is written as a value in the list, never as a test.

Sorting happens on the CPU after readback. For an 11×11 aperture at sixteen layers that is at most 1936 entries — nothing. This is why no depth attachment is needed: the ordering the depth buffer would have computed on the GPU, discarding everything but the winner, is computed on the CPU while keeping everything.

The target is sized to the region, not to the viewport. The frustum is the region's frustum, so culling rejects essentially the whole scene for a click aperture: the work is one cull dispatch plus a handful of surviving draws rasterizing into ~121 pixels. This is not a re-render of the scene; it is the existing cull with a different frustum.

Marquee is the same render with a fatter frustum and correspondingly more survivors. Marquee semantics only need the front layer, so the marquee path caps the list depth at one and the cost stays bounded.

### 3.3 Identity is query-scoped

The editor allocates a **dense id range per query**, partitioned by whatever granularity that query wants — one range per object, or per object per section, or per element within a single mesh — and throws the table away afterwards.

There is no global registry, no persistent handle, no lifetime to manage, and no object that must exist merely to be pickable. Granularity becomes a per-query decision rather than an architectural commitment: the same machinery picks objects today and mesh elements later without changing anything but the table the editor built.

The id written by the shader comes from **the per-draw record**, not from the material. The renderer already carries a per-draw object index for bindless mesh and material lookup, so the pick shader is a single shader for every material in the scene. This is the specific mistake to avoid: requiring each material to author a picking variant makes any material that forgets one silently unpickable, and puts an editor concern permanently in the hands of every shader author.

### 3.4 Alpha-tested geometry is out of scope for now

Everything is treated as fully opaque. A cutout leaf picks as its quad.

The consequence is that foliage, decals and any alpha-clipped surface pick their full geometry rather than their visible shape. That is accepted for now. Fixing it later requires the pick shader to reproduce the material's opacity source, which is only tractable if opacity stays a data-driven input rather than arbitrary shader code — worth keeping in mind when the material graph decides how opacity is expressed, but not a constraint this plan imposes.

### 3.5 Resolving a query

Readback gives the region's lists. Resolution is entirely CPU and entirely editor policy:

- **Point pick.** Walk the aperture in an outward spiral from the centre pixel, front layer first, scoring candidates by distance from centre. An exact hit under the cursor always wins because it is reached first; a near miss is still found. Tolerance and exactness are not in tension — the tension only exists if you read a single pixel.
- **Escalating tolerance.** Resolve at a tight radius first so overlapping things stay separable, and only if nothing wins, resolve again at a loose radius so thin geometry is not painful. Same data, no second query.
- **Cycling.** The depth-sorted list at the resolved pixel *is* the candidate list. Repeated clicks walk it. No hide-and-re-render, no heuristics.
- **Marquee and lasso.** Union the ids over the region, masked by the rasterized polygon for lasso.

### 3.6 Caching and latency

The pick render is **out of band**: it is not a pass in the frame, it is a render issued in response to a query, into a target that is not the presented image.

That removes the ordering problem entirely. Nothing about the pick render depends on where the overlay is drawn, because the overlay is not in it.

Results are cached with a dirty flag invalidated by camera movement and scene change. A static scene under a static camera answers hover queries from a CPU-side copy with zero GPU work. When the cache is dirty the query re-renders — bounded, as above.

The readback should not use `Texture::readbackRegion` (`Texture.cpp:730-803`) with its `waitIdle`. It wants a fence or timeline wait, and for hover specifically an async ring where the answer arrives a frame later. Hover staleness is acceptable here for the reason the whole split exists: scene geometry does not move with the cursor.

---

## 4. The tool path: the overlay

### 4.1 Submission

The editor builds a draw list every frame. It is immediate mode: cleared each frame, resubmitted by whoever still wants to be visible, with nothing to keep alive and nothing to clean up when a tool is dismissed.

Primitives are screen-aware: lines and polylines with a pixel width, filled triangles and convex fans, points and billboarded sprites, each with a colour.

Two per-primitive properties matter more than the primitive set:

- **Constant screen size** — anchored at a world position, sized in pixels, so handles stay grabbable at any distance without every tool recomputing a world scale from camera distance.
- **Depth policy** — test against scene depth, ignore it, or draw the occluded part faded. Per submission: a translate arrow wants to sit on top, a collider outline wants honest occlusion.

Each primitive also carries the three things hit testing needs: a **handle id** the engine stores and returns but never interprets, a **priority layer**, and a **pick tolerance in pixels**.

### 4.2 Drawing versus hit testing are different operations

This is the part that is easy to miss. The overlay is **drawn on the GPU** by an injected pass, and **hit tested on the CPU** from the same submission. Its geometry never enters any id render.

Drawing: the overlay pass builds a vertex stream from the submission and draws it after tonemapping, so gizmo colours are the exact UI colours and do not shift with exposure.

Hit testing: for a query at a pixel, each primitive computes its screen-space distance analytically from the same parameters that produced its vertices — distance to a segment for an axis, to a projected ellipse for a rotation ring, to a quad for a plane handle, to a rect for an icon. A primitive is a candidate when that distance is within its own tolerance. There is no readback, no latency, and no pixel-exactness to fight.

Nothing about this requires the overlay to be pickable by the GPU, and making it so would cost the two things that make handles feel right: per-part tolerance, and an answer about *this* frame rather than a completed one.

### 4.3 Arbitration

Both paths produce candidates as `(id, priorityLayer, screenDistancePixels, depth)`. One comparator ranks the merged list:

1. **priority layer** — tools sit above scene geometry, so a handle beats an entity behind it
2. **screen distance in pixels** — within a layer, nearer to the cursor wins
3. **depth** — ties break to the nearest

The ordering matters and both obvious alternatives get it wrong. Sorting by priority alone means a tool pixel several pixels away beats an entity dead under the cursor, which is why picking near a widget feels grabby in editors that do it. Sorting by depth plus a coarse source order cannot express "two pixels away wins" at all.

Escalating tolerance applies across the merged list, not per path: resolve everything tight, and only if nothing wins, resolve everything loose.

---

## 5. Capture

Once a handle is grabbed, **hit testing stops entirely**. No other candidate may respond until release.

The captured tool receives raw pointer positions — including outside the handle, outside the viewport image and outside the window — and resolves them against a **constraint captured at grab time**: the axis, the plane, the ring's plane, the initial hit point, the initial transform. It never re-hit-tests, and never re-derives the constraint from the current cursor. That is an architectural guarantee against drag drift rather than a tuning exercise: if drag resolution can only see the captured constraint and a ray, it cannot drift.

Amethyst already provides the mechanism — `Window::captureMouse` (`components/window.h:52`), which the current gizmo canvas uses at `gizmo.cpp:175-176,188`. What changes is that the panel stops interpreting the drag and starts relaying it: pressed at pixel, moved to pixel, released, plus modifiers, forwarded to the tool layer.

Capture also settles priority against the camera. While a tool holds capture, no pick query runs and the camera controller receives no intent. The full ordering is: captured tool, then the arbitrated hover result, then the camera.

---

## 6. Selection outlines

Outlines stop being derived from a full-scene id buffer and become their own **per-frame injected pass**, which is what makes occluded outlines possible.

The pass renders only the selected set into a small mask target, writing selection id and depth with its own depth handling rather than the scene's. The outline is an id discontinuity between neighbouring pixels, which follows the silhouette exactly, holds constant width at any distance, and — unlike a full-scene id buffer — draws a border *between* two touching selected objects.

Occlusion becomes one comparison rather than a limitation: where the mask's depth is behind the scene depth, that pixel of the outline is occluded, so it can be dashed, dimmed or hidden as a policy choice. Wide or soft outlines are a jump-flood post-step on the same mask.

Style, thickness, colour and the occluded-portion policy are editor inputs. The engine's half is "render this set of entities into a mask"; it does not know the set is a selection.

---

## 7. The injection interface

Both the overlay pass and the outline pass are editor-owned passes that must run inside the engine's frame. Neither belongs in `DeferredRenderer`'s member list.

The renderer gains **named insertion points** and a registry of external passes at each. The points follow the existing sequence in `DeferredRenderer::recordCommandBuffer` (`DeferredRenderer.cpp:489-531`): after g-buffer, after ambient occlusion, after lighting, after skybox, after tonemap/composite, before present. An injected pass declares its point and a relative order within it.

An injected pass is a `RenderPass` (`RenderPass.h:65-134`) — the existing contract already fits, since it records into a secondary from a job, declares its attachments per frame in flight, and gets `beginRendering`/`endRendering` around the replay. What it additionally needs is read and write access to the frame's named targets, which `RenderPassContext` already carries, and a way to own targets of its own.

Compute passes need the same treatment with an execute-style entry rather than attachments, matching how ambient occlusion is already invoked (`DeferredRenderer.cpp:496`).

This is the interface a scripting layer would use later. Nothing about it is editor-specific: it is "insert work at a defined moment in the frame, with access to the frame's targets".

The pick query in section 3 is deliberately *not* part of this. It is out of band, driven by the editor asking a question, not by the frame advancing.

---

## 8. Who engages what

**The editor drives everything.** The engine provides three capabilities and no policy:

| Capability | Engine provides | Editor decides |
|---|---|---|
| Scene query render | render the scene for an arbitrary frustum into an arbitrary target, writing a per-draw id supplied by the caller | what the ids mean, region shape and size, granularity, spiral radius, escalation, what wins |
| Overlay | submission API, drawing, analytic hit testing against the submission | what the handles look like, what their ids mean, tolerances, priority layers, what a drag does |
| Pass injection | named insertion points, target access | which passes exist and what they draw |

A click in the viewport therefore flows: Amethyst reports a press at a window pixel on the viewport image → the panel maps it to viewport pixels and forwards it → the overlay is hit tested on the CPU for this frame → if no handle wins, a pick query is issued → results are arbitrated → the editor acts on the winner.

The engine is never told that a selection happened.

---

## 9. What this removes

- **The g-buffer entity id target** (`GBufferPass.cpp:123,464-473`) and the fifth attachment slot, once outlines move to their own mask pass and picking moves to query renders. The g-buffer returns to shading data only.
- **`GBufferPass::readEntityId`** (`GBufferPass.cpp:363-383`), `DeferredRenderer::pickEntity` (`:196-202`), `Renderer::pickEntity` (`Renderer.h:51`) and `Viewport::pickEntity` (`Viewport.cpp:62-78`) in their current form.
- **`SelectionOutlinePass`'s knowledge of selection** (`SelectionOutlinePass.h:74-76`) — the engine stops listening to editor events.
- **`Amethyst::Gizmo`** in its entirety. The widget, its canvas, its colours, its config and its operation and space enums are all editor policy. What survives the move is the axis and ring geometry, the constraint maths and the drag state machine, with its two ends replaced: canvas drawing becomes overlay submission, and hand-rolled distance tests become tolerances and handle ids on the submission.
- **The gizmo-canvas coupling in picking** (`ViewportPanel.cpp:396-429`), since the viewport supplies its own pixel mapping.

Fixed on the way through: the gizmo takes its matrices from the viewport hosting it, not from `scene->getMainCamera()` (`ViewportPanel.cpp:460`), which is wrong the moment there are two viewports.

---

## 10. What this requires the engine to gain

- **Culling against an arbitrary frustum**, not just the camera's. If culling is hardcoded to the view frustum this is the real work in the plan.
- **A per-pixel bounded fragment list target**, with an atomic counter per pixel and defined overflow behaviour.
- **A non-blocking readback path**, replacing `waitIdle` (`Texture.cpp:794-797`) with a fence or timeline wait, plus an async ring for hover.
- **An immediate-mode 3D primitive facility** with constant-screen-size and depth-policy properties, and analytic distance functions per primitive type.
- **Pass insertion points and an external pass registry** in the renderer.

---

## 11. Open questions

**Does the overlay replace `InstancedShapesPass`?** The primitive encoding and pipelines are close enough that they should probably share a drawing layer, with the component-driven pass reduced to another producer. It is currently commented out of the frame (`DeferredRenderer.cpp:513-515`), so nothing breaks either way. Consolidation, not a prerequisite.

**Where does the pick query live?** `Viewport` owns the camera, the targets and the pixel space, so it is the natural owner, but the query render needs renderer internals. Whether it is a `Viewport` method delegating to the renderer, or a service the renderer exposes, is unresolved.

**How does an injected pass own targets?** The existing passes create their own textures per frame in flight and hand them out through `RenderPassTargets`. An external pass needs the same ability without being in that struct.

**Marquee cost during drag.** Re-issuing the query every frame while dragging a large rectangle is the one path where the frustum is not a sliver. Throttling, or previewing cheaply and resolving on release, is unresolved.
