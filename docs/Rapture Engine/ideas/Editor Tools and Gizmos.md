# Editor Tools and Gizmos

Where the transform gizmo, and interactive 3D editor tools generally, should live.

The immediate job is moving the gizmo out of [[Amethyst]] and deleting it there. The larger job is that the move forces a facility into the engine that every later 3D tool wants anyway, so it is worth designing that facility properly rather than carving out a gizmo-shaped hole.

## Which input system applies

It depends on what the thing actually is.

If a tool is truly in the scene, it can rely on the Rapture input system. If it is something in the editor, like a button or the viewport image, it belongs to Amethyst. The viewport image is just an image, with no context about what is drawn on it.

The gizmo depends entirely on what is on that image, and that is why Amethyst is the wrong owner for it. It is part of the reason for moving it out.

## The split

The engine owns the visual part. The editor owns the logic.

Clicking and dragging in the viewport does invoke Amethyst callbacks, but only at a surface level: the image has been clicked, the image is hovered. Never "this entity has been clicked".

The editor needs a gizmo, so the engine exposes something the editor can use — a 3D UI facility. The editor builds the draw list that gives the gizmo its look.

The engine hit tests against that draw list. The editor does not do the hit testing itself; it submits geometry and gets a result back.

The engine needs to know nothing about editor-only tools, and Amethyst is freed from the gizmo entirely.

Three parties, then, with a clean sentence each:

- Amethyst reports surface facts about a rectangle on screen — hovered, pressed at this pixel, released, and it holds the mouse capture.
- The engine turns pixels into 3D — it draws submitted primitives over the rendered image, and answers "what is under this pixel".
- The editor decides what any of it means — what a handle looks like, what dragging it does, what gets modified.

## The overlay

The facility is a per-viewport immediate-mode draw list. A [[Viewport]] already owns the camera, the render target and the pixel space, and it is already the thing that answers `pickEntity`, so it is the natural owner of the overlay too. Two viewports get two independent overlays with no shared state, which falls out for free and is the correct behaviour.

Submissions are cleared every frame. A tool that persists is a tool that resubmits. There is no handle to keep alive, no component to add, nothing to clean up when a tool is dismissed — the tool simply stops drawing. This is the opposite of the [[InstancedShapesPass]] route, where shapes are ECS components on scene entities, and that difference is deliberate: an editor tool is not part of the scene, must not be saved with it, must not be selectable as an entity, and must not appear in the outliner.

The primitive set wants to stay small and screen-aware:

- lines and polylines with a pixel width
- filled triangles and convex fans (arrow heads, plane quads, discs, ring segments)
- points and billboarded sprites
- a colour per primitive, with alpha

Two properties make the difference between "debug draw" and "usable tool":

**Constant screen size.** A handle anchored at a world position but sized in pixels stays grabbable regardless of distance. If the engine understands this as a per-primitive property, every tool gets it, and no tool has to recompute a world scale from camera distance each frame.

**A depth policy.** Test against scene depth, ignore it entirely, or draw occluded parts faded. Per submission, not global — a translate arrow usually wants to sit on top of everything, while a collider outline usually wants to be occluded honestly.

The pass should run after tonemapping. Gizmo colours are UI colours; pushing them through the tonemapper means the palette shifts with exposure and the handle you picked in the theme is not the handle you see.

## Handles and hit testing

Every submission can carry a caller-defined handle id. The engine stores it, and returns it. It never interprets it.

The editor's translate gizmo submits its X arrow under some id it made up, and gets that id back when the cursor is over it. The engine does not know what an axis is, and that is the entire point — the same mechanism serves a light radius handle, a probe volume corner, or a spline control point without the engine learning anything new.

Entity picking already works by reading an id back from a render target. Tool handles are the same problem with a different id space, so they should share one query. A single "what is under this pixel" answer that can be a tool handle or an entity, with tools taking priority, collapses the current two-step dance where the panel asks the gizmo whether it is hovered before it is allowed to pick an entity. It also means occlusion is resolved by the depth buffer instead of by ordering rules in the editor.

The cost is one frame of staleness, since the readback sees the previously rendered ids. That is already true of entity picking today. It matters for hover highlight and for the exact pixel of a press, and it does not matter during a drag at all, because once a handle is captured the drag is solved analytically from the ray and the constraint — nothing is hit tested again until release.

## Capture

Once a tool has been clicked, the interaction needs to be captured.

Without capture, moving the mouse quickly loses the target. The visuals also need feedback from the mouse position so they stay fresh on the next draw and can move with the drag.

Amethyst already has the mechanism — the window can capture the mouse for a UI object, and the viewport image is that object. What changes is who the captured stream is forwarded to. The panel stops interpreting the drag and starts relaying it: press at pixel, moved to pixel, released, plus modifier state, on into the tool layer.

Capture also settles priority, which is currently spread thin. While a tool holds capture, entity picking does not run and the camera controller receives no intent. The ordering is: captured tool, then hovered tool, then entity pick, then camera. The camera is what everything else steals from, and it should be the one thing that never steals back mid-drag.

## What the engine does not do

The engine gives the editor a ray and a projection, and stops there.

From a pixel it can produce a world-space ray, and from a world position a pixel. That is enough to build every constraint solve a tool needs — intersect the ray with the axis plane, project onto the axis line, measure the angle around the ring — and those solves are tool semantics, so they belong to the editor along with the snapping, the increments and the modifier keys.

That projection pair is also the thing that removes the last piece of coupling in the panel today, where the mapping from window pixels to render target pixels is derived from whatever rectangle the gizmo happened to be drawn into. It is the viewport's rectangle, and it should come from the viewport.

## Text

If the editor wants text on a gizmo, it can draw it on top. The editor controls the final location anyway, so it already knows where the text belongs.

It needs a world-to-screen projection to place it, which the overlay already owes it, so a delta readout or an axis label is an Amethyst text element positioned from a projected anchor. Text stays in the text system, which is where the shaping, the font atlas and the theming already are.

## What the gizmo becomes

Nothing of the gizmo stays in Amethyst. The widget, its canvas, its colours, its config and its operation and space enums all belong to the editor, because all of them are editor policy.

What survives the move is the interesting half: the axis and ring geometry, the constraint maths, the drag state machine. That becomes an editor-side tool that submits primitives with handle ids and asks the viewport what is hovered. It is roughly the same code with its two ends replaced — 2D canvas drawing becomes overlay submission, and hand-rolled screen-space distance tests become handle ids.

One thing worth fixing on the way through: the gizmo currently takes its matrices from the scene's main camera rather than from the viewport it is drawn in. A tool should only ever see the camera of the viewport hosting it, or it will be subtly wrong the moment there are two.

## What it unlocks

The gizmo is the first consumer, not the only one. Once submit-and-hit-test exists, each of these is a small editor-side file and no engine work:

- light handles — cone angle, radius, direction, the sun disc
- [[DDGI]] probe volume bounds, with draggable corners and a visible probe grid
- camera frustum handles for near and far planes
- collider outlines and their extents, once physics lands
- spline and path editing, which is the same control-point drag as the [[Curve Editor Widget]] but in world space
- measurement and snapping aids, rulers, alignment guides
- bone and joint handles

The test of the design is whether adding one of those requires touching the engine. If it does, the handle id abstraction leaked.

## Open questions

**Hit test mechanism.** Reading back an id target reuses the entity picking path and gets pixel-exact occlusion for free, at one frame of latency. A CPU-side analytic test against the submitted primitives has no latency but needs pick geometry that is not the same as draw geometry, and needs its own depth reasoning. The id target is the better default; a small analytic override for the press path is a refinement if mis-picks during fast motion turn out to be noticeable.

**Line thickness versus pick tolerance.** A one-pixel line is drawn correctly and picked terribly. Either the pass writes ids with a widened footprint, or primitives carry a pick radius separate from their visual width.

**Whether [[InstancedShapesPass]] merges into this.** The pipelines and primitive encoding are close enough that they should probably share a layer, with the component-driven pass reduced to another producer feeding the same drawing code. That is a consolidation, not a prerequisite.

Related: [[Gizmo]], [[Viewport]], [[Amethyst]], [[InstancedShapesPass]], [[Input]], [[GBufferPass]]
