# Editor Tools and Gizmos

Where the transform gizmo, and interactive 3D editor tools generally, should live.

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

## Capture

Once a tool has been clicked, the interaction needs to be captured.

Without capture, moving the mouse quickly loses the target. The visuals also need feedback from the mouse position so they stay fresh on the next draw and can move with the drag.

## Text

If the editor wants text on a gizmo, it can draw it on top. The editor controls the final location anyway, so it already knows where the text belongs.

Related: [[Gizmo]], [[Viewport]], [[Amethyst]]
