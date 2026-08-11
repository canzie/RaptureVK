# Scene Object

An authored object that exists in its own right inside a [[Scene]]. It holds a place in the scene tree, appears in the outliner, owns an [[Entity]], and can contain other scene objects.

Prose calls it a scene object. The C++ class is `Instance`, and scene objects are the branch of it that has a place, the other being [[Scene Component]].

## What it carries

From `Instance`, shared with scene components: a stable id, a name, type info for `isA` and `as`, the serialize pair, a destroy signal and the owning scene.

Its own: parent and children, the child creation and lookup family, the parent changed hook, ownership of the entity, and the attached components.

`Node3D` sits below it and adds the transform. Placeless scene objects such as folders and the environment skip `Node3D` and derive from the base directly, which is why transform is a test for the tier rather than the definition of it.

## Owning the entity

The entity belongs to the scene object, and every component attached to it writes its storage onto that same entity. Systems therefore query one entity for everything about an object, rather than joining across an object and its parts.

The tradeoff this buys is described in [[Scene Object Model]]: one component of each type per object.

## Coming into existence

Authored directly into a scene, or authored outside one as an asset and spawned with a link back to it, so later edits to the asset reach what has already been placed.

A scene object asset holds a whole subtree, not a single object. Multi part things are expressed as child scene objects with their own transforms.

## Related

[[Scene Component]], [[Scene Object Model]], [[Scene]], [[Entity]], [[Terrain3D]]
