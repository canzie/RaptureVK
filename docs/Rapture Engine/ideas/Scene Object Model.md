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

---

# Sourcing

The section above says, in one line, that spawning a scene object asset keeps a link back to it and that placements record their overrides as a diff. [[Scene Object]] repeats the promise. Neither designs it, and the code does the opposite. This is that design.

## What is actually wrong

A scene object asset is a document. Placing one reads the document, builds fresh objects from it, gives every one of them a brand new id, and then drops the asset handle on the floor. Nothing on the result records where it came from. The content browser's spawn action fetches the document by handle and never stores the handle. `spawnSubtree` is the flatten.

So a placed Sponza is not a placed Sponza. It is one hundred and four unrelated scene objects that happen to have been typed out by a machine. Saving the scene writes all one hundred and four in full, because scene serialization is an unconditional walk of the tree with no notion of a boundary. The outliner shows all of them for the same reason.

Three consequences follow, and they are usually mistaken for three separate problems.

Editing the asset cannot reach what has been placed, because there is no edge to propagate along. The archived prefab work already hit this and resolved it as destroy-and-reinstantiate, which discards interior edits, with per-field override tracking deliberately not built.

There is nowhere to put a per-placement change, because a per-placement change only means something relative to a source, and there is no source. Every change is equally authored.

And "is this subtree a reusable unit" is unanswerable, because unit-ness is currently inferred from a file existing somewhere, which the placed copy has no knowledge of. That is the thing that makes the scoping rule unenforceable, and it is a symptom of the missing edge rather than a question about rules.

## The framing that was blocking this

The choice was posed as Godot or Unreal. Either nothing is special and unit-ness is just a file, or a tier exists that a child structurally cannot be, and two types shadow one concept.

Both poles answer the same question — what makes a subtree a unit — by attaching the answer to the subtree. One attaches it to the file, the other to the class. That shared premise is the actual constraint, and it is not a real one.

It is also a misreading of Godot. Godot is not the null option. An instanced sub-scene in a `.tscn` is one node carrying `instance=ExtResource(...)`, and the nodes inside it are written only when they carry an override — the save is an explicit diff against the source, computed by asking the source for its default and storing the property only if the live value differs. Godot has live links, overrides and propagation on a completely uniform node type. What Godot lacks is not the mechanism, it is good addressing: overrides are keyed by node name and parent path, which is why renaming or reparenting inside a source breaks them. Godot is currently fixing exactly that, adding a per-node `unique_scene_id` written into the file specifically to survive refactoring of base and instanced scenes; it is on the development branch and not in a stable release.

So one pole of the dichotomy has the mechanism this engine wants and is presently retrofitting the one part it got wrong — the part this engine already has right.

## How everyone else addresses an override

This is the question that decides whether the design survives contact with editing.

Godot addresses by name and parent path, and breaks on rename and reparent. Unity addresses by a file-local object id plus the source asset's guid, which is sturdier, but the ids are regenerated when a prefab is replaced or restructured, and losing overrides that way is a well known failure; Unity ships a dedicated tool for removing override data that no longer resolves. Blender addresses by property path rooted at the overridden datablock, and its manual states plainly that structural change in the source needs a resync, and that during a resync overrides can be deleted outright if the library changed them.

Three systems, three addressing schemes, and the same failure in all three: the name of the thing an override applies to is derived from where the thing sits or what it is called, so editing the source invalidates the name.

Rapture does not have this problem and has not noticed. Every object already serializes a stable UUID alongside its class and name, so a saved document already contains a permanent, position-independent, rename-independent name for every object inside it. That is the strongest of the four addressing schemes, and it exists today as a side effect of the id work.

There is one bug in the way. Opening a scene object asset in its workspace spawns it, which remints every id, and saving serializes the live tree back over the document. So every open-edit-save cycle rewrites every id in the asset. The key is stable in the format and unstable in practice, for a fixable reason: the workspace is editing those objects, not copies of them, so it should load rather than spawn. That also stops the ids churning in git on every save.

## Deletion, which is the hard one

Adding to a placement is easy in every system. Changing a value is easy. Removing something the source defines is where they diverge.

Unity can represent it: removed components and removed objects are first-class entries in the instance record, addressed the same way as any other override. Godot cannot, and this is worth stating precisely because it is an absence rather than a limitation anyone wrote down — there is no field, anywhere in the packed scene format, that records a node as removed, and instancing always recreates every node the source defines before applying property overrides on top. Godot's editor will let you delete such a node in the session; nothing persists it.

USD's answer is the one worth taking, and it is neither. A prim is not deleted, it is deactivated — `active` is ordinary metadata, authored like any other opinion, and a prim is active only if neither it nor any ancestor is deactivated. Suppression is an opinion, so it composes, layers and reverts like every other opinion, and no special case is needed anywhere.

That is the right answer here for a reason that has nothing to do with elegance: a source can reintroduce anything. Real deletion of something a source owns is a promise the placement cannot keep, because the next load rebuilds it. Suppression is a promise it can keep.

USD also has a genuine list-editing primitive underneath, where a list can be edited by an ordering of delete, prepend and append rather than replaced wholesale. That is the general form. Suppression is the small, sufficient case of it, and the general form is not needed to get started.

## What USD actually contributes

Not layering, which is a film pipeline concern about many departments editing one shot non-destructively, and not the full arc set, which is six kinds of link with a documented strength ordering that people find genuinely hard.

The load-bearing idea is much smaller, and it is the thing that dissolves the dichotomy.

In USD, whether a prim is a reusable unit is a boolean written on an ordinary prim. `instanceable` is metadata. It is authored, it can be cleared, it can be overridden per layer, and it changes nothing about the prim's type. A prim that is a unit and a prim that is not are the same kind of thing with a different opinion written on them.

So unit-ness is neither a type nor an accident of file-existence. It is a declared property of an ordinary object, and the link to the source is a separate represented thing. That is the third door, and it has been load-bearing in production for a decade.

The strength ordering is worth knowing about and not worth copying. Local opinions beat everything, then inherits, variants, relocates, references, payloads and specializes, in that order. With one source and one set of local opinions there is no ordering to get wrong, and the entire concept can stay out of the engine until something forces it in.

## The proposal

A scene object may carry a **source document**: the asset handle of a document its contents are read from. Any scene object may carry one. Carrying one does not change its class.

An object holding a source document is a **sourced object**. Its children are **sourced children**, built by reading that document rather than by the scene authoring them. An object with no source document is exactly what exists today.

The scene stores, next to the handle, an **override layer**: a sparse document of the opinions this placement holds about what came out of the source, keyed by **source id** — the id each object has inside the source document, which is not the same as the id it has in the scene.

Loading a sourced object reads the source, builds the subtree, then applies the override layer over it. Saving writes the handle and the override layer, and does not write the sourced children.

Two ids, and they do different jobs. The scene id is freshly minted per placement, unique in the scene, and is what a reference to an object resolves to — so two placements of one source do not collide. The source id is only unique within the source, which is exactly right, because an override layer is stored per placement and keyed by it, so two placements have independent override sets keyed by the same source ids.

Suppression is an override entry like any other: a sourced child can be marked suppressed, and it is not built. Additions are authored children written into the override layer in full, with their parent named by source id.

Nesting works because a source document may itself contain sourced objects. Addressing something two levels down needs the chain of source ids rather than one, which is a list of UUIDs and therefore still survives renaming and reparenting. A first implementation that only accepts depth one needs no format change to grow, because a chain of length one is a chain.

## Why this is a small change

Most of it already exists, which is the main argument for it.

Every `deserialize` in the codebase is already written as take-the-key-if-present, otherwise keep the current value, and the document reader guarantees this in its own header: a missing key or a wrong type returns the fallback. Applying a sparse override document over a live object is therefore already the semantics — it is a second `deserialize` call. No per-field override machinery, no per-property diff records, no touching any existing class.

Reconciling a live tree against a document by id already exists and works. Play mode's restore matches live objects to a snapshot by id, reparents what moved, deserializes over what stayed, creates what is missing and destroys what is extra. That is the algorithm for re-applying an edited source to something already placed, already written and already debugged for another purpose.

And a sourced object already ships. `SkeletonPose` holds an asset handle, builds its entire bone subtree from that asset rather than from the document, marks those children unserialized so the scene never writes them, and on load rebuilds the whole subtree from the handle alone. It is the proposed mechanism, specialised to one asset type. The generalisation is to point it at a document instead of a skeleton, and to add the override layer it does not have — and note that it does not have one, which is why a posed skeleton's pose does not currently survive a save.

Even the flags exist. A child can already be marked as belonging to the object that made it rather than to whoever authored the scene, and separately as written or not written. Sourced children want the second without the first: visible in the outliner, absent from the scene file. That is a flag combination the code already spans.

## What it costs

Composition happens at load, so a scene stops being self contained. Opening it depends on every source document it names resolving. A missing or failed source leaves a hole where a subtree was, and that needs a real answer in the editor rather than a log line.

Saving stops being "serialize the tree". Producing an override layer means knowing which values differ from what the source would have produced, which means either recomposing to diff against, or recording opinions as they are authored. The first is simpler and costs a load; the second is faster and puts a hook in every setter. This is the largest unresolved piece.

Every `deserialize` must continue to never write a value it did not read. That is true today by convention across every class, not by anything enforcing it. One class regressing to an unconditional write silently breaks overrides on that class, and the failure is quiet.

Overrides orphan when a source restructures. Every system surveyed has this and none has solved it. The choice is what to do with an override whose source id no longer exists, and the answer is to keep it in the file and report it rather than drop it, so that reverting the source restores it. Dropping silently is what makes the Unity and Blender versions of this painful.

The editor gets harder in a way that is easy to underestimate. Every field in the properties panel now has three states rather than one — inherited, overridden here, and suppressed — and writing to a field has to know which layer it lands in. USD ships an entire inspector for this question and people still find it hard. This is the real cost, and it is UI work, not engine work.

## The scoping rule this started from

The original question was how to allow a script only on a reusable object, given that reusable is a state rather than a type. Sourcing does not make it a type, and pretending otherwise would be dishonest.

What it does is move the question. Behaviour is not a per-placement property — the scripting notes already concluded that script text needs no override because it is not per-placement mutable. So the rule is not "which objects may carry a script", it is "which fields may appear in an override layer", and script source is simply not one of them.

That is a declaration on a field, checked in one place, rather than a condition every UI path has to remember. It is not free and it is not structural, but it is one check instead of many, and it is the same mechanism that answers the same question for every other non-overridable field.

## Naming

The category has an established name and it is not "prefabs". Prefab is one product's word for one instance of it.

USD calls it **composition**, and the individual links **composition arcs**; the DCC world calls the same family **referencing** with **overrides**; game engines say prefabs, nested prefabs and variants. The underlying idea is older than all of them and the language literature calls it **prototype-based inheritance**, with the specific practice of an object storing only its differences from its prototype called **differential inheritance** — the Self and JavaScript lineage. I have not traced a canonical citation for that last term and would not put weight on it, but it is the accurate description: a placement is a delta against a prototype.

For what this proposal introduces, the words are **source document**, **override layer**, **sourced object**, **sourced child** against **authored child**, and **source id** against scene id.

Two collisions were avoided deliberately. "Composed" is USD's word and the natural one, but this document already uses composition for what an object is made of, which is components, and one word for two relations is the mistake this model exists to avoid. "Reference" and "link" are both taken — the first by references between objects in a scene, the second by the load-time step that resolves them.

## Still open

Whether the override layer is produced by diffing on save or recorded as opinions are authored.

What the editor shows for a source that will not load, and whether a placement can be repaired without losing its overrides.

Whether "suppressed" is a field on the override entry or a distinct kind of entry, which matters once list editing is wanted for reordering as well as removal.

Whether a sourced object can be edited in place in the scene, the way Unreal's level instances can, or whether editing means opening the source in its workspace.

Whether the source-id chain for deep overrides lands with the first version or after it.
