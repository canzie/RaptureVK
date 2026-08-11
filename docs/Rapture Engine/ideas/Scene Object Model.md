# Scene Object Model

The vision for how authored objects are structured, and the rules that decide where a new feature goes.

## The law

The scene tree expresses containment. Composition is expressed by components. Nothing is ever a child in the tree purely to add a capability to its parent.

Godot breaks this by using one edge for two unrelated relations, "is spatially inside of" and "is made of". A collision shape is not positioned beneath a body in the world, it is part of the body, and an engine that cannot say so has to invent a validation warning to tell you your composition is incomplete.

## The two tiers

A [[Scene Object]] exists in its own right. It has a place in the tree, appears in the outliner, owns an entity, and can contain other scene objects.

A [[Scene Component]] exists only as part of one scene object. It has no place, no children, and no independent existence.

The heuristic that decides almost every case: if you would ever want two of them at different positions, it is a scene object. Transform is not the law itself, because a folder is placeless and still belongs in the tree, but it is the test that answers the question in practice.

## Why not Unreal's shape

Unreal's axis is transform and attachment, not physicality, and their own documentation says so. The confusion comes from `AActor` carrying three unrelated jobs at once, spawn unit, network unit and transform root, while having no transform of its own.

Their three tiers map onto two here.

| Unreal | Rapture |
| --- | --- |
| `UActorComponent`, no transform, abstract behaviour | scene component |
| `USceneComponent`, location, no geometry | scene object |
| `UPrimitiveComponent`, geometry, renders and collides | scene object |

Hoisting the primitive tier up into scene objects is what removes the need for component nesting. Unreal nests because the actor has no transform and needs a root component to hold one. A scene object holds its own, so the reason is gone and the component list stays flat.

It also removes their wrapper classes. `AStaticMeshActor` exists only because a component cannot be dragged into a level. A scene object is directly placeable, so there is one class where Unreal has two, and a light stays a scene object instead of being pushed down into a component and given an actor wrapper to become placeable again.

## Extending an object

The tier is a mechanical choice, not a judgement call.

Every object of a kind gains data, so the data goes on the component that kind always attaches. No new class.

Some objects gain an optional capability, so it becomes a scene component that can be attached and detached. No new class and no new tree node.

A genuinely different kind of object gets a scene object subclass. A mesh that deforms qualifies. A mesh that can be pushed around does not.

## Ticking

Components do not update by default, and a component that never opts in costs nothing because it is not in the list at all. Opting in is one switch rather than Unreal's two, since there is no tick dependency graph here to declare capability against separately.

When there are many of the same kind, write a system rather than a tick. The physics simulation steps every body in one call, which is the point of the ECS underneath. Update is the escape hatch for bespoke one off behaviour. Ticking hundreds of one thing is the signal it wanted to be a system.

## Entities

A scene object owns the entity. Components attach their storage to that same entity, which keeps every system query on one entity instead of fracturing into a join.

The consequence is at most one scene component of each type per object. That lines up with the heuristic, because the cases wanting multiples almost always want distinct transforms, and those are scene objects.

A component may create its own entities for derived work, generated geometry or debug drawing that needs a real mesh and material of its own. Those are derived, never authored: no name, no id, never serialized, never in the outliner. That boundary is what stops a component with an entity from quietly becoming a scene object with the outliner switched off. Transient per frame debug geometry needs no entity, an immediate mode draw from update is simpler than owning entity lifetime for one frame.

Unreal's render state and physics state are the same idea under other names, a component registering with a subsystem and being torn down when it unregisters. Both are covered by attach and detach, with the change channels already serving as the render side.

## Authoring outside a scene

The asset is the serialized form. A scene object never exists without a scene and never needs to, so authoring one outside a scene means editing a document. The object workspace opens it into its own preview scene, you manipulate real live objects there, and it serializes back on save.

That workspace is its own thing rather than a second level editor. It shows an outliner for selecting scene objects and a component panel authoring one selected object at a time.

A scene object asset holds a subtree, not a single object. A cluster of rocks is three child meshes with three transforms, in the one tree that already exists, rather than Unreal's three mesh components with relative transforms hidden inside a component list.

## Asset form

Two kinds of asset want two containers, and the split is by whether the payload is a document or a cooked blob.

Document assets, scene objects and modules and materials and scenes, serialize to a document already. They belong on disk as text under a per type extension, with the metadata as a header object inside the document rather than a binary record in front of it. Readable, greppable, and diffable by the tools a project is already versioned with.

Cooked assets, textures and meshes and audio, are compressed blocks and vertex data. Diffing them means nothing, so they keep a binary container. A per type extension still buys legibility in the browser and on disk for nothing.

The shipping bake packs everything into the binary form regardless, so editor legibility costs the runtime nothing. The extension naming the packed form and the extension naming the editor's own file are two different things and should not be the same one.

Metadata carries the class an asset holds, so the browser can show a scene object asset as the `StaticMesh3D` or `PointLight3D` it is rather than as an undifferentiated scene object, and so assets can be filtered by class without being loaded. Modules already work this way; scene objects reuse the same field, resolving it through their own registry.

## Decisions settled

Rigid bodies are scene components. Nothing simulated stands alone in the tree, so the host pointer, the off tree member and the inverse offset all go away, because the simulation writes to the entity's transform which is the object's transform.

An environment is a placeless scene object in the outliner, and sky, fog and sky light are components on it. One of each is enforced by structure rather than by convention. Post process belongs on the camera, since it is a property of the view, with the environment holding the world default a camera without its own grade falls back to.

Folders stay real serialized scene objects rather than editor only decoration, because they are path segments and a path that resolves in the editor but not in a build is a trap. Whether they stay that way is revisited once scripting exists.

Spawning a scene object asset keeps a link back to it, so editing the asset updates what has already been placed and instances record their overrides as a diff. Prefab therefore stops being an asset type and becomes that propagation mechanism.

Scripts are a later third origin and a different kind. A script defined scene object is a class rather than an asset, sitting in the same tier as the engine's own subclasses and constructing its components in code.
