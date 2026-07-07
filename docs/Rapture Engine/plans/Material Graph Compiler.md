# Material Graph Compiler

**Parent: [[Material System Overhaul]] (Phase 2b). Related: [[GraphInstanceData]], [[MaterialInstance]], [[MaterialData]], [[GBufferPass]]**

The design for Phase 2b: the `MaterialGraph` IR and the **data-driven** GLSL compiler that turns a node graph into an `evalSurface_<Name>` function inside `generated/SurfaceGraphs.glsl`. This is codegen only — no interpreter, no bytecode (see [[Material System Overhaul#8. Why codegen, not the interpreter]]). It supersedes the enum-`switch` compiler sketched in `Engine/src/materials/PROCEDURAL_MATERIALS_DESIGN.md`, whose one-`case`-per-node emitter is exactly the "can't add nodes" wall we are escaping.

---

## 0. What already exists (verified, do not rebuild)

The 2a plumbing this compiler feeds into is live and proven on a lit sphere:

- **GLSL contract** (`Engine/assets/shaders/glsl/common/MaterialCommon.glsl:95-111`): `SurfaceInputs` (inputs a surface eval reads) and `SurfaceData` (outputs it writes). These are the compiler's fixed I/O boundary — the emitted function reads `si.*` and writes `surf.*`.
  - `SurfaceInputs { vec2 uv; vec3 worldPos; vec3 worldNormal; vec3 tangent; vec3 bitangent; uint flags; }`
  - `SurfaceData { vec3 albedo; vec3 normal; float roughness; float metallic; float ao; uint shadingModelId; }`
- **Pool** (`MaterialCommon.glsl:76-86`): `GraphInstanceData { uint textures[16]; vec4 constants[16]; }`, read as `u_graphData.instances[gii].textures[k]` / `.constants[j]`. C++ mirror `GraphInstanceData.h` (320 B, `alignas(16)`, `static_assert`).
- **Dispatch seam** (`GBuffer.fs.glsl:80-82`): `matHasFlag(flags, MAT_FLAG_IS_GRAPH) ? evalSurfaceGraph(mat.graphId, si, mat.graphInstanceIndex) : evalStaticSurface(si, mat)`. The compiler owns the true side.
- **Generated file** (`generated/SurfaceGraphs.glsl`): currently hand-written graph 0 (`fract(si.worldPos*0.5) * constants[0].rgb`) + an `evalSurfaceGraph(graphId, si, gii)` dispatcher `switch`. The compiler's job is to **emit this file's contents** instead of hand-authoring them.
- **Instance API** (`MaterialInstance.h:67`): `setGraph(uint32_t graphId, const GraphInstanceData& data)` — allocates a graph slot, uploads the pool, sets `MAT_FLAG_IS_GRAPH` + `graphId` + `graphInstanceIndex`. The compiler produces the `(graphId, GraphInstanceData)` pair this consumes.
- **Shader load** (`GBufferPass.cpp:658`): the G-buffer shader is imported as an asset; it `#include`s `generated/SurfaceGraphs.glsl` (`GBuffer.fs.glsl:6`). Regenerating the file + rebuilding that shader is all it takes to pick up new/changed graphs (the rebuild wiring is 2c, not here).

**So 2b adds no GPU code and no pipeline changes.** It is pure C++ that writes one `.glsl` file. Until we flip graph 0 from hand-written to generated, rendering is byte-identical — this is a low-risk chunk.

---

## 1. The one idea: data-driven nodes

Adding a node must be a **data entry**, never a compiler edit. Every node type is a `NodeDefinition` — its pins and a GLSL template string. The compiler never `switch`es on node type; it substitutes placeholders into the template. This is the whole point of the chunk and the difference from `PROCEDURAL_MATERIALS_DESIGN.md`'s `emitGLSLNode` (a giant `switch` with one hand-written `case` per node — the thing that doesn't scale).

Exactly **two** things are not pure data:
1. The **output sink** (`surface_output`) — its input pins map to `surf.*` fields, so it is special-cased in the emit pass (it declares no local, it assigns the struct).
2. **Resource slot assignment** — a node flagged `resourceKind` reserves a `textures[]` / `constants[]` pool slot. Driven by the flag, not by type name.

Everything else — math, mix, texture sample, the `si.*` input readers — is a template.

---

## 2. IR data structures

New folder `Engine/src/materials/graph/` (snake_case, per project convention). Keep entt out of it; these are plain data. Doxygen `@brief` on the public types.

### 2.1 `MaterialGraphTypes.h` — vocabulary + node definitions

```cpp
enum class PinType { FLOAT, INT, VEC2, VEC3, VEC4 };

// The closed set of node types. External formats (MaterialX, glTF) are translated onto
// these at import, so the set is fixed at compile time. NONE is a sentinel.
enum class GraphNodeType {
    NONE,
    POSITION, NORMAL, TEXCOORD,
    CONSTANT_FLOAT, CONSTANT_INT, CONSTANT_VEC3, CONSTANT_VEC4,
    TEXTURE_SAMPLE,
    MULTIPLY_FLOAT, MULTIPLY_VEC3, MULTIPLY_INT,
    ADD_VEC3, ADD_INT,
    MIX_FLOAT, MIX_VEC3,
    FRACT_VEC3,
    SURFACE_OUTPUT,
};

enum class ResourceKind {
    NONE,       // pure compute / input reader
    TEXTURE,    // reserves one textures[] pool slot, exposed as {tex}
    CONSTANT,   // reserves one constants[] pool slot, exposed as {const}
};

struct PinDef {
    std::string name;               // referenced in templates as {name}
    PinType type;
    glm::vec4 defaultValue{0.0f};   // emitted as a literal when the input is unconnected
};

struct NodeDefinition {
    GraphNodeType type = GraphNodeType::NONE;
    std::vector<PinDef> inputs;
    std::vector<PinDef> outputs;        // starter set is single-output; multi-output supported (see 4.4)
    std::string glslTemplate;           // the output RHS expression; placeholders per 4.1
    ResourceKind resourceKind = ResourceKind::NONE;
};
```

Node types are an **enum, not strings** — the set is closed because external formats are translated onto it at import (the importer owns the MaterialX-name to `GraphNodeType` mapping), and enum keys give type safety and a lookup with no string comparison. Types come in **typed variants** (`MULTIPLY_VEC3` / `MULTIPLY_FLOAT` / `MULTIPLY_INT`) so pins line up by type; coercion is the deliberate exception, not the bridge holding a graph together.

### 2.2 `NodeRegistry.h/.cpp` — the node registry

```cpp
class NodeRegistry {
  public:
    static const NodeDefinition* get(GraphNodeType type);   // nullptr if unknown
    static void registerNode(NodeDefinition def);
    static void registerBuiltins();   // seeds the starter set (Section 5)
};
```

A `static std::unordered_map<GraphNodeType, NodeDefinition>`. `registerBuiltins()` runs from the `SurfaceGraphManager` constructor. Adding a node later = a new `GraphNodeType` enumerator plus one `registerNode({...})` call. No compiler logic changes.

### 2.3 `MaterialGraph.h` — the authored graph

```cpp
struct GraphNode {
    uint32_t id = 0;
    GraphNodeType type = GraphNodeType::NONE;
    glm::vec4 constantValue{0.0f};  // CONSTANT nodes: the slot value
    AssetPtr<Texture> texture;      // TEXTURE nodes: bound texture, bindless index read at compile
    glm::vec2 editorPosition{0.0f}; // UI only, ignored by the compiler
};

struct GraphConnection {
    uint32_t srcNode;  uint32_t srcPin;   // pin indices into the node's outputs / inputs
    uint32_t dstNode;  uint32_t dstPin;
};

struct MaterialGraph {
    std::string name;                    // sanitized -> evalSurface_<Name>
    std::vector<GraphNode> nodes;
    std::vector<GraphConnection> connections;
    uint32_t outputNodeId = 0;           // the surface_output node
    // exposed params (editor-tweakable constants) added with the editor, Phase 3
};
```

`AssetPtr<Texture>` on the texture node matters for eviction: it holds an `AssetRef` alive, so a graph keeping a texture won't have the bindless slot freed under it (see [[project_asset_eviction]] / [[Material System Overhaul]] texture-ref handling). The compiler reads `texture`'s bindless index at compile time and writes it into `GraphInstanceData.textures[k]`.

---

## 3. Compiler output contract

```cpp
struct GraphSlotMapping {
    // node id -> pool slot, so the editor knows where to write a node's live value
    std::unordered_map<uint32_t, uint32_t> constantSlots;   // CONSTANT node -> constants[] index
    std::unordered_map<uint32_t, uint32_t> textureSlots;    // TEXTURE  node -> textures[]  index
};

struct CompileResult {
    bool success = false;
    std::string error;               // human-readable, first failure

    std::string functionName;        // "evalSurface_MossyRock"
    std::string glslFunction;        // the full function body
    uint32_t dispatcherCase = 0;     // graphId assigned by the manager, filled in on register

    GraphInstanceData defaults;      // pool pre-filled from constant literals + texture bindless indices
    GraphSlotMapping mapping;        // for the editor / MaterialInstance re-writes
};

class MaterialGraphCompiler {
  public:
    CompileResult compile(const MaterialGraph& graph);   // pure; no GPU, no file IO
};
```

`compile` is a pure function — no file IO, no Vulkan. That keeps it unit-testable and safe to run anywhere (including off the main thread in 2c; it never touches glslang, only string-builds — glslang runs later when the shader rebuilds, on a dedicated thread per [[no-glslang-on-fibers]]).

---

## 4. Codegen rules (the part worth pinning down)

### 4.1 Template placeholder language

For a **single-output node** (the whole starter set), `glslTemplate` is the output **expression** (RHS only — no `{out}`, no trailing `;`). The compiler declares the var and emits `<type> _nN = <expr>;`, so generated code reads like the hand-authored example (`vec3 _n2 = mix(...);`). Placeholders:

| Placeholder   | Expands to                                                        |
|---------------|-------------------------------------------------------------------|
| `{pinName}`   | the resolved expression feeding input pin `pinName` (see 4.2)     |
| `{tex}`       | `u_graphData.instances[gii].textures[K]` — this node's texture slot |
| `{const}`     | `u_graphData.instances[gii].constants[K]` — this node's const slot  |

Pin names may not be `tex` or `const`. Substitution is literal string replace (the `{}` braces delimit, so `{a}` never matches inside `{ab}`).

Examples (starter set):
- `MULTIPLY_VEC3`: `{a} * {b}`  ·  `ADD_VEC3`: `{a} + {b}`  ·  `MIX_VEC3`: `mix({a}, {b}, {t})`  ·  `FRACT_VEC3`: `fract({a})`
- `TEXTURE_SAMPLE`: `texture(u_textures[{tex}], {uv})` (`resourceKind = TEXTURE`)
- `TEXCOORD`: `si.uv`  ·  `POSITION`: `si.worldPos`  ·  `NORMAL`: `si.worldNormal`
- `CONSTANT_VEC3`: `{const}.xyz`  ·  `CONSTANT_FLOAT`: `{const}.x`  ·  `CONSTANT_VEC4`: `{const}` — each `resourceKind = CONSTANT`, reading its slot at the node's type.

**Multi-output** nodes (deferred past the starter set, Section 4.4) use statement-form templates with `{out:name}` writing each pre-declared output var.

### 4.2 Input resolution + type coercion

For each input pin the compiler finds the incoming `Connection`. If connected, the source is the upstream node's output var (or `si.*` for readers). If **unconnected**, it emits the pin's `defaultValue` as a typed literal (`float 0.5`, `vec3(1.0, 0.0, 0.0)`, …).

Emission is **typed**: locals are `float`/`int`/`vec2`/`vec3`/`vec4`, and because nodes come in typed variants the common wire is same-type and needs no conversion. When a source pin's type genuinely differs from the dest pin's (a deliberate crossing), insert a coercion `coerce(expr, from, to)`:

| from → to        | expansion                    |
|------------------|------------------------------|
| scalar → scalar  | `<to>(expr)` (`float`↔`int`) |
| scalar → vector  | `vecN(expr)` (splat)         |
| vector → scalar  | `(expr).x`, wrapped `int(...)` for `INT` |
| `VEC4 → VEC3`    | `(expr).xyz`                 |
| `VEC3 → VEC4`    | `vec4(expr, 1.0)`            |
| `VEC2 → VEC3`    | `vec3(expr, 0.0)`            |
| `VEC3 → VEC2`    | `(expr).xy`                  |
| same             | `expr`                       |

(Alpha defaults to `1.0` on vec3→vec4, right for colours. These are conventions, not correctness; revisit if a node needs a different pad.)

### 4.3 The compile pipeline (ordered)

1. **Validate**: exactly one `surface_output` node (== `outputNodeId`); every node's `type` resolves in `NodeRegistry`; the output is reachable. On failure set `error`, `success=false`, return.
2. **Topo-sort** from the output backwards over `connections`. Only nodes that feed the output are visited → free dead-code elimination. Cycle ⇒ error.
3. **Resource pass**: walk sorted nodes; for each `resourceKind != NONE` assign the next pool slot (`textures` or `constants`), record it in `mapping`, and pre-fill `defaults`: CONSTANT node's `constantValue → defaults.constants[K]`, TEXTURE node's `texture` bindless index `→ defaults.textures[K]`. Overflow (>16 either pool) ⇒ error (the signal to migrate pool to the variable-length SSBO slice, [[Material System Overhaul#6.2]] Option B).
4. **Emit pass**: for each sorted non-output node, allocate `_n<counter>` of the output's type, substitute the template, emit `  <glslType> _nN = <rhs>;` (or the raw statement for multi-out). Record `nodeId → (varName, PinType)`.
5. **Output sink**: for each `SurfaceData` field, find the `surface_output` input pin of that name; emit `surf.<field> = coerce(srcVar, srcType, fieldType);`. Unconnected fields get sensible defaults — `albedo=vec3(1.0)`, `normal=normalize(si.worldNormal)`, `roughness=0.5`, `metallic=0.0`, `ao=1.0`, `shadingModelId=SM_OPENPBR_STANDARD` (matches the current hand-written graph 0 and the magenta-error fallback conventions in the existing file).
6. **Wrap**: `SurfaceData evalSurface_<Name>(SurfaceInputs si, uint gii) { SurfaceData surf; <emitted>; return surf; }` into `CompileResult.glslFunction`.

### 4.4 Multi-output (design-complete, starter set skips it)

Nodes like `split` (vec4 → r,g,b,a) declare >1 output pin; the compiler pre-declares one var per output and the template assigns each `{out:r} = {in}.r;` etc. The IR already supports it (`outputs` is a vector); the starter set is single-output only to keep the first cut small.

---

## 5. Starter node set

The full catalogue with pins is [[Material Graph Nodes]]. In brief, the starter set is typed variants:

- Inputs: `POSITION`, `NORMAL` (vec3), `TEXCOORD` (vec2).
- Values: `CONSTANT_FLOAT`, `CONSTANT_INT`, `CONSTANT_VEC3`, `CONSTANT_VEC4` (each CONSTANT).
- Sample: `TEXTURE_SAMPLE` (uv vec2 → vec4, TEXTURE).
- Math: `MULTIPLY_FLOAT`/`MULTIPLY_VEC3`/`MULTIPLY_INT`, `ADD_VEC3`/`ADD_INT`, `MIX_FLOAT`/`MIX_VEC3` (t is a scalar factor), `FRACT_VEC3`.
- Sink: `SURFACE_OUTPUT` with pins `albedo` (vec3), `normal` (vec3), `roughness`/`metallic`/`ao` (float). (`emission` / explicit `shadingModel` join in Phase 5 with RT3.)

**Acceptance test:** author a graph = `fract(position * scale) * tint → surface_output.albedo` (scale = `CONSTANT_VEC3` `(0.5)`, tint = `CONSTANT_VEC3` `(1.0, 0.4, 0.2)`), compile it, register it as graph 0, confirm the sphere renders identically to the hand-written version, then **delete the hand-written `evalSurface_Test`**. That is the moment 2b is real. (The tint moves from a hand-set `constants[0]` to a compiler-assigned slot — same pixels, now data-driven.)

---

## 5b. Worked example (end to end, actual values)

The acceptance-test graph, all the way through — this is the concrete glue.

### The node definitions it uses (real `NodeDefinition` values)

```cpp
// registerBuiltins() entries (abridged to the ones this graph touches)
{ .type=POSITION,       .outputs={{"out",VEC3}},  .glslTemplate="si.worldPos" }

{ .type=CONSTANT_VEC3,  .outputs={{"out",VEC3}},  .glslTemplate="{const}.xyz",
  .resourceKind=CONSTANT }

{ .type=MULTIPLY_VEC3,  .inputs={{"a",VEC3,vec4(1)},{"b",VEC3,vec4(1)}},
  .outputs={{"out",VEC3}},  .glslTemplate="{a} * {b}" }

{ .type=FRACT_VEC3,     .inputs={{"a",VEC3,vec4(0)}},
  .outputs={{"out",VEC3}},  .glslTemplate="fract({a})" }

{ .type=SURFACE_OUTPUT,
  .inputs={{"albedo",VEC3},{"normal",VEC3},{"roughness",FLOAT},{"metallic",FLOAT},{"ao",FLOAT}} }
  // the sink: special-cased, see 4.3 step 5
```

### The authored graph (real `MaterialGraph` values)

```
nodes:
  1  POSITION
  2  CONSTANT_VEC3   constantValue = (0.5, 0.5, 0.5, 0)     // scale
  3  MULTIPLY_VEC3
  4  FRACT_VEC3
  5  CONSTANT_VEC3   constantValue = (1.0, 0.4, 0.2, 1.0)   // tint
  6  MULTIPLY_VEC3
  7  SURFACE_OUTPUT
connections:  (srcNode.pin -> dstNode.pin)
  1.out -> 3.a      2.out -> 3.b
  3.out -> 4.a
  4.out -> 6.a      5.out -> 6.b
  6.out -> 7.albedo
outputNodeId = 7
```

### Compile, step by step

**Topo-sort** from node 7 backwards → `[1, 2, 3, 4, 5, 6]` (7 handled by the sink pass).

**Resource pass** assigns pool slots and pre-fills `GraphInstanceData defaults`:
| node | kind     | slot          | defaults written                  |
|------|----------|---------------|-----------------------------------|
| 2    | CONSTANT | `constants[0]`| `constants[0] = (0.5,0.5,0.5,0)`  |
| 5    | CONSTANT | `constants[1]`| `constants[1] = (1.0,0.4,0.2,1.0)`|

`mapping.constantSlots = { 2→0, 5→1 }`. All `textures[]` stay 0.

**Emit pass** (var counter `_n0..`; every wire is vec3-to-vec3, so no coercion):
| node | emitted line                                              | note                   |
|------|-----------------------------------------------------------|------------------------|
| 1    | `vec3 _n0 = si.worldPos;`                                 | template `si.worldPos` |
| 2    | `vec3 _n1 = u_graphData.instances[gii].constants[0].xyz;` | `{const}.xyz` → slot 0 |
| 3    | `vec3 _n2 = _n0 * _n1;`                                    |                        |
| 4    | `vec3 _n3 = fract(_n2);`                                   |                        |
| 5    | `vec3 _n4 = u_graphData.instances[gii].constants[1].xyz;` | `{const}.xyz` → slot 1 |
| 6    | `vec3 _n5 = _n3 * _n4;`                                    |                        |

**Sink pass** (node 7): connected pins assigned, unconnected pins take semantic defaults:
```
surf.albedo = _n5;                        // connected
surf.normal = normalize(si.worldNormal);  // unconnected default
surf.roughness = 0.5;                     // unconnected default
surf.metallic = 0.0;
surf.ao = 1.0;
surf.shadingModelId = SM_OPENPBR_STANDARD;
```

### The generated function (what lands in `generated/SurfaceGraphs.glsl`)

```glsl
SurfaceData evalSurface_Graph0(SurfaceInputs si, uint gii) {
    SurfaceData surf;
    vec3 _n0 = si.worldPos;
    vec3 _n1 = u_graphData.instances[gii].constants[0].xyz;
    vec3 _n2 = _n0 * _n1;
    vec3 _n3 = fract(_n2);
    vec3 _n4 = u_graphData.instances[gii].constants[1].xyz;
    vec3 _n5 = _n3 * _n4;
    surf.albedo = _n5;
    surf.normal = normalize(si.worldNormal);
    surf.roughness = 0.5;
    surf.metallic = 0.0;
    surf.ao = 1.0;
    surf.shadingModelId = SM_OPENPBR_STANDARD;
    return surf;
}
```

Plus the manager-appended dispatcher:

```glsl
SurfaceData evalSurfaceGraph(uint graphId, SurfaceInputs si, uint gii) {
    switch (graphId) {
        case 0u: return evalSurface_Graph0(si, gii);
    }
    SurfaceData surf;                       // unknown graph -> magenta error
    surf.albedo = vec3(1.0, 0.0, 1.0);
    surf.normal = normalize(si.worldNormal);
    surf.roughness = 0.5; surf.metallic = 0.0; surf.ao = 1.0;
    surf.shadingModelId = SM_OPENPBR_STANDARD;
    return surf;
}
```

### How it glues to the material

`compile()` returned `defaults = { constants[0]=(0.5,0.5,0.5,0), constants[1]=(1,0.4,0.2,1) }` and `graphId = 0`. `TestLayer` no longer hand-builds the pool — it does `setGraph(0, defaults)`. The `_n2 = _n0 * _n1` line is `worldPos * 0.5`; `_n5` is `fract(...) * tint` — **pixel-identical to the hand-written `evalSurface_Test`** (its `roughness = 0.6` vs the generated `0.5` default is the only difference; wire a `CONSTANT_FLOAT` `0.6` into `surface_output.roughness` to match exactly).

---

## 5c. Worked example 2 (complex, typed)

A realistic "mossy rock": rock albedo blended with a moss colour by a noise mask, roughness blending rough rock to smooth moss by the same noise. This exercises two textures, four typed constants, both a `vec3` and a `float` mix, and shows that with typed variants **coercion only appears where you deliberately cross types**, not as the norm.

### The authored graph

```
nodes:
   1  texcoord
   2  position
   3  constant_vec3   (4, 4, 4, 0)          // noise uv scale
   4  multiply_vec3
   5  texture_sample  rock  albedo texture
   6  texture_sample  noise texture
   7  constant_vec3   (0.10, 0.35, 0.08, 1) // moss colour
   8  mix_vec3
   9  constant_float  0.85                  // rough rock
  10  constant_float  0.40                  // smooth moss
  11  mix_float
  12  surface_output
connections:  (src -> dst.pin)
   2 -> 4.a      3 -> 4.b                    // position * scale
   1 -> 5.uv                                 // rock at mesh uv
   4 -> 6.uv                                 // noise at scaled position
   5 -> 8.a      7 -> 8.b      6 -> 8.t       // albedo = mix(rock, moss, noise)
   9 -> 11.a    10 -> 11.b     6 -> 11.t      // roughness = mix(rough, smooth, noise)
   8 -> 12.albedo    11 -> 12.roughness
outputNodeId = 12
```

### Compile

**Topo-sort** → `[1, 5, 7, 2, 3, 4, 6, 8, 9, 10, 11]` (12 handled by the sink).

**Resource pass** (slots assigned in topo order):
| slot | from node | default value |
|------|-----------|---------------|
| `textures[0]`  | 5 rock   | rock texture bindless index |
| `textures[1]`  | 6 noise  | noise texture bindless index |
| `constants[0]` | 7 moss   | `(0.10, 0.35, 0.08, 1)` |
| `constants[1]` | 3 scale  | `(4, 4, 4, 0)` |
| `constants[2]` | 9 rough  | `(0.85, 0, 0, 0)` |
| `constants[3]` | 10 smooth| `(0.40, 0, 0, 0)` |

Slots are assigned by traversal order, not authoring order (the moss constant lands in `constants[0]` even though scale is authored earlier) — the `mapping` records it, so `getDefaults` seeds the right slots regardless.

### Generated function

```glsl
SurfaceData evalSurface_MossyRock(SurfaceInputs si, uint gii) {
    SurfaceData surf;
    vec2 _n0 = si.uv;
    vec4 _n1 = texture(u_textures[u_graphData.instances[gii].textures[0]], _n0);
    vec3 _n2 = u_graphData.instances[gii].constants[0].xyz;
    vec3 _n3 = si.worldPos;
    vec3 _n4 = u_graphData.instances[gii].constants[1].xyz;
    vec3 _n5 = _n3 * _n4;
    vec4 _n6 = texture(u_textures[u_graphData.instances[gii].textures[1]], (_n5).xy);
    vec3 _n7 = mix((_n1).xyz, _n2, (_n6).x);
    float _n8 = u_graphData.instances[gii].constants[2].x;
    float _n9 = u_graphData.instances[gii].constants[3].x;
    float _n10 = mix(_n8, _n9, (_n6).x);
    surf.albedo = _n7;
    surf.normal = normalize(si.worldNormal);
    surf.roughness = _n10;
    surf.metallic = 0.0;
    surf.ao = 1.0;
    surf.shadingModelId = SM_OPENPBR_STANDARD;
    return surf;
}
```

Every `vec3 * vec3`, `mix(float, float, float)`, and same-type wire is coercion-free. The only conversions are the three deliberate crossings: `(_n1).xyz` (rock `vec4` texel into the `vec3` albedo input), `(_n6).x` (noise `vec4` into the `float` mask, taking red), and `(_n5).xy` (scaled `vec3` position into the `vec2` uv). That is coercion as the exception, which is the point of typing the nodes.

---

## 6. Manager + file emission

`SurfaceGraphManager` (`Engine/src/materials/graph/SurfaceGraphManager.h/.cpp`) owns registered graphs and the generated file:

- `uint32_t registerGraph(const MaterialGraph&)` → compiles, assigns the next `graphId` (== dispatcher case), stores the `CompileResult`, returns the id. The id is what `MaterialInstance::setGraph` takes.
- `writeGeneratedFile(path)` → concatenates every `CompileResult.glslFunction`, appends the `evalSurfaceGraph(graphId, si, gii)` dispatcher `switch` (one `case graphId: return evalSurface_<Name>(si, gii);` per graph) + the magenta-error `default`, writes `Engine/assets/shaders/glsl/generated/SurfaceGraphs.glsl` with a `// GENERATED — DO NOT EDIT` banner and the `#ifndef SURFACE_GRAPHS_GLSL` guard the current file already has.
- `getDefaults(graphId)` / `getMapping(graphId)` → surfaced to callers building a `MaterialInstance` (`TestLayer`'s sphere becomes: register a graph, take its id + defaults, `setGraph(id, defaults)`).

In 2b the file is written at startup (before the G-buffer shader imports). Regenerating the file and **recreating the pipeline on a graph edit needs new engine support** (shader rebuild trigger + last-good pipeline swap) — that is Phase 2c, decided later. Here the file is generated once, deterministically, and the existing shader-import path (`GBufferPass.cpp:658`) picks it up on startup.

---

## 7. Serialization — one Rapture format, not a new extension per type

No bespoke `.matgraph`. Reuse a single generic Rapture asset format so we don't accumulate a new extension per asset kind:

- **`.rapt`** — human-readable JSON for development / version control. A generic Rapture-asset envelope; a material graph is one section, e.g. `{ "materials": { ... nodes / connections / output ... } }`. Same node/connection/output shape as `PROCEDURAL_MATERIALS_DESIGN.md:1094-1157`, but keyed by `typeName` string (not the dropped `NodeType` enum) so new nodes need no format change.
- **`.rasset`** — the binary counterpart (packed Rapture assets) for shipping, same schema.

Load/save round-trips a `MaterialGraph` out of the `materials` section. This is the last slice of 2b and unblocks the Phase 3 editor (it saves/loads what the canvas edits). The exact envelope layout is a project-wide asset-format decision beyond this doc — the compiler only needs a `MaterialGraph` in and out, so it is agnostic to which container carries it.

---

## 8. Build order within 2b (low-risk staging)

1. **2b.1 — IR + registry + compiler, headless.** `MaterialGraphTypes.h`, `NodeRegistry`, `MaterialGraph.h`, `MaterialGraphCompiler`, starter node set. Pure C++, no engine wiring. Validate by compiling a graph in a scratch test and inspecting the emitted string. **Zero rendering risk.**
2. **2b.2 — `SurfaceGraphManager` + flip graph 0.** Emit the real `generated/SurfaceGraphs.glsl`; reproduce graph 0; run the Section 5 acceptance test; delete the hand-written function. First generated pixels.
3. **2b.3 — `.rapt` load/save.** Round-trip a graph to disk.

Then 2c (async), then Phase 3 (editor), Phase 4 (MaterialX/glTF lowering into this IR).

---

## 9. Risks / open questions

- **Normal output space.** `surface_output.normal` is world-space `SurfaceData.normal`. Tangent-space normal-map output (the common case) needs a TBN transform the graph can express — expose the TBN via `SurfaceInputs` (`si.tangent`/`si.bitangent` already there, `GBuffer.fs.glsl:42-46` builds the matrix) as a future `normal_map` node, or bake a `tangent_to_world` node. Flagged in [[Material System Overhaul#12]]; starter set outputs world normal directly.
- **Coercion ambiguity** (vec3→vec4 alpha, float→vec splat) is convention, not correctness — documented in 4.2, revisit if a node needs different defaults.
- **Pool overflow** at 16 tex / 16 const is the migration trigger to the variable-length SSBO slice (Option B), a data-layout change, not a compiler rewrite — the mapping indirection already isolates it.
- **Constant vs variable (planned split, later tweak).** Today the single `constant` node is slot-backed — its value lives in `constants[k]`, so a value change is a data write with **no recompile**, only structural edits recompile. Good default. Later, split into two node types:
  - *variable* — slot-backed, exactly today's behaviour (live-editable, uses a pool slot).
  - *constant* — a true literal **baked into the generated GLSL** (`0.5`, `vec3(...)`), consuming **no** slot but requiring a recompile when changed. Cheaper at runtime and frees pool slots for values that never move.

  The `resourceKind` mechanism already supports this: a baked constant is just `resourceKind = NONE` with the literal substituted directly into consumers, no slot assigned. Deferred — the starter set keeps one slot-backed `constant`.

---

## 10. Known limitation: per-node values are underpowered (compiler overhaul owed)

The node editor's model is "an unconnected input pin always carries an editable value; a control (e.g. a constant editor) carries its value." The IR/compiler cannot express that today. This is an accepted, written-down gap — the compiler needs an overhaul before authored input values reach the GPU.

**Where values can live today:**
- `GraphNode.constantValue` — a **single** `PinValue`, used **only** by `CONSTANT` nodes. It maps to one `{const}` pool slot (`s_assignResources` → `mapping.constantSlots[nodeId]`), and the template language has exactly **one** `{const}` per node. So a node can bake **at most one** value.
- Every other node's unconnected-input value comes from the **shared** `NodeDefinition.PinDef.defaultValue` via `s_literal` (`s_resolveInput`), or — for the `SURFACE_OUTPUT` sink — from **hardcoded fallbacks** in `s_emitSurfaceBody` (`"0.5"`, `"vec3(1.0)"`, `normalize(si.worldNormal)`, …). Neither is per-node, neither is authored.

**What breaks because of it:**
1. **`SURFACE_OUTPUT` is the clearest case.** Its `albedo/normal/roughness/metallic/ao` inputs have no single "node value" — each is a separate input. When unconnected they emit compiler-hardcoded fallbacks, so a user cannot set "this material's roughness = 0.6" without wiring a constant node. The node conceptually has *many* values (one per input); the IR gives it zero.
2. **Per-node input-default overrides have no home.** The editor stores an unconnected input's value on the pin (`PinView.value`), but `GraphNode` has nowhere to put it, so `s_resolveInput` can never read it — editing an input default in the UI cannot affect the output.
3. **A node cannot have more than one baked/control value.** Single `constantValue`, single `{const}` placeholder. A future node with several tweakable parameters is unrepresentable.

**Overhaul owed (compiler + IR):**
- Give `GraphNode` **per-input** value storage (e.g. `std::vector<PinValue> inputValues` indexed by input-pin index) and, for multi-value control nodes, more than one control value.
- `s_resolveInput` and the `SURFACE_OUTPUT` sink read the node's **authored** value for an unconnected input instead of the shared def default / hardcoded fallback.
- Make authored unconnected-input values into **pool constants** (runtime-editable, no recompile — same mechanism as constant nodes), which means resource-slot assignment for those inputs and template placeholders that can reference **multiple** per-node constant slots (indexed `{const0}`, `{const1}`, or a per-input slot ref) rather than the single `{const}`.
- This subsumes and generalizes the single-`{const}`-per-node design; the current single `GraphNode.constantValue` becomes one case of the general per-value storage.

Until then: constant nodes are the only way to author a value that reaches the shader, and `SURFACE_OUTPUT`'s unconnected channels use fixed fallbacks.

**Proposed fix: [[Per-node Graph Values]].**
