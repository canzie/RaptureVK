# Custom Entity Component System

> **CANCELLED.** The engine stays on EnTT. See [[ECS Tiers and Ownership]] for what is being done instead.
>
> The investigation was worth doing but reached the wrong conclusion. Nearly everything this document wants — derived data out of components, hierarchy with correct lifetime, transform propagation, a field description layer, change tracking, opaque handles — is achievable on top of EnTT, because none of it depends on the storage layer. What a custom ECS would genuinely add over EnTT is direct-indexed storage for near-universal components (a performance win, and section 3 of this document argues performance is not the problem) and the ability to *enforce* that writes go through a tracking accessor rather than merely offering one.
>
> Kept for the analysis in Part I, which is still accurate and is the basis for the replacement plan.

The question is not "how do we reimplement EnTT". That question produces a worse EnTT. The question is: **the engine has accumulated a set of workarounds that exist only because the ECS underneath it cannot express something. Which of those disappear if the ECS is designed for this engine instead of for everyone?**

Part I builds the case from what the code actually does today. Part II is the design that follows from it. Part III is whether it is worth doing.

---

# Part I — Why bother

## 1. What an ECS is, and what problem it solves

An entity-component-system is three ideas.

An **entity** is an identity and nothing else — a number. It has no data and no behaviour. The intuition to unlearn is that an entity is an object; it is closer to a row id in a database.

A **component** is a piece of data attached to an entity. `TransformComponent`, `MeshComponent`. A component belongs to exactly one entity and knows nothing about which one.

A **system** is code that runs over every entity that has some particular combination of components. "For everything with a transform and a mesh, submit a draw call."

The reason to build software this way is memory layout. The object-oriented alternative gives each game object its own allocation and puts every piece of its data in it. Rendering then walks a list of pointers, jumping to a scattered address per object and pulling in a whole cache line of which it uses forty bytes. An ECS instead stores all the transforms together and all the meshes together, so a system that wants transforms reads a contiguous array and every byte the cache fetches gets used. On a scene of a few thousand objects this is the difference between the loop being memory-bound and being free.

Everything difficult about an ECS follows from one tension: the layout that is fast to *scan* is not the layout that is fast to *change*, and it is not the layout that answers "what components does entity 4192 have" quickly either. Every design in Part II is a different position on that trade.

Concretely, an ECS has to answer two questions, and its data structure is whatever answers both:

1. **Random access.** Given entity `E` and component type `T`, where is that data?
2. **Iteration.** Which entities have *both* `T` and `U`, and where is all of it?

Some vocabulary used throughout, so it is not ambiguous later:

- **Pool** — the storage for one component type. `TransformComponent` has a pool; `MeshComponent` has a pool.
- **Dense array** — an array with no holes in it, so scanning it wastes nothing.
- **Query** or **view** — a request for all entities matching a set of component types, which you then iterate.
- **Structural change** — creating or destroying an entity, or adding or removing a component. Distinguished from writing to a component that already exists, which is *not* a structural change and is always cheap.

## 2. What this engine's workload actually looks like

This matters more than any general argument, because the right ECS for a bullet-hell shooter is not the right ECS for a renderer, and general-purpose libraries are necessarily built for the average of all users.

Counting the real query sites in the Engine gives this picture:

| What runs | Which components | How often |
|---|---|---|
| Shadow caster submission | transform, mesh, bounding box | once **per shadow-casting light** per frame |
| G-buffer submission | transform, mesh, material, bounding box | per frame |
| Ray-tracing instance list | transform, mesh, material, excluding lights | per frame when dirty |
| Physics writeback | transform + body handle | per physics step |
| GPU buffer sync | one component type at a time | per frame, dirty entries only |
| Camera, environment lookups | by entity handle | a few per frame |
| Inspector | every component on one entity | per editor click |
| Loading, spawning, editor edits | structural change | per load or per click |

Four things stand out.

**The same handful of queries run every frame, and they are all known when the code is compiled.** There is no user typing arbitrary queries at runtime. That removes any need for query planning, caching, or a general matching engine — features that cost a lot in a general-purpose ECS.

**Almost every hot query contains the transform.** It appears in roughly half of all query sites. Transform is not one component among twenty-four; it is the component, and a design that treats it as ordinary is leaving something on the table.

**Structural change is rare and never in a frame-critical path.** Entities are created when a model loads or when someone clicks in the editor. Nothing spawns thousands of entities per frame. So an ECS that makes adding a component *slow* in exchange for making scans *fast* is trading in the right direction here — the opposite of what a simulation-heavy game would want.

**The output is almost always a GPU buffer.** The engine does not iterate transforms to make decisions; it iterates transforms to pack them into an SSBO and upload them. This is the single most important fact in this document and section 9 is built on it.

Scale, for calibration: Sponza is about 400 nodes; a large scene is 10–50k entities. At those counts a linear pass over an array is measured in microseconds under any design below. **Performance is therefore not the reason to do this.** It is a constraint (do not make it worse) rather than a goal. The reason is section 3.

## 3. The evidence: what the code works around today

This is the actual argument. Each item below is a real piece of the engine that exists to compensate for something the ECS cannot do. For each one: what it is, why it had to exist, and what it implies about the ECS.

### 3.1 `HierarchyComponent` — parent/child as a component

Entities are stored flat, so parent/child relationships are expressed by giving entities a component holding `Entity parent` and `std::vector<Entity> children`, with static helper functions keeping the two directions in sync.

**Why it exists:** EnTT has no notion of one entity relating to another. There is nowhere else to put it.

**What it costs:** three separate things.

First, lifetime correctness. `Scene::destroyEntity` destroys an entity without touching the hierarchy, so destroying a parent leaves its children pointing at a dead parent and leaves a dangling entry in the grandparent's child list. It is not a live bug — the outliner is the only caller and it correctly routes through the hierarchy helpers first — but the unsafe function is the public, obvious one, and the safe path is a convention rather than a guarantee.

Second, allocation. Every parent's `children` vector is a separate heap block.

Third, and worst: **transform propagation does not exist in this engine**, and this is why. Propagating a parent's transform to its children requires visiting parents before children. Holding that ordering requires the hierarchy to be a structure the ECS maintains, and it is not — it is a component, so the ordering would have to be rebuilt from scratch by chasing pointers through vectors every time anything moved. The visible consequence is that `Prefab::instantiate` flattens its node tree into world space at instantiate time, because there is nowhere to keep the parent-relative relationship afterwards. The prefab plan already identifies that flatten as the thing to delete once propagation exists.

**What it proves:** the hierarchy is not a component. It is structural, it has to be maintained with lifetime, and something has to own an ordering derived from it.

### 3.2 `renderDataSlot`, `SceneRenderData`, and `markDirty` — the GPU mirror

This is the largest one, at roughly 99 references across the engine.

`SceneRenderData` is described in its own header as a "GPU-side mirror of a scene's ECS data". It owns four `GPUDataStore`s (meshes, lights, cameras, shadows). It holds a pimpl'd `SignalBridge` of EnTT connections with ten handlers — `onMeshAdded`, `onMeshRemoved`, `onLightAdded`, and so on — whose job is to allocate and free a slot in the corresponding GPU array whenever a component appears or disappears. `MeshComponent` then carries the field `uint32_t renderDataSlot` so it can remember which slot it was given. Every frame, `onUpdate` walks the components and repacks the dirty ones into the SSBO for the current frame in flight. When anything changes a transform, it must call `Entity::markDirty`, which calls `SceneRenderData::markDirty`, which flags the slot for re-upload.

**Why it exists:** two reasons, both about what the ECS cannot say.

The layout the ECS stores is not the layout the GPU wants — `MeshComponent` holds an `AssetPtr`, some flags, and the slot index, while `MeshGPUData` is a packed struct with alignment rules — so a translation step is unavoidable somewhere. Fine. But the second reason is the real one: **the ECS cannot tell anyone what changed.** So the engine hand-builds a change notification path out of signals, a manually maintained slot index living inside a gameplay component, and an explicit `markDirty` call that every mutation site has to remember to make.

**What it costs:** a component that is supposed to describe a mesh instead carries a GPU allocator's bookkeeping. Ten signal handlers exist to keep two arrays in step. Every code path that moves an object must remember one extra call, and forgetting it produces a silently stale render with no error.

**What it proves:** the storage should be the thing that knows what changed, and it should be uploadable without a parallel index. Section 9 and section 10.

### 3.3 `Transforms::getGeneration` — hand-rolled change detection

`TransformComponent` wraps a `Transforms` object that carries a generation counter, bumped on every write, so consumers can compare against a stored stamp and skip work if nothing moved.

**Why it exists:** same missing feature as 3.2, solved a second time in a different way for one specific type.

**What it proves:** change tracking is wanted, it is being implemented ad hoc, and it is being implemented per-type rather than once. It belongs in the storage layer.

### 3.4 `Scene::m_pendingRigidBodies` — a queue in front of a signal handler

`Scene` connects `onRigidBodyConstructed` to the registry, and that handler does not create the physics body. It pushes the entity onto `m_pendingRigidBodies`, and `registerRigidBodies` drains the queue later.

**Why it exists:** doing real work inside a construct handler is unsafe — the registry is mid-operation, and creating a Jolt body may want to touch other components or add one.

**What it proves:** the reactive path needs a defined, safe point at which structural work happens, rather than every consumer inventing its own deferral queue.

### 3.5 `Prefab`'s staging tree — no way to build a world off the main thread

`Prefab` is a pure-POD tree of nodes with zero registry contact, and `Prefab::instantiate` is the only place in the engine that turns loaded asset data into entities. The asset pipeline is multi-threaded on a fiber-based job system, but nothing on a fiber may touch the registry, so loading produces a parallel representation of a scene graph that then gets converted on the main thread.

**Why it exists:** entity creation is not thread-safe and there is no deferred command mechanism.

**What it proves:** building entities off the main thread is a real requirement with a real consumer already, and the workaround is an entire duplicate representation of the same data.

### 3.6 `InstanceComponent` — an ECS inside a component

`InstanceComponent` holds `std::vector<MaterialComponent>`, `std::vector<TransformComponent>`, and a parallel vector of instance ids, with its own hand-rolled id counter.

**Why it exists:** the entity is too heavy for a thing that just needs a transform and a material. Making ten thousand grass instances into ten thousand entities is not viable, so the component became a container of components with its own private id allocation.

**What it proves:** this is the clearest signal in the codebase that the entity abstraction has a cost problem. Whether the answer is "make entities cheap enough that this is unnecessary" or "support this case explicitly" is a real open question — but a parallel-array id-allocating container living inside a component is the ECS admitting it could not express the case.

### 3.7 `Entity` — a wrapper that throws

The `Entity` class exists to keep EnTT from spreading through the codebase. It carries an `entt::entity` plus a `Scene*`, and its accessors validate and then throw `EntityException` on failure — in a codebase whose stated rule is that exceptions are never used anywhere.

Meanwhile `Scene::getRegistry()` is called from 32 places, so EnTT spreads anyway.

**Why it exists:** to make a raw, unsafe, unfamiliar API safe and familiar.

**What it proves:** the abstraction is not paying for itself. A wrapper that leaks in 32 places and violates the project's own error-handling rule to do its job is an argument for owning the layer underneath it rather than papering over it.

### 3.8 `EntityView` — a wrapper that is slower than what it wraps

`EntityView::operator*` constructs a `ViewEntity` for each entity, and that constructor calls `registry.get<Component>(entity)` again for every component in the view — the view has already located that data, and the wrapper throws the result away and looks it up a second time. `ViewEntity::isValid` adds a further validity check. This is documented in CLAUDE.md as roughly ten times slower than raw EnTT views, with the note that performance-critical loops should bypass it.

**What it proves:** an iteration API that hands back a per-entity object is the wrong shape. Iteration must hand back references directly.

### 3.9 The editor: a hardcoded inspector, no save, and an Undo button wired to nothing

Three symptoms of one missing capability. The properties panel is a hardcoded chain of "if the entity has component X, draw these specific widgets", so every new component means editing the panel. Scene serialization does not exist yet and its plan requires per-component functions written by hand. And `AmethystLayer.cpp` contains `d.action("Undo", [] {});` — a menu item with an empty callback.

**Why they exist:** C++ has no reflection, and EnTT deliberately does not provide any usable substitute (`entt::meta` exists, is awkward, and is unused here). So no code can ask "what fields does this component have" and every consumer that needs that must be written out by hand, per component, per consumer.

**What it proves:** a description of each component's fields is not an optional extra — it is the single missing thing behind three separate unbuilt features. Section 13.

### 3.10 Singletons modelled as entities

`Scene::environmentEntity()` is documented as "always present and not destroyable". The main camera is a stored entity handle.

**Why it exists:** there is nowhere to put per-world data that is not a component, so unique things become entities with a special rule attached.

**What it proves:** "exactly one of these per world" deserves to be expressible, and when it is not, the special case leaks into the public API and the thing shows up in every query and in the outliner.

## 4. What this adds up to

Read together, those ten items say something fairly specific, and it is not "EnTT is slow".

EnTT is a good general-purpose ECS. The mismatch is that this engine wants an ECS with **opinions** — about hierarchy, about transforms, about what changed since last frame, and about GPU upload — and a general-purpose library cannot have those opinions, because they would be wrong for most of its users. So the engine has grown each opinion outside the ECS, one workaround at a time, and each workaround is worse than the built-in version would have been because it cannot see the storage.

Which gives the actual design brief:

1. **Hierarchy and transforms are core**, not components (from 3.1).
2. **Storage knows what changed** and can be uploaded without a parallel index or a manual slot (3.2, 3.3).
3. **Structural work happens at defined points**, and can be produced off the main thread (3.4, 3.5).
4. **Iteration hands back references**, never wrapper objects (3.8).
5. **Every component describes its own fields**, once, for all consumers (3.9).
6. **Per-world singletons exist** (3.10).
7. Entities are cheap enough not to need escape hatches (3.6) — or the escape hatch is designed rather than improvised.

Note what is *not* on that list: faster iteration, a query planner, archetypes, relationships, a scheduler. The wins here are structural, not throughput.

**In-engine or a submodule?** This brief answers it. A library that owns your transform representation, your scene graph, and your GPU upload path is not a library, it is a renderer. Points 1, 2 and 5 are exactly the opinions a reusable ECS cannot hold, and they are the entire reason to build one. So: in-engine, at `Engine/src/ecs/`, with the discipline enforced by a rule instead of by a repository boundary — nothing under `ecs/` may include from `renderer/`, `components/`, `asset_manager/` or `scenes/`. That keeps it extractable if that is ever wanted, and costs nothing now.

---

# Part II — The design

## 5. Entities

An entity is a 32-bit id. It stays 32-bit because entity ids already flow into GPU-adjacent code — ray-tracing instance removal keys off them, and the plan for viewport picking writes them into a G-buffer attachment to be read back.

The id is split into an **index** and a **generation**. The index says which slot in the entity array this is. The generation exists to solve one specific problem, which is worth spelling out because it is the reason every ECS does this.

Slots get reused. If entity 7 is destroyed and a new entity is created, the new one takes slot 7 because reusing slots is what keeps the arrays dense. Now anyone still holding the old id — an editor selection, a physics body's owner, a queued command — is holding an id that is *valid* and points at a completely unrelated object. That is a silent, extremely confusing class of bug. So each slot carries a counter that increments on every destroy, the id carries a copy of the counter from when it was handed out, and `isValid` compares the two. A stale id now fails a cheap equality check instead of silently aliasing.

The split has to be chosen. **20 bits of index and 12 bits of generation** gives 1,048,576 simultaneous entities and 4096 reuses of a slot before the counter wraps and a very old id could alias again. 24/8 gives 16 million entities and only 256 reuses, which is not many for a slot that is being recycled every time a bullet dies. Given section 2's scale, index space is not the constraint; 20/12 is the safer pick.

The per-entity record is:

```cpp
struct EntityRecord {
    uint32_t parent;        // entity index, or INVALID
    uint32_t firstChild;
    uint32_t nextSibling;
    uint32_t prevSibling;
    uint64_t components;    // bit per component type
    uint16_t generation;
    uint16_t flags;         // alive, and whatever else earns a bit
};
```

The four link fields are the hierarchy and are explained in section 8. The `components` bitmask is worth its eight bytes for three reasons: destroying an entity can iterate exactly the pools that hold data for it instead of asking all twenty-four; "walk every component on this entity" — which the inspector and scene saving both need — becomes a bit scan rather than twenty-four failed lookups; and it makes "does this entity match this query" a single AND if that is ever wanted. The cost is a hard ceiling of 64 component types, asserted at registration. Twenty-four exist today, and if the ceiling is ever hit the mask becomes two words with no other change, so it is not a one-way door.

Destroyed slots form a free list. The trick is that a dead record's `parent` field is meaningless, so it stores the index of the next free slot — the free list needs no extra memory.

## 6. Component storage

Now the central question from section 1: given entity `E` and type `T`, where is the data, and how do you walk everything having both `T` and `U`? There are three real answers. Each is explained here on its own terms before comparing them, because the trade only makes sense once the mechanics are clear.

### 6.1 Sparse set

This is what EnTT uses. Each pool holds three arrays:

```cpp
std::vector<uint32_t> sparse;  // indexed by entity index -> position in dense, or INVALID
std::vector<EntityID> dense;   // packed list of entities that have this component
std::vector<T>        data;    // parallel to dense: the actual components
```

The `dense` and `data` arrays are packed — no holes — so iterating the component is a perfect linear scan. The `sparse` array is the index that makes random access work: it is as long as the highest entity index, mostly empty, and each live entry says where in the packed arrays that entity's data sits.

**Reading** `T` for entity `E` is `data[sparse[E.index]]`. Two loads, and the second address depends on the result of the first, so it is a genuine pointer chase rather than two loads the CPU can issue in parallel.

**Adding** appends to `dense` and `data` and writes one entry in `sparse`.

**Removing** is the interesting one. You cannot just erase from the middle of `data`, because that would shift everything after it and invalidate every `sparse` entry pointing past the hole. So you swap the last element into the hole and pop:

```
d    = sparse[E.index]
last = dense.size() - 1
data[d]  = std::move(data[last]);
dense[d] = dense[last];
sparse[dense[d].index] = d;      // the entity that moved needs its index fixed
pop_back both;
sparse[E.index] = INVALID;
```

Constant time, but note the consequence: **removal reorders the array.** Iteration order therefore depends on the history of what was added and removed, so two runs that build the same scene by different routes iterate it in different orders. Nothing external may cache a position into `data` either.

**Iterating one type** is optimal — walk `data`.

**Iterating `T` and `U`** is where it gets weaker. You walk whichever pool is smaller, and for each entity you check the other pool: read `sparse[E.index]`, check it is valid, then read `data[...]`. That is a random access into two arrays of the second pool for every candidate. It is fine while entity ids are clustered and degrades as a scene is edited and ids scatter.

**Memory** is one `uint32_t` per entity slot *per component type*, whether or not that type is used. Twenty-four types across 50k entities is 4.8 MB of mostly-empty index. EnTT softens this by allocating `sparse` in pages on first touch.

### 6.2 Archetypes

This is what flecs and Unity DOTS use. Entities are grouped by their exact set of components; each group is a table with one column per component type and one row per entity. Everything with exactly `{transform, mesh, material}` lives in one table, everything with `{transform, mesh, material, light}` in another.

**Iterating `T` and `U`** is the best in this list: find every table whose set contains both, and walk both columns in lockstep. Both are contiguous, and there is no indirection at all — the data is laid out exactly as the loop consumes it.

**Adding or removing a component** is the price. The entity's set changed, so it belongs in a different table, so **every component it owns is copied to the new table** and its old row is swap-removed. Adding a tag to an entity with twelve components moves all twelve. It also invalidates every outstanding reference to that entity's components, which quietly breaks the natural `auto& t = get<Transform>(); add<Foo>(); t.x = 1;` shape.

The other cost is fragmentation. Query setup is paid per table, so a scene whose entities have many distinct component combinations ends up with many small tables and the per-table overhead eats the iteration win.

This is a genuinely excellent design for a simulation with a few large homogeneous populations. It is a poor fit here: section 2 says the frame-loop queries are few and fixed, and section 3.6 shows the engine already prefers to avoid entity churn, so the thing archetypes are good at is not the thing being asked for, and the thing they are bad at — cheap structural edits and stable references — is what the editor does all day.

### 6.3 Direct indexing with a bitset

The third option drops the packed array entirely:

```cpp
std::vector<T>        data;  // indexed DIRECTLY by entity index; holes are just unused slots
std::vector<uint64_t> bits;  // one bit per entity index: does it have this component?
```

**Reading** is `data[E.index]`. One load. There is no index to chase because the entity index *is* the position.

**Adding** writes the value and sets a bit. **Removing** clears a bit.

**Iterating one type** means walking the bitset and visiting the set bits, which skips empty regions 64 entities at a time.

**Iterating `T` and `U`** is the part worth seeing written out, because it is unusually simple:

```cpp
for (size_t w = 0; w < wordCount; ++w) {
    uint64_t bits = a.bits[w] & b.bits[w];
    while (bits) {
        uint32_t i = (w << 6) + std::countr_zero(bits);   // index of lowest set bit
        bits &= bits - 1;                                  // clear it
        func(i, a.data[i], b.data[i]);
    }
}
```

Intersecting two component sets is a bitwise AND that handles 64 entities per instruction. For a 50k-entity scene the entire matching step for any query is about 800 AND operations. Excluding a type is one more `& ~c.bits[w]` in the same expression, so exclusion is close to free and costs the same whether you exclude one type or three.

The order is always ascending entity index, regardless of what was added or removed when, which means it is identical across runs.

**The cost is memory:** `sizeof(T) × capacity`, occupied or not. For a transform on every entity that is exactly right. For a component that three entities in the scene have, it is absurd.

There is also a subtlety that the summary tables usually skip. `std::vector<T>` where `T` has a real constructor means default-constructing `capacity` of them, and a `std::string` or `std::vector` member turns that into an allocation per empty slot. Avoiding it means raw storage plus placement-new and manual destruction — real machinery, and a source of real bugs. The clean way out is a rule at registration: **the direct-indexed backend only accepts trivially constructible and trivially destructible components.** Everything else uses a sparse set. That costs nothing, because the components that want to be direct-indexed are the plain-data ones anyway, and it deletes the placement-new layer entirely.

One thing neither backend can promise, and it is worth stating because the assumption is natural and wrong: **a `T&` does not survive an unrelated add.** Growing `data` reallocates in both designs.

### 6.4 Choosing

Now the comparison means something:

| | sparse set | archetype | direct + bitset |
|---|---|---|---|
| read one component | 2 dependent loads | 2–3 loads | **1 load** |
| iterate one type | optimal | optimal per table | good, depends on occupancy |
| iterate several types | random access per entity | **optimal** | AND the bitsets, then direct index |
| add / remove component | O(1) | **O(every component on the entity)** | **O(1), one bit** |
| memory when few entities have it | good | **best** | **worst** |
| memory when most entities have it | index overhead | best | **best** |
| iteration order | depends on history | table order | **entity index, always** |
| splitting work across threads | over the packed array | per table, uneven | **over bit ranges, even** |

The decisive observation is that **the engine's components fall into two clearly separated populations**, so there is no need to pick one:

- Nearly every entity has a transform. Most have a mesh, a material, a bounding box, a name.
- A handful of entities have a cascaded shadow, terrain, atmosphere, skybox, fog, or indirect lighting — often exactly one per scene.

Direct indexing is the best option for the first group on every axis including memory, because at high occupancy there are barely any wasted slots. It is the worst option for the second group, where sparse sets are the best option. So: **one pool interface, two backends, chosen per component type at registration.**

This is not a compromise; it is the correct answer to a bimodal distribution. And it lands well, because every hot query from section 2 consists entirely of first-group components — so the frame loop is a bitset AND followed by direct indexing, with one load per component and no per-entity object anywhere.

Queries that mix the two tiers need a rule so that call sites do not have to care: **the smallest sparse-tier pool drives the loop, and every direct-indexed pool is a filter**, tested with a single bit read. If a query has no sparse-tier component in it, the bitset AND drives.

Section 9 depends on one further property of direct indexing that is worth flagging here: because position equals entity index, the position of a component in its array is a number the rest of the engine already knows.

## 7. Queries

```cpp
for (auto [entity, transform, mesh, bounds] : world.view<Transform, Mesh, Bounds>()) {
    // ...
}

world.view<Transform, Mesh>().exclude<Light>().each([](EntityID e, Transform& t, Mesh& m) {
    // ...
});
```

A view is a small value object — pointers to the pools involved, plus the exclusion set — built at the call site each time it is used. Constructing one is a few pointer lookups, so there is no reason to cache them, and caching them is what forces general-purpose ECSs to maintain invalidation machinery on every structural change.

Two rules the design must actually enforce, from section 3.8:

**Iteration yields references, not objects.** The structured-binding form binds a temporary tuple of references that the optimiser removes entirely. There is no `ViewEntity`, no `Scene*` carried per entity, and no second lookup of data the view already found. This is the specific mistake the current wrapper makes and the entire reason it is ten times slower than what it wraps.

**Validity is not re-checked during iteration.** The view produced these entities by scanning live storage; asking again whether they are alive is redundant work in the hottest loop in the engine.

Because direct-indexed words are independent of one another, splitting a view across job-system fibers is a matter of giving each fiber a range of bitset words. That is an even split by construction, unlike archetype tables whose sizes are whatever the scene happened to produce. This does not need to exist on day one, but the reference-yielding rule above is what keeps it possible later — a wrapper holding a `Scene*` would not be safely shareable.

## 8. Transform and hierarchy belong in the core

This is the first of the two opinionated moves, and it comes directly from 3.1.

The world owns, as parallel arrays indexed by entity index:

- `local` — the local transform, the existing `Transforms` representation, which already maintains matrix and TRS in both directions.
- `world` — the resolved world matrix.
- the `parent` / `firstChild` / `nextSibling` / `prevSibling` links already shown in the entity record.

No `TransformComponent`, no `HierarchyComponent`. Every entity has a transform, and it is a core property like the generation counter is.

**Why put them here rather than leave them as components?** Four reasons, in increasing order of importance.

*Links are cheaper than vectors.* Four `uint32_t` per entity, no allocation, versus a vector header plus a heap block per parent. Child iteration is `for (uint32_t c = rec.firstChild; c != INVALID; c = records[c].nextSibling)`. Having `prevSibling` as well as `nextSibling` is what makes unlinking a child O(1) instead of a walk, which is the operation reparenting does constantly.

*Lifetime becomes correct by construction.* There is one destroy, it owns the links, and it cannot leave a dangling parent pointer because there is no second place the relationship is written down. The footgun from 3.1 stops existing rather than being documented around.

*It removes a whole class of "did you remember to" bugs.* Propagation runs at one defined point per frame, owned by the world.

*And the real one: it makes transform propagation possible at all.* Which needs explaining, because the obvious approach does not work.

### Why propagation needs its own ordering

Propagation must visit a parent before its children. The tempting trick is to arrange for parents to always have lower entity indices than their children, so a single ascending loop is correct. That does not survive reparenting: making it true again after a reparent means *moving entities to different indices*, which invalidates every id anyone is holding — exactly the problem section 5's generation counters exist to prevent.

So the ordering lives in its own array and the ids never move:

```cpp
std::vector<uint32_t> order;         // entity indices, parents always before children
std::vector<uint32_t> levelOffsets;  // where each depth level begins in order
bool orderDirty = false;
```

`order` is rebuilt by a breadth-first walk from the roots over the sibling links. BFS is used rather than depth-first because it produces the array grouped by depth as a side effect, filling `levelOffsets` for free. The rebuild is linear and only happens when the structure changed — creating, destroying or reparenting sets `orderDirty`, and the rebuild is deferred until just before propagation runs. A load that reparents four hundred nodes therefore rebuilds once, not four hundred times.

Propagation itself is then one flat pass with no recursion and no pointer chasing:

```cpp
for (uint32_t slot = 0; slot < order.size(); ++slot) {
    uint32_t i = order[slot];
    uint32_t p = records[i].parent;
    world[i] = (p != INVALID) ? world[p] * local[i] : local[i];
}
```

Depth-grouped ordering pays off twice more. Every entity at a given depth has its parent at a shallower depth, already computed, and no two entities at the same depth write the same output — so each level is a parallel dispatch requiring no synchronisation inside it. And level zero is precisely the list of root entities, which the outliner wants and would otherwise have to find by scanning.

`Prefab`'s flat node array already demonstrates this exact layout works; the difference is that the invariant is held permanently instead of being rebuilt at every instantiate — which is what lets the flatten in `Prefab::instantiate` be deleted.

### Operations

`reparent(child, newParent)` walks up from the new parent looking for the child and rejects a cycle by logging and doing nothing, because the outliner can and will hand it a bad drag. Then unlink, relink, set `orderDirty`. It takes a flag for whether to preserve the world transform by recomputing `local = inverse(parentWorld) * world` — dragging in the editor wants that, an animation rig does not.

`destroy(entity)` takes the whole subtree, destroying leaves first so that any teardown sees children removed before their parents. There is no second, unsafe destroy.

`destroyKeepChildren(entity)` relinks the children into the grandparent's list first.

## 9. Storage the GPU can read directly

The second opinionated move, from 3.2 — the largest workaround in the engine.

Recall what `SceneRenderData` does: it allocates a slot in a GPU array whenever a mesh component appears, frees it when the component disappears, stores that slot number inside `MeshComponent`, keeps ten signal handlers to maintain the mapping, and repacks dirty entries every frame.

**Every part of that except the packing exists to answer one question: where in the GPU array does this entity's data live?**

Direct-indexed storage answers it for free. The array position *is* the entity index. So:

- There is no slot to allocate, so there is no free list.
- There is no slot to free, so the ten add/remove signal handlers are not needed.
- There is nothing to remember, so `renderDataSlot` comes off `MeshComponent`.
- The GPU-side index and the entity id are **the same number**, which means the entity-id G-buffer attachment that viewport picking needs is written for free, and the ray-tracing instance id can be the entity id rather than a mapping maintained alongside it.

That last point is the sort of thing that only shows up when the ECS is designed for the engine rather than in general, and it is the strongest single argument in this document.

What remains is the genuinely necessary part: the CPU layout and the GPU layout are different, so something has to translate. Two ways to handle that, and the choice matters.

**Make the component be the GPU struct.** Declare the packed, aligned struct as the component and upload the array as-is. Fastest possible, and no packing step at all. But it forces GPU alignment rules into components that also want to hold an `AssetPtr`, so it splits every concept into a CPU half and a GPU half.

**Register a pack function per component type.** The component stays natural; the ECS owns a GPU-side array sized to capacity, plus the dirty tracking, and calls the pack function on changed entries only. One extra copy per changed entity per frame — which at the scale in section 2 is nothing, and only for things that actually changed.

The second is the right trade: it keeps components readable and keeps GPU concerns out of the type that gameplay code touches, at a cost that the numbers say is irrelevant. `SceneRenderData` does not disappear — it keeps the pack functions and the descriptor plumbing, which is real work — but the slot allocation, the signal bridge, and the component field all do.

One case does not fit and should be called out rather than glossed. Lights want to be **compact** on the GPU, because the lighting shader loops over all of them and holes would mean the loop runs over dead slots. There are dozens of lights, not thousands, so packing them into a tight array every frame costs nothing. So the rule is: **components that are numerous and indexed individually (meshes) use identity slots; components that are few and iterated as a set (lights) are packed per frame.** That is a real distinction, not a special case, and both paths are cheaper than what exists now.

## 10. Change tracking

Sections 3.2 and 3.3 both come down to the same missing feature, implemented twice by hand: `Entity::markDirty` threading through 99 call sites, and a generation counter inside `Transforms`.

The storage layer is the right place for it, because the storage layer is what the writes go through. The mechanism is a per-pool bitset of entities written this frame, cleared at the frame boundary after upload consumes it.

The catch, and it must be stated honestly: **the pool can only know about a write if the write goes through it.** If `get<T>()` hands out a `T&`, anyone can modify the data invisibly. So the accessors split — `get<T>()` returns const, `getMut<T>()` returns a mutable reference and sets the bit. That is a discipline rather than a guarantee, and someone will occasionally take a mutable reference and not write to it, marking a clean entity dirty. That is harmless: the cost is one redundant upload.

It is worth being clear about what makes this better than what exists, since it is still a discipline. Today the discipline is "remember to call `markDirty` on the right object after mutating", which is invisible in the type system, easy to forget, and produces a stale render with no diagnostic. Under `getMut` the discipline is enforced by const-correctness — you cannot write without asking for write access — and the failure mode flips from "silently wrong" to "slightly wasteful".

Not every type needs it, and tracking types that nobody queries for changes is pure overhead. So it is opt-in at registration, per component type.

Transform is the case that matters most and it is already core (section 8), so its dirty bit lives with `local` and drives propagation, the GPU upload, ray-tracing acceleration structure refits, and bounding-volume invalidation from the same source of truth — rather than each of those maintaining its own comparison against its own stored stamp.

## 11. What is left for events

Under EnTT this engine has twenty-nine signal connections and they look load-bearing. It is worth checking what actually remains once the sections above land, because a design that quietly leaves the old feature untouched has not really changed anything.

Ten of them are `SceneRenderData` maintaining GPU slots — gone in section 9. Several more are change notification — gone in section 10. What genuinely remains is a small set of *side effects on lifetime*: creating a Jolt body when a rigid body appears, destroying it when it goes away, building a bottom-level acceleration structure when a mesh appears.

So the event system shrinks from an architectural centrepiece to a modest facility: per component type, callbacks on construct and destroy. That is a good outcome and a sign the rest of the design is doing its job.

Two properties are load-bearing and easy to get wrong:

**Timing.** Construct callbacks run *after* the component exists and the entity's component mask is updated, so a handler sees a complete entity and can read what it was given. Destroy callbacks run *before* the data is removed, so a handler can still read the thing it needs in order to clean up — which is exactly what removing the right physics body requires.

**Structural work is deferred, not immediate.** Section 3.4 shows the engine already discovered this the hard way and built a queue in front of one handler. Rather than each consumer inventing its own, a handler that wants to create or destroy entities queues a command (section 12) which drains at a defined point. Handlers may freely write component data; they may not restructure the world underneath the code that called them. In debug builds each pool carries an iteration counter and add/remove asserts it is zero, which turns a class of undefined behaviour into an assert.

On the implementation: the existing `EventSignal` would work, but it is built for the engine's eleven coarse fire sites — window resize, shader recompile — not for something that fires per component. It stores slots in a `std::map` with a heap node per connection, holds a `std::function` per slot, and runs `std::erase_if` over every slot on every fire even when nothing needs erasing. It also has a latent re-entrancy bug: `m_firing` is a plain `bool`, so a nested fire clears it on return and the outer fire loses its removal deferral and erases while iterating. Nothing today re-enters, so it has never been hit. A per-type callback list here is a flat vector of function pointer plus payload, with a depth counter rather than a bool. The `EventSignal` re-entrancy fix and the unconditional `erase_if` are worth fixing on their own merits regardless of whether any of this happens.

What is kept from `EventSignal` is its connection model: `EventConnection` disconnects when it is destroyed, which is better than EnTT's manual pairing and would let `SceneRenderData`'s connect/disconnect blocks become held members.

## 12. Building a world off the main thread

From 3.5. The asset pipeline is already multi-threaded and already produces an entire parallel representation — `Prefab`'s POD node tree — solely because a fiber may not touch the registry.

The mechanism is a **command buffer**: a per-thread arena of encoded operations, drained at an explicit point on the main thread.

```cpp
CommandBuffer& cmd = world.commands();
EntityID e = cmd.create();
cmd.add<MeshComponent>(e, asset);
cmd.reparent(e, parent);
```

The one part that needs care is entity creation, because a producer almost always wants to add components to the entity it just created, and it cannot wait for the drain to learn the id. The clean resolution is to **reserve the slot immediately** — an atomic bump on the free list hands back a real, valid id straight away — and defer only the component writes. No placeholder ids and no remapping table when the buffer drains, which is where this kind of design usually gets complicated.

Drain order is submission order within a buffer, and registration order across buffers, so a given load produces the same result every run.

This subsumes the deferral queue from 3.4, gives handlers the safe path section 11 requires, and makes destroying an entity during iteration well-defined instead of undefined.

Whether `Prefab`'s staging tree then disappears entirely is a separate question — it also serves as the cooked on-disk representation of an imported model, which is a real job independent of threading.

## 13. Describing components: one description, five consumers

From 3.9, and this is the section with the largest payoff relative to its size.

Each component declares its fields once:

```cpp
void MeshComponent::describe(Archive& ar)
{
    ar.field("mesh", mesh);
    ar.field("mobility", mobility);
    ar.field("enabled", isEnabled);
}
```

`Archive` is an interface with a virtual overload per supported field type. Different implementations of it do different things with the same description:

- **Writing and reading scene files.** The order fields are declared in is the order they are written, which makes the output byte-identical for identical scenes — which in turn is what lets the engine detect an externally modified scene file by comparing a hash rather than by diffing structures.
- **The inspector.** Each `field()` call emits the appropriate widget. The hardcoded per-component chain in the properties panel collapses into: read the entity's component mask, call `describe` on each.
- **Undo and redo.** The empty `d.action("Undo", [] {})` becomes real. An edit records the field's identity and its value before and after; undo applies the before. Every component gets undo automatically, including ones written later, because the editor does not need to know what a component is in order to record a change to it.
- **Play-mode snapshots.** Entering play records every authored field; leaving play writes them back. Because it is per field rather than a blob, "keep this one entity's changes" is just skipping part of the walk.
- **Copy, paste and duplicate.** Same walk, different destination.

Five features from one description each, and adding a component means writing `describe` once instead of touching five places — or, as today, not having four of the five at all.

**On the interface being virtual rather than templated:** a templated archive would inline and be faster. Everything in this list runs on a click, a save, or a play-mode transition. A virtual call per field is free at that frequency, and the non-template version is far simpler to route through type-erased pools and produces comprehensible compile errors. Take the simple one.

**Field attributes** are what let one description serve consumers with different needs, passed as trailing arguments:

- `Transient{}` — derived state that should never be saved. The inspector may still show it read-only, which is genuinely useful for debugging.
- `Range{min, max}`, `Tooltip{...}` — read by the inspector, ignored by everything else.
- Asset references save as a UUID and inspect as an asset picker, because the archive dispatches on the field's type.
- Entity references save as a scene-local index, matching the existing decision that entity ids are not persistent.

The point is that these are not different descriptions for different consumers. They are one description plus enough metadata for each consumer to know what it may ignore.

## 14. Deliberately not building

Stating these explicitly so they do not get re-argued later.

**Archetype storage** — section 6.2. Wrong shape for a workload with rare structural change and an editor that mutates single components constantly.

**A system scheduler** — flecs, Bevy and DOTS infer execution order and parallelism from what systems read and write. The render pass order here is explicit and deliberate, and a scheduler would spend its life being overridden. The useful part of it — running one query across job-system fibers — is section 7 and does not need a scheduler.

**Arbitrary relationships between entities** — flecs-style entity pairs. A hierarchy is in the core; a general relationship system is a much larger feature with real fragmentation costs and no consumer here.

**Cached queries and groups** — a pre-matched entity list maintained across structural changes. Section 6.3's matching step is around 800 AND operations for a large scene, so the maintenance would cost more than the thing it saves.

**Runtime-defined component types** — no consumer. Worth noting only that section 13 is the natural hook if scripting ever happens, since a runtime component is a name and a field list and every consumer already works from descriptions.

**A general reflection system** beyond section 13's field descriptions.

**Splitting components into per-field columns** for SIMD, as DOTS does. Direct-indexed storage is already contiguous per component; splitting the transform into separate translation, rotation and scale arrays is the next step *if* propagation ever shows up in a profile, and the pool interface can hide it when that day comes.

Two things from section 4's brief are deliberately left open rather than answered, because they need a decision rather than a design:

**Per-world singletons** (3.10) are small and obviously worth having — `world.singleton<T>()` storing one instance outside the pools, so the environment stops being an entity with a special rule and stops appearing in every query. The open part is which of the current special entities should become singletons and which are genuinely entities.

**The `InstanceComponent` problem** (3.6) is not solved by anything above, and pretending otherwise would be dishonest. Ten thousand grass instances should not be ten thousand entities under any design in this document. Whether the answer is a first-class notion of a lightweight instance, or accepting a container component but giving it a real design, is a genuine open question that deserves its own investigation.

---

# Part III

## 15. Is it worth doing

The honest accounting.

**What it costs.** Storage with two backends, an entity allocator, queries, hierarchy and propagation, deferred commands, the description layer, plus migrating 44 EnTT references, 23 query sites and 29 signal connections. And permanently owning the bugs in all of it — EnTT is battle-tested by a large number of users, and a first version of this will not be.

**What it buys.** Not speed — section 2 is explicit that a linear pass is microseconds either way. It buys, in rough order of value:

1. Transform propagation, which does not exist today and cannot be built cleanly without it (3.1).
2. The inspector, scene saving, undo and play-mode snapshots all coming from one description instead of four hand-written per-component chains, two of which are not built and one of which is an empty callback (3.9).
3. The deletion of the GPU slot mirror: ten signal handlers, a free list, a field inside a gameplay component, and 99 `markDirty` call sites (3.2).
4. Entity id and GPU index being the same number, which makes viewport picking and ray-tracing instance mapping fall out for free (section 9).
5. One destroy that cannot corrupt the hierarchy (3.1).
6. A wrapper that no longer throws exceptions in a codebase that forbids them, and no longer leaks the underlying library in 32 places (3.7).

Items 1 and 2 are the ones that matter. They are both *missing features*, not optimisations, and both are blocked on the same thing.

**Where it fits.** Not now. The asset and editor roadmap is mid-flight and this touches everything. The natural moment is after scene serialization is designed but before it is written — because item 2 means writing serialization on top of the description layer instead of writing per-component functions by hand and then rewriting them.

There is also a smaller, cheaper option worth keeping visible: **build the description layer (section 13) first, on top of EnTT.** It is independent of storage, it unlocks the inspector, saving and undo, and it is the single highest-value item on the list. If it lands and the rest never happens, that is still most of the benefit for a fraction of the work — and if the rest does happen later, the description layer carries over unchanged.

## 16. Open questions

- **Does the entity name go in the record or stay a component?** Every entity is created with a name, which suggests the record. But it is a `std::string`, and section 6.3's rule means a string-holding component cannot be direct-indexed — so this decision also decides whether the name can participate in a fast query at all.
- **What happens to `Mobility`?** Currently a field inside `MeshComponent`. Static versus dynamic is exactly the sort of thing queries want to filter on, and a zero-size marker component costs literally one bit per entity under direct indexing, which makes filtering on it nearly free.
- **Does the dense tier allocate to capacity or to a high-water mark**, and how does it grow? Doubling a 50k-entity transform array is a multi-megabyte copy at an unpredictable moment.
- **Does `Entity` keep its `Scene*`?** Section 7 requires queries to yield bare ids. Whether the fat handle survives elsewhere as an ergonomic convenience is a separate and much smaller question.
- **How are `InstanceComponent`-style populations expressed?** See section 14 — the largest genuinely unanswered question here.
- **Where does the world transform live** if a future design wants transforms optional rather than universal? Section 8 assumes every entity has one, which is true today and simplifies propagation enormously, but it is an assumption worth being explicit about.
