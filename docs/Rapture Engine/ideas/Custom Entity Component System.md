# Custom Entity Component System

What an in-house ECS would have to do, and what data structure would sit under it, if [[Scene]]
stopped using EnTT.

> Idea / recon. Nothing decided. Scope is storage, iteration, lifetime and hierarchy only —
> serialization format is [[Play Mode and Scene Serialization]], asset formats are
> [[Project Serialization]].

## 1. What EnTT is actually used for

Five live files, 44 references. The whole surface in use is `view`, `on_construct`/`on_destroy`,
and `create`/`destroy`/`valid`/`get`/`try_get` — 23 view sites, 29 signal connections, a handful
of lifetime calls, one exclusion.

**Unused:** `entt::meta`, groups, `sort`, observers, snapshot, runtime type registration. None of
EnTT's differentiating features are load-bearing. 24 component types exist.

Two facts that shape everything below:

- **`Scene::destroyEntity` does not maintain the hierarchy.** It destroys the entity without
  touching `HierarchyComponent`, so a destroyed parent leaves children pointing at a dead parent
  and a dangling entry in the grandparent's child list. Not a live bug — the [[Outliner]] is the
  only caller and it correctly routes through the hierarchy helpers — but the unsafe function is
  the public, obvious one.
- **Change detection is already solved outside the ECS.** [[Transforms]] carries an O(1)
  generation stamp, so there is no need for an `on_update` signal.

## 2. Features it would need built in

### Entity lifecycle

`create` / `destroy` / `isValid`, with generation bits in the handle so a stale handle cannot
alias a recycled slot.

`EntityID` stays `uint32_t` — entity ids already flow into GPU-adjacent code such as TLAS instance
removal. Candidate split: **20 bits index** (1,048,576 entities) / **12 bits generation** (4096
reuses before wraparound).

### Component storage

`add` / `remove` / `get` / `tryGet` / `has`. That is the complete set in use today.

### Intrinsic hierarchy

Parent/child as part of the entity record rather than a component. Operations: `reparent`,
`destroySubtree`, `destroyKeepChildren`, `getRoot`, root iteration, child iteration.

Three reasons it belongs inside rather than layered on:

1. **Lifetime correctness.** With the hierarchy inside the ECS, `destroyEntity` is the only
   destroy and handles the subtree by construction. The footgun stops existing rather than being
   documented around.
2. **Topological ordering enables flat transform propagation.** Transform propagation doesn't
   exist yet, and [[Prefab]] flattens to world at instantiate time precisely because there is
   nowhere to keep the ordering invariant. If parents are kept before children, propagation is one
   linear pass:

   ```cpp
   for (uint32_t i = 0; i < count; ++i) {
       world[i] = (parent[i] != INVALID_ENTITY_ID) ? world[parent[i]] * local[i] : local[i];
   }
   ```

   No recursion, no pointer chasing, contiguous, vectorizable. A `std::vector<Entity> children`
   per node can never give this — every node is a separate allocation. [[Prefab]]'s flat pre-order
   node array already proves the pattern; the invariant would just be held permanently instead of
   rebuilt per load.
3. **Structural data serializes as an index, not a handle.** `HierarchyComponent` is the only
   place [[Entity]] appears as a component member. In the entity record, parent is a plain index,
   which removes entity-handle remapping from [[Play Mode and Scene Serialization]] entirely.

**Scope limit:** *a hierarchy*, not a general relationship system. Arbitrary relationship pairs
(flecs-style) are a large feature with real fragmentation costs and no consumer here.

### Construct / destroy callbacks

Per component type, multiple listeners, connect and disconnect. Load-bearing — this is how
[[SceneRenderData]] keeps its GPU arrays in sync, and how rigid bodies register with the physics
world. No `on_update`, per §1.

**[[EventSignal]] is the obvious candidate, and it is not quite the right primitive.** It is what
the engine already uses for shader recompile, swapchain recreation, window resize/close/focus,
prefab structure change and editor selection — 11 fire sites, all coarse and infrequent. The ECS
would be its first *high-cardinality* consumer: one signal instance per component type, fired once
per add and once per remove. That is a few thousand fires during a Sponza load and hundreds of
thousands for a large scene.

Three properties become costs at that frequency:

- **`std::map<uint32_t, Slot>`** — a heap node per connection and a pointer chase per slot.
  Listener counts here are tiny (one or two per component type), so a flat vector is strictly
  better.
- **`fire()` runs `std::erase_if` over every slot on every call, unconditionally** — a full
  traversal to collect `once` and `pendingRemove` slots even when there are none, which for ECS
  listeners is always, since they are permanent. Pure waste, and worth gating behind a flag
  regardless of whether the ECS ever happens.
- **`std::function` per slot** — type-erased, never inlined. EnTT's equivalent stores a raw
  function pointer plus a payload pointer in a vector, so it is cheaper on all three axes.

Order of magnitude: roughly 10× per fire. Irrelevant for Sponza, noticeable on a large scene load,
never relevant per frame — construct/destroy is structural, and §3 puts structural change in the
irrelevant tier. So EventSignal is *adequate*; it is wasteful in a way that happens to be cheap to
fix.

Two API consequences matter more than the throughput:

- **Re-entrancy.** `m_firing` is a plain bool, so a nested fire on the same signal clears it on
  return and the outer fire loses its removal deferral, erasing mid-iteration. Unreachable today
  because no current signal re-enters. The ECS reaches it the first time a construct handler adds
  a component of the same type to another entity. It needs to be a depth counter.
- **Connect-during-fire.** Adding a slot while iterating is safe with `std::map` but not with the
  vector suggested above, so connections would need the same deferral as removals.

The connection model is a better fit than EnTT's, though: `EventConnection` disconnects on
destruction, which would replace [[SceneRenderData]]'s paired connect and disconnect blocks with
held connection members.

### Views over 1–N types with exclusion

### Deferred structural commands

A per-thread command buffer drained at a sync point. Two cases:

- **Parallel loading.** [[Prefab]] is already a pure-POD staging tree with zero registry contact,
  and its instantiate is the only place in the Engine that spawns entities from loaded assets.
  Command buffers generalise that to any producer on a [[JobSystem]] fiber.
- **Destroy during iteration**, currently undefined behaviour with no guard.

### Type-erased pool iteration

`IComponentPool` plus a name→pool table, so "walk every component type on this entity" is
expressible. Two consumers: scene save/load, and collapsing the hardcoded per-component chain in
the [[Properties Panel]]. Design belongs in [[Play Mode and Scene Serialization]].

### Multiple independent worlds

Scene, World, SceneManager and Project all exist, so the registry is an instantiable object and
handles are world-scoped — as [[Entity]] already assumes by carrying a `Scene*`.

### Explicitly out

Runtime type registration, arbitrary relationships, a meta/reflection registry, groups,
observers/change lists, snapshot/checkpoint, scripting bindings.

## 3. Where it would have to be fast

Ranked by real frequency across the 23 view sites in the Engine:

| Path | Query shape | Frequency | Bar |
|------|-------------|-----------|-----|
| Shadow casters ([[ShadowMapping]], [[CascadedShadowMapping]]) | `Transform, Mesh, BoundingBox` | **per light × per frame** | hot |
| G-buffer submission ([[GBufferPass]]) | `Transform, Mesh, Material, BoundingBox` | per frame | hot |
| Transform propagation | parent→child walk | per frame, all dynamic | hot |
| Physics writeback | `Transform` + Jolt body | per step | hot |
| RT instances | `Material, Mesh, Transform`, excl. lights | per frame when dirty | warm |
| [[SceneRenderData]] sync | single-component | dirty-driven only | warm |
| Camera / environment | by handle | few per frame | irrelevant |
| Inspector | ~12 components by handle | per click | irrelevant |
| Prefab instantiate, add/remove | structural | per load or click | irrelevant |

Two conclusions:

- **The hot set is ~6 query shapes, all known at compile time, all containing
  `TransformComponent`** — which appears in 11 of the 23 view sites. No general query planner
  needed.
- **Shadow mapping is the multiplier.** The same 3-component query runs once per shadow-casting
  light, so iteration cost is amplified by N there and nowhere else.

**Calibration.** At realistic counts (Sponza ≈ 400 nodes; a large scene 10–50k entities) a linear
pass is microseconds under *any* design in §4. The structural choice is therefore not where the
performance is won or lost — what matters is that iteration hands back raw references with no
per-entity wrapper object and no redundant validity check, and that the hot queries avoid random
access. Structural-change throughput and random-access latency do not need optimising at all.

## 4. The underlying data structure

The question every ECS answers: given entity `E` and type `T`, where is `T`'s data, and how do you
walk every `E` having both `T` and `U`?

### Sparse set (what EnTT does)

Per component type: a packed `dense` array of components, a parallel array of owning entity ids,
and a `sparse` array indexed by entity index that points into `dense`.

- `get` — `dense[sparse[E.index]]`, **2 indirections**
- `add` is a push_back plus a sparse write; `remove` is swap-and-pop plus a sparse fixup. Both O(1)
- **iterate `T`** — linear over `dense`. Optimal
- **iterate `T,U`** — walk the smaller dense array, then sparse-lookup each entity in `U`. That
  lookup is a random access into two of `U`'s arrays; cache behaviour degrades as entities scatter
- memory — one `uint32_t` per entity slot *per type*, mostly empty. At 24 types × 50k entities
  that is **4.8 MB of sparse arrays**. EnTT pages these to soften it

### Archetype / table (flecs, DOTS)

Entities grouped by exact component set; each group is a table with one column per type, one row
per entity.

- `get` — entity → `{table, row}`, then column lookup
- `add` / `remove` — **moves every component of `E`** to a different table
- **iterate `T,U`** — for each table containing both, walk both columns in lockstep, both
  contiguous, **zero indirection**. Best case in this list
- cost — fragmentation. Per-table setup is paid per query per table, so many small tables erase
  the win

### Direct-indexed + bitset

Per component type: a `data` array indexed *directly* by entity index, plus a presence bitset.

- `get` — `data[E.index]`, **1 indirection**. Fastest possible
- `add` / `remove` — set or clear a bit
- **iterate `T`** — scan the bitset, skip gaps. Excellent at high occupancy, wasteful at low
- **iterate `T,U`** — **AND the two bitsets**, iterate set bits, direct-index both. 64 entities per
  instruction. For 50k entities the entire filter is **782 `uint64` ANDs**
- memory — `sizeof(T) × maxEntities` regardless of occupancy. Fine for `TransformComponent`,
  absurd for `CascadedShadowComponent`. The bitsets themselves are negligible: 50k bits ≈ 6.25 KB
  per type, ≈150 KB for all 24

### Comparison

| | sparse set | archetype | direct + bitset |
|---|---|---|---|
| `get` indirections | 2 | 2–3 | **1** |
| iterate 1 type | optimal | optimal per table | good, gap-dependent |
| iterate N types | random lookups | **optimal** | bitset AND + direct index |
| add / remove | O(1) | O(all components) | **O(1), one bit** |
| memory at low occupancy | good | **best** | worst |
| memory at high occupancy | sparse array overhead | best | **good** |
| parallel split | over dense array | per table (lumpy) | **over bit ranges (even)** |

The last row matters for [[JobSystem]]: bitset ranges divide evenly across fibers, whereas
archetype tables are whatever size the scene happened to make them.

### How this maps onto the components

The 24 types split cleanly by occupancy:

- **Near-universal** — `TransformComponent`, hierarchy. Every entity.
- **Common** — `MeshComponent`, `MaterialComponent`, `BoundingBoxComponent`, `TagComponent`.
- **Rare** — `CascadedShadowComponent`, `TerrainComponent`, `AtmosphereComponent`,
  `SkyboxComponent`, `IndirectLightingComponent`, `FogComponent`. One or a handful per scene.

**So the structural choice is per-type, not global: one pool interface with two storage backends,
picked at component registration.** Direct-indexed + bitset for the dense tier, sparse set for the
rare tier.

Every hot query in §3 (`Transform, Mesh, BoundingBox [, Material]`) lands entirely in the dense
tier, so the hot path becomes a bitset AND followed by direct indexing — 1 indirection per
component, no per-entity object, no random lookups. The rare tier never appears in a hot query; it
appears in single-type [[SceneRenderData]] rebuilds where packed storage is the right trade.

## 5. Open questions

- **Name and `Mobility` as fields rather than components.** `createEntity` takes a name for every
  entity, which suggests it belongs in the entity record rather than `TagComponent`. Same question
  for `Mobility`. Affects the `Transform, Mesh, Material, TagComponent` query.
- **Child storage layout** — a `vector<EntityID>` per parent, or first-child/next-sibling indices.
  The latter is allocation-free and fits the flat ordering; the former is easier to reorder in the
  [[Outliner]].
- **Handle bit split** — 20/12 above; 24/8 buys 16M entities at 256 generations.
- **Does the dense tier allocate to max entity index or to a high-water mark**, and what is the
  growth policy.
- **Whether [[Entity]] keeps its `Scene*`** (12–16 bytes, cheap to copy) or becomes a bare
  `EntityID` with the world passed explicitly.
