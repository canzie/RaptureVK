# Custom Entity Component System

> **Status: open, and more likely than it was.** Previously cancelled on the grounds that everything wanted here was achievable on top of EnTT. That reasoning has since narrowed to a single point, and it is the one point that matters: EnTT cannot *enforce* that writes go through a tracking accessor. It can only offer one. Every other item on the original list has either been built on EnTT already or turned out not to need the storage layer at all.

The question is not "how do we reimplement EnTT". That question produces a worse EnTT. The question is: **the engine has accumulated a set of workarounds that exist only because the ECS underneath it cannot express something. Which of those disappear if the ECS is designed for this engine instead of for everyone?**

Part I builds the case from what the code actually does. Part II is the design that follows. Part III is whether it is worth doing.

---

# Part I — Why bother

## 1. What an ECS is, and what problem it solves

An entity-component-system is three ideas.

An **entity** is an identity and nothing else — a number. It has no data and no behaviour. The intuition to unlearn is that an entity is an object; it is closer to a row id in a database.

A **component** is a piece of data attached to an entity. `TransformComponent`, `MeshComponent`. A component belongs to exactly one entity and knows nothing about which one.

A **system** is code that runs over every entity that has some particular combination of components. "For everything with a transform and a mesh, submit a draw call."

The reason to build software this way is memory layout. The object-oriented alternative gives each game object its own allocation and puts every piece of its data in it. Rendering then walks a list of pointers, jumping to a scattered address per object and pulling in a whole cache line of which it uses forty bytes. An ECS instead stores all the transforms together and all the meshes together, so a system that wants transforms reads a contiguous array and every byte the cache fetches gets used.

Everything difficult about an ECS follows from one tension: the layout that is fast to *scan* is not the layout that is fast to *change*, and it is not the layout that answers "what components does entity 4192 have" quickly either. Every design in Part II is a different position on that trade.

Concretely, an ECS has to answer two questions, and its data structure is whatever answers both:

1. **Random access.** Given entity `E` and component type `T`, where is that data?
2. **Iteration.** Which entities have *both* `T` and `U`, and where is all of it?

Vocabulary used throughout:

- **Pool** — the storage for one component type.
- **Dense array** — an array with no holes, so scanning it wastes nothing.
- **Query** or **view** — a request for all entities matching a set of component types.
- **Structural change** — creating or destroying an entity, or adding or removing a component. Distinguished from writing to a component that already exists, which is *not* structural and is always cheap.

## 2. What this engine's workload actually looks like

This matters more than any general argument, because the right ECS for a bullet-hell shooter is not the right ECS for a renderer, and general-purpose libraries are built for the average of all users.

| What runs | Which components | How often |
|---|---|---|
| Shadow caster submission | transform, mesh, bounding box | once **per shadow-casting light** per frame |
| G-buffer submission | transform, mesh, material, bounding box | per frame |
| Ray-tracing instance list | transform, mesh, material | per frame when dirty |
| Physics writeback | transform + body handle | per physics step |
| GPU buffer sync | one component type at a time | per frame |
| Camera, environment lookups | by entity handle | a few per frame |
| Inspector | every component on one entity | per editor click |
| Loading, spawning, editor edits | structural change | per load or per click |

Four things stand out.

**The same handful of queries run every frame, and they are all known at compile time.** No user types arbitrary queries at runtime. That removes any need for query planning, caching, or a general matching engine — features that cost a lot in a general-purpose ECS.

**Almost every hot query contains the transform.** Transform is not one component among twenty-four; it is *the* component, and a design that treats it as ordinary leaves something on the table.

**Structural change is rare and never in a frame-critical path.** So an ECS that makes adding a component slow in exchange for fast scans is trading in the right direction here — the opposite of what a simulation-heavy game would want.

**The output is almost always a GPU buffer.** The engine does not iterate transforms to make decisions; it iterates transforms to pack them into an SSBO and upload them. This is the single most important fact in this document and section 9 is built on it.

Scale, for calibration: Sponza is about 400 nodes; a large scene is 10–50k entities. At those counts a linear pass is microseconds under any design below. **Performance is not the reason to do this.** It is a constraint (do not make it worse) rather than a goal.

## 3. The evidence: what the code works around today

### 3.1 Hierarchy — resolved, and resolved *outside* the ECS

This used to be the headline argument: parent/child expressed as a component, transform propagation impossible, `Prefab::instantiate` flattening its tree to world space because there was nowhere to keep the parent-relative relationship.

**All of that is now built, and none of it went into the ECS.** The hierarchy lives on the `Instance` tree as `m_parent` plus a `std::vector<std::unique_ptr<Instance>>` of children. Propagation is eager and lives on `Node3D`. `HierarchyComponent` is dead code. The prefab flatten is deleted. See [[Scene]] and the transform work for the built design.

This is worth stating loudly because it **inverts the original argument**. The claim was "hierarchy must be core to the ECS or propagation cannot be built cleanly". Propagation was built cleanly, and the reason is that the hierarchy belongs to the *scene object* layer, not to the entity layer. Folders, environments and cameras all sit in that tree; only some of them are things the renderer iterates.

So the design brief loses an item, and gains a boundary: **the ECS stores per-entity data. It does not model relationships between entities.** A custom ECS should not reintroduce parent links in the entity record, and should not own the transform. `TransformComponent` stays a component.

### 3.2 `renderDataSlot`, `SceneRenderData`, and `markDirty` — the GPU mirror

This is the largest workaround and the one that has not gone away.

`SceneRenderData` is described in its own header as a "GPU-side mirror of a scene's ECS data". It owns four `GPUDataStore`s. It holds a pimpl'd `SignalBridge` of EnTT connections with ten handlers — `onMeshAdded`, `onMeshRemoved`, and so on — whose job is to allocate and free a slot in the corresponding GPU array whenever a component appears or disappears. `MeshComponent` then carries a `uint32_t renderDataSlot` so it can remember which slot it was given. When anything changes a transform it must call `Entity::markDirty`, which flags the slot for re-upload.

**Why it exists:** the layout the ECS stores is not the layout the GPU wants, so a translation step is unavoidable. Fine. But the second reason is the real one: **the ECS cannot tell anyone what changed.** So the engine hand-builds a change notification path out of signals, a manually maintained slot index living inside a gameplay component, and an explicit `markDirty` call that every mutation site has to remember.

**What it proves:** the storage should be the thing that knows what changed, and it should be uploadable without a parallel index.

### 3.3 Change tracking, implemented a dozen times

The single missing feature from 3.2 has been solved independently, differently, and incompatibly, in at least twelve places: per-slot per-frame dirty bitfields, unconditional per-frame repacks, a generation counter on `LightComponent`, cached-previous-value comparisons on both shadow components, a value compare that publishes an event, a set of dirty ids inside `RtInstanceData`, a full matrix compare in `updateTLAS`, a scene-level bool, and a value compare in `Environment`.

Four of those answer the *same question* — "did this entity's world transform change?" — and they disagree with each other. A direct component write updates the TLAS geometry but not the mesh SSBO, so the acceleration structure and the ray-hit shading data end up describing different worlds.

Two consequences worth naming, because they are the argument in miniature:

- The mutating `needsUpdate` check on the shadow components is *consumed by whoever reads first*, and two different systems read it each frame. Spot and point shadow maps get their view matrix updated and then never re-render.
- `SpotLight3D::setRange` writes the component and calls `markDirty`, so the light SSBO updates — but it never bumps the light generation, so the shadow projection derived from that range does not.

Both are silent. Neither is catchable by review. Full detail and citations in [[Change Tracking]].

**What it proves:** change tracking belongs in the storage layer, once, with one answer.

### 3.4 `Scene::m_pendingRigidBodies` — a queue in front of a signal handler

The `onRigidBodyConstructed` handler does not create the physics body; it pushes onto a queue drained later, because doing real work inside a construct handler is unsafe — the registry is mid-operation.

**What it proves:** the reactive path needs a defined, safe point at which structural work happens, rather than every consumer inventing its own deferral queue.

### 3.5 `Prefab`'s staging tree — no way to build a world off the main thread

`Prefab` is a pure-POD tree with zero registry contact, and `Prefab::instantiate` is the only place that turns loaded asset data into entities. The asset pipeline is multi-threaded on a fiber-based job system, but nothing on a fiber may touch the registry, so loading produces a parallel representation of a scene graph that then gets converted on the main thread. See [[Prefab]].

**What it proves:** building entities off the main thread is a real requirement with a real consumer, and the workaround is an entire duplicate representation.

### 3.6 `InstanceComponent` — an ECS inside a component

`InstanceComponent` holds parallel vectors of materials, transforms and instance ids, with its own hand-rolled id counter, because making ten thousand grass instances into ten thousand entities is not viable.

**What it proves:** this is the clearest signal that the entity abstraction has a cost problem. Whether the answer is "make entities cheap enough" or "support this case explicitly" is a genuine open question — but a parallel-array id-allocating container inside a component is the ECS admitting it could not express the case.

### 3.7 `Entity` — a wrapper that throws

The `Entity` wrapper carries an `entt::entity` plus a `Scene*`, and its accessors throw `EntityException` — in a codebase whose stated rule is that exceptions are never used. Meanwhile `Scene::getRegistry()` is called from dozens of places, so EnTT spreads anyway.

**What it proves:** the abstraction is not paying for itself.

### 3.8 The editor: a hardcoded inspector, and an Undo button wired to nothing

The properties panel is a hardcoded chain of "if the entity has component X, draw these widgets", so every new component means editing the panel. Serialization requires per-component functions written by hand. And the Undo menu item has an empty callback.

**Why:** C++ has no reflection, and EnTT deliberately does not provide a usable substitute.

**What it proves:** a description of each component's fields is not an optional extra — it is the single missing thing behind three separate unbuilt features. Section 13.

### 3.9 Singletons modelled as entities

The environment entity is documented as "always present and not destroyable". The main camera is a stored handle.

**What it proves:** "exactly one of these per world" deserves to be expressible, and when it is not, the special case leaks into the public API and shows up in every query.

## 4. What this adds up to

EnTT is a good general-purpose ECS. The mismatch is that this engine wants an ECS with **opinions** — about what changed since last frame, about GPU upload, about what a write even is — and a general-purpose library cannot hold those opinions, because they would be wrong for most of its users.

The design brief:

1. **Storage knows what changed**, and a write that does not announce itself is not expressible (3.2, 3.3).
2. **Storage is uploadable without a parallel slot index** (3.2).
3. **Structural work happens at defined points**, and can be produced off the main thread (3.4, 3.5).
4. **Iteration hands back references**, never wrapper objects.
5. **Every component describes its own fields**, once, for all consumers (3.8).
6. **Per-world singletons exist** (3.9).
7. Entities are cheap enough not to need escape hatches (3.6) — or the escape hatch is designed rather than improvised.

Note what is *not* on that list: faster iteration, a query planner, archetypes, relationships, a scheduler, hierarchy. Item 1 is the reason to do this at all; everything else is achievable on EnTT and some of it already has been.

**In-engine or a submodule?** A library that owns your GPU upload path is not a library, it is a renderer. Items 1, 2 and 5 are exactly the opinions a reusable ECS cannot hold. So: in-engine, at `Engine/src/ecs/`, with the discipline enforced by a rule — nothing under `ecs/` may include from `renderer/`, `components/`, `asset_manager/` or `scenes/`.

---

# Part II — The design

## 5. Entities

An entity is a 32-bit id, because entity ids already flow into GPU-adjacent code — ray-tracing instance removal keys off them, and viewport picking writes them into a G-buffer attachment.

The id splits into an **index** and a **generation**. Slots get reused: if entity 7 is destroyed and a new entity created, the new one takes slot 7, because reusing slots is what keeps the arrays dense. Anyone still holding the old id — an editor selection, a physics body's owner, a queued command, *a stale entry in the change log* — is holding an id that is valid and points at an unrelated object. So each slot carries a counter incremented on destroy, the id carries a copy from when it was handed out, and `isValid` compares the two.

**20 bits of index and 12 bits of generation** gives 1,048,576 simultaneous entities and 4096 reuses of a slot before the counter wraps. 24/8 gives 16 million entities and only 256 reuses, which is not many for a recycled slot. Index space is not the constraint here; 20/12 is the safer pick.

```cpp
struct EntityRecord {
    uint64_t components;    // bit per component type
    uint32_t nextFree;      // free-list link when dead
    uint16_t generation;
    uint16_t flags;
};
```

No parent or sibling links — 3.1. The hierarchy is not the ECS's business.

The `components` bitmask earns its eight bytes three ways: destroying an entity iterates exactly the pools that hold data for it instead of asking all twenty-four; "walk every component on this entity" — which the inspector and serialization both need — becomes a bit scan; and "does this entity match this query" is a single AND. The cost is a ceiling of 64 component types, asserted at registration. If it is ever hit the mask becomes two words with no other change.

## 6. Component storage

Given entity `E` and type `T`, where is the data, and how do you walk everything having both `T` and `U`? Three real answers.

### 6.1 Sparse set

What EnTT uses. Three arrays per pool:

```cpp
std::vector<uint32_t> sparse;  // entity index -> position in dense, or INVALID
std::vector<EntityID> dense;   // packed list of entities that have this component
std::vector<T>        data;    // parallel to dense
```

**Reading** is `data[sparse[E.index]]` — two loads, the second address depending on the first, so a genuine pointer chase.

**Adding** appends and writes one sparse entry. **Removing** swaps the last element into the hole and pops, patching the sparse entry of whatever moved. Constant time, but **removal reorders the array**, so iteration order depends on the history of what was added and removed, and nothing external may cache a position.

**Iterating one type** is optimal. **Iterating `T` and `U`** is the weak spot: walk the smaller pool, and for each entity probe the other pool's sparse array. That is a random access per candidate, fine while entity ids are clustered and degrading as a scene is edited.

**Memory** is one `uint32_t` per entity slot per component type whether used or not, softened by paging the sparse array.

### 6.2 Groups — sparse sets with the probes removed

The standard fix for 6.1's multi-component weakness. Maintain an invariant: for a group over `A` and `B`, the first `m_size` elements of `A.dense` and `B.dense` are the same entities in the same order. Iteration becomes K parallel linear walks with no probes at all:

```cpp
for (uint32_t i = 0; i < m_size; i++) {
    fn(entities[i], a[i], b[i]);
}
```

Maintenance is O(K) swaps per structural change — cheap.

**The constraint that kills it here:** a pool can be owned by at most one group, because two groups want contradictory orderings of the same array. `<Transform, Mesh>` and `<Transform, Light>` cannot both own `Transform`, and section 2 says nearly every hot query contains the transform. Partially-owning groups (own one pool, probe the rest) work around it and give back some of the win.

### 6.3 Archetypes

What flecs and Unity DOTS use. Entities are grouped by their exact component set; each group is a table with one column per type. Iterating `T` and `U` is the best in this list — find every table containing both and walk both columns in lockstep, no indirection at all.

**The price** is that adding or removing a component moves the entity to a different table, copying *every component it owns*. It also invalidates every outstanding reference, which breaks the natural `auto& t = get<Transform>(); add<Foo>(); t.x = 1;` shape. And optional components fragment the table space combinatorially — mesh with/without shadow with/without rigid body with/without script is sixteen tables holding the same logical thing.

Excellent for a simulation with a few large homogeneous populations. Poor fit here: the frame-loop queries are few and fixed, and an editor mutates single components all day.

### 6.4 Direct indexing with a bitset

Drop the packed array entirely:

```cpp
std::vector<T>        data;  // indexed DIRECTLY by entity index; holes are unused slots
std::vector<uint64_t> bits;  // one bit per entity index
```

**Reading** is `data[E.index]`. One load, no chase, because the entity index *is* the position.

**Adding** writes the value and sets a bit. **Removing** clears a bit.

**Iterating `T` and `U`** is a bitwise AND handling 64 entities per instruction:

```cpp
for (size_t w = 0; w < wordCount; ++w) {
    uint64_t bits = a.bits[w] & b.bits[w] & ~c.bits[w];   // exclusion is one more term
    while (bits) {
        uint32_t i = (w << 6) + std::countr_zero(bits);
        bits &= bits - 1;
        fn(i, a.data[i], b.data[i]);
    }
}
```

For a 50k-entity scene the entire matching step is about 800 AND operations. Exclusion costs the same whether you exclude one type or three. Order is always ascending entity index, identical across runs. There is no ownership conflict, so every query gets the good path — which is precisely what groups cannot promise.

**The cost is memory:** `sizeof(T) × capacity`, occupied or not. For a transform on every entity that is exactly right. For a component three entities have, it is absurd.

There is a subtlety the summary tables skip: `std::vector<T>` where `T` has a real constructor means default-constructing `capacity` of them, and a `std::string` member turns that into an allocation per empty slot. The clean way out is a registration rule — **the direct-indexed backend accepts only trivially constructible and trivially destructible components.** Everything else uses a sparse set. That costs nothing, because the components that want direct indexing are the plain-data ones anyway, and it deletes the placement-new layer entirely.

Neither backend can promise that a `T&` survives an unrelated add. Growing `data` reallocates in both.

### 6.5 Choosing

| | sparse set | group | archetype | direct + bitset |
|---|---|---|---|---|
| read one component | 2 dependent loads | 2 dependent loads | 2–3 loads | **1 load** |
| iterate one type | optimal | optimal | optimal per table | good, depends on occupancy |
| iterate several types | random access per entity | **optimal** | **optimal** | AND the bitsets, then direct index |
| every query gets the good path | n/a | **no — one owner per pool** | yes | **yes** |
| add / remove component | O(1) | O(1) + K swaps | **O(every component on the entity)** | **O(1), one bit** |
| memory when few entities have it | good | good | **best** | **worst** |
| memory when most entities have it | index overhead | index overhead | best | **best** |
| iteration order | history | history | table order | **entity index, always** |
| splitting across threads | over the packed array | over the packed array | per table, uneven | **over bit ranges, even** |

The decisive observation is that **the engine's components fall into two clearly separated populations**, so there is no need to pick one:

- Nearly every entity has a transform. Most have a mesh, a material, a bounding box, a name.
- A handful have a cascaded shadow, terrain, atmosphere, skybox, fog — often exactly one per scene.

Direct indexing wins for the first group on every axis including memory, because at high occupancy there are barely any wasted slots. It is the worst option for the second, where sparse sets are best. So: **one pool interface, two backends, chosen per component type at registration.**

Groups are the right answer if the whole engine is on sparse sets, but they are strictly worse than direct indexing for the dense tier: same read cost, worse structural cost, history-dependent order, and only one query per pool gets the fast path. They earn no place once the dense tier is direct-indexed, and the sparse tier is too small to need them.

Queries mixing tiers need a rule so call sites do not care: **the smallest sparse-tier pool drives the loop, and every direct-indexed pool is a filter**, tested with a single bit read. With no sparse-tier component in the query, the bitset AND drives.

Section 9 depends on one further property: because position equals entity index, the position of a component in its array is a number the rest of the engine already knows.

## 7. Reading and writing

This is the section the whole document now hangs on, because it is the one thing EnTT cannot do.

**Reads return const references.** Not copies — a `TransformComponent` is two matrices and gets read several times per frame per consumer. Const reference gives immutability for free.

```cpp
const TransformComponent &t = entity.read<TransformComponent>();
```

**Writes go through a proxy that announces on scope exit.**

```cpp
template <typename T>
class Write {
  public:
    ~Write() { m_log->touch(m_entity, m_aspect); }

    T *operator->() { return m_data; }
    T &operator*() { return *m_data; }

  private:
    T *m_data;
    ChangeLog *m_log;
    EntityID m_entity;
    ChangeAspect m_aspect;
};
```

The write itself is as direct as it is today — you get a `T*` and assign fields in place, no copy and no patch lambda. The only thing that changed is where the mutable pointer comes from:

```cpp
auto mesh = entity.write<MeshComponent>();
mesh->materialIndex = i;
mesh->isEnabled = true;          // one announcement, at scope exit
```

**Nothing else produces a mutable `T&`.** Iteration is const by default, and mutation is a separately named path:

```cpp
for (auto [entity, transform, mesh] : world.read<TransformComponent, MeshComponent>()) { ... }
for (auto [entity, transform] : world.write<TransformComponent>()) { ... }
```

This is the entire reason to own the storage layer. On EnTT, `registry.view<T>().get<T>()` hands out a raw `T&` and CLAUDE.md correctly forbids wrapping views to stop it — so the guarantee can only ever be a convention, which is what 3.3's twelve mechanisms and two silent bugs are made of. Owning the ECS turns "remember to call `markDirty`" into "it does not compile".

Two rules iteration must enforce:

**Iteration yields references, not objects.** The structured-binding form binds a temporary tuple of references the optimiser removes entirely. No per-entity wrapper, no `Scene*` carried along, no second lookup of data the view already found. This is the specific mistake the old `EntityView` made and the reason it was ten times slower than what it wrapped.

**Validity is not re-checked during iteration.** The view produced these entities by scanning live storage.

Because direct-indexed words are independent, splitting a view across job-system fibers is a matter of giving each fiber a range of bitset words — an even split by construction. This need not exist on day one, but the reference-yielding rule is what keeps it possible.

## 8. Change tracking: revisions, aspects and cursors

The mechanism is a monotonic revision per `(entity, aspect)`, plus an append-only log per aspect that consumers read with a cursor.

**Aspects are split by what set of derivations is invalidated together** — not by consumer, not by mobility:

| Aspect | Invalidates |
|---|---|
| `TRANSFORM_WORLD` | mesh SSBO row, world bounding box, TLAS instance, RT instance matrix, light SSBO row, shadow view matrix, camera view matrix |
| `MESH_BINDING` | mesh SSBO row, MDI batch key, BLAS validity, RT buffer indices |
| `MATERIAL_BINDING` | object info material index, RT material index |
| `LIGHT_PARAMS` | light SSBO row, shadow projection (range, cone angle) |
| `SHADOW_SETTINGS` | shadow map allocation, shadow SSBO row |
| `CAMERA_PARAMS` | camera SSBO row, frustum |
| `VISIBILITY` | draw list membership |

The split is principled because each aspect names a *cause*, and a consumer subscribes to causes rather than to publishers. `LIGHT_PARAMS` covers spot range and cone angle, which is exactly the case the generation counter misses today.

A component declares its default aspect, so the common case cannot be forgotten. A write may narrow it — `entity.write<MeshComponent>(ASPECT_MATERIAL_BINDING)` — which hands a *classification* decision back to the writer, but the failure mode is "did more work than needed" rather than "silently stale".

`touch` is the whole write cost:

```cpp
void ChangeLog::touch(EntityID entity, ChangeAspect aspect)
{
    AspectLog &log = m_aspects[aspect];
    uint32_t index = entityIndex(entity);

    log.revision[index] = ++m_revisionCounter;
    if (log.lastLoggedFrame[index] == m_frame) {
        return;
    }

    log.lastLoggedFrame[index] = m_frame;
    log.ring[log.head++ % log.ring.size()] = entity;
}
```

One store, one compare, one conditional push. Per-frame dedupe means a node written forty times in one frame lands in the ring once.

`head` is a monotonic counter of every record ever appended; the ring is a fixed window of the last R. A cursor is a saved `head` value, so "what changed since I last looked" is the range `[cursor, head)`:

```cpp
ChangeSpan ChangeLog::since(ChangeAspect aspect, Cursor &cursor) const
{
    const AspectLog &log = m_aspects[aspect];

    bool resynced = (log.head - cursor.position) > log.ring.size();
    uint64_t begin = resynced ? log.head : cursor.position;
    cursor.position = log.head;

    return ChangeSpan(log.ring, begin, log.head, resynced);
}
```

`resynced` means the consumer fell far enough behind that the records it needed were overwritten. It is not an error path — it is the base case. `rebuild()` covers everything up to `head` by definition, so advancing the cursor there is correct.

**A cursor belongs to a destination, not to a system.** Three frames in flight are three SSBOs, so three cursors. When the SSBO for frame index 0 uploads it asks for everything since *it* last uploaded — roughly three frames of change, not one. That is what makes "written in frame 1, is frame 2's copy stale" a non-question.

The cursor advancing inside `since` looks like the mutating `needsUpdate` check that 3.3 indicts, and the difference is the point: the cursor is *per-consumer state owned by the caller*. `needsUpdate`'s state lives on the shared component, so the first reader destroys it for everyone else.

Consumers pull; nothing broadcasts. A write costs one store regardless of how many consumers exist. A consumer that does not run this frame costs nothing. And a destroyed consumer just stops asking — no dangling listener, no cross-scene aliasing, because the log is per-scene.

Two things a consumer must still handle. Entities in the log may have been **destroyed** since being logged, which the generation bits in the id catch. And most logged entities are **not yours**, so a consumer opens with an O(1) lookup in its own map and bails. That is the same filter a broadcast would do, but paid O(consumers that ran) instead of O(listeners × changes).

## 9. Storage the GPU can read directly

Recall what `SceneRenderData` does: allocate a slot when a mesh component appears, free it when it disappears, store the slot number inside `MeshComponent`, keep ten signal handlers maintaining the mapping, and repack dirty entries every frame.

**Every part of that except the packing exists to answer one question: where in the GPU array does this entity's data live?**

Direct-indexed storage answers it for free. The array position *is* the entity index. So:

- No slot to allocate, so no free list.
- No slot to free, so the ten add/remove handlers are not needed.
- Nothing to remember, so `renderDataSlot` comes off `MeshComponent`.
- The GPU index and the entity id are **the same number**, so the entity-id G-buffer attachment viewport picking needs is written for free, and the ray-tracing instance id can be the entity id rather than a maintained mapping.
- And the change log gets its entity → slot mapping for free too, which is the lookup every consumer in section 8 does per changed entity.

That last pair only shows up when the ECS is designed for the engine rather than in general, and it is the strongest single argument here.

What remains is the genuinely necessary part: CPU and GPU layouts differ, so something translates. **Register a pack function per component type** — the component stays natural, the ECS owns a GPU-side array sized to capacity plus the dirty tracking, and calls pack on changed entries only. The alternative (declare the packed GPU struct *as* the component) is faster and forces alignment rules into components that also hold an `AssetPtr`, splitting every concept into a CPU half and a GPU half. One extra copy per changed entity per frame is nothing at this scale.

`SceneRenderData` does not disappear — it keeps the pack functions and the descriptor plumbing, which is real work. The slot allocation, the signal bridge and the component field all do. See [[SceneRenderData]].

One case does not fit and should be called out. Lights want to be **compact** on the GPU, because the lighting shader loops over all of them and holes would mean iterating dead slots. There are dozens of lights, not thousands, so packing them tightly every frame costs nothing. The rule: **components that are numerous and indexed individually (meshes) use identity slots; components that are few and iterated as a set (lights) are packed per frame.**

## 10. Mobility is a hint, not a partition

Mobility is authored intent — "this shadow never needs re-rendering", "this wall will never move at runtime". The engine cannot derive it, because observation only tells you the past: a door, a lift and a permanent wall are indistinguishable until the moment they differ.

Two things must not be fused under that name:

- **Mobility** answers "how often does this change". `STATIONARY` (default — does not change most frames, but is dragged around the editor all day) and `DYNAMIC` (camera, sun, player, physics bodies).
- **Bake state** answers "is there precomputed data, and is it still valid". Derived, not authored. This is where the build-lighting flow lives. UE fused the two because in UE4 the second determined the first; with DDGI and ray tracing here, it does not.

It is a **hint the engine may ignore**, never a contract. Moving a `STATIONARY` thing — from the gizmo, from a script, from anything — always works, and costs a resync of whatever was optimised on that assumption. A contract means every gameplay programmer who wants to move a wall goes and sets everything `DYNAMIC` to stop the friction, at which point the flag says nothing.

What it buys: `STATIC` shadow maps that render once and are never re-rendered; geometry that can live device-local, enter the TLAS non-updatable, and batch into draws that never rebuild; lights whose contribution can be baked. What it must **not** buy is a partition key in the SSBO — see [[Change Tracking]] for why that specific use of it is being removed.

Under direct indexing, mobility as a zero-size marker component costs one bit per entity and filtering on it is free.

## 11. What is left for events

Under EnTT this engine has twenty-nine signal connections and they look load-bearing. Ten are `SceneRenderData` maintaining GPU slots — gone in section 9. Several more are change notification — gone in section 8. What genuinely remains is a small set of *side effects on lifetime*: creating a Jolt body when a rigid body appears, destroying it when it goes away, building a BLAS when a mesh appears.

So the event system shrinks from an architectural centrepiece to a modest facility: per component type, callbacks on construct and destroy. Two properties are load-bearing:

**Timing.** Construct callbacks run *after* the component exists and the mask is updated, so a handler sees a complete entity. Destroy callbacks run *before* the data is removed, so a handler can still read what it needs to clean up — exactly what removing the right physics body requires.

**Structural work is deferred, not immediate.** 3.4 shows the engine already discovered this and built a queue in front of one handler. Rather than each consumer inventing its own, a handler queues a command (section 12) that drains at a defined point. Handlers may write component data; they may not restructure the world underneath their caller. In debug builds each pool carries an iteration counter and add/remove asserts it is zero.

The existing `EventSignal` connection model is worth keeping — `EventConnection` disconnects on destruction, which is better than EnTT's manual pairing. Its implementation is not: a `std::map` with a heap node per connection, a `std::function` per slot, an unconditional `erase_if` over every slot on every fire, and a plain `bool m_firing` that a nested fire clears on return, losing the outer fire's removal deferral. Nothing re-enters today so it has never been hit. Those two are worth fixing on their own merits regardless.

## 12. Building a world off the main thread

From 3.5. The mechanism is a **command buffer**: a per-thread arena of encoded operations, drained at an explicit point on the main thread.

```cpp
CommandBuffer &cmd = world.commands();
EntityID e = cmd.create();
cmd.add<MeshComponent>(e, asset);
```

The one part needing care is creation, because a producer almost always wants to add components to the entity it just made and cannot wait for the drain to learn the id. The clean resolution is to **reserve the slot immediately** — an atomic bump on the free list hands back a real, valid id — and defer only the component writes. No placeholder ids and no remapping table, which is where this design usually gets complicated.

Drain order is submission order within a buffer and registration order across buffers, so a load produces the same result every run.

This subsumes 3.4's deferral queue, gives handlers the safe path section 11 requires, and makes destroying an entity during iteration well-defined.

## 13. Describing components: one description, five consumers

From 3.8, and the largest payoff relative to its size.

```cpp
void MeshComponent::describe(Archive &ar)
{
    ar.field("mesh", mesh);
    ar.field("mobility", mobility);
    ar.field("enabled", isEnabled);
}
```

`Archive` is an interface with a virtual overload per field type. Different implementations do different things with the same description:

- **Scene files.** Declaration order is write order, so output is byte-identical for identical scenes — which is what lets an externally modified file be detected by hash rather than by diff.
- **The inspector.** Each `field()` emits a widget. The hardcoded per-component chain collapses into: read the mask, call `describe` on each.
- **Undo and redo.** The empty callback becomes real. An edit records the field's identity and its before and after. Every component gets undo automatically, including ones written later.
- **Play-mode snapshots.** Entering records every authored field; leaving writes them back. Per-field rather than a blob, so "keep this one entity's changes" is skipping part of the walk.
- **Copy, paste, duplicate.** Same walk, different destination.

**On the interface being virtual rather than templated:** everything in this list runs on a click, a save, or a play-mode transition. A virtual call per field is free at that frequency, and the non-template version routes through type-erased pools far more simply and produces comprehensible compile errors.

**Field attributes** let one description serve consumers with different needs: `Transient{}` for derived state that is never saved (the inspector may still show it read-only), `Range{min, max}` and `Tooltip{...}` for the inspector, asset references saving as a UUID and inspecting as a picker, entity references saving as a scene-local index.

## 14. Deliberately not building

**Hierarchy or transforms in the core** — 3.1. Built outside the ECS and working. A custom ECS must not reintroduce parent links in the entity record.

**Archetype storage** — 6.3. Wrong shape for rare structural change plus an editor mutating single components constantly.

**Groups** — 6.2. Strictly worse than direct indexing for the dense tier, and the sparse tier is too small to need them.

**A system scheduler** — the render pass order here is explicit and deliberate, and a scheduler would spend its life being overridden. The useful part — one query across fibers — is section 7 and needs no scheduler.

**Arbitrary relationships between entities** — flecs-style pairs. Large feature, real fragmentation cost, no consumer.

**Cached queries** — 6.4's matching step is ~800 AND operations for a large scene, so maintenance would cost more than it saves.

**Runtime-defined component types** — no consumer. Section 13 is the natural hook if scripting ever happens, since a runtime component is a name and a field list.

**Splitting components into per-field columns** for SIMD. Direct indexing is already contiguous per component; the pool interface can hide a split if propagation ever shows up in a profile.

---

# Part III

## 15. Is it worth doing

**What it costs.** Storage with two backends, an entity allocator, queries, the read/write access layer, the change log, deferred commands, the description layer, plus migrating every EnTT reference, query site and signal connection. And permanently owning the bugs — EnTT is battle-tested and a first version of this will not be.

**What it buys**, in rough order of value:

1. **A write that does not announce itself becomes inexpressible.** This is the whole argument. Section 3.3 is twelve mechanisms and at least two silent, unreviewable bugs, and every one of them exists because the storage cannot enforce the contract.
2. The inspector, serialization, undo and play-mode snapshots from one description instead of four hand-written chains, two of which are unbuilt and one of which is an empty callback (3.8).
3. Deletion of the GPU slot mirror: ten signal handlers, a free list, a field inside a gameplay component, and thirty `markDirty` call sites (3.2).
4. Entity id and GPU index being the same number, which gives viewport picking, ray-tracing instance mapping, and the change log's slot lookup for free (section 9).
5. A wrapper that no longer throws in a codebase that forbids exceptions (3.7).

**The honest counterweight** is that items 2 through 5 do not need a custom ECS. The description layer is independent of storage. The GPU mirror can be deleted by the change log alone. Only item 1 genuinely requires owning the storage — and item 1 is a *correctness guarantee*, not a feature, which makes it easy to undervalue right up until it costs a day of debugging a shadow that will not move.

**Where it fits.** [[Change Tracking]] is the same design expressed on EnTT, and it is the sane first move: it deletes most of items 2–4 without touching the storage layer, and everything it builds — aspects, the log, cursors, ranged upload — carries over unchanged if the ECS is written later. The ECS then becomes a question about item 1 alone, asked with the rest already working.

## 16. Open questions

- **Does the entity name go in the record or stay a component?** It is a `std::string`, and 6.4's rule means a string-holding component cannot be direct-indexed — so this also decides whether the name can participate in a fast query.
- **Does the dense tier allocate to capacity or to a high-water mark**, and how does it grow? Doubling a 50k-entity transform array is a multi-megabyte copy at an unpredictable moment.
- **Does `Entity` keep its `Scene*`?** Section 7 requires queries to yield bare ids. Whether the fat handle survives elsewhere as a convenience is a smaller, separate question.
- **How are `InstanceComponent`-style populations expressed?** Ten thousand grass instances should not be ten thousand entities under any design here. The largest genuinely unanswered question.
- **How large is the change log ring, and how is overflow tested?** The resync path will be rarely exercised and therefore rarely correct unless overflow is forced deliberately.
