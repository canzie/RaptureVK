# Entity Types and Authoring Schema

> Status: **largely implemented** (2026-08-04). Built: `TypeInfo`, `Instance`, `Node3D`, `Folder`, `Mesh3D`, `StaticMesh3D`, `Light3D` and the three lights, `Camera3D`, `Environment`, the scene's hidden root, the outliner and properties panel driven by the class chain, and prefab instantiation producing nodes. Not built: transform propagation, scene save/load, skeletal and animation classes. This document is the design of record; the parts marked Open are the only ones still undecided.

Entities are not arbitrary bags of components, so the engine should stop pretending they are.

Related: [[Play Mode and Scene Serialization]], [[ECS Tiers and Ownership]], [[Prefab]], [[Asset Metadata]], [[Entity]], [[Scene]], [[Components]], [[Amethyst UI Architecture]]

## Where this came from

Serialising a scene means walking each entity's components and writing them out, and with an open component set that needs either a virtual `serialize` on a base component class or a type-id registry with casts.

The way out is not a nicer dispatch. Components have dependencies nothing enforces, and nobody authors an entity by adding components one at a time. They want a mesh, or a point light, and the components are the implementation of that wish.

## The shape

**A tree of node classes is the authoring and scripting surface. The ECS is the storage and query engine underneath.**

Neither half works alone, which is why both are there.

Pure inheritance fails the query. If a base class holds the transform as a member and a subclass adds a mesh and material, then "give me everything with a transform, a mesh and a material" is a walk over a polymorphic tree with a cast per node, and that is the renderer's hot path.

Classic composition, where the node owns its components directly as members, fails the same way for the same reason. The data is scattered across individually allocated objects.

So the components live in the registry, where insertion and removal are cheap, iteration is dense, and a query composes with and/or/not. The node class is a thin interface over that. The ECS never learns what a light is; it answers "everything with these components" and nothing else.

The classes live in the **Engine**, not the Editor. A shipped game running Lua needs them exactly as much as the editor does, and the editor is just another consumer.

## Instance and Node3D

`Instance` is the base of everything in the tree, named after Roblox's, which is the model this follows most closely. `Node3D` is everything with a place in the world, and the `3D` suffix marks exactly that rather than disambiguating from a 2D that does not exist.

```
Instance                    name, parent, children, entity
├─ Node3D                   adds TransformComponent
│  ├─ Mesh3D                adds Mesh, Material, BoundingBox
│  │  └─ StaticMesh3D
│  ├─ Light3D               shared light surface, abstract
│  │  ├─ DirectionalLight3D
│  │  ├─ PointLight3D
│  │  └─ SpotLight3D
│  └─ Camera3D
├─ Folder                   no transform, organisation only
└─ Environment              placeless, at most one per scene
```

Three rules hold for every class:

**Single inheritance, one parent per class, no exceptions.**

**The constructor attaches that layer's components.** That is the whole mechanism, and it is why a class can be two lines. `Mesh3D`'s constructor attaches mesh, material and bounding box; `StaticMesh3D` adds nothing and exists because it is the named thing a user picks.

**A node never caches a component pointer.** The registry relocates components, so the node holds its entity handle and looks up on every access.

Beyond the constructor a class carries only helpers that read or write its own components, and its serialisation.

A bare `Node3D` is also the answer to grouping with a shared frame, so there is no separate empty or group type.

## Types without RTTI

Each class has a `TypeInfo` holding its name, its base, its depth, and its ancestry as a chain indexed by depth. Because inheritance is single, the ancestry is a straight line, so a subtype test is one depth compare and one pointer compare:

```cpp
template <typename T>
bool isA() const
{
    const TypeInfo &self = type();
    const TypeInfo &other = T::staticType();
    return self.depth >= other.depth && self.chain[other.depth] == &other;
}
```

`as<T>()` is that plus a `static_cast`. No `dynamic_cast`, no bit budget that runs out at 64 classes, and the same `TypeInfo::name` is what serialisation writes and the outliner shows.

`staticType()` is the class's own type, known at compile time. `type()` is virtual and returns the type of the actual object. The test compares one against the other, which is why both exist.

The `TypeInfo` needs a constructor rather than aggregate initialisation, because its chain has to contain a pointer to itself and a function returning by value can only take the address of a temporary.

## Node or component

**It is a node if it needs its own transform, or can have children, or drives things beyond itself. It is a component if it is settings for the one node that owns it.**

The failure mode of getting this wrong is worth naming. Put a light on the mesh's entity and it works until the first asset where the bulb is offset, at which point a `lightOffset` appears on the light component, then a `lightRotationOffset`, and a transform has been reinvented badly.

Shadow settings, a rigid body, a skeleton reference: components. A light on a lamp model, a collision shape sitting off-centre, a spring arm: nodes.

The third clause is what makes an animation player a node despite having no transform, and it is why non-spatial nodes are ordinary rather than a folder special case.

### The spring arm

A spring arm is a node, a child of the player, with the camera as *its* child. Godot agrees, and Unreal's version is a component only because Unreal gives components their own attachment tree inside the actor, which is the same idea wearing different clothes.

Its entity carries a transform and a spring arm component holding length, probe radius and lag. A system queries those two and writes the transform; the camera child follows by ordinary propagation.

It has to run **inside** the depth-ordered propagation walk at its own depth, not in a pass before or after it. Its input is its parent's world transform and its output is its child's parent frame, so a separate pass leaves the camera a frame behind.

## Optional parts

There is no feature system, no checkbox mechanism and no offer table, because the class owns the mapping from its own properties to components.

`Light3D::setCastsShadow` attaches or removes the shadow component, and which one differs per subclass: directional attaches the cascaded variant, point and spot the plain one. A collision property that is an enum would attach a different shape component per value. One property requiring another is the class's own logic in its own setter.

Anything a generic mechanism could express, the class expresses directly, and the things a generic mechanism could not express stop being a problem.

## Folders

A folder is a node with no entity.

A spatial node's transform frame is its **nearest `Node3D` ancestor**, so folders are skipped and dropping something into a folder can never move it.

That gives one tree with arbitrary organisation inside it, rather than a second parallel structure that has to be kept in sync. `node.parent` works when the parent is a folder, because a folder is a node.

## Nodes and entities are different populations

A node always has an entity, except folders. An entity often has no node.

Sixty pooled bullets spawned by gameplay are entities with no nodes. They never enter the tree, never appear in the outliner and are never serialised. Watching live entities during play is a debug view over the registry, which is a different panel with different rules.

So systems must never assume a node exists. The render query is components only, and it does not care where they came from.

The link runs both ways without either side searching: the node stores its entity, and the entity carries an `InstanceComponent` holding the node. That back reference is what viewport picking resolves through, since the scene query renderer hands back a raw entity id, and `nullptr` correctly means "not authored".

The scene owns a **hidden root** that every authored instance lives under. It is never named, shown or written to a file, and it exists so reparenting is ordinary tree surgery rather than a special case for things that happen to be at the top.

## Environment

The environment is placeless, so it derives from `Instance` rather than `Node3D`, and it owns the sky and atmosphere settings along with the derived skybox generator and image based lighting.

At most one per scene, because two of them would be indistinguishable in effect. That is a fact about them rather than a rule the tree enforces: it is deletable like anything else, and re-adding it goes through a dedicated panel rather than the ordinary add menu. Unreal does exactly this with the Environment Light Mixer, whose Create buttons appear only when that actor is absent.

The scene holds a raw pointer that the instance sets in its constructor and clears in its destructor, so the lookup is O(1) and every reader checks for null.

## Hierarchy and propagation

Two structures, deliberately.

**The instance tree owns child order**, which is an authoring concern: outliner rows and deterministic file output.

**A `ParentComponent` owns the parent link**, because propagation is engine work and the engine should never traverse instances. It holds the parent id and a depth, eight bytes with no allocation, written only by `Instance::addChild` and `removeChild`.

Propagation does not need a children list. It needs to read its parent's already computed world matrix, so an upward link plus depth ordering is sufficient. Sorting the pool by depth when the topology changes makes a plain view iteration already depth ordered, so the pass is linear and every parent is guaranteed done before its children.

Depth counts `Node3D` ancestors, so folders are transparent without the walk knowing they exist.

`TransformComponent`'s TRS is the authored **local** transform and gains a derived world matrix alongside it. For an unparented node the two are equal, which is why everything works today with no propagation at all.

**A rigid body owns the transform of its subtree until another body takes over.** A child with no body of its own is part of the parent's body, its shape folding into a compound shape; a child with its own body is independent and connecting them is a constraint, not parenting. Unity absorbs bodyless child colliders the same way, Unreal calls it welding, Roblox calls it a weld constraint. In the propagation pass this needs no special case: a node whose transform physics owns simply does not read its parent, and everything below it continues normally.

## Skeletons and animation

**Bones are not nodes.** They are a flat array inside a skeleton, with local transforms, parent indices and inverse bind matrices. glTF makes joints ordinary nodes, which is the temptation, but a hundred bone character would become a hundred outliner rows and a hundred transform components while the animation system wants a contiguous array it can blend and upload. Godot and Unreal both moved away from bone-as-object for exactly this.

**A skeleton instance is a live pose**, referenced by meshes rather than owning them. Ten meshes pointing at one instance deform in lockstep, which is what a body, its clothes and its hair want. Two characters are two instances sharing one skeleton asset.

The mesh contributes one `uint` to its per-draw info: the offset into the palette buffer. That is the entire runtime cost of being skinned, on top of the extra vertex streams. The GPU has no idea whether those matrices came from a clip, a blend, IK, a script or a ragdoll, which is why ragdoll later is just another producer and the render path is untouched.

A mesh belongs to exactly one skeleton. The vertex data forces it, since joint indices index a single joint list, and glTF has the same constraint. The correct key for a palette is (skeleton instance, skin), not (skeleton instance), because inverse bind matrices belong to the skin — in practice every mesh bound to one armature shares a skin, so it collapses to one palette.

**An `AnimationPlayer` is a node** with no transform, added anywhere, finding what it drives through the hierarchy. Two target kinds decided by the clip, not the player: a clip keying bones writes into a skeleton's bone array, and a clip keying nodes writes node transforms addressed relative to the player. Same class both times, because glTF already models both as channels targeting a thing.

Node-targeted animation is what makes a door and its handle work, and addressing by relative path is what survives prefab instantiation.

**The skeleton instance exposes a pose pipeline, not a writable bone array.** If systems and scripts can poke bones directly you get last-writer-wins and can never add a stage later. The chain is sample, blend, additive, then post. Blending is local space and IK is model space, so the conversion point is where the pipeline splits. A nod is a clip in the blend and a look-at is a post stage, which is how they compose instead of competing, and a script sets `lookAt.target` rather than touching a bone.

Only the pose-is-a-value part needs deciding now. Everything else is addable later.

## Serialisation

Each class serialises its own layer and chains to its parent, so a skinned mesh writes the skeleton, calls up for the mesh and material, which calls up for the transform.

Reading needs a dispatch, because the file names a class the reader has to construct. One `name -> factory` registry, which is the same thing Amethyst already does for its instances.

The scene file is a walk of the tree from the hidden root. Node references serialise as indices into that walk, and the tree owns its own ordering, so the writer is deterministic and the outliner stops reordering itself when something is deleted.

Assets are referenced by UUID, as everywhere else in the project.

## The editor surfaces

The **outliner** is the node tree. Rows are nodes, the type name comes from the class, and folders are ordinary rows.

The **properties panel** is driven by the class chain, base first, so a spot light shows Transform, then Light, then Spot Light. Each class contributes one collapsible header, and a class with no editor contributes nothing. The chain is already an ordered list of class identities, so no registry is needed — a list of `isA` tests in base-to-derived order is the same shape the panel already had.

The immediate payoff was three near-identical light editors collapsing into one shared editor plus three small ones.

## Prefabs

A prefab is a blueprint. Instantiating it produces ordinary nodes: the root is a `Node3D` holding the blueprint reference, mesh nodes are `StaticMesh3D`, and mesh-less nodes are `Node3D` groups. They serialise, select and edit like anything else.

There is no prefab instance class. The blueprint link is a component on whatever root the prefab defines, which matches "a prefab is a blueprint" more honestly than a dedicated node did.

Updating instances after the blueprint changes is destroy-and-reinstantiate, triggered explicitly. Since interiors are real entities, that discards edits made to them; the alternative is per-field override tracking, which is a whole architecture to avoid.

See [[Prefab]] for what glTF import produces and why the tree is the asset.

## Open

**Blueprint update losing interior edits.** Accepted above as the cost of interiors being ordinary nodes. Whether it stays acceptable is a question for the first time someone reskins an instance.

**Shadow authored fields.** The shadow components discard their constructor arguments, so resolution, cascade count and lambda exist only inside the derived object. Until they are real fields there is nothing for a node to expose, and those are the last two component-driven sections in the properties panel.

**Environment uniqueness.** Nothing prevents a second one today. The rule belongs in the environment panel, which does not exist.

**Terrain and indirect lighting** are still components read as a scan over a pool holding one row. Terrain wants the same treatment as the environment. Indirect lighting is deliberately deferred, since placeable probe volumes would be ordinary `Node3D`s with bounds and may dissolve the scene-wide settings question entirely.
