# Project Serialization (.rapt)

> Status: **locked architecture, implementation started** (refined 2026-07-12 in design
> discussion, supersedes the 2026-07-11 revision). The 2026-07-12 pass replaced the "Project
> calls one system per section" ownership model with an **event-driven** one, split load into two
> synchronous phases, moved material instances into the asset registry, and made the editor block
> panel-owned + pull-on-read/upsert-on-save. Built so far: S0 wrapper, eviction-policy
> groundwork, and the three project events wired end-to-end with stub callbacks (see the stage
> list). This is the source of truth for the flow; refine it, don't re-derive it. Open refinement
> points are collected at the bottom.

**Related: [[Material System Overhaul]], [[Material Graph Compiler]], [[Asset Manager]], [[Scene]], [[Asset Metadata]], [[Prefab]]**

## Goal / intent

One human-readable **`.rapt` project file** (JSON) that holds everything needed to load a project:
the asset registry (textures), the layered material data (graphs / base materials / instances),
later scene references and editor settings. Loading a project reconstructs all of it in dependency
order. This is the foundation for "select a material and draw its nodes" and for saving authored
work at all.

A binary **`.rasset` cook** for shipped games comes later as a separate export step (see
[[#Binary path (.rasset)]]); nothing here builds it, everything here is designed so it stays
mechanical.

---

## Locked decisions

1. **Project is NOT an asset.** It sits *above* the AssetManager and populates it. Making it an
   asset would create a cycle (the project living inside the registry it defines). `Project`
   stays owned by `Application` (`Engine/src/scenes/Project.h`).
2. **Serialization goes through a tiny swappable wrapper**, never raw `yyjson_*` calls in type
   code. Backend today = yyjson; the API exposes no yyjson types so the format can be swapped by
   reimplementing one .cpp. See [[#The serialization wrapper]].
3. **Fragments are tree nodes, not strings.** A type serializes itself into a subtree of the
   shared document (write cursor) and deserializes from a read cursor. `read` = obtaining the
   cursor (no instantiation), `load` = deserializing + registering. Same read≠load split as
   before, zero re-parsing. Strings as fragment currency only appear in the binary cook.
4. **Everything referenceable has a persistent UUID** (the existing `AssetHandle`/`UUID` type,
   written as a uint64 JSON number — yyjson round-trips uint64 exactly). One id currency for
   textures, graphs, base materials, instances, later scenes/meshes.
5. **Engine builtins get reserved fixed UUIDs** (default textures, glTF base graph/material, ...).
   Their *definitions are never serialized*, only references to their fixed ids. Code recreates
   them every run under the same handle, so there is no code-vs-file drift and no load-time
   duplication.
6. **Graphs and base materials stay material-module-internal** (owned by
   `MaterialManager`/`SurfaceGraphManager`, serialized inside the materials section). They are
   NOT AssetManager assets: they are never evictable or lazily loadable (every graph must compile
   into the ubershader anyway), so the asset machinery buys nothing. They still use UUIDs, so
   promoting them later is possible. **MaterialInstances stay assets** — and because they are
   assets, their fragments live in the **asset registry section, not the `materials` section**
   (each section has exactly one owning subscriber; `materials` = graphs + bases only, owned by
   `MaterialManager`; instances are owned by `AssetManager` alongside textures/meshes).
7. **Procedural texture nodes are promoted into the engine graph** as their own `GraphNodeType`s
   with a params payload. Anything that affects pixels lives in the engine fragment; the editor
   block holds presentation only. See [[#Procedural texture nodes]].
8. **Graphs and bases load eagerly at project open** (graphs must — every one compiles into the
   ubershader; bases are tiny CPU-only records). **Instances and textures load lazily**: project
   load registers metadata only, first actual use instantiates.
9. **If nothing uses an asset it is not in memory.** The registry metadata carries a per-type
   reload source, so unused instances (and their textures) evict normally and come back on
   demand. Lifetime is the existing refcount; eviction *hints* are a later residency layer. See
   [[#Asset lifetime: reload from metadata]].
10. **Save is explicit and event-driven**: the Editor initiates it (File → Save Project /
    Ctrl+S). `Project::saveProject` fires `onProjectSerialize(root)`; each section owner writes
    its own section into the shared document, then `Project` writes the file atomically (temp +
    rename). No per-edit disk writes; autosave later is a timer firing the same event.
11. **Serialization goes through three project events, not direct Project→manager calls** (see
    [[#Event model]]). `Project` knows nothing about types or sections — it fires signals and does
    file I/O; every section owner *subscribes* and pulls/writes its own section. This decouples
    editor-only writers (panels) from the engine — `Project` never reaches into editor code.
12. **The editor block is panel-owned, pull-on-read, upsert-on-save** (see [[#Editor block]]).
    Panels read their own `editor.<panel>` section directly on open (no event) and write it on the
    save event. Writes **upsert per key** (e.g. per graph UUID) and **never replace a whole
    section**, so multiple open panels don't clobber each other and data for graphs no panel has
    open survives untouched. This requires `Project` to hold a **persistent read+write document**
    for its lifetime, not a doc that dies after load.

---

## Event model

`Project` sits *above* everything and knows only two things: the `.rapt` bytes and the fact that
there are two load phases. It does **not** route sections to systems, does not know meshes-from-
materials, and never touches a node or a param. It fires events; every section owner *subscribes*
and pulls/writes its own top-level section by name (`root.child("materials")`). Defined in
`Engine/src/events/ProjectEvents.h`:

| Event | Payload | Fired by | Subscribers do |
|---|---|---|---|
| `onProjectSerialize` | `WriteNode& root` | `Project::saveProject` | Write (upsert) their own section into `root`. |
| `onProjectRegister` | `ReadNode& root` | `Project::loadProject` | Register **metadata only** from their section — synchronous, no load, no compile, no job dispatch. |
| `onProjectRegisterComplete` | — | `Project::loadProject` | Eager post-registration work (compile graphs, resolve bases) now that all metadata exists. |

Who owns what:

| Owner | Section(s) | On serialize | On register / complete |
|---|---|---|---|
| `Project` | `metadata` + file I/O | Writes `metadata`, writes the file | Parses the file, gates `formatVersion`, fires the two phases |
| `AssetManager` | `textures`, `meshes`, `material_instances` (all asset-typed) | Writes each entry | Registers `AssetMetadata` per entry, no load; it maps section-name → `AssetType` internally (it *is* the asset authority — there is no per-type manager) |
| `MaterialManager` | `materials` (graphs + bases) | Writes graphs + bases | Register: retain raw graph/base records. Complete: compile all graphs, resolve `uuid → graphId` + pin-key tables |
| Editor panels | `editor.<panel>` | Upsert their own subsection (save only) | — reads are **pull**, on panel open, not an event (see [[#Editor block]]) |
| `SceneManager` (later) | `scenes` | Writes scene refs | Registers scene metadata |

The **fragment level is unchanged**: each type serializes one self-contained record and
cross-references **only by persistent UUID**, never by pointer or runtime index. Free functions
per module (`Material_serializeGraph(const MaterialGraph&, WriteNode)`) keep the wrapper out of
core headers like `MaterialGraph.h`.

### Ordering is a non-problem, by construction

- **Save**: sections are independent keys in the root object, so write order is irrelevant.
- **Load**: the only hard dependency is "textures registered before graphs compile." It is solved
  by the **phase split**, not by ordering subscribers: everything registers metadata in phase 1
  (order-free — nobody touches loaded state), then phase 2 does the eager builds (order-free —
  all metadata already exists). `Project`'s entire ordering knowledge is "register before
  complete." Because `EventBus::publish` is **synchronous/blocking** (`Events.h`), when the
  register `publish` returns registration is provably complete, so firing complete on the next
  line is safe — no completion tracking. This is *why* register is contractually metadata-only.

### Startup ordering

A default `Project` is created and the engine systems `init()` (and subscribe) at app startup
**before** any `loadProject`. Loading can never be the bootstrap — the systems must be operational
and listening first. This is why subscriptions live in each system's `init()`/ctor.

- **Scenes** (later): assets in **separate files** referenced from the `.rapt`
  (`"scenes": [{ "id": ..., "path": "scenes/main.rscene" }]`), not inline — scenes get huge and
  want per-scene diffs. "Load" = register metadata; "activate" = deserialize entities into a live
  `Scene` via `SceneManager`. Same read≠load split one level up. Only the section name is
  reserved for now.

---

## The serialization wrapper

`Engine/src/serialization/` (new). Super small, tree-shaped, format-agnostic API; yyjson lives
only in the .cpp. Sketch (names refinable):

```cpp
class SerialDocument {           // owns the backing document
  public:
    static SerialDocument parse(std::string_view text); // read path
    SerialDocument();                                   // write path
    std::string toText() const;                         // pretty JSON out
    WriteNode root();
    ReadNode rootView() const;
};

class WriteNode {                // cheap cursor into a document being built
  public:
    WriteNode addObject(std::string_view key);
    WriteNode addArray(std::string_view key);
    WriteNode appendObject();    // array element
    void set(std::string_view key, uint64_t v);         // + int64/double/bool/string_view
    void append(double v);       // array scalar element, + overloads
};

class ReadNode {                 // cheap cursor into a parsed document
  public:
    bool valid() const;
    ReadNode child(std::string_view key) const;
    uint64_t asU64(uint64_t fallback = 0) const;        // typed gets WITH fallbacks
    size_t size() const;         // array/object length
    ReadNode at(size_t i) const; // array element
};
```

Semantics (no global state anywhere):
- A `SerialDocument` **instance** owns the whole tree; `WriteNode`/`ReadNode` are non-owning
  cursors (a doc pointer + a node pointer, ~16 bytes, pass by value) into that instance. They are
  invalid after the document dies. Two documents can coexist; nothing is shared.
- `addObject(key)` is NOT a detached placeholder: it creates an empty child object, attaches it
  under `key` in the tree **immediately**, and returns a cursor to it. `set`/`append` also attach
  immediately. There is no separate "commit a node" step, so cursors can be created and filled in
  any order and handed down to type serializers.
- Nothing hits a string until the very end: `doc.toText()` serializes the finished tree once, and
  `Project` writes those bytes to the file.

```cpp
SerialDocument doc;                            // write mode
WriteNode root = doc.root();
WriteNode meta = root.addObject("metadata");
meta.set("formatVersion", 1u);

WriteNode textures = root.addObject("textures");
textures.set("version", 1u);
WriteNode entries = textures.addArray("entries");
WriteNode entry = entries.appendObject();      // one registry record
entry.set("id", handle);
entry.set("path", "textures/gold_albedo.png");

std::string text = doc.toText();               // Project writes this to disk
```

Rules:
- Typed reads always take a fallback — a missing field is never an error, it's the default.
  That is where most forward/backward compatibility comes from.
- No file I/O in the wrapper. `Project` reads/writes the `.rapt` bytes; `AssetManager` owns
  asset-file I/O (textures etc.), as today.
- Types implement free functions per module
  (`Material_serializeGraph(const MaterialGraph&, WriteNode)`,
  `Material_deserializeGraph(ReadNode) -> MaterialGraph`), keeping serialization includes out of
  core headers.

### Required wrapper extensions (not built yet)

The event/pull model above needs two capabilities the S0 wrapper does **not** have. Verified
against `SerialDocument.cpp`:

1. **A persistent read+write document.** Today `parse()` frees the mutable doc and produces an
   **immutable** `yyjson_read` doc, and a `SerialDocument` is read-XOR-write (`root()` errors on a
   parsed doc, `rootView()` on a built one). The pull model needs one live doc that panels read on
   open *and* write on save. Fix: `parse` builds a **mutable** doc (`yyjson_doc_mut_copy`), and
   `ReadNode` learns to read mutable values (it currently only calls immutable `yyjson_val`
   getters). `Project` then holds this doc for its whole lifetime (the existing `m_saveFile`
   member).
2. **Upsert-by-key writes.** `addObject`/`set` all call `yyjson_mut_obj_add`, which **appends the
   key even if it already exists** → duplicate keys. Needed instead: get-or-create child object by
   key (`yyjson_mut_obj_getn` + create) and replace-in-place (`yyjson_mut_obj_put`). Engine
   sections use it to `put` (replace their whole section — the managers are authoritative on the
   persistent doc); the editor uses it per-key (upsert one graph UUID's entry, keep unknowns).

---

## Persistent identity

### Generated ids
Disk/authored things keep the ids they were created with: `AssetManagerEditor::importAsset`
generates a UUID at first import (`AssetManagerEditor.cpp:112`); save writes it out; load
re-registers under the **same** handle. The UUID layout (42-bit ns timestamp << 22 | random,
`Engine/src/utils/UUID.h`) means every generated id is astronomically larger than 2^22.

### Reserved ids
Low integers < 1024 are reserved for builtins and can never collide with generated ids:

```cpp
// Engine/src/asset_manager/ReservedAssets.h (new)
// plain enum, NOT enum class: the values ARE the handles, implicit conversion is the point
enum ReservedAsset {
    RE_NONE, // = 0, matches the null handle
    RE_WHITE_TEXTURE,
    RE_FLAT_NORMAL_TEXTURE,
    RE_DEFAULT_MATERIAL,
    RE_GLTF_BASE_GRAPH,
    RE_GLTF_BASE_MATERIAL,
    RE_COUNT // append new entries above, never reorder
};
static_assert(RE_COUNT < 1024, "reserved asset ids must stay below the reserved range");
```

No explicit value assignment — append-only ordering keeps the values stable, `RE_COUNT` guards the
range.

- Init registers the default assets under these handles (replacing the ad-hoc
  `m_defaultAssetHandles` lookup path over time; the evict-protection check at
  `AssetManagerEditor.cpp:337-341` becomes a reserved-id-range check).
- `AssetManager` gets a convenience overload: `getAsset(RE_WHITE_TEXTURE)` (plain enum converts
  to the handle value implicitly, no casts anywhere).
- Serialized references to builtins round-trip with zero special-casing: an instance referencing
  the default white texture just writes `1`.
- `s_obtainGltfBaseMaterial` (`glTFLoader.cpp:758`) registers its graph/base under
  `RE_GLTF_BASE_GRAPH`/`RE_GLTF_BASE_MATERIAL`; at project load, an already-registered reserved
  id is simply skipped by the loader.

### Graph ids: persistent UUID vs runtime graphId
The runtime `graphId` stays exactly what it is — an index into `SurfaceGraphManager::m_graphs`
baked into the generated GLSL dispatcher (`SurfaceGraphManager.cpp:39`). It is **never
serialized**. Each authored graph additionally carries a persistent UUID; a `uuid → graphId` map
is rebuilt after load-time compilation. Registration order (and thus graphId values) may differ
between runs — irrelevant, because the generated GLSL is rewritten at load and nothing persistent
ever stores a graphId.

---

## `.rapt` schema

Top-level sections keyed by category; the section name IS the type, entries carry no per-item
type field. Paths are relative to the project root = the `.rapt` file's directory (the root is
never stored in the file).

Section ownership drives the layout: asset-typed sections (`textures`, `meshes`,
`material_instances`) belong to `AssetManager`; `materials` (graphs + bases) to `MaterialManager`;
`editor` to the editor panels.

```json
{
  "metadata": { "formatVersion": 1, "name": "MyProject" },

  "textures": {
    "version": 1,
    "entries": [
      { "id": 812334991022, "path": "textures/gold_albedo.png", "importConfig": { } }
    ]
  },

  "material_instances": {
    "version": 1,
    "entries": [
      { "id": 991002310007, "name": "Gold", "base": 554002110004,
        "params":   { "ALBEDO": [1.0, 0.8, 0.3, 1.0], "ROUGHNESS": 0.4 },
        "textures": { "ALBEDO_MAP": 812334991022 } }
    ]
  },

  "materials": {
    "version": 1,
    "graphs": [
      {
        "id": 931442210001,
        "name": "MossyRock",
        "output": 20,
        "nodes": [
          { "id": 1, "type": "TEXCOORD" },
          { "id": 2, "type": "TEXTURE_SAMPLE", "texture": 812334991022 },
          { "id": 3, "type": "TEXTURE_PERLIN", "params": { "scale": 10.0, "octaves": 4 } },
          { "id": 4, "type": "CONSTANT_VEC3", "values": { "0": [1.0, 0.8, 0.3] } }
        ],
        "links": [ [1, 0, 2, 1], [2, 0, 20, 0] ]
      }
    ],
    "base_materials": [
      { "id": 554002110004, "name": "glTF Base Material", "graph": 931442210001,
        "table": { "ALBEDO": [2, 0], "ROUGHNESS": [10, 1] } }
    ]
  },

  "editor": {
    "nodeEditor": {
      "graphs": { "931442210001": { "positions": { "1": [0, 0], "2": [240, 0] } } }
    }
  },

  "scenes": { "version": 1, "entries": [] }
}
```

Schema notes (each is a deliberate rule):
- **Material instances are their own asset section (`material_instances`), not part of
  `materials`.** They are assets (owned by `AssetManager`), so they sit with the other assets;
  `materials` holds only the non-asset graphs + bases (owned by `MaterialManager`). One section,
  one owner.
- **The `editor` section is panel-owned and keyed for upsert.** `editor.<panel>.<...>.<uuid>` —
  each panel owns its subsection, positions keyed by graph UUID. Written by upsert on save, never
  a whole-section replace (see [[#Editor block]]). Dropped entirely by the `.rasset` cook.
- **Node types serialize by name** via `Graph_nodeTypeName`/`Graph_nodeTypeFromName`
  (`MaterialGraphTypes.h:208-215`, already built) — enum reordering is a non-event; an unknown
  name fails only that graph.
- **`values` is sparse by input-pin index** (`GraphNode::inputValues` is per-pin optionals,
  `MaterialGraph.h:22`) — pin-list growth on a node definition doesn't shift old data.
- **`ParameterID` serializes by name** — needs the same X-macro name table trick as
  `GraphNodeType` (does not exist yet, `MaterialParameters.h:6-27`).
- **Base material `table` stores `[nodeId, pinIndex]`, NOT slice offsets.** The runtime table maps
  `ParameterID → uint offset` into the instance slice (`Material.h:39`), but offsets are
  compiler-assigned (via `GraphSlotMapping`, see how `glTFLoader.cpp:814-818` builds the table)
  and change when the compiler changes. On load the pin key is re-resolved through the freshly
  compiled mapping (`Graph_pinKey(nodeId, pinIndex)`), so a compiler upgrade can't corrupt
  authored materials.
- **Instance `params` are semantic typed values keyed by parameter name**, never a raw slice dump
  (same compiler-layout independence). On save, values are read back through the base's table +
  the mapping's `PinType`; on load, written via `setParameter`.
- **Texture references are always UUIDs** — a generated id for disk textures, a reserved id for
  defaults. Never paths (paths live only in the registry entry), never bindless indices.

### Editor block

The `editor` section is **optional, presentation-only, and owned entirely by the editor panels** —
the engine never stores, sees, or round-trips it. (The earlier design had the engine graph record
hold the block "opaquely"; that made the engine the custodian of data it can't model and the
wrapper has no raw passthrough — dropped.) It is a **separate top-level section**, not nested in
each graph, so the panel that owns it writes it independently without coordinating with
`MaterialManager`'s `materials` section.

Panels manage it themselves, asymmetrically:

- **Read = pull, on open, no event.** When a panel opens a graph it reads
  `editor.nodeEditor.graphs.<uuid>` from `Project`'s live document and applies the positions. If
  missing (e.g. the code-built glTF base graph), it synthesizes an auto-layout.
- **Write = on the save event, by upsert.** On `onProjectSerialize` each open panel writes only
  the graphs it holds, upserting **per graph UUID** — update the entry if present, add it if not,
  and **never replace the whole section**. Consequences:
  - Two panels open on different graphs write disjoint UUID keys — no clobber.
  - A graph no panel has open keeps its existing entry untouched (the data lives in the persistent
    document, not in any panel), so nothing is lost by not having an editor open. This is the hole
    the panel-owned + persistent-doc design closes.
  - Two panels on the *same* graph is last-writer-wins for that UUID; a finer per-node diff is a
    later refinement, not v1.

Anything that affects the rendered result is banned from this block — which is why procedural
params moved into the engine node (next section). The `.rasset` cook drops the section entirely.

---

## Procedural texture nodes

Previously editor-only (`NodeEditorPanel::TextureNodeKind` + `TextureNodeData`,
`NodeEditorPanel.h:50-57, 96-104`), lowered to `TEXTURE_SAMPLE` at compile. That made the
generator params unserializable engine-side and graph→canvas reconstruction lossy. Promoted:

- **New `GraphNodeType`s**: `TEXTURE_WHITE_NOISE`, `TEXTURE_PERLIN`, `TEXTURE_SIMPLEX`,
  `TEXTURE_RIDGED` (mirroring `TextureNodeKind` minus `ASSET`, which is the existing
  `TEXTURE_SAMPLE`). They compile exactly like `TEXTURE_SAMPLE` — the node's texture is its
  generated output; the compiler treats it as a texture it was handed.
- **Params live on the engine node** as a name→value payload (`GraphNode` gains an optional
  `generatorParams`), keyed by the reflected `ProceduralParameter::name`
  (`ProceduralTextures.h:105-115`). Name-keyed so generator param reordering/addition doesn't
  break old files.
- **Regeneration at load**: `ProceduralTexture` is engine code
  (`Engine/src/generators/textures/ProceduralTextures.h:150`), so graph deserialization can
  regenerate the texture (compute dispatch) and register it as a virtual texture before the graph
  compiles. Constraint: its shader must come precompiled (`.spv`) or compile on the main thread —
  glslang never runs on job fibers (see [[no-glslang-on-fibers]]).
- **The editor keeps only the interactive part**: preview image, param drag widgets,
  regenerate-on-edit. `TextureNodeData` becomes a view over the engine node's payload instead of
  the owner of the truth.
- **Runtime/cook**: the game never generates — the `.rasset` cook bakes the generated texture as
  a real texture asset. (Refinement open: whether the editor session regenerates on every load or
  caches the generated texture to disk — start with regenerate-on-load, it's one compute
  dispatch per node.)

---

## Asset lifetime: reload from metadata

The registry entry (metadata) is the persistent identity; **the loaded object exists only while
something uses it**. If nothing references an asset, it does not stay in memory — that is what
the metadata is for. No new storage type, no eviction flag, no root set.

[[Asset Metadata]] refines this section: it splits **provenance** (where it came from, reimport
only, allowed to dangle) from the **reload source** below (how to get the bytes back, always
valid), and defines what import writes. [[Prefab]] covers the asset layer glTF import produces.

The reload source lives in the metadata and is **flexible per asset type** — a variant of options,
not one mechanism:
- **Own file**: a texture's is its `filePath` (as today).
- **Inline description**: a material instance's is a **`MaterialInstanceDescription`** — base
  uuid, param values, texture uuids, name — stored as a new alternative in the metadata's
  `AssetImportConfigVariant` (`AssetImportConfig.h:27`, the variant already means "data needed to
  build the loaded object"). Plain CPU data, a few hundred bytes: no GPU objects, no `AssetRef`s,
  so an unloaded instance pins nothing.
- **Container locator** (same variant, later): a reference into a container file — the `.rapt` +
  entry id, and for the binary path literally "`.rasset` + offset + size, read those bytes".
  Whether an instance's source is the inline description or a `.rapt` locator is an
  implementation choice per type; v1 uses the inline description (it's tiny and avoids re-parsing
  the project file), the cook emits locators.

Lifecycle:
- **Project load registers, never instantiates**: each instance fragment deserializes into a
  `MaterialInstanceDescription` in the registry. Engine sections copy everything they need out of
  the document during register/complete and never point back into it; the document itself
  **persists** for the project's lifetime so the editor panels can pull their sections lazily.
- **First `getAsset(handle)`** → `AssetImporter::loadMaterial` builds the `MaterialInstance` from
  the description: slot alloc, SSBO write, `getAsset` on its texture uuids — **textures load
  here, at first actual use**, not at project open.
- **While referenced** (mesh components, an open editor panel — the material editor holds an
  `AssetRef` to the material it has open): resident; its `AssetPtr<Texture>` members pin its
  textures. Correct — it is in use.
- **Count hits 0** → evicted like anything else → texture refs drop → unused textures hit 0 and
  evict too. Megabytes freed because nothing uses them.
- **The registry entry survives eviction** — the erase-on-evict rule keys on *reloadability*
  (metadata has a reload source), not on `VIRTUAL` per se. True virtuals (no source) still erase
  as today (`AssetManagerEditor.cpp:358-361`).
- **Flush rule**: the description is always the persistence record. A loaded instance flushes its
  live state back into its description on project save and on evict — so edits can never be lost
  to an eviction, and save can serialize every instance uniformly from metadata.

The editor's material list is a registry scan (metadata names), not loaded assets — listing
materials loads nothing.

### Residency refinement (future, orthogonal to serialization)

Instant evict at count 0 is correct but can hitch mid-play: a material that only *sometimes*
appears (the animal-skin case) would reload — textures included — at the worst moment. The fix is
a residency layer over the same mechanics, inside `AssetManagerEditor`:
- count 0 → a **cold LRU list** instead of instant free (each entry: handle, GPU byte size, last
  used frame); actually freed when memory pressure demands it.
- **Pressure = our watermarks over the driver-reported budget** (`VK_EXT_memory_budget` via
  `vmaGetHeapBudgets` on the device-local heap, polled ~1/s in `onUpdate`; the extension is only
  a sensor — usage and budget per heap, already accounting for other processes — the policy below
  is ours). With `budget = min(driverBudget, userCapBytes if set)`:
  - **soft, usage > 80% of budget** → drain the cold list oldest-first, skipping
    `EVICT_HINT_LAST`, down to 75% (drain past the line, else it thrashes at the watermark);
  - **hard, usage > 90% of budget** → drain the whole cold list, hints included;
  - cold list empty but still over → warn and move on, what remains is live data.
  Comparing raw `usage` vs `budget` needs no separate evictable-pool accounting — render targets
  and arenas are inside `usage` and simply aren't in the cold list. The explicit user cap is an
  override/clamp (testing, self-discipline), not the primary mechanism — a static "% of total
  VRAM" is wrong on shared systems and iGPUs, which is why the driver budget is the denominator.
  Future refinement: clamp the headroom in bytes (`min(20%, ~1.5GB)`) so huge cards don't idle
  gigabytes; v1 uses the plain percentages.
- An **eviction-hint policy** on the metadata expresses retention priority. Every value is a
  *hint*, never a promise — there is deliberately no "always keep" value, because that is just a
  leak with a name. *Implemented (groundwork only):* `AssetEvictionPolicy` in `AssetCommon.h` —
  `EVICT_IMMEDIATE` (today's behavior, default), `EVICT_HINT_LAZY` (cold list until budget
  pressure), `EVICT_HINT_LAST` (the "keep loaded for me please" hint — loaded stuff a developer
  expects to need again evicts last, but still evicts under real pressure). Field lives on
  `AssetMetadata`; the decision point sits in `AssetManagerEditor::evictAsset`. Until the
  residency layer exists, the hint values deliberately behave identically to immediate — skipping
  eviction without a budget system would leak.
- **Scene warmup** = the scene's material list `getAsset`-ed ahead of use, refs dropped — they
  sit warm in the cold list instead of loading mid-play. Pairs with `EVICT_HINT_LAST` for the
  might-appear cases.

Serialization only requires that eviction is *safe* (reload-from-metadata above); the residency
layer is a later AssetManager improvement and nothing in this plan depends on it.

---

## The flow

### Save (`Project::saveProject`)
1. `Project` writes `metadata` into its live document.
2. `Project` fires `onProjectSerialize(root)`. Each subscriber upserts its own section:
   - `AssetManager` → `textures` / `meshes` / `material_instances` (**disk / describable assets
     only** — relativized paths or `MaterialInstanceDescription`; virtual and reserved-id assets
     skipped). Engine sections `put` (replace their whole section — the managers are authoritative).
   - `MaterialManager` → `materials` (`graphs`, `base_materials`; reserved-id builtins skipped).
   - Each open editor panel → its `editor.<panel>` subsection, **upsert per key** (never replaces
     the section, so unopened graphs and other panels' data survive).
3. `Project` writes the document to a temp file and renames over the `.rapt`.

Order within step 2 is irrelevant — sections are independent keys.

### Load (`Project::loadProject`)
1. Read + parse into the **persistent live document**; gate on `formatVersion`.
2. `Project` fires `onProjectRegister(root)` — **phase 1, metadata only, synchronous**:
   - `AssetManager` → `AssetManagerEditor::registerAsset(handle, metadata)` per entry in each
     asset section (registry insert with a *given* handle, **no load**, no UUID generation, no
     disk). `material_instances` register as `MaterialInstanceDescription` metadata — nothing
     instantiates.
   - `MaterialManager` → retain each raw authored graph + base record by UUID. **No compile yet.**
   - Editor panels do **not** listen here — their reads are pull, on open.
3. `Project` fires `onProjectRegisterComplete()` — **phase 2, eager builds** (all metadata now
   exists, so this is order-free):
   - `MaterialManager` compiles all graphs (regenerating procedural textures; resolving texture
     UUIDs via `getAsset` — graph-referenced textures load here since compilation bakes their
     bindless indices into the defaults slice), builds `uuid → graphId`, recreates `BaseMaterial`s
     (pin-key table → offsets through the fresh `GraphSlotMapping`), `writeGeneratedFiles` once +
     triggers the shader rebuild.
4. Lazy afterward (no `Project` involvement): first `getAsset(handle)` on an instance builds the
   live `MaterialInstance` from its description, and only then do its textures load. Scene
   activation drives the cascade.

`Project`'s only ordering knowledge is "register before complete." `publish` is synchronous, so
when the register call returns the metadata is all present and complete can fire on the next line.

---

## Versioning and failure containment

- **Integer versions, not semver.** One top-level `formatVersion` (overall shape) plus a small
  `version` per section. Per-section versions are what make "breaking materials doesn't break
  textures" true: each section loader checks its own version and degrades independently.
- **Entry-level containment:** every entry deserializes independently; a bad entry logs
  (`RP_CORE_ERROR` with section + id) and is skipped; the section continues; the project still
  opens. An instance whose base is missing binds the default material (`RE_DEFAULT_MATERIAL`).
- **Forward compat:** unknown sections and unknown fields warn and are ignored. Missing fields
  take the reader's fallback defaults.
- **The honesty rule:** if anything was skipped during load, the project is flagged
  dirty-with-data-loss and the editor warns before the next save overwrites the file. That is the
  guard against a partial load silently destroying authored work. (Carrying unknown sections
  through verbatim is possible later — the original doc is kept alive — but is not v1.)

---

## Binary path (.rasset)

A separate export/cook step, not a parallel writer maintained now. The editor (or a CLI cook)
reads live state or the `.rapt` and emits packed binary sections; a runtime `AssetManagerBase`
sibling reads them. Properties above that make this mechanical later — the checklist to not
violate:
- fragments self-contained, versioned per section
- all cross-references are persistent UUIDs
- editor block droppable without touching meaning
- generated GLSL rebuildable from graphs (or shipped precompiled)
- procedural textures bakeable to real texture assets

---

## Implementation stages

- **S0 — wrapper. [DONE]** `Engine/src/serialization/SerialDocument.{h,cpp}` —
  `SerialDocument`/`WriteNode`/`ReadNode` exactly per the sketch above; yyjson confined to the
  .cpp (cursors are `{void*, void*}` value types). Additions beyond the sketch: `appendArray()`,
  and `const char*` overloads of `set`/`append` (without them a string literal binds to the
  `bool` overload). Misuse paths log `RP_CORE_ERROR` and return invalid cursors. Not yet
  build-verified.
- **S1 — identity groundwork.** `ReservedAsset` enum + defaults registered under reserved
  handles + `getAsset(ReservedAsset)` overload; `AssetManagerEditor::registerAsset(handle,
  metadata)` (registry insert with a given handle, no load, no UUID generation).
  - *Pulled forward, [DONE]:* the eviction-policy groundwork from the residency section —
    `AssetEvictionPolicy` in `AssetCommon.h`, field on `AssetMetadata` (default
    `EVICT_IMMEDIATE`), decision point in `AssetManagerEditor::evictAsset`. All three values
    currently take the identical eviction path (no residency layer yet), so behavior is
    unchanged.
- **S1.5 — project events. [DONE, stubs]** `Engine/src/events/ProjectEvents.h` —
  `onProjectSerialize(WriteNode&)`, `onProjectRegister(ReadNode&)`, `onProjectRegisterComplete()`.
  `ListenerID` hoisted to a namespace-scope `EventListenerId` in `Events.h`. `Project::saveProject`
  fires serialize; `loadProject` fires register then register-complete. Stub subscribers registered
  (IDs stored, removed on teardown): `AssetManager::init` (serialize + register),
  `MaterialManager::init` (serialize + register + complete, removed in `shutdown`),
  `NodeEditorPanel` ctor (serialize only, per-instance, removed in dtor). All callback bodies are
  empty `(void)root;` stubs. Also here: `Project` moved to a `.cpp`, `loadProject` returns
  `unique_ptr`, worlds are now `SceneManager`-owned `unique_ptr` + raw `World*`.
- **S1.6 — wrapper: persistent read+write doc + upsert.** The two [[#Required wrapper extensions
  (not built yet)]]: `parse` → mutable doc, `ReadNode` over mutable values, `Project` holds the
  doc for its lifetime; get-or-create-by-key + `put`/replace so writes upsert instead of appending
  duplicate keys. Everything below depends on these.
- **S2 — graph library.** `SurfaceGraphManager` (or a small library class inside it) retains the
  authored `MaterialGraph` + name + UUID alongside each `CompileResult`
  (today the source graph is discarded at `SurfaceGraphManager.cpp:37-45`); `uuid → graphId`
  map; `BaseMaterial` gains a UUID; `ParameterID` name table (X-macro). The editor block is **not**
  retained here — it's panel-owned (see [[#Editor block]]).
- **S3 — procedural promotion.** New `GraphNodeType`s + `generatorParams` payload + compiler
  treats them as texture samples; editor's `TextureNodeKind` becomes a view; regenerate at load.
- **S4 — fill the engine callbacks.** `MaterialSerialization.{h,cpp}`: graph/base serialize +
  deserialize; fill `MaterialManager`'s serialize (write `materials`) / register (retain raw) /
  complete (compile) stubs. Fill `AssetManager`'s serialize (write `textures` / `meshes` /
  `material_instances`) / register (`registerAsset`) stubs. `MaterialInstanceDescription` in the
  metadata variant + the `loadMaterial` importer path that builds an instance from it + the
  flush-on-save/evict rule.
- **S5 — Project save/load body.** `metadata` write, `formatVersion` gate, atomic temp+rename,
  containment rules; Editor Save command (fires `onProjectSerialize` via `Project`) + dirty flag.
- **S6 — editor hookup.** Fill `NodeEditorPanel`'s serialize stub (upsert `editor.nodeEditor` per
  graph UUID) + pull-on-open read; `selectMaterial` pulls the retained authored graph into the
  canvas and applies positions from the `editor` section (`NodeEditorPanel::loadGraph` exists;
  `selectMaterial` currently only sets the dropdown text, `NodeEditorPanel.cpp` region shifted).
- **Later:** scenes section + `.rscene` files, `.rasset` cook, the residency layer (cold LRU +
  memory budget + eviction hints + scene warmup), per-node editor-block diffing for the
  same-graph-in-two-panels case.

---

## Open refinement points

- Exact home of the retained authored graphs: inside `SurfaceGraphManager` next to `m_graphs`,
  or a sibling `GraphLibrary` it owns. (Lean: inside, until it grows lookup weight.)
- Where the persistent `SerialDocument` lives on `Project` and its exact accessor for panels
  (a `Project::editorSection(name)` cursor vs. handing back the doc). `m_saveFile` is the intended
  home.
- Whether load-time procedural regeneration caches generated textures to disk or regenerates
  every open (start: regenerate).
- glTF import persistence: today glTF import registers virtual instances every run; long-term an
  import becomes a one-time step that persists instances into the project (classic editor model).
  Tied to scene serialization, does not block materials. **Designed in [[Asset Metadata]] and
  [[Prefab]]** (import cooks + evicts, prefab records the arrangement); still unbuilt, still not
  blocking materials.
- Instance param read-back needs the base's `GraphSlotMapping` `PinType` to write typed JSON —
  confirm the mapping is retained per base or re-queryable at save time.
- `metadata` growth: project config (initial world name, shader dir) once scenes serialize.
