# Prefab

> **Archived 2026-08-17.** `Prefab`, `PrefabComponent` and `buildPrefab` no longer exist anywhere in
> the engine; the asset-that-records-arrangement role is now `SceneObjectImportData` and the scene
> object tree. Kept because the reasoning about arrangement, sub-assets and blueprint updates still
> applies to whatever fills that role, not because any statement below is current.

> Status: **implemented** (2026-07-18). Defines the asset layer between a [[Mesh]] and a [[Scene]],
> and what a glTF import actually produces. Companion to [[Asset Metadata]], which covers how those
> assets are stored and reloaded. The core is built: `Prefab` (`components/systems/Prefab.h`) with a
> flat `Node` array, `Prefab::instantiate` (`Prefab.cpp`), `PrefabComponent`, the glTF loader emits
> one prefab per scene (`glTFLoader.cpp:598` `buildPrefab`), and the duplicate-mesh bug is fixed.
> Still designed-only: sub-prefabs from selection, the kit-split option, thumbnails, reimport.

> **Revision 2026-08-04.** Instantiation now produces **nodes**, not loose entities (see [[Entity Types and Authoring Schema]]). The root is a `Node3D` carrying the blueprint reference, a node with a mesh becomes a `StaticMesh3D`, and a mesh-less node becomes a `Node3D` group. There is no prefab instance class — the blueprint link stays a component on whatever root the prefab defines. `HierarchyComponent` is gone; parenting is the instance tree. Sponza is one outliner row with its 103 children beneath it rather than 103 loose rows. Updating instances when the blueprint changes is destroy-and-reinstantiate, triggered explicitly, which discards edits made to interiors — the alternative is per-field override tracking, which is deliberately not built.

**Related: [[Asset Metadata]], [[Project Serialization]], [[Entity]], [[Scene]], [[Entity Types and Authoring Schema]]**

## Goal / intent

Importing Sponza should not hand you 103 anonymous fragments with no way to place them as a
building. Importing a humanoid should not hand you 10 loose limbs. But dragging in one curtain, by
itself, must also work. A **prefab** is the asset that records arrangement, so that both are true
at once.

---

## Decisions taken

1. **A prefab is a blueprint.** It holds no meshes hostage, takes no ownership, contains no
   geometry. Pure references plus transforms.
2. **The prefab is the tree, not a node.** One prefab per glTF scene. Node count is data inside one
   record, not a number of records. See [[#The tree is the asset]].
3. **Meshes are first-class and individually draggable.** Drag a prefab and get an arrangement;
   drag a mesh and get one entity at the origin. Neither is secondary.
4. **One entity per primitive.** Merging a node's primitives into a single entity is rejected. See
   [[#One entity per primitive]].
5. **Prefabs store local transforms, not world.** See [[#The propagation landmine]].
6. **glTF has no object layer.** Do not build an asset type on a layer that is not reliably there.
   See [[#glTF has no object layer]].

---

## glTF has no object layer

The hierarchy is: `scenes[]` → root `nodes[]` → each node has a transform, children, and optionally
references one `meshes[i]` → each mesh has `primitives[]` → one primitive is exactly one draw (one
accessor set, one material).

The two sample files use **opposite conventions**:

| | Sponza | adamHead |
|---|---|---|
| scenes | 1 | 1 |
| nodes | **1** (unnamed, scale 0.008) | **80** (all named) |
| meshes | **1** (unnamed) | 76 (all named) |
| primitives | **103, all in that one mesh** | ~1 per mesh |
| materials | 25 (none named) | 13 (all named) |
| grouping lives at | the primitive | the node |

So there is no layer that reliably means "an object," and exporters disagree about which one they
use. This kills the intuitive "a glTF `mesh` is a model" rule: in Sponza the glTF mesh is *the
entire building*.

**In Sponza there is no curtain.** Its 103 primitives are material batches — 25 materials across
103 chunks, one material covering 15 of them. A primitive looks like an object only when it happens
to have its own material. Nothing can recover an object the file never recorded.

Corollary: an asset type named `MODEL` was proposed and dropped. It has no layer to bind to. The
enum value is now `PREFAB` (`AssetCommon.h:21`) and the prefab is what glTF import produces.

---

## The tree is the asset

Two levels, not three:

- **[[Mesh]]** = one primitive = one draw call. Vertex/index range plus a material slot. The GPU
  unit.
- **Prefab** = a node tree. Each node has a name, a parent, a local transform, and optionally a
  mesh + material.

Recursive, so it covers every case with one record shape:

| Import | Produces |
|---|---|
| Sponza | 1 prefab / 104 nodes / 103 mesh refs / 25 materials / 69 textures |
| adamHead | 1 prefab / 80 nodes / 76 mesh refs / 13 materials |
| humanoid split into 10 by material | 1 prefab / 1 node / 10 mesh refs |

```
prefab { id, name, nodes: [ { name, parent, localTransform, mesh?, material? } ] }
mesh   { id, name, provenance, reloadSource, defaultMaterial }
```

Both are a few KB of JSON and neither pins a byte of GPU memory.

**The rule is not "node = prefab."** That rule gives 1 prefab for Sponza and 80 for adamHead, which
is the reductio that rejects it. adamHead is **one** prefab whose record contains 80 nodes.

### Meshes carry a defaultMaterial

Because a mesh is draggable alone, it needs a material for that case — dragging
`Sponza_Primitive_57` in should give you a curtain, not untextured geometry. In glTF a primitive is
`{attributes, indices, material}`, so the material is fused into the primitive and is intrinsic at
import.

The prefab still stores its own per-node material ref, so an arrangement can override. For a fresh
glTF import the two are identical. That is not redundancy — it is the seam where authoring diverges
from import, and it earns its keep the first time someone reskins a prefab.

### Sub-prefabs are authored, never inferred

glTF carries no marker for "this subtree is a reusable unit," so a head-inside-a-body sub-prefab
cannot be derived at import. **Create prefab from selection** is a later editor action where a
human supplies the intent the file never had.

The one exception is a **kit-bash file**: twenty unrelated props under one root, where you want 20
prefabs rather than one prefab containing 20 props at arbitrary layout positions. glTF cannot
distinguish that from a single assembly — the structure is identical. So it is an import option,
**split root children into separate prefabs** (default off), and it is legitimate for the same
reason: a human decides, nothing is inferred.

---

## One entity per primitive

`Prefab::instantiate` (`Prefab.cpp`) creates an entity per node, adds a `TransformComponent` to all
of them, and adds `MeshComponent` (+ `BoundingBoxComponent` + `MaterialComponent`) only to nodes
that carry a mesh (`Prefab.cpp:59-81`). That is correct and stays.

Collapsing a node's N primitives into one entity with a submesh list (Unreal's StaticMesh sections)
was considered and rejected:

- **Skinning and bones need per-part entities.** So does a readable hierarchy panel. With an ECS
  this is the grain of the wood.
- **Sponza wants 103 separately cullable chunks.** Collapsing loses per-primitive culling.
- **It only pays off if the draws merge too**, which needs the material id in a vertex attribute,
  which needs a static bake — and then per-primitive material assignment is gone as well. That
  bargain is why StaticMesh sections are rigid.
- The CPU side stays awkward regardless.

An entity holds one mesh and one material. The prefab is what says those 10 entities are one
object, which is enough to fix what you drag without touching entity granularity at all.

---

## The propagation landmine

**Transform propagation does not exist yet.** The only mention is an aspirational comment at
`HierarchyComponent.h:11`.

That is why instantiation currently gets away with a contradiction: the prefab stores each node's
**local** transform (`glTFLoader.cpp:608`), and `Prefab::instantiate` flattens
`world = parentWorld * localTransform` (`Prefab.cpp:49-50`), puts that **world** matrix on the
entity, *while also* calling `HierarchyComponent::setParent` (`Prefab.cpp:53-57`). Nothing composes
parent into child, so nothing double-applies — today.

The day propagation lands, Sponza's 0.008 node scale is applied twice (0.000064) and adamHead's 80
levels compound, and the flatten in `instantiate` becomes the thing to delete.

A prefab **must** store the local transform, because a blueprint you can drop anywhere cannot hold
world coordinates. So the prefab record holds local and `instantiate` flattens until propagation
exists.

---

## Naming

The prefab fixes *what you drag*. It cannot fix names.

Sponza registers 103 mesh assets named `Sponza_Mesh0_Prim0..102` — the name is synthesized from the
file stem plus mesh/primitive index (`glTFLoader.cpp:529-530`) precisely because the node and mesh
are both unnamed. There is no `extras` either. The file simply contains no names, so nothing can
recover them.

This is why thumbnails are load-bearing rather than polish. See [[Asset Metadata#Thumbnails]].

---

## Fixed: duplicate meshes on instanced nodes

Previously `loadMesh` decoded and registered a glTF mesh once **per referencing node**, so two nodes
instancing the same crate produced two separate mesh assets. Now `loadMesh` caches the decoded
primitives in `m_meshCache` keyed on `meshIndex` (`glTFLoader.cpp:355-374`), so instanced nodes
share the same mesh assets — the same map that `buildPrefab` needs to emit the prefab records.

Not triggered by Sponza (1 node), but mattered for adamHead's 80 nodes against 76 meshes.

---

## Open refinement points

- ~~Whether `AssetType::MODEL` is repurposed as `PREFAB`~~ — done, the enum value is now `PREFAB`
  (`AssetCommon.h:21`).
- `AssetType::SCENE` today means "a glTF file" (`AssetManagerEditor.cpp:216`), which collides with
  the engine `.rscene` world that [[Project Serialization]] reserves the `scenes` section for. A
  glTF scene is an instantiable asset; an engine scene is a world you edit. Rename before scene
  serialization lands.
- Whether prefabs live inline in the `.rapt` or get their own files. Sponza's ~104 nodes are fine
  inline; a 50k-node CAD import is not — the same argument that already sent `.rscene` to its own
  file.
- Bounding boxes at the prefab-node level (a node with no mesh has none today).
