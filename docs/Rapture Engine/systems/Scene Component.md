# Scene Component

An authored capability that exists only as part of one [[Scene Object]]. It has no place in the scene tree, no children, and no existence apart from its owner.

Distinct from an ECS component, which is storage the engine iterates. A scene component is the authored, named, serialized thing a user attaches in the editor; the ECS component is how it is stored. Users never touch the latter, so the shared word costs nothing.

## What it carries

From `Instance`, shared with scene objects: a stable id, a name, type info, the serialize pair, a destroy signal and the owning scene.

Its own: a back reference to its owner, attach and detach hooks, and an optional update.

## Attach and detach

Where a component registers with and unregisters from a subsystem, creating a physics body or a GPU resource and destroying it again. Unreal's render state and physics state are two specialised versions of this same hook.

Because a scene component is a polymorphic object owned by a unique pointer and never copied, it is allowed a destructor and allowed to own such a resource. This is the tier that lifecycle side effects belong in, and the reason they must not go in an ECS component.

## Updating

Off by default. Enabling it inserts the component into a flat list on the [[Scene]], so components that never opt in cost nothing at all, with no virtual call and no tree walk.

Prefer a system when there are many of one kind. Update is for bespoke one off behaviour, not the default way components do work.

## Entities

A component reaches its owner's entity to read and attach storage, and needs no permission to do so.

It may also create its own entities for derived work such as generated geometry or debug drawing that needs its own mesh and material. Those are derived rather than authored: unnamed, unserialized, absent from the outliner, and never the owner's authored data. Transient per frame debug geometry should use an immediate mode draw instead of owning an entity.

## Staying flat

Components never nest. Unreal nests because its actor has no transform and needs a root component to hold one, which is what grew their second hierarchy. A scene object holds its own transform, so anything wanting a distinct position is a child scene object instead.

## Related

[[Scene Object]], [[Scene Object Model]], [[Scene]], [[Entity]]
